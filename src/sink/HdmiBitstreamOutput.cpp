#include "veyra/sink/HdmiBitstreamOutput.h"
#include "HdmiCarrierQueue.h"
#include "../Ac3CarrierIntegrity.h"

#ifdef VEYRA_HDMI_BITSTREAM_DIAGNOSTICS
#include "veyra/Log.h"
#include <format>
#endif

#include <Windows.h>
#include <audioclient.h>
#include <ks.h>
#include <ksmedia.h>
#include <initguid.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <avrt.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <chrono>

#include <mutex>
#include <thread>

namespace veyra::sink {
namespace {
using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
constexpr unsigned carrierFrameBytes = 4;
constexpr unsigned carrierPrimeMs = 40;
constexpr unsigned carrierBacklogMs = 125;
constexpr auto carrierIdleTimeout = 500ms;
size_t carrierBytes(unsigned rate, unsigned ms) {
    return size_t(rate) * ms / 1000 * carrierFrameBytes;
}

struct Event {
    HANDLE handle = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    Event() = default;
    Event(const Event&) = delete;
    ~Event() { if (handle) CloseHandle(handle); }
};

struct ComApartment {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComApartment() { if (SUCCEEDED(hr)) CoUninitialize(); }
};

struct RenderPriority {
    DWORD index = 0;
    HANDLE task = AvSetMmThreadCharacteristicsW(L"Pro Audio", &index);
    ~RenderPriority() { if (task) AvRevertMmThreadCharacteristics(task); }
};

// GUIDs defined by Windows for IEC61937 AC-3 and DTS core, respectively.
constexpr GUID ac3Subtype{0x92, 0, 0x0010, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};
constexpr GUID dtsSubtype{0x08, 0, 0x0010, {0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71}};

ComPtr<IAudioClient> audioClient(IMMDevice* device) {
    ComPtr<IAudioClient> client;
    device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                     reinterpret_cast<void**>(client.GetAddressOf()));
    return client;
}

std::vector<WAVEFORMATEXTENSIBLE_IEC61937> formats(BitstreamKind codec, unsigned rate) {
    WAVEFORMATEXTENSIBLE_IEC61937 format{};
    auto& ext = format.FormatExt;
    auto& wave = ext.Format;
    wave.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    wave.nChannels = 2;
    wave.nSamplesPerSec = rate;
    wave.nAvgBytesPerSec = rate * carrierFrameBytes;
    wave.nBlockAlign = carrierFrameBytes;
    wave.wBitsPerSample = 16;
    wave.cbSize = sizeof(ext) - sizeof(wave);
    ext.Samples.wValidBitsPerSample = 16;
    ext.dwChannelMask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
    ext.SubFormat = codec == BitstreamKind::Ac3 ? ac3Subtype : dtsSubtype;

    std::vector<WAVEFORMATEXTENSIBLE_IEC61937> result{format};
    wave.cbSize = sizeof(format) - sizeof(wave);
    format.dwEncodedSamplesPerSec = rate;
    format.dwEncodedChannelCount = 6;
    result.push_back(format);
    if (codec == BitstreamKind::Ac3) {
        wave.wFormatTag = WAVE_FORMAT_DOLBY_AC3_SPDIF;
        wave.cbSize = 0;
        result.push_back(format);
    }
    return result;
}

bool supportsEncodedFormat(IAudioClient* client, BitstreamKind codec, unsigned rate) {
    return std::ranges::any_of(formats(codec, rate), [&](const auto& format) {
        return client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE,
            &format.FormatExt.Format, nullptr) == S_OK;
    });
}

struct Endpoint {
    HdmiBitstreamEndpoint info;
    ComPtr<IMMDevice> device;
};

HRESULT deviceEnumerator(ComPtr<IMMDeviceEnumerator>& enumerator) {
    return CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                            IID_PPV_ARGS(enumerator.ReleaseAndGetAddressOf()));
}

std::wstring deviceId(IMMDevice* device) {
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id))) return {};
    std::wstring result(id);
    CoTaskMemFree(id);
    return result;
}

std::wstring pcmEndpoint(IMMDeviceEnumerator* enumerator, const std::wstring& preferred) {
    ComPtr<IMMDevice> device;
    DWORD state = 0;
    if (!preferred.empty() && SUCCEEDED(enumerator->GetDevice(preferred.c_str(), device.GetAddressOf())) &&
        SUCCEEDED(device->GetState(&state)) && state == DEVICE_STATE_ACTIVE) return preferred;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, device.ReleaseAndGetAddressOf())))
        return deviceId(device.Get());
    return {};
}

class DefaultOutputNotification final : public IMMNotificationClient {
public:
    std::atomic<bool> changed{false};

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** object) override {
        if (!object) return E_POINTER;
        *object = nullptr;
        if (iid != __uuidof(IUnknown) && iid != __uuidof(IMMNotificationClient)) return E_NOINTERFACE;
        *object = static_cast<IMMNotificationClient*>(this);
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const auto count = --references_;
        if (!count) delete this;
        return count;
    }
    HRESULT STDMETHODCALLTYPE OnDefaultDeviceChanged(EDataFlow flow, ERole role, LPCWSTR) override {
        if (flow == eRender && role == eConsole) changed.store(true);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE OnDeviceStateChanged(LPCWSTR, DWORD) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceAdded(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnDeviceRemoved(LPCWSTR) override { return S_OK; }
    HRESULT STDMETHODCALLTYPE OnPropertyValueChanged(LPCWSTR, const PROPERTYKEY) override { return S_OK; }

private:
    std::atomic<ULONG> references_{1};
};

struct DefaultOutputWatch {
    IMMDeviceEnumerator* enumerator = nullptr;
    ComPtr<DefaultOutputNotification> notification;

    void start(IMMDeviceEnumerator* owner) {
        notification.Attach(new DefaultOutputNotification);
        const HRESULT hr = owner->RegisterEndpointNotificationCallback(notification.Get());
        if (SUCCEEDED(hr)) enumerator = owner;
#ifdef VEYRA_HDMI_BITSTREAM_DIAGNOSTICS
        else log::warn("bitstream-output", std::format("default output notification unavailable hr=0x{:08X}", unsigned(hr)));
#endif
    }
    bool changed() const { return enumerator && notification->changed.load(); }
    ~DefaultOutputWatch() {
        if (enumerator) enumerator->UnregisterEndpointNotificationCallback(notification.Get());
    }
};

HRESULT enumerate(IMMDeviceEnumerator* enumerator, std::vector<Endpoint>& result) {
    ComPtr<IMMDeviceCollection> devices;
    HRESULT hr = enumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, devices.GetAddressOf());
    if (FAILED(hr)) return hr;

    UINT count = 0;
    hr = devices->GetCount(&count);
    if (FAILED(hr)) return hr;
    for (UINT i = 0; i < count; ++i) {
        Endpoint endpoint;
        if (FAILED(devices->Item(i, endpoint.device.GetAddressOf()))) continue;
        ComPtr<IPropertyStore> props;
        if (FAILED(endpoint.device->OpenPropertyStore(STGM_READ, props.GetAddressOf()))) continue;
        PROPVARIANT value{};
        if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &value)) && value.vt == VT_LPWSTR)
            endpoint.info.name = value.pwszVal;
        PropVariantClear(&value);
        endpoint.info.id = deviceId(endpoint.device.Get());
        if (endpoint.info.id.empty()) continue;
        if (endpoint.info.name.empty()) endpoint.info.name = endpoint.info.id;
        result.push_back(std::move(endpoint));
    }
    return S_OK;
}

struct Stream {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    UINT32 frames = 0;
    bool started = false;
#ifdef VEYRA_HDMI_BITSTREAM_DIAGNOSTICS
    Clock::time_point lastGapReport;
#endif
    ~Stream() { if (started) client->Stop(); }

    HRESULT open(IMMDevice* device, BitstreamKind codec, unsigned rate, HANDLE event) {
        HRESULT result = AUDCLNT_E_UNSUPPORTED_FORMAT;
        for (const auto& format : formats(codec, rate)) {
            auto candidate = audioClient(device);
            if (!candidate) return E_FAIL;
            const auto* wave = &format.FormatExt.Format;
            HRESULT hr = candidate->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, wave, nullptr);
            if (hr != S_OK) {
                if (FAILED(hr) && hr != AUDCLNT_E_UNSUPPORTED_FORMAT) result = hr;
                continue;
            }

            REFERENCE_TIME minimum = 0;
            hr = candidate->GetDevicePeriod(nullptr, &minimum);
            if (FAILED(hr)) { result = hr; continue; }
            // Prefer one complete AC-3 burst per hardware period. Keep the
            // shorter period for DTS and for drivers that reject this size.
            const auto shortPeriod = std::max<REFERENCE_TIME>(160000, minimum);
            const auto ac3Period = (10000000LL * iecBurstFrames(0x01) + rate / 2) / rate;
            auto period = codec == BitstreamKind::Ac3
                ? std::max<REFERENCE_TIME>(ac3Period, minimum) : shortPeriod;
            const auto initialize = [&](REFERENCE_TIME duration) {
                return candidate->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK,
                                             duration, duration, wave, nullptr);
            };
            hr = initialize(period);
            if (period != shortPeriod && (hr == AUDCLNT_E_INVALID_DEVICE_PERIOD ||
                                          hr == AUDCLNT_E_BUFFER_SIZE_ERROR)) {
                candidate = audioClient(device);
                if (!candidate) return E_FAIL;
                hr = initialize(shortPeriod);
            }
            if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED && SUCCEEDED(candidate->GetBufferSize(&frames))) {
                period = (10000000LL * frames + rate / 2) / rate;
                candidate = audioClient(device);
                if (!candidate) return E_FAIL;
                hr = initialize(period);
            }
            if (SUCCEEDED(hr)) hr = candidate->GetBufferSize(&frames);
            if (SUCCEEDED(hr) && frames == 0) hr = E_UNEXPECTED;
            if (SUCCEEDED(hr)) hr = candidate->SetEventHandle(event);
            if (SUCCEEDED(hr)) hr = candidate->GetService(IID_PPV_ARGS(render.ReleaseAndGetAddressOf()));
            if (SUCCEEDED(hr)) {
                client = std::move(candidate);
                return S_OK;
            }
            result = hr;
        }
        return result;
    }
};
}

struct HdmiBitstreamOutput::Impl {
    mutable std::mutex mutex;
    std::atomic<bool> enabled = false;
    Event wake;
    std::thread worker;
    bool stop = false;
    HdmiBitstreamSettings settings;
    const ReserveEndpoint reserveEndpoint;
    explicit Impl(ReserveEndpoint reserve) : reserveEndpoint(std::move(reserve)) {}
    HdmiBitstreamState state;
    uint64_t generation = 0;
    detail::HdmiCarrierQueue queue;
    std::vector<uint8_t> lastValidAc3;
    Clock::time_point lastBurst;
#ifdef VEYRA_HDMI_BITSTREAM_DIAGNOSTICS
    Clock::time_point lastDropReport;
    Clock::time_point lastInvalidReport;
    size_t unreportedDrops = 0;
#endif

    ~Impl() {
        {
            std::lock_guard lock(mutex);
            stop = true;
            if (wake.handle) SetEvent(wake.handle);
        }
        if (worker.joinable()) worker.join();
    }

    void invalidate() {
        ++generation;
        queue.clear();
        lastValidAc3.clear();
        state = {settings.enabled ? HdmiBitstreamStatus::Connecting : HdmiBitstreamStatus::Disabled};
        if (wake.handle) SetEvent(wake.handle);
    }

    void update(uint64_t revision, HdmiBitstreamStatus status, HRESULT hr = S_OK,
                const std::wstring& endpoint = {}) {
        std::lock_guard lock(mutex);
        if (revision != generation) return;
        state.status = status;
        state.error = hr;
        state.endpoint = endpoint;
    }

    HRESULT fill(Stream& stream, uint64_t revision, detail::HdmiCarrierQueue::Cursor& cursor,
                 BitstreamKind codec, unsigned rate) {
        BYTE* destination = nullptr;
        const HRESULT hr = stream.render->GetBuffer(stream.frames, &destination);
        if (FAILED(hr)) return hr;
        const std::span output(destination, size_t(stream.frames) * carrierFrameBytes);
        size_t silentBytes = 0;
        {
            std::lock_guard lock(mutex);
            if (revision == generation) {
                silentBytes = queue.fill(cursor, output, carrierBytes(rate, carrierPrimeMs));
            } else {
                std::ranges::fill(output, uint8_t(0));
            }
        }
        if (silentBytes && codec == BitstreamKind::Ac3)
            writeAc3PauseBursts(output.last(silentBytes));
        const HRESULT result = stream.render->ReleaseBuffer(stream.frames, 0);
#ifdef VEYRA_HDMI_BITSTREAM_DIAGNOSTICS
        if (silentBytes && Clock::now() - stream.lastGapReport >= 1s) {
            stream.lastGapReport = Clock::now();
            log::warn("bitstream-output", std::format("carrier gap; inserted {} {} frames",
                silentBytes / carrierFrameBytes, codec == BitstreamKind::Ac3 ? "AC-3 pause" : "silent carrier"));
        }
#else
        static_cast<void>(silentBytes);
#endif
        return result;
    }

    void waitToRetry(uint64_t revision) {
        const auto retry = Clock::now() + 1s;
        while (Clock::now() < retry) {
            {
                std::lock_guard lock(mutex);
                if (stop || generation != revision) return;
            }
            WaitForSingleObject(wake.handle, 100);
        }
    }

    void run() {
        ComApartment com;
        Event renderEvent;
        RenderPriority priority;
        for (;;) {
            HdmiBitstreamSettings config;
            HdmiBitstreamState input;
            uint64_t revision = 0;
            bool ready = false;
            {
                std::lock_guard lock(mutex);
                if (stop) return;
                const auto prebufferBytes = carrierBytes(state.rate, carrierPrimeMs);
                ready = settings.enabled && queue.bytes() >= prebufferBytes &&
                        state.rate && Clock::now() - lastBurst < carrierIdleTimeout;
                config = settings;
                input = state;
                revision = generation;
            }
            if (!ready) {
                WaitForSingleObject(wake.handle, INFINITE);
                continue;
            }

            ComPtr<IMMDeviceEnumerator> enumerator;
            std::vector<Endpoint> devices;
            HRESULT hr = com.hr;
            if (SUCCEEDED(hr)) hr = renderEvent.handle ? deviceEnumerator(enumerator) : E_FAIL;
            DefaultOutputWatch defaultWatch;
            if (SUCCEEDED(hr) && config.endpointId.empty()) defaultWatch.start(enumerator.Get());
            if (SUCCEEDED(hr)) hr = enumerate(enumerator.Get(), devices);
            struct Reservation {
                const ReserveEndpoint& change;
                bool held = false;
                void acquire(const std::wstring& id) { if (change) { change(id); held = true; } }
                void release() { if (held) { change({}); held = false; } }
                ~Reservation() { release(); }
            } reservation{reserveEndpoint};
            Stream stream;
            std::wstring name, selectedId;
            if (SUCCEEDED(hr)) {
                selectedId = config.endpointId.empty() ? pcmEndpoint(enumerator.Get(), {}) : config.endpointId;
                hr = HRESULT_FROM_WIN32(ERROR_DEVICE_NOT_CONNECTED);
                for (auto& endpoint : devices) {
                    if (endpoint.info.id != selectedId) continue;
                    auto client = audioClient(endpoint.device.Get());
                    if (!client) { hr = E_FAIL; break; }
                    if (!supportsEncodedFormat(client.Get(), input.codec, input.rate)) {
                        hr = AUDCLNT_E_UNSUPPORTED_FORMAT;
                        break;
                    }
                    client.Reset();
                    hr = stream.open(endpoint.device.Get(), input.codec, input.rate, renderEvent.handle);
                    if (SUCCEEDED(hr) || hr == AUDCLNT_E_DEVICE_IN_USE) reservation.acquire(selectedId);
                    // The PCM owner observes the reservation and closes its
                    // shared client at the next audio event.
                    for (unsigned attempt = 0;
                         attempt != 12 && hr == AUDCLNT_E_DEVICE_IN_USE && reservation.held; ++attempt) {
                        WaitForSingleObject(wake.handle, 25);
                        {
                            std::lock_guard lock(mutex);
                            if (stop || revision != generation) break;
                        }
                        hr = stream.open(endpoint.device.Get(), input.codec, input.rate, renderEvent.handle);
                    }
                    if (SUCCEEDED(hr)) name = endpoint.info.name;
                    break;
                }
            }
            if (FAILED(hr)) {
                reservation.release();
                update(revision, hr == AUDCLNT_E_UNSUPPORTED_FORMAT ? HdmiBitstreamStatus::Unsupported :
                       HdmiBitstreamStatus::Error, hr);
                // Retry slowly while input is still arriving. Settings and codec
                // changes invalidate the revision and can retry immediately.
                waitToRetry(revision);
                continue;
            }

            {
                std::lock_guard lock(mutex);
                if (stop || revision != generation) continue;
            }
            detail::HdmiCarrierQueue::Cursor cursor;
            ResetEvent(renderEvent.handle);
            hr = fill(stream, revision, cursor, input.codec, input.rate);
            if (SUCCEEDED(hr)) hr = stream.client->Start();
            stream.started = SUCCEEDED(hr);
            if (stream.started) update(revision, HdmiBitstreamStatus::Active, S_OK, name);
#ifdef VEYRA_HDMI_BITSTREAM_DIAGNOSTICS
            const auto periodMs = 1000.0 * stream.frames / input.rate;
            const char* stopReason = "capture or settings reset";
            if (stream.started)
                log::info("bitstream-output", std::format("started codec={} carrierRate={} periodMs={:.2f}",
                          int(input.codec), input.rate, periodMs));
#endif

            const HANDLE events[]{renderEvent.handle, wake.handle};
            auto deadline = Clock::now() + 500ms;
            bool inputIdle = false;
            while (SUCCEEDED(hr)) {
                {
                    std::lock_guard lock(mutex);
                    if (stop || revision != generation) break;
                    if (Clock::now() - lastBurst >= carrierIdleTimeout) {
                        inputIdle = true;
                        invalidate();
                        break;
                    }
                }
                const DWORD event = WaitForMultipleObjects(2, events, FALSE, 100);
                if (event == WAIT_OBJECT_0) {
                    hr = fill(stream, revision, cursor, input.codec, input.rate);
                    if (hr == AUDCLNT_E_BUFFER_ERROR)
                        hr = Clock::now() >= deadline ? AUDCLNT_E_DEVICE_INVALIDATED : S_OK;
                    else deadline = Clock::now() + 500ms;
                } else if (event == WAIT_FAILED) {
                    hr = HRESULT_FROM_WIN32(GetLastError());
                } else if (Clock::now() >= deadline) {
                    hr = AUDCLNT_E_DEVICE_INVALIDATED;
                }
                if (defaultWatch.changed()) {
                    update(revision, HdmiBitstreamStatus::Connecting);
#ifdef VEYRA_HDMI_BITSTREAM_DIAGNOSTICS
                    stopReason = "default output changed";
#endif
                    break;
                }
            }
#ifdef VEYRA_HDMI_BITSTREAM_DIAGNOSTICS
            if (inputIdle) stopReason = "500 ms without a capture burst";
            if (FAILED(hr)) log::warn("bitstream-output", std::format("render interrupted hr=0x{:08X}", unsigned(hr)));
            else if (stream.started) log::info("bitstream-output", std::format("stopped: {}", stopReason));
#else
            static_cast<void>(inputIdle);
#endif
            if (FAILED(hr)) {
                update(revision, HdmiBitstreamStatus::Error, hr, name);
                if (stream.started) {
                    stream.client->Stop();
                    stream.started = false;
                }
                stream.render.Reset();
                stream.client.Reset();
                reservation.release();
                waitToRetry(revision);
            }
        }
    }
};

HdmiBitstreamOutput::HdmiBitstreamOutput(ReserveEndpoint reserve)
    : p_(std::make_unique<Impl>(std::move(reserve))) {}
HdmiBitstreamOutput::~HdmiBitstreamOutput() = default;

void HdmiBitstreamOutput::configure(HdmiBitstreamSettings settings) {
    std::lock_guard lock(p_->mutex);
    if (p_->settings == settings) return;
    p_->settings = std::move(settings);
    p_->enabled = p_->settings.enabled;
    p_->invalidate();
    if (!p_->settings.enabled || p_->worker.joinable()) return;
    try {
        if (p_->wake.handle) p_->worker = std::thread([impl = p_.get()] { impl->run(); });
    } catch (const std::system_error&) {
        // Report worker startup failure below.
    }
    if (!p_->worker.joinable()) {
        p_->state.status = HdmiBitstreamStatus::Error;
        p_->state.error = E_FAIL;
    }
}

bool HdmiBitstreamOutput::enabled() const { return p_->enabled.load(); }

HdmiBitstreamState HdmiBitstreamOutput::state() const {
    std::lock_guard lock(p_->mutex);
    return p_->state;
}

void HdmiBitstreamOutput::submit(uint16_t pc, uint16_t pd, unsigned rate, std::span<const uint8_t> payload) {
    if (!enabled()) return;
    const unsigned type = pc & 0x1f;
    const auto codec = type == 1 ? BitstreamKind::Ac3 :
        (type >= 0x0b && type <= 0x0d) ? BitstreamKind::Dts :
        type == 0x15 ? BitstreamKind::Eac3 : type == 0x11 ? BitstreamKind::DtsHd :
        type == 0x16 ? BitstreamKind::TrueHd : BitstreamKind::None;
    const bool supported = iecBurstFrames(pc) && (rate == 32000 || rate == 44100 || rate == 48000);
    auto burst = supported ? makeIecBurst(pc, pd, payload) : std::vector<uint8_t>{};
    const bool invalidAc3 = !burst.empty() && codec == BitstreamKind::Ac3 && !validAc3CarrierPayload(pd, payload);
    std::unique_lock lock(p_->mutex);
    if (!p_->settings.enabled || !p_->worker.joinable()) return;
    if (p_->state.codec != codec || p_->state.rate != rate) {
        p_->invalidate();
        p_->state.codec = codec;
        p_->state.rate = rate;
    }
    if (!supported) {
        p_->state.status = HdmiBitstreamStatus::Unsupported;
        return;
    }
    if (burst.empty()) return;
    const size_t capacity = carrierBytes(rate, carrierBacklogMs);
    const auto now = Clock::now();
    if (now - p_->lastBurst >= carrierIdleTimeout) {
        p_->queue.clear();
        p_->lastValidAc3.clear();
    }
    const bool replayedAc3 = invalidAc3 && p_->lastValidAc3.size() == burst.size();
    if (replayedAc3)
        std::ranges::copy(p_->lastValidAc3, burst.begin());
    else if (codec == BitstreamKind::Ac3 && !invalidAc3)
        p_->lastValidAc3 = burst;
    const auto dropped = p_->queue.push(std::move(burst), capacity);
    p_->lastBurst = now;
    if (p_->queue.bytes() >= carrierBytes(rate, carrierPrimeMs) && p_->state.status != HdmiBitstreamStatus::Active)
        SetEvent(p_->wake.handle);
#ifdef VEYRA_HDMI_BITSTREAM_DIAGNOSTICS
    const bool reportInvalid = invalidAc3 && now - p_->lastInvalidReport >= 1s;
    if (reportInvalid) p_->lastInvalidReport = now;
    p_->unreportedDrops += dropped;
    size_t droppedToReport = 0;
    if (p_->unreportedDrops && now - p_->lastDropReport >= 1s) {
        droppedToReport = p_->unreportedDrops;
        p_->unreportedDrops = 0;
        p_->lastDropReport = now;
    }
    lock.unlock();
    if (droppedToReport)
        log::warn("bitstream-output", std::format("queue overflow: dropped {} complete capture bursts", droppedToReport));
    if (reportInvalid)
        log::warn("bitstream-output", replayedAc3 ? "damaged AC-3 frame; replayed last valid frame"
                                                 : "damaged AC-3 frame; no valid frame available for replay");
#else
    static_cast<void>(dropped);
#endif
}

void HdmiBitstreamOutput::reset() {
    if (!enabled()) return;
    std::lock_guard lock(p_->mutex);
    if (p_->worker.joinable()) p_->invalidate();
}

std::vector<HdmiBitstreamEndpoint> HdmiBitstreamOutput::endpoints(unsigned rate) {
    ComApartment com;
    std::vector<HdmiBitstreamEndpoint> result;
    ComPtr<IMMDeviceEnumerator> enumerator;
    std::vector<Endpoint> devices;
    if ((FAILED(com.hr) && com.hr != RPC_E_CHANGED_MODE) ||
        FAILED(deviceEnumerator(enumerator)) || FAILED(enumerate(enumerator.Get(), devices))) return result;
    for (auto& endpoint : devices) {
        auto client = audioClient(endpoint.device.Get());
        if (client) {
            constexpr unsigned captureRates[]{48000, 44100, 32000};
            const auto supports = [&](BitstreamKind codec) {
                if (rate) return supportsEncodedFormat(client.Get(), codec, rate);
                return std::ranges::any_of(captureRates, [&](unsigned candidate) {
                    return supportsEncodedFormat(client.Get(), codec, candidate);
                });
            };
            endpoint.info.ac3 = supports(BitstreamKind::Ac3);
            endpoint.info.dts = supports(BitstreamKind::Dts);
        }
        if (endpoint.info.ac3 || endpoint.info.dts) result.push_back(std::move(endpoint.info));
    }
    return result;
}

} // namespace veyra::sink
