#include "veyra/source/XcapAudio.h"
#include "veyra/Log.h"
#include "xcap_edid.h"
#include "veyra/sink/BitstreamAudio.h"
#include "veyra/sink/ExclusivePassthrough.h"

#include <windows.h>
#include <audioclient.h>
#include <avrt.h>
#include <ks.h>
#include <ksmedia.h>
#include <mmdeviceapi.h>
#include <functiondiscoverykeys_devpkey.h>
#include <wrl/client.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>
#include <deque>
#include <format>
#include <mutex>
#include <thread>

namespace veyra::source::xcapture {
using Microsoft::WRL::ComPtr;

const wchar_t* codecName(Codec c) {
    switch (c) {
    case Codec::Ac3: return L"Dolby Digital";
    case Codec::Dts: return L"DTS";
    case Codec::DtsHd: return L"DTS-HD";
    case Codec::Eac3: return L"Dolby Digital Plus";
    case Codec::Mat: return L"Dolby MAT / TrueHD";
    default: return L"PCM";
    }
}

sink::BitstreamKind bitstreamKind(Codec c) {
    switch (c) {
    case Codec::Ac3: return sink::BitstreamKind::Ac3;
    case Codec::Eac3: return sink::BitstreamKind::Eac3;
    case Codec::Mat: return sink::BitstreamKind::TrueHd;
    case Codec::DtsHd: return sink::BitstreamKind::DtsHd;
    case Codec::Dts: return sink::BitstreamKind::Dts;
    default: return sink::BitstreamKind::None;
    }
}

// ---------------------------------------------------------------- parser ----

namespace {
constexpr unsigned kLostAfter = 24576;   // frames without a preamble before the stream counts as PCM again (0.13 s at 192 kHz)
bool codecOf(uint16_t pc, unsigned channels, Codec& out) {
    switch (pc & 0x1F) {
    case 1: out = Codec::Ac3; return true;
    case 11: case 12: case 13: out = Codec::Dts; return true;
    case 17: out = channels == 8 ? Codec::DtsHd : Codec::Dts; return true;
    case 21: out = Codec::Eac3; return true;
    case 22: out = Codec::Mat; return true;
    }
    return false;
}
}

void AudioParser::reset() {
    seen_ = channels_ = sinceSync_ = 0;
    waitPc_ = false;
    codec_ = Codec::Pcm;
    pcm_.clear();
    raw_.clear();
}

void AudioParser::resync() {
    seen_ = 0;
    waitPc_ = false;
}

void AudioParser::push(const uint32_t* words, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const uint32_t w = words[i];
        const unsigned ch = (w >> 24) & 7;
        if (ch == 0 && seen_) frame(seen_);
        if (ch != seen_) { seen_ = 0; if (ch) continue; }      // out of step: wait for channel 0
        cur_[ch] = int32_t(w << 8) >> 8;
        seen_ = ch + 1;
        if (seen_ == 8) frame(8);
    }
    flush();
}

void AudioParser::frame(unsigned channels) {
    seen_ = 0;
    if (channels & 1) return;
    if (channels != channels_) { flush(); channels_ = channels; if (out_.changed) out_.changed(codec_, channels_); }
    const auto word = [&](unsigned c) { return uint16_t(uint32_t(cur_[c]) >> 8); };
    // IEC 61937: Pa Pb on a channel pair, Pc Pd on the next pair. Eight channels carry one HBR
    // stream as four consecutive pairs; a burst normally starts on the first one.
    Codec found = codec_;
    bool have = false, sync = false;
    const bool afterPreamble = waitPc_;      // the frame before this one ended with Pa Pb
    if (waitPc_) have = codecOf(word(0), channels, found);
    waitPc_ = false;
    for (unsigned c = 0; c + 1 < channels && !sync; c += 2) {
        if (word(c) != 0xF872 || word(c + 1) != 0x4E1F) continue;
        sync = true;
        if (c + 2 < channels) have = codecOf(word(c + 2), channels, found); else waitPc_ = true;
    }
    if (sync) sinceSync_ = 0; else if (sinceSync_ < kLostAfter) ++sinceSync_;
    if (have && found != codec_) {
        // Pa Pb of this burst sit in the frame before (always so with two channels): they belong to the new stream.
        uint16_t lead[8]{};
        unsigned leadWords = 0;
        if (afterPreamble && codec_ == Codec::Pcm) {
            if (pcm_.size() >= channels) pcm_.resize(pcm_.size() - channels);       // that frame, queued as silence
            leadWords = channels;
            lead[channels - 2] = 0xF872;
            lead[channels - 1] = 0x4E1F;
        } else if (afterPreamble && raw_.size() >= channels) {
            leadWords = channels;
            std::copy(raw_.end() - channels, raw_.end(), lead);
            raw_.resize(raw_.size() - channels);
        }
        flush();
        codec_ = found;
        if (out_.changed) out_.changed(codec_, channels_);
        raw_.insert(raw_.end(), lead, lead + leadWords);
    }
    if (codec_ != Codec::Pcm && sinceSync_ >= kLostAfter) { flush(); codec_ = Codec::Pcm; if (out_.changed) out_.changed(codec_, channels_); }
    if (codec_ == Codec::Pcm) {
        // a preamble that is not (yet) understood must not reach the speakers
        const bool silence = sync || waitPc_;
        for (unsigned c = 0; c < channels; ++c) pcm_.push_back(silence ? 0 : cur_[c]);
    } else {
        for (unsigned c = 0; c < channels; ++c) raw_.push_back(word(c));
    }
}

void AudioParser::flush() {
    if (!pcm_.empty() && channels_ && out_.pcm) out_.pcm(pcm_.data(), channels_, pcm_.size() / channels_);
    if (!raw_.empty() && out_.carrier) out_.carrier(codec_, channels_, raw_.data(), raw_.size());
    pcm_.clear();
    raw_.clear();
}

// ------------------------------------------------------------- endpoints ----

namespace {
struct Carrier { GUID subtype; unsigned rate, channels, encodedChannels; };

// More than one subtype can describe the same wire format; the first accepted one wins.
std::vector<Carrier> carriers(Codec codec) {
    const GUID mat21 = {0x0000030c, 0x0cea, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}}; // MAT 2.1
    const GUID mat20 = {0x0000010c, 0x0cea, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}}; // MAT 2.0
    const GUID eac3Atmos = {0x0000010a, 0x0cea, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
    switch (codec) {
    case Codec::Ac3: return {{KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL, 48000, 2, 6}};
    case Codec::Dts: return {{KSDATAFORMAT_SUBTYPE_IEC61937_DTS, 48000, 2, 6}};
    case Codec::DtsHd: return {{KSDATAFORMAT_SUBTYPE_IEC61937_DTS_HD, 192000, 8, 8}};
    case Codec::Eac3: return {{eac3Atmos, 192000, 2, 8}, {KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_DIGITAL_PLUS, 192000, 2, 8}};
    case Codec::Mat: return {{mat21, 192000, 8, 8}, {mat20, 192000, 8, 8}, {KSDATAFORMAT_SUBTYPE_IEC61937_DOLBY_MLP, 192000, 8, 8}};
    default: return {};
    }
}

WAVEFORMATEXTENSIBLE_IEC61937 waveOf(const Carrier& c) {
    WAVEFORMATEXTENSIBLE_IEC61937 w{};
    auto& f = w.FormatExt.Format;
    f.wFormatTag = WAVE_FORMAT_EXTENSIBLE;
    f.nChannels = WORD(c.channels);
    f.nSamplesPerSec = c.rate;
    f.wBitsPerSample = 16;
    f.nBlockAlign = WORD(c.channels * 2);
    f.nAvgBytesPerSec = c.rate * c.channels * 2;
    f.cbSize = sizeof(w) - sizeof(WAVEFORMATEX);
    w.FormatExt.Samples.wValidBitsPerSample = 16;
    w.FormatExt.dwChannelMask = c.channels == 8 ? KSAUDIO_SPEAKER_7POINT1_SURROUND : KSAUDIO_SPEAKER_STEREO;
    w.FormatExt.SubFormat = c.subtype;
    w.dwEncodedSamplesPerSec = 48000;
    w.dwEncodedChannelCount = c.encodedChannels;
    return w;
}

std::wstring friendlyName(IMMDevice* device) {
    ComPtr<IPropertyStore> store;
    PROPVARIANT v;
    PropVariantInit(&v);
    std::wstring name;
    if (SUCCEEDED(device->OpenPropertyStore(STGM_READ, &store)) && SUCCEEDED(store->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR) name = v.pwszVal;
    PropVariantClear(&v);
    return name;
}

// The TV's endpoint first, then the default one, then every other active one.
std::vector<ComPtr<IMMDevice>> endpoints() {
    std::vector<ComPtr<IMMDevice>> out;
    ComPtr<IMMDeviceEnumerator> e;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&e)))) return out;
    ComPtr<IMMDevice> d;
    std::wstring first;
    if (SUCCEEDED(e->GetDefaultAudioEndpoint(eRender, eConsole, &d))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(d->GetId(&id))) { first = id; CoTaskMemFree(id); }
        out.push_back(d);
    }
    ComPtr<IMMDeviceCollection> all;
    UINT n = 0;
    if (SUCCEEDED(e->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &all)) && SUCCEEDED(all->GetCount(&n)))
        for (UINT i = 0; i < n; ++i) {
            ComPtr<IMMDevice> x;
            LPWSTR id = nullptr;
            if (FAILED(all->Item(i, &x)) || FAILED(x->GetId(&id))) continue;
            if (first != id) out.push_back(x);
            CoTaskMemFree(id);
        }
    const auto tv = tvName();
    if (!tv.empty()) std::stable_partition(out.begin(), out.end(), [&](const auto& d) { return friendlyName(d.Get()).find(tv) != std::wstring::npos; });
    return out;
}

bool accepts(IMMDevice* device, const Carrier& c) {
    ComPtr<IAudioClient> client;
    if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) return false;
    const auto w = waveOf(c);
    return client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &w.FormatExt.Format, nullptr) == S_OK;
}

std::atomic<unsigned> g_ingress{0};
std::atomic<int> g_delay{40};
std::atomic<bool> g_auto{true};
std::atomic<float> g_video{0.f};         // smoothed picture delay, ms
}

std::wstring tvName() {
    wchar_t v[256]{};
    GetEnvironmentVariableW(L"VEYRA_XCAP_TV", v, 256);
    const auto monitors = xcap::activeMonitors();
    const auto* tv = xcap::chooseTv(monitors, v);
    return tv ? tv->name : std::wstring(v);
}

PassthroughSupport passthroughSupport() {
    const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    PassthroughSupport best;
    for (auto& device : endpoints()) {
        PassthroughSupport s;
        const auto any = [&](Codec c) { for (const auto& k : carriers(c)) if (accepts(device.Get(), k)) return true; return false; };
        s.ac3 = any(Codec::Ac3);
        s.eac3 = any(Codec::Eac3);
        s.mat = any(Codec::Mat);
        s.dts = any(Codec::Dts);
        s.dtsHd = any(Codec::DtsHd);
        s.endpoint = friendlyName(device.Get());
        const auto score = [](const PassthroughSupport& x) { return int(x.mat) * 8 + int(x.eac3) * 4 + int(x.ac3) * 2 + int(x.dts) + int(x.dtsHd); };
        if (best.endpoint.empty() || score(s) > score(best)) best = s;
        if (!tvName().empty() && s.endpoint.find(tvName()) != std::wstring::npos) { best = s; break; }
    }
    if (SUCCEEDED(co)) CoUninitialize();
    return best;
}

void setAudioIngress(unsigned mode) { g_ingress = mode > 2 ? 0 : mode; }
unsigned audioIngress() { return g_ingress; }
void setAudioDelay(int ms) { g_delay = std::clamp(ms, 0, 500); }
void setAutoAudioDelay(bool on) { g_auto = on; }
void videoLatency(double ms) {
    if (!(ms > 0) || ms > 500) return;
    const float old = g_video;
    g_video = old <= 0 ? float(ms) : old + (float(ms) - old) / 64.f;
}
int audioDelay() {
    const float v = g_video;
    // The middle line leaves the console about 8 ms before the last one, and is scanned out about
    // 4 ms after the frame is presented (120 Hz): that is how late the picture is against a direct cable.
    if (g_auto && v > 0) return std::clamp(int(v + 12.5f), 20, 250);
    return g_delay;
}
PassthroughSupport offeredFormats(const PassthroughSupport& support) {
    PassthroughSupport s = support;
    if (g_ingress == 1) s.ac3 = s.eac3 = s.mat = s.dts = s.dtsHd = false;
    if (g_ingress == 2) s.ac3 = s.eac3 = s.mat = s.dts = s.dtsHd = true;
    // automatic: compressed audio only when a receiver can take Atmos from this PC; multichannel PCM is better than plain AC-3
    else if (!s.mat && !s.eac3) s.ac3 = s.dts = s.dtsHd = false;
    return s;
}

// ------------------------------------------------------- carrier decoder ----

CarrierDecoder::CarrierDecoder() = default;
CarrierDecoder::~CarrierDecoder() = default;
unsigned CarrierDecoder::channels() const { return decoder_ ? decoder_->channels() : 0; }
bool CarrierDecoder::open(Codec codec, unsigned) {
    codec_ = codec;
    state_ = 0;
    major_ = false;
    stream_.clear();
    decoder_ = std::make_unique<sink::BitstreamDecoder>();
    if (!decoder_->open(bitstreamKind(codec), false)) { decoder_.reset(); return false; }
    return true;
}
void CarrierDecoder::push(const uint16_t* words, size_t count, std::vector<float>& out) {
    if (!decoder_) return;
    for (size_t i = 0; i < count; ++i) {
        const uint16_t w = words[i];
        switch (state_) {
        case 0: if (w == 0x4E1F && last_ == 0xF872) state_ = 1; break;
        case 1: type_ = w & 0x1F; state_ = 2; break;
        case 2: {
            Codec c;
            if (!codecOf(type_, codec_ == Codec::DtsHd || codec_ == Codec::Mat ? 8 : 2, c) || c != codec_) { state_ = 0; break; }
            want_ = type_ == 21 || type_ == 22 || type_ == 17 ? w : (size_t(w) + 7) / 8;       // bytes for E-AC-3, MAT and DTS-HD, bits for the others
            payload_.clear();
            state_ = want_ ? 3 : 0;
            break;
        }
        default:
            payload_.push_back(uint8_t(w >> 8));
            payload_.push_back(uint8_t(w));
            if (payload_.size() >= want_) { payload_.resize(want_); state_ = 0; burst(out); }
        }
        last_ = w;
    }
}
void CarrierDecoder::burst(std::vector<float>& out) {
    ++bursts_;
    if (codec_ == Codec::Mat) { trueHd(payload_.data(), payload_.size(), out); return; }
    const uint8_t* p = payload_.data();
    size_t n = payload_.size();
    static constexpr uint8_t kDtsHd[10] = {1, 0, 0, 0, 0, 0, 0, 0, 0xFE, 0xFE};
    if (codec_ == Codec::DtsHd && n > 12 && !std::memcmp(p, kDtsHd, 10)) {
        const size_t size = size_t(p[10]) << 8 | p[11];
        p += 12;
        n = std::min(n - 12, size);
    }
    if (n) decoder_->pushFramed(p, n, out);
}
// A MAT frame is TrueHD access units with zero padding, cut by a start, a middle and an end code. Units
// may continue in the next frame, so the codes are removed and the rest is read as one stream.
void CarrierDecoder::trueHd(const uint8_t* data, size_t size, std::vector<float>& out) {
    static constexpr uint8_t kStart[20] = {0x07, 0x9E, 0x00, 0x03, 0x84, 0x01, 0x01, 0x01, 0x80, 0x00, 0x56, 0xA5, 0x3B, 0xF4, 0x81, 0x83, 0x49, 0x80, 0x77, 0xE0};
    static constexpr uint8_t kMiddle[12] = {0xC3, 0xC1, 0x42, 0x49, 0x3B, 0xFA, 0x82, 0x83, 0x49, 0x80, 0x77, 0xE0};
    static constexpr uint8_t kEnd[4] = {0xC3, 0xC2, 0xC0, 0xC4};
    if (size < 61000 || std::memcmp(data, kStart, sizeof(kStart))) { stream_.clear(); major_ = false; return; }
    const uint8_t* at = data + sizeof(kStart);
    const uint8_t* end = data + size;
    const uint8_t* stop = std::search(end - 24, end, kEnd, kEnd + 4);
    const uint8_t* middle = std::search(data + 30000, data + 31500, kMiddle, kMiddle + sizeof(kMiddle));
    if (middle == data + 31500) { stream_.insert(stream_.end(), at, stop); }
    else { stream_.insert(stream_.end(), at, middle); stream_.insert(stream_.end(), middle + sizeof(kMiddle), stop); }
    size_t i = 0;
    while (i + 8 <= stream_.size()) {
        const uint8_t* u = &stream_[i];
        if (!u[0] && !u[1]) { i += 2; continue; }                        // padding
        const size_t length = (size_t(u[0] & 0x0F) << 8 | u[1]) * 2;
        if (length < 8) { i += 2; continue; }
        if (i + length > stream_.size()) break;
        const bool major = u[4] == 0xF8 && u[5] == 0x72 && u[6] == 0x6F && (u[7] & 0xFE) == 0xBA;
        if (!major_ && !major) { i += 2; continue; }                     // find a place to start
        major_ = true;
        if (!decoder_->pushFramed(u, length, out)) { major_ = false; decoder_->reset(); i += 2; continue; }
        i += length;
    }
    stream_.erase(stream_.begin(), stream_.begin() + ptrdiff_t(i));
    if (stream_.size() > 200000) { stream_.clear(); major_ = false; }
}

// --------------------------------------------------------- carrier queue ----

void CarrierQueue::reset(unsigned channels, unsigned rate, size_t periodWords) {
    *this = CarrierQueue{};
    channels_ = channels;
    rate_ = size_t(rate) * channels;
    period_ = periodWords;
}

void CarrierQueue::push(const uint16_t* words, size_t count) {
    for (size_t i = 0; i < count; ++i) {
        const uint16_t w = words[i];
        queue_.push_back(w);
        if (current_.size() <= 2 * kLongest) current_.push_back(w);
        ++tail_;
        // Sync words before the known burst length is over are payload that looks like them, or follow a gap in
        // the input: no boundary. A real change of length is seen at the first preamble at or after that length.
        if (w == 0x4E1F && last_ == 0xF872 && !(tail_ & 1) && current_.size() >= 64 && !(burst_ && current_.size() - 2 < burst_)) {
            const size_t length = current_.size() - 2;                  // the burst that just ended
            const bool whole = synced_ && length <= kLongest && length % channels_ == 0;
            // only a length seen twice in a row is trusted for repeating or leaving out a burst
            if (whole && length == lastLength_) { burst_ = length; previous_.assign(current_.begin(), current_.end() - 2); }
            else { burst_ = 0; previous_.clear(); }
            lastLength_ = whole ? length : 0;
            uint64_t at = tail_ - 2;
            if (repeat_ && burst_) {
                queue_.insert(queue_.end() - 2, previous_.begin(), previous_.end());
                starts_.push_back(at);
                tail_ += burst_;
                at += burst_;
                ++repeated;
                settle();
            }
            repeat_ = false;
            starts_.push_back(at);
            current_.assign({0xF872, 0x4E1F});
            synced_ = true;
        }
        last_ = w;
    }
    correct();
    const size_t most = target_ + period_ + std::max(burst_ * 2, rate_ * 120 / 1000);     // far off (format switch, stall): start over
    if (queue_.size() > most) {
        size_t drop = queue_.size() - target_ - period_;
        drop -= drop % channels_;
        queue_.erase(queue_.begin(), queue_.begin() + ptrdiff_t(drop));
        head_ += drop;
        low_ = ~size_t(0);
        ++trims;
    }
}

void CarrierQueue::pull(uint16_t* out, size_t count) {
    while (!starts_.empty() && starts_.front() < head_) starts_.pop_front();
    if (!primed_ && queue_.size() >= target_ + count) {
        primed_ = true;
        low_ = ~size_t(0);
        windowEnd_ = 0;
    }
    if (!primed_ || queue_.size() < count) {
        if (primed_) { primed_ = false; ++underruns; }          // refill to the delay before resuming
        std::memset(out, 0, count * 2);
        return;
    }
    std::copy_n(queue_.begin(), count, out);
    queue_.erase(queue_.begin(), queue_.begin() + ptrdiff_t(count));
    head_ += count;
    low_ = std::min(low_, queue_.size());
}

void CarrierQueue::settle() {
    windowEnd_ = 0;
    pauseUntil_ = tail_ + rate_;                      // at most one correction a second
}

// The lowest fill over half a second is the delay without arrival jitter.
void CarrierQueue::correct() {
    if (!primed_ || !burst_) return;
    if (!windowEnd_) { windowEnd_ = tail_ + rate_ / 2; low_ = ~size_t(0); return; }
    if (tail_ < windowEnd_) return;
    if (low_ != ~size_t(0) && tail_ >= pauseUntil_) {
        const int64_t error = int64_t(low_) - int64_t(target_), burst = int64_t(burst_);
        const int64_t lowLimit = std::min(burst * 55 / 100, int64_t(target_) / 2);
        const int64_t highLimit = quiet_ ? std::max(burst * 55 / 100, burst - lowLimit + burst / 20) : std::max(burst * 9 / 10, burst - lowLimit + burst / 10);
        if (error < -lowLimit) { repeat_ = true; windowEnd_ = tail_ + rate_ / 2; low_ = ~size_t(0); return; }   // at the next preamble
        if (error > highLimit) {
            while (!starts_.empty() && starts_.front() < head_) starts_.pop_front();
            if (starts_.size() < 2) return;
            const uint64_t a = starts_[0], b = starts_[1];
            if (b - a != burst_ || (a - head_) % channels_) return;
            const auto first = queue_.begin() + ptrdiff_t(a - head_);
            queue_.erase(first, first + ptrdiff_t(burst_));
            starts_.erase(starts_.begin());
            for (auto& s : starts_) s -= burst_;
            tail_ -= burst_;
            ++dropped;
            settle();
            return;
        }
    }
    windowEnd_ = tail_ + rate_ / 2;
    low_ = ~size_t(0);
}

// ----------------------------------------------------------- passthrough ----

struct CarrierSink::Impl {
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    HANDLE event = nullptr, stop = nullptr;
    std::thread thread;
    std::atomic<bool> dead{false};          // set by the render thread when the endpoint is gone
    std::mutex mutex;
    CarrierQueue queue;
    unsigned channels = 2, rate = 48000;
    double endpointMs = 0;
    UINT32 bufferFrames = 0;
    // decodes a copy of the stream to find quiet moments for corrections
    std::unique_ptr<CarrierDecoder> meter;
    std::vector<float> meterOut;
    uint64_t quietFor = 0;

    size_t targetWords() const { return size_t(std::max(5.0, audioDelay() - endpointMs) * rate / 1000) * channels; }
    void loop() {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        DWORD task = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
        HANDLE waits[] = {stop, event};
        // The endpoint can go away under the stream: the TV or the receiver is switched off, or Windows lists
        // its devices again. Then no event comes any more, or every buffer call fails. The owner sees failed()
        // and opens the stream again; until then the PCM output must not keep waiting for this one.
        const char* why = nullptr;
        HRESULT hr = S_OK;
        unsigned failures = 0;
        for (;;) {
            const DWORD woke = WaitForMultipleObjects(2, waits, FALSE, 2000);
            if (woke == WAIT_OBJECT_0) break;
            if (woke != WAIT_OBJECT_0 + 1) { why = "no buffer event for 2 s"; break; }
            BYTE* data = nullptr;
            hr = render->GetBuffer(bufferFrames, &data);
            if (SUCCEEDED(hr)) {
                auto* out = reinterpret_cast<uint16_t*>(data);
                {
                    std::lock_guard lock(mutex);
                    queue.setTarget(targetWords());
                    queue.pull(out, size_t(bufferFrames) * channels);
                }
                hr = render->ReleaseBuffer(bufferFrames, 0);
            }
            if (SUCCEEDED(hr)) { failures = 0; continue; }
            if (hr == AUDCLNT_E_DEVICE_INVALIDATED || hr == AUDCLNT_E_RESOURCES_INVALIDATED || hr == AUDCLNT_E_SERVICE_NOT_RUNNING) { why = "endpoint invalidated"; break; }
            if (++failures >= 100) { why = "buffer calls keep failing"; break; }        // half a second
        }
        if (why) {
            dead = true;
            veyra::sink::setExclusivePassthroughActive(false);
            log::warn("xcap-audio", std::format("passthrough stream lost: {} hr=0x{:08X}", why, uint32_t(hr)));
        }
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        if (SUCCEEDED(co)) CoUninitialize();
    }
};

CarrierSink::CarrierSink() : p_(std::make_unique<Impl>()) {}
CarrierSink::~CarrierSink() { close(); }
bool CarrierSink::opened() const { return p_->thread.joinable(); }
bool CarrierSink::failed() const { return p_->dead; }

bool CarrierSink::open(Codec codec, unsigned channels, std::wstring& error) {
    close();
    auto& p = *p_;
    p.dead = false;
    for (auto& device : endpoints()) for (const auto& c : carriers(codec)) {
        if (c.channels != channels) continue;
        const auto w = waveOf(c);
        ComPtr<IAudioClient> client;
        if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client)) ||
            client->IsFormatSupported(AUDCLNT_SHAREMODE_EXCLUSIVE, &w.FormatExt.Format, nullptr) != S_OK) continue;
        REFERENCE_TIME def = 0, minimum = 0;
        client->GetDevicePeriod(&def, &minimum);
        REFERENCE_TIME period = std::max<REFERENCE_TIME>(minimum, 50000);
        // From Initialize on, shared streams on this endpoint are cut off: the PCM output is told first, so that
        // it waits for the release instead of reporting a lost device.
        veyra::sink::setExclusivePassthroughActive(true);
        HRESULT hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, &w.FormatExt.Format, nullptr);
        if (hr == AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED) {
            UINT32 frames = 0;
            client->GetBufferSize(&frames);
            period = REFERENCE_TIME(10000000.0 * frames / c.rate + 0.5);
            client.Reset();
            if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, &client))) { veyra::sink::setExclusivePassthroughActive(false); continue; }
            hr = client->Initialize(AUDCLNT_SHAREMODE_EXCLUSIVE, AUDCLNT_STREAMFLAGS_EVENTCALLBACK, period, period, &w.FormatExt.Format, nullptr);
        }
        if (FAILED(hr)) { veyra::sink::setExclusivePassthroughActive(false); log::warn("xcap-audio", std::format("exclusive Initialize hr=0x{:08X}", uint32_t(hr))); continue; }
        p.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        p.stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (FAILED(client->SetEventHandle(p.event)) || FAILED(client->GetBufferSize(&p.bufferFrames)) || FAILED(client->GetService(IID_PPV_ARGS(&p.render)))) {
            close();
            veyra::sink::setExclusivePassthroughActive(false);
            continue;
        }
        BYTE* data = nullptr;
        if (SUCCEEDED(p.render->GetBuffer(p.bufferFrames, &data))) p.render->ReleaseBuffer(p.bufferFrames, AUDCLNT_BUFFERFLAGS_SILENT);
        p.client = client;
        p.channels = c.channels;
        p.rate = c.rate;
        p.endpointMs = 1500.0 * p.bufferFrames / c.rate;         // one period playing, half of one waiting on average
        p.queue.reset(c.channels, c.rate, size_t(p.bufferFrames) * c.channels);
        p.queue.setTarget(p.targetWords());
        p.meter = std::make_unique<CarrierDecoder>();
        if (!p.meter->open(codec, channels)) p.meter.reset();
        p.quietFor = 0;
        p.thread = std::thread([&p] { p.loop(); });
        client->Start();
        log::info("xcap-audio", std::format("passthrough codec={} channels={} rate={} bufferFrames={} delayMs={}", int(codec), c.channels, c.rate, p.bufferFrames, audioDelay()));
        return true;
    }
    error = std::format(L"没有音频设备接受 {} 直通", codecName(codec));
    return false;
}

void CarrierSink::push(const uint16_t* words, size_t count) {
    auto& p = *p_;
    if (!opened()) return;
    bool quiet = false;
    if (p.meter) {
        p.meterOut.clear();
        p.meter->push(words, count, p.meterOut);
        float peak = 0;
        for (float v : p.meterOut) peak = std::max(peak, std::abs(v));
        // quiet = under -46 dBFS since at least 100 ms
        if (peak > 0.005f) p.quietFor = 0; else if (!p.meterOut.empty()) p.quietFor += p.meterOut.size() / std::max(1u, p.meter->channels());
        quiet = p.quietFor > 4800;
    }
    std::lock_guard lock(p.mutex);
    p.queue.quiet(quiet);
    p.queue.setTarget(p.targetWords());
    p.queue.push(words, count);
}

void CarrierSink::close() {
    auto& p = *p_;
    if (p.stop) SetEvent(p.stop);
    if (p.thread.joinable()) p.thread.join();
    if (p.client) p.client->Stop();
    if (p.client) veyra::sink::setExclusivePassthroughActive(false);
    if (p.client) log::info("xcap-audio", std::format("passthrough closed underruns={} trims={} burstsDropped={} burstsRepeated={}", p.queue.underruns, p.queue.trims, p.queue.dropped, p.queue.repeated));
    p.render.Reset();
    p.client.Reset();
    if (p.event) { CloseHandle(p.event); p.event = nullptr; }
    if (p.stop) { CloseHandle(p.stop); p.stop = nullptr; }
    p.queue.reset(2, 48000, 0);
    p.meter.reset();
}

} // namespace veyra::source::xcapture
