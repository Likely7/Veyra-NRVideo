#include "veyra/source/MagewellCapture.h"
#include "MagewellFramePump.h"

#include <windows.h>
#include <avrt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cwctype>
#include <filesystem>
#include <format>
#include <limits>
#include <mutex>
#include <system_error>
#include <thread>

#include "veyra/Log.h"

#ifdef VEYRA_HAS_MAGEWELL
// Magewell's own headers (MWCapture SDK 3.3.1.1596, kept outside the source tree under
// deps/magewell). Their licence lets the header files and library be used, modified and
// redistributed with Magewell's copyright notice and disclaimer; see THIRD_PARTY_NOTICES.md.
#include <LibMWCapture/MWCapture.h>
#endif

namespace veyra::source::magewell {

namespace {

std::atomic<bool> g_preference{false};
// What the capture dialog shows (statusText()).
std::mutex g_statusMutex;
std::wstring g_lastError;
std::atomic<bool> g_active{false};
std::atomic<double> g_latencyMs{0};

void setLastError(std::wstring text) {
    std::lock_guard lock(g_statusMutex);
    g_lastError = std::move(text);
}

std::string narrow(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(std::max(size, 0)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.c_str(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring lower(std::wstring_view text) {
    std::wstring out(text);
    for (auto& c : out) c = wchar_t(std::towlower(c));
    return out;
}

// "\\?\pci#ven_1cd7&dev_0010&...#4&2b0f...&0&00e0#{guid}\global" -> "pci#ven_1cd7&dev_0010&...#4&2b0f...&0&00e0"
std::wstring instanceOf(std::wstring_view path) {
    std::wstring text = lower(path);
    if (text.starts_with(L"\\\\?\\")) text.erase(0, 4);
    if (const auto guid = text.find(L"#{"); guid != std::wstring::npos) text.erase(guid);
    return text;
}

std::filesystem::path programDirectory() {
    wchar_t buffer[MAX_PATH * 4]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, DWORD(std::size(buffer)));
    return std::filesystem::path(std::wstring(buffer, length)).parent_path();
}

#ifdef VEYRA_HAS_MAGEWELL
struct Api {
    HMODULE module = nullptr;
    std::wstring where;
    bool ready = false;
    decltype(&MWCaptureInitInstance) initInstance = nullptr;
    decltype(&MWRefreshDevice) refreshDevice = nullptr;
    decltype(&MWGetChannelCount) channelCount = nullptr;
    decltype(&MWGetChannelInfoByIndex) channelInfo = nullptr;
    decltype(&MWGetDevicePath) devicePath = nullptr;
    decltype(&MWOpenChannelByPath) openChannel = nullptr;
    decltype(&MWCloseChannel) closeChannel = nullptr;
    decltype(&MWPinVideoBuffer) pinBuffer = nullptr;
    decltype(&MWUnpinVideoBuffer) unpinBuffer = nullptr;
    decltype(&MWStartVideoCapture) startCapture = nullptr;
    decltype(&MWStopVideoCapture) stopCapture = nullptr;
    decltype(&MWGetVideoBufferInfo) bufferInfo = nullptr;
    decltype(&MWGetVideoFrameInfo) frameInfo = nullptr;
    decltype(&MWGetVideoSignalStatus) signalStatus = nullptr;
    decltype(&MWRegisterNotify) registerNotify = nullptr;
    decltype(&MWUnregisterNotify) unregisterNotify = nullptr;
    decltype(&MWGetNotifyStatus) notifyStatus = nullptr;
    decltype(&MWGetDeviceTime) deviceTime = nullptr;
    decltype(&MWCaptureVideoFrameToVirtualAddressEx) captureFrame = nullptr;
    decltype(&MWGetVideoCaptureStatus) captureStatus = nullptr;
};

// Loaded once and kept for the life of the process (the SDK instance is never torn down).
Api& api() {
    static Api a = [] {
        Api x;
        const auto bundled = programDirectory() / L"runtime" / L"magewell" / L"LibMWCapture.dll";
        if (std::filesystem::exists(bundled)) {
            x.module = LoadLibraryExW(bundled.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (x.module) x.where = bundled.wstring();
        }
        if (!x.module) {
            // Installed by Magewell's own software (driver / SDK runtime).
            x.module = LoadLibraryExW(L"LibMWCapture.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
            if (x.module) x.where = L"LibMWCapture.dll (system)";
        }
        if (!x.module) { x.where = L"LibMWCapture.dll not found"; return x; }
        bool all = true;
        const auto bind = [&](auto& slot, const char* name) {
            slot = reinterpret_cast<std::remove_reference_t<decltype(slot)>>(GetProcAddress(x.module, name));
            if (!slot) { all = false; log::error("magewell", std::format("missing export {}", name)); }
        };
        bind(x.initInstance, "MWCaptureInitInstance");
        bind(x.refreshDevice, "MWRefreshDevice");
        bind(x.channelCount, "MWGetChannelCount");
        bind(x.channelInfo, "MWGetChannelInfoByIndex");
        bind(x.devicePath, "MWGetDevicePath");
        bind(x.openChannel, "MWOpenChannelByPath");
        bind(x.closeChannel, "MWCloseChannel");
        bind(x.pinBuffer, "MWPinVideoBuffer");
        bind(x.unpinBuffer, "MWUnpinVideoBuffer");
        bind(x.startCapture, "MWStartVideoCapture");
        bind(x.stopCapture, "MWStopVideoCapture");
        bind(x.bufferInfo, "MWGetVideoBufferInfo");
        bind(x.frameInfo, "MWGetVideoFrameInfo");
        bind(x.signalStatus, "MWGetVideoSignalStatus");
        bind(x.registerNotify, "MWRegisterNotify");
        bind(x.unregisterNotify, "MWUnregisterNotify");
        bind(x.notifyStatus, "MWGetNotifyStatus");
        bind(x.deviceTime, "MWGetDeviceTime");
        bind(x.captureFrame, "MWCaptureVideoFrameToVirtualAddressEx");
        bind(x.captureStatus, "MWGetVideoCaptureStatus");
        if (!all) { x.where += L" (incomplete exports)"; return x; }
        x.ready = x.initInstance() != FALSE;
        if (!x.ready) x.where += L" (MWCaptureInitInstance failed)";
        log::info("magewell", std::format("runtime {} ready={}", narrow(x.where), x.ready));
        return x;
    }();
    return a;
}

DWORD fourccOf(Format format) {
    switch (format) {
    case Format::Nv12: return MWFOURCC_NV12;
    case Format::Yuy2: return MWFOURCC_YUY2;
    case Format::P010: return MWFOURCC_P010;
    case Format::Bgra: return MWFOURCC_BGRA;
    }
    return 0;
}
#endif

} // namespace

bool isProCaptureDevicePath(std::wstring_view directShowPath) {
    return lower(directShowPath).find(L"ven_1cd7") != std::wstring::npos;
}

bool runtimeAvailable(std::wstring* detail) {
#ifdef VEYRA_HAS_MAGEWELL
    auto& a = api();
    if (detail) *detail = a.where;
    return a.ready;
#else
    if (detail) *detail = L"this build has no Magewell SDK support";
    return false;
#endif
}

void setLowLatencyPreference(bool enabled) {
    if (g_preference.exchange(enabled) != enabled)
        log::info("magewell", std::format("low-latency preference={} (takes effect on the next connect)", enabled));
}
bool lowLatencyPreference() { return g_preference.load(); }

std::wstring statusText() {
    if (g_active.load()) {
        const double ms = g_latencyMs.load();
        return ms > 0 ? std::format(L"低延迟采集中 · 一帧从开始进卡到完整进内存 {:.1f} ms", ms) : std::wstring(L"低延迟采集中");
    }
    std::lock_guard lock(g_statusMutex);
    return g_lastError.empty() ? std::wstring() : L"未启用：" + g_lastError;
}

struct LowLatencyVideo::Impl {
    VideoRequest request;
    Deliver deliver;
    std::thread thread;
    HANDLE stopEvent = nullptr;
    std::atomic<bool> running{false};
    mutable std::mutex statsMutex;
    VideoStats stats;
#ifdef VEYRA_HAS_MAGEWELL
    HCHANNEL channel = nullptr;
    void loop();
#endif
};

LowLatencyVideo::LowLatencyVideo() : p_(std::make_unique<Impl>()) {}
LowLatencyVideo::~LowLatencyVideo() { stop(); }
bool LowLatencyVideo::running() const { return p_->running.load(); }
VideoStats LowLatencyVideo::stats() const { std::lock_guard lock(p_->statsMutex); return p_->stats; }

bool LowLatencyVideo::start(const VideoRequest& request, Deliver deliver, std::wstring& error) {
    stop();
    setLastError({});
    const bool ok = startImpl(request, std::move(deliver), error);
    if (!ok) setLastError(error);
    return ok;
}

bool LowLatencyVideo::startImpl(const VideoRequest& request, Deliver deliver, std::wstring& error) {
#ifdef VEYRA_HAS_MAGEWELL
    auto& a = api();
    if (!a.ready) { error = L"没有可用的美乐威运行库（" + a.where + L"）"; return false; }
    if (!request.width || !request.height || !request.stride || !request.frameBytes ||
        request.frameBytes > (std::numeric_limits<DWORD>::max)() || !deliver) {
        error = L"采集格式无效";
        return false;
    }
    a.refreshDevice();
    const int count = a.channelCount();
    const std::wstring wantedPath = lower(request.directShowPath), wantedInstance = instanceOf(request.directShowPath);
    int exact = -1, byInstance = -1, proCount = 0, onlyPro = -1, instanceMatches = 0;
    for (int i = 0; i < count; ++i) {
        MWCAP_CHANNEL_INFO info{};
        if (a.channelInfo(i, &info) != MW_SUCCEEDED) continue;
        wchar_t path[MAX_PATH * 2]{};
        if (a.devicePath(i, path) != MW_SUCCEEDED) continue;
        const bool pro = info.wFamilyID == MW_FAMILY_ID_PRO_CAPTURE;
        log::info("magewell", std::format("channel {} family={} product={} board={} channel={} path={}", i, info.wFamilyID,
            info.szProductName, int(info.byBoardIndex), int(info.byChannelIndex), narrow(path)));
        if (!pro) continue;
        ++proCount; onlyPro = i;
        if (lower(path) == wantedPath) exact = i;
        if (instanceOf(path) == wantedInstance) { byInstance = i; ++instanceMatches; }
    }
    int chosen = exact >= 0 ? exact : instanceMatches == 1 ? byInstance : -1;
    if (chosen < 0 && proCount == 1) {
        chosen = onlyPro;
        log::warn("magewell", "no channel path matched the DirectShow device; using the only Pro Capture channel");
    }
    log::info("magewell", std::format("directshow path={} chosen channel={} (exact={} instance={}x{} pro={})",
        narrow(request.directShowPath), chosen, exact, byInstance, instanceMatches, proCount));
    if (chosen < 0) { error = L"找不到与所选设备对应的美乐威 Pro Capture 通道"; return false; }
    wchar_t path[MAX_PATH * 2]{};
    if (a.devicePath(chosen, path) != MW_SUCCEEDED) {
        error = L"打开美乐威采集通道失败";
        return false;
    }
    p_->channel = a.openChannel(path);
    if (!p_->channel) { error = L"打开美乐威采集通道失败"; return false; }
    p_->request = request;
    p_->deliver = std::move(deliver);
    {
        std::lock_guard lock(p_->statsMutex);
        p_->stats = {};
    }
    p_->stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!p_->stopEvent) {
        log::error("magewell", std::format("CreateEventW(stop) failed error={}", GetLastError()));
        error = L"美乐威采集启动失败";
        stop();
        return false;
    }
    p_->running = true;
    g_latencyMs = 0;
    try {
        p_->thread = std::thread([impl = p_.get()] { impl->loop(); });
    } catch (const std::system_error& e) {
        log::error("magewell", std::format("capture thread start failed: {}", e.what()));
        error = L"美乐威采集启动失败";
        stop();
        return false;
    }
    log::info("magewell", std::format("low-latency video started {}x{} fourcc=0x{:08X} stride={} bytes={} partialLines={}",
        request.width, request.height, fourccOf(request.format), request.stride, request.frameBytes,
        partialLinesFor(request.height, request.frameBytes)));
    return true;
#else
    (void)request; (void)deliver;
    error = L"此版本未包含美乐威 SDK 支持";
    return false;
#endif
}

void LowLatencyVideo::stop() {
    if (p_->stopEvent) SetEvent(p_->stopEvent);
    if (p_->thread.joinable()) p_->thread.join();
#ifdef VEYRA_HAS_MAGEWELL
    if (p_->channel) { api().closeChannel(p_->channel); p_->channel = nullptr; }
#endif
    if (p_->stopEvent) { CloseHandle(p_->stopEvent); p_->stopEvent = nullptr; }
    g_active = false;
    p_->running = false;
}

#ifdef VEYRA_HAS_MAGEWELL
void LowLatencyVideo::Impl::loop() {
    using Clock = std::chrono::steady_clock;
    auto& a = api();
    DWORD taskIndex = 0;
    HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Capture", &taskIndex);
    if (!mmcss) log::warn("magewell", std::format("Capture MMCSS unavailable error={}", GetLastError()));
    struct Slot {
        uint8_t* buffer = nullptr;
        bool pinned = false;
        bool discontinuity = false;
        int64_t frameStart = 0;
        uint32_t framePeriod = 0;
        unsigned ringIndex = 0, ringSize = 0;
        MWCAP_PTR64 context = 0;
        Clock::time_point armedAt;
    };
    std::array<Slot, 2> slots{};
    HANDLE captureEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HANDLE notifyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    HNOTIFY notify = 0;
    bool capturing = false;
    const DWORD fourcc = fourccOf(request.format);
    const int partialLines = partialLinesFor(request.height, request.frameBytes);
    const wchar_t* failure = nullptr;
    VideoStats counters{};
    CaptureCadence cadence;
    auto lastProgress = Clock::now();
    auto reportStart = lastProgress;
    uint64_t reportFrames = 0;
    double dmaMs = 0, publishMs = 0;
    do {
        failure = L"美乐威采集启动失败，已回到普通采集（详见日志 magewell）";
        if (!captureEvent || !notifyEvent) { log::error("magewell", "capture event allocation failed"); break; }
        for (auto& slot : slots) {
            slot.buffer = static_cast<uint8_t*>(VirtualAlloc(nullptr, request.frameBytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
            if (!slot.buffer) { log::error("magewell", std::format("DMA buffer allocation failed error={}", GetLastError())); break; }
            const auto result = a.pinBuffer(channel, slot.buffer, DWORD(request.frameBytes));
            if (result != MW_SUCCEEDED) { log::error("magewell", std::format("MWPinVideoBuffer failed result={}", int(result))); break; }
            slot.pinned = true;
        }
        if (!slots.back().pinned) break;
        const auto started = a.startCapture(channel, captureEvent);
        if (started != MW_SUCCEEDED) { log::error("magewell", std::format("MWStartVideoCapture failed result={}", int(started))); break; }
        capturing = true;
        notify = a.registerNotify(channel, notifyEvent,
            MWCAP_NOTIFY_VIDEO_FRAME_BUFFERING | MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE | MWCAP_NOTIFY_VIDEO_INPUT_SOURCE_CHANGE);
        if (!notify) { log::error("magewell", "MWRegisterNotify failed"); break; }
        failure = nullptr;
        bool discontinuity = true;
        bool havePreviousStart = false;
        int64_t previousStart = 0;
        uintptr_t nextContext = 0;
        const auto arm = [&](unsigned index, bool wait) {
            if (Clock::now() - lastProgress >= std::chrono::seconds(2)) {
                ++counters.timeouts;
                log::warn("magewell", "no completed frame for two seconds; returning to DirectShow");
                failure = L"美乐威低延迟采集超时，已回到普通采集";
                return ArmResult::Stop;
            }
            HANDLE waitOn[] = {stopEvent, notifyEvent};
            const DWORD woken = WaitForMultipleObjects(2, waitOn, FALSE, wait ? 250 : 0);
            if (woken == WAIT_OBJECT_0) return ArmResult::Stop;
            if (woken == WAIT_TIMEOUT) return ArmResult::NoFrame;
            if (woken != WAIT_OBJECT_0 + 1) {
                log::error("magewell", std::format("frame notification wait failed wait={} error={}", woken, GetLastError()));
                failure = L"美乐威低延迟采集失败，已回到普通采集";
                return ArmResult::Stop;
            }
            ULONGLONG bits = 0;
            if (a.notifyStatus(channel, notify, &bits) != MW_SUCCEEDED) return ArmResult::NoFrame;
            if (bits & (MWCAP_NOTIFY_VIDEO_SIGNAL_CHANGE | MWCAP_NOTIFY_VIDEO_INPUT_SOURCE_CHANGE)) {
                discontinuity = true;
                havePreviousStart = false;
                ++counters.signalChanges;
            }
            if (!(bits & MWCAP_NOTIFY_VIDEO_FRAME_BUFFERING)) return ArmResult::NoFrame;
            MWCAP_VIDEO_SIGNAL_STATUS signal{};
            if (a.signalStatus(channel, &signal) != MW_SUCCEEDED || signal.state != MWCAP_VIDEO_SIGNAL_LOCKED) {
                discontinuity = true;
                havePreviousStart = false;
                return ArmResult::NoFrame;
            }
            MWCAP_VIDEO_BUFFER_INFO bufferInfo{};
            if (a.bufferInfo(channel, &bufferInfo) != MW_SUCCEEDED) return ArmResult::NoFrame;
            MWCAP_VIDEO_FRAME_INFO frameInfo{};
            if (a.frameInfo(channel, bufferInfo.iNewestBuffering, &frameInfo) != MW_SUCCEEDED ||
                frameInfo.state == MWCAP_VIDEO_FRAME_STATE_INITIAL) return ArmResult::NoFrame;
            const int64_t frameStart = frameInfo.allFieldStartTimes[0];
            if (havePreviousStart && frameStart == previousStart) return ArmResult::NoFrame;

            auto& slot = slots[index];
            // The SDK returns this opaque cookie in the DMA completion status.
            const auto context = reinterpret_cast<MWCAP_PTR64>(++nextContext);
            slot.armedAt = Clock::now();
            const auto result = a.captureFrame(channel, bufferInfo.iNewestBuffering, slot.buffer, DWORD(request.frameBytes), DWORD(request.stride),
                request.bottomUp ? TRUE : FALSE, context, fourcc, int(request.width), int(request.height), 0, partialLines,
                0, nullptr, 0, 100, 0, 100, 0, MWCAP_VIDEO_DEINTERLACE_BLEND, MWCAP_VIDEO_ASPECT_RATIO_IGNORE, nullptr, nullptr, 0, 0,
                MWCAP_VIDEO_COLOR_FORMAT_UNKNOWN, MWCAP_VIDEO_QUANTIZATION_UNKNOWN, MWCAP_VIDEO_SATURATION_UNKNOWN);
            if (result != MW_SUCCEEDED) return ArmResult::NoFrame;
            slot.discontinuity = discontinuity || (havePreviousStart && frameStart < previousStart);
            slot.frameStart = frameStart;
            slot.framePeriod = signal.bInterlaced ? 0 : signal.dwFrameDuration;
            slot.ringIndex = bufferInfo.iNewestBuffering;
            slot.ringSize = bufferInfo.cMaxFrames;
            slot.context = context;
            discontinuity = false;
            previousStart = frameStart;
            havePreviousStart = true;
            if (!wait) ++counters.earlySubmissions;
            return ArmResult::Armed;
        };
        const auto complete = [&](unsigned index) {
            const auto& slot = slots[index];
            const auto deadline = slot.armedAt + std::chrono::milliseconds(200);
            for (;;) {
                const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
                HANDLE partWait[] = {stopEvent, captureEvent};
                // Poll once even at the deadline in case it completed while publishing the previous frame.
                const DWORD part = WaitForMultipleObjects(2, partWait, FALSE, DWORD(std::max<int64_t>(0, remaining)));
                if (part == WAIT_OBJECT_0) return false;
                if (part != WAIT_OBJECT_0 + 1) {
                    if (part != WAIT_TIMEOUT) log::error("magewell", std::format("DMA event wait failed wait={} error={}", part, GetLastError()));
                    break;
                }
                MWCAP_VIDEO_CAPTURE_STATUS status{};
                const auto result = a.captureStatus(channel, &status);
                if (result != MW_SUCCEEDED) {
                    log::error("magewell", std::format("MWGetVideoCaptureStatus failed result={}", int(result)));
                    break;
                }
                if (status.pvContext == slot.context && status.bFrameCompleted) {
                    lastProgress = Clock::now();
                    dmaMs += std::chrono::duration<double, std::milli>(lastProgress - slot.armedAt).count();
                    LONGLONG now = 0;
                    if (a.deviceTime(channel, &now) == MW_SUCCEEDED && slot.frameStart >= 0 && now >= slot.frameStart) {
                        const double ms = double(now - slot.frameStart) / 10000.0;
                        auto& value = counters.frameStartToHostMs;
                        value = value > 0 ? value * 0.97 + ms * 0.03 : ms;
                    }
                    return true;
                }
                if (Clock::now() >= deadline) break;
            }
            // Do not reuse either pinned buffer with an unconfirmed DMA. End the SDK stream
            // and let CaptureCardSource resume the already negotiated DirectShow path.
            ++counters.timeouts;
            log::warn("magewell", std::format("DMA completion failed context={} frameIndex={}; returning to DirectShow", slot.context, slot.ringIndex));
            failure = L"美乐威低延迟采集超时，已回到普通采集";
            return false;
        };
        const auto publish = [&](unsigned index) {
            const auto& slot = slots[index];
            const auto health = cadence.observe(slot.frameStart, slot.framePeriod, slot.ringIndex, slot.ringSize, slot.discontinuity);
            counters.missedInputFrames += health.missed;
            const auto start = Clock::now();
            deliver(slot.buffer, request.frameBytes, double(slot.frameStart) / 1e7, slot.discontinuity);
            publishMs += std::chrono::duration<double, std::milli>(Clock::now() - start).count();
            ++counters.frames;
            {
                std::lock_guard lock(statsMutex);
                stats = counters;
            }
            g_latencyMs = counters.frameStartToHostMs;
            g_active = true;
            if (counters.frames % 300 == 0 || health.fallback) {
                const auto now = Clock::now();
                const auto n = counters.frames - reportFrames;
                const auto elapsed = std::chrono::duration<double>(now - reportStart).count();
                log::info("magewell", std::format("frames={} sdkFps={:.2f} inputPeriod100ns={} missedInputFrames={} earlySubmissions={} "
                    "dmaWaitMs={:.2f} publishMs={:.2f} frameStartToHostMs={:.2f} fallback={}",
                    counters.frames, elapsed > 0 ? n / elapsed : 0, slot.framePeriod, counters.missedInputFrames,
                    counters.earlySubmissions, dmaMs / n, publishMs / n, counters.frameStartToHostMs, health.fallback));
                reportStart = now;
                reportFrames = counters.frames;
                dmaMs = publishMs = 0;
            }
            if (health.fallback) {
                log::warn("magewell", "sustained missing device frames; returning to DirectShow at the negotiated format");
                failure = L"美乐威低延迟模式持续丢帧，已回到普通采集";
                return false;
            }
            return true;
        };
        pumpFrames(arm, complete, publish);
    } while (false);
    if (notify) a.unregisterNotify(channel, notify);
    bool stopped = !capturing;
    if (capturing) {
        const auto result = a.stopCapture(channel);
        stopped = result == MW_SUCCEEDED;
        if (!stopped) log::error("magewell", std::format("MWStopVideoCapture failed result={}", int(result)));
    }
    if (stopped) {
        for (auto& slot : slots) {
            if (!slot.pinned) continue;
            const auto unpinned = a.unpinBuffer(channel, slot.buffer);
            if (unpinned != MW_SUCCEEDED) log::warn("magewell", std::format("MWUnpinVideoBuffer failed result={}", int(unpinned)));
        }
    }
    // Closing the SDK channel also releases any pins left by partially initialized/failed capture.
    // Do it before freeing virtual memory and before signaling that DirectShow can resume.
    a.closeChannel(channel);
    channel = nullptr;
    for (auto& slot : slots) if (slot.buffer) VirtualFree(slot.buffer, 0, MEM_RELEASE);
    if (captureEvent) CloseHandle(captureEvent);
    if (notifyEvent) CloseHandle(notifyEvent);
    if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    {
        std::lock_guard lock(statsMutex);
        stats = counters;
    }
    log::info("magewell", std::format("low-latency video stopped frames={} timeouts={} signalChanges={} missedInputFrames={} earlySubmissions={} "
        "frameStartToHostMs={:.2f}", counters.frames, counters.timeouts, counters.signalChanges, counters.missedInputFrames,
        counters.earlySubmissions, counters.frameStartToHostMs));
    if (failure) setLastError(failure);
    g_active = false;
    running = false;
}
#endif

} // namespace veyra::source::magewell
