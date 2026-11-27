#include "veyra/source/XcapCapture.h"
#include "veyra/source/XcapAudio.h"
#include "veyra/sink/BitstreamAudio.h"
#include "veyra/Log.h"

#include <windows.h>
#include <avrt.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <format>
#include <mutex>
#include <thread>

#include "xcap.h"
#include "xcap_edid.h"

namespace veyra::source::xcapture {
using decklink::Device;
using decklink::Driver;
using decklink::DriverSink;
using decklink::Frame;
using decklink::Mode;
using decklink::Pixel;
namespace {

constexpr std::wstring_view kHdmi = L"xcap-native:hdmi:";
constexpr std::wstring_view kPattern = L"xcap-native:pattern:";
constexpr unsigned kSlots = 6;          // 100 ms at 60 Hz; the consumer normally holds one
constexpr unsigned kSliceLines = 36;    // 0.27 ms of a 4K60 frame
constexpr unsigned kSpinUs = 60;        // after each wake-up, before sleeping again

struct Timing { uint32_t width, height; int64_t duration, scale; const wchar_t* name; };
constexpr std::array<Timing, 11> kTimings{{     // a mode's id is its place here + 1 and is saved in the settings: append only
    {3840, 2160, 1000, 60000, L"2160p60"}, {3840, 2160, 1001, 60000, L"2160p59.94"},
    {3840, 2160, 1000, 30000, L"2160p30"}, {2560, 1440, 1000, 60000, L"1440p60"},
    {2560, 1440, 1000, 120000, L"1440p120"}, {1920, 1080, 1000, 120000, L"1080p120"},
    {1920, 1080, 1000, 60000, L"1080p60"}, {1920, 1080, 1001, 60000, L"1080p59.94"},
    {2560, 1440, 1001, 60000, L"1440p59.94"}, {2560, 1440, 1001, 120000, L"1440p119.88"},
    {1920, 1080, 1001, 120000, L"1080p119.88"},
}};

bool patternPath(std::wstring_view path) { return path.starts_with(kPattern); }
std::wstring devicePath(std::wstring_view path) {
    return std::wstring(path.substr(patternPath(path) ? kPattern.size() : kHdmi.size()));
}

pipeline::ColorDescription colorOf(Pixel pixel, uint32_t height, const uint32_t (&meta)[XCAP_META_WORDS]) {
    using namespace pipeline;
    ColorDescription c;
    const uint32_t flags = meta[XCAP_META_FLAGS];
    const unsigned eotf = (flags >> 4) & 15;        // CTA-861: 0/1 SDR, 2 PQ, 3 HLG
    const bool bt2020 = (flags & 2) != 0;
    c.pixelFormat = pixel == Pixel::V210 ? SourcePixelFormat::V210 : SourcePixelFormat::Bgra8;
    c.range = (flags & 4) ? ColorRange::Full : ColorRange::Limited;
    c.matrix = bt2020 ? YuvMatrix::BT2020NCL : height > 576 ? YuvMatrix::BT709 : YuvMatrix::BT601;
    c.primaries = bt2020 ? ColorPrimaries::BT2020 : height > 576 ? ColorPrimaries::BT709 : ColorPrimaries::BT601_525;
    c.transfer = (flags & 1) && eotf == 2 ? TransferFunction::PQ : (flags & 1) && eotf == 3 ? TransferFunction::HLG : TransferFunction::BT709;
    c.transferAssumed = !(flags & 1);
    c.preserveSdrCodeValues = !c.isHdrPath();
    if (c.isHdrPath()) {
        c.hdrMaxCllNits = float(meta[XCAP_META_LIGHT] & 0xFFFF);
        c.hdrMaxFallNits = float(meta[XCAP_META_LIGHT] >> 16);
        c.hdrMasteringPeakNits = float(meta[XCAP_META_MASTER_LUM] & 0xFFFF);
    }
    return c;
}

Mode modeOf(size_t index) {
    const auto& t = kTimings[index];
    Mode m;
    m.id = uint32_t(index + 1);
    m.width = t.width;
    m.height = t.height;
    m.duration = t.duration;
    m.scale = t.scale;
    m.name = t.name;
    m.color.range = pipeline::ColorRange::Limited;
    m.color.matrix = pipeline::YuvMatrix::BT709;
    m.color.primaries = pipeline::ColorPrimaries::BT709;
    m.color.transfer = pipeline::TransferFunction::BT709;
    m.color.transferAssumed = m.color.preserveSdrCodeValues = true;
    return m;
}

// What the HDMI firmware on the card currently receives.
struct Detected {
    bool firmware = false, locked = false, rgb = false, usable = false;
    Mode mode;
    uint32_t colour = 0, bits = 0;
    bool same(const Detected& o) const {
        // A rate outside the table is the measured one and moves by a few mHz: the same within 0.02 %
        // (60 and 59.94 are 0.1 % apart).
        const double a = double(mode.scale) * double(o.mode.duration), b = double(o.mode.scale) * double(mode.duration);
        return locked == o.locked && colour == o.colour && mode.width == o.mode.width && mode.height == o.mode.height &&
            std::abs(a - b) <= 0.0002 * std::max(a, b);
    }
};

Detected detect(xcap::Device& device) {
    Detected d;
    uint32_t geom = 0, state = 0, fw = 0, rate = 0;
    const auto word = [&](uint32_t n, uint32_t& v) { return device.read(XCAP_REG_HDMI0 + 4 * n, v); };
    if (!word(XCAP_META_FW, fw) || !fw || fw == ~0u) return d;
    d.firmware = true;
    if (!word(XCAP_META_STATE, state) || !word(XCAP_META_GEOM, geom) || !word(XCAP_META_RATE, rate)) return d;
    d.colour = (state >> 1) & 7;
    d.bits = (state >> 8) & 255;
    d.rgb = d.colour == 0;
    const uint32_t w = geom & 0xFFFF, h = geom >> 16;
    d.locked = (state & 1) && w >= 2 && h && rate > 1000;
    if (!d.locked) return d;
    // 4:2:2 fills v210; RGB up to 8 bits fills BGRX. 4:4:4 and 4:2:0 YCbCr have no packing on the card.
    d.usable = (d.colour == 1 || (d.rgb && d.bits <= 8)) && w <= 3840 && h <= 2160 && w % 12 == 0;
    for (size_t i = 0; i < kTimings.size(); ++i) {
        const double nominal = double(kTimings[i].scale) * 1000 / double(kTimings[i].duration);
        // 60 and 59.94 differ by 0.1 %: match within half of that
        if (kTimings[i].width == w && kTimings[i].height == h && std::abs(nominal - rate) < nominal * 0.0004) { d.mode = modeOf(i); return d; }
    }
    d.mode = modeOf(0);
    d.mode.id = 0x1000u;
    d.mode.width = w;
    d.mode.height = h;
    d.mode.scale = rate;
    d.mode.duration = 1000;
    d.mode.name = std::format(L"{}x{} {:.2f}", w, h, rate / 1000.0);
    return d;
}

decklink::Signal signalOf(const Detected& d) {
    decklink::Signal s;
    s.mode = d.mode;
    s.rgb = d.rgb;
    s.bits = d.rgb ? 8 : 10;
    return s;
}

// The console is told about the TV and the receiver behind this PC: their luminance, speakers and the
// compressed formats Windows can pass on. A change is one hot plug, so the console needs a few seconds.
bool applySinkEdid(xcap::Device& device_, bool audio_) {
    const auto support = passthroughSupport();
    const auto offered = offeredFormats(audio_ ? support : PassthroughSupport{});
    const auto monitors = xcap::activeMonitors();
    const xcap::TvEdid* tv = xcap::chooseTv(monitors, tvName());
    bool changed = false;
    if (tv && audioIngress() != 2) {
        const auto info = xcap::parseTv(tv->bytes);
        xcap::AudioCaps pc;
        pc.ac3 = offered.ac3; pc.eac3 = offered.eac3; pc.mat = offered.mat; pc.dts = offered.dts; pc.dtsHd = offered.dtsHd;
        // A TV with a receiver behind it changes its EDID as the receiver wakes up or the mode changes. While the
        // console is sending, only a real gain is worth the hot plug; a loss waits until the signal is gone.
        static std::mutex lock;
        static struct { bool valid = false; xcap::AudioCaps pc; unsigned pcm = 0; } last;
        std::lock_guard guard(lock);
        const bool gain = (pc.mat && !last.pc.mat) || (pc.eac3 && !last.pc.eac3) || (!pc.mat && !pc.eac3 && info.pcmChannels > last.pcm);
        uint32_t state = 0;
        device_.read(XCAP_REG_HDMI0 + 4 * XCAP_META_STATE, state);
        if (last.valid && !gain && (state & 0x20u) && (state & 1u)) return false;
        const auto edid = xcap::composeEdid(info, pc);
        if (xcap::applyEdid(device_, edid, &changed)) {
            last.valid = true;
            last.pc = pc;
            last.pcm = info.pcmChannels;
            log::info("xcap", std::format("EDID #{} from monitor hdr={} peakCode={} pcm={} ac3={} eac3={} mat={} dts={} dtshd={} changed={}", xcap::edidNumber(edid), info.hdr,
                info.maxLum, info.pcmChannels, pc.ac3, pc.eac3, pc.mat, pc.dts, pc.dtsHd, changed));
            if (changed) Sleep(800);
            return changed;
        }
    }
    // old FPGA image, no monitor EDID, or bitstream forced by the user: the two built-in lists
    const bool bitstream = offered.mat || offered.eac3;
    uint32_t host = 0, state = 0;
    device_.read(XCAP_REG_HOST_CTRL, host);
    const uint32_t want = bitstream ? 0x10u : 0u;
    changed = (host & 0x30u) != want;
    if (!changed) return false;
    device_.write(XCAP_REG_HOST_CTRL, want);
    log::info("xcap", std::format("EDID audio={}", bitstream ? "bitstream" : "pcm"));
    for (int i = 0; i < 100 && device_.read(XCAP_REG_HDMI0 + 4 * XCAP_META_STATE, state) && ((state & 0x30u) != want); ++i) Sleep(20);
    Sleep(800);
    return true;
}

class XcapDriver final : public Driver {
public:
    ~XcapDriver() override { stop(); }

    bool open(std::wstring_view path, DriverSink& sink, std::wstring& error) override {
        stop();
        pattern_ = patternPath(path);
        if (!isPath(path) || !device_.open(devicePath(path))) {
            error = std::format(L"打不开 XCAP 采集卡（错误 {}），请确认驱动已安装且没有被别的程序占用", device_.lastError());
            return false;
        }
        sink_ = &sink;
        channelsFixed_ = false;
        xcap_info info{};
        if (device_.info(info))
            log::info("xcap", std::format("driver={:08x} hw={:08x} link=Gen{}x{} mps={} pattern={}", info.drv_version, info.hw_version,
                info.link_speed, info.link_width, info.max_payload, pattern_));
        return true;
    }

    std::vector<Mode> modes() override {
        std::vector<Mode> out;
        for (size_t i = 0; i < kTimings.size(); ++i)
            if (!pattern_ || (kTimings[i].width == 3840 && kTimings[i].scale == 60000 && kTimings[i].duration == 1000)) out.push_back(modeOf(i));
        return out;
    }
    bool autoDetection() const override { return true; }

    bool enable(const Mode& mode, Pixel pixel, bool automatic, bool audio, std::wstring& error) override {
        if (!device_.isOpen()) return false;
        if (pixel != Pixel::V210 && pixel != Pixel::Bgra8) { error = L"XCAP 采集卡不支持这种像素格式"; return false; }
        audio_ = audio;
        automatic_ = automatic;
        chooseAudioChannels();
        mode_ = mode;
        pixel_ = pixel;
        if (pattern_) {
            // Two pixels per clock, 2250 lines: 60 Hz at either video clock; every picture twice, like 30 fps in a 60 Hz signal.
            device_.write(XCAP_REG_TPG_MODE, XCAP_TPG_BARS);
            device_.write(XCAP_REG_TPG_REPEAT, 2);
            uint32_t vclk = 0;
            const uint32_t total = device_.read(XCAP_REG_VCLK_HZ, vclk) && vclk == 300000000u ? 2222u : 2200u;
            device_.write(XCAP_REG_TPG_H, 1920u | total << 16);
            device_.write(XCAP_REG_TPG_V, 2160u | 2250u << 16);
        }
        return true;
    }

    bool start(std::wstring& error) override {
        std::wstring ignored;
        pause(ignored);
        armed_ = {};
        if (!pattern_) {
            const bool changed = applySinkEdid(device_, audio_);
            // Give a source that just saw the hot plug time to come up.
            Detected d;
            for (int i = 0; i < (changed ? 350 : 100); ++i) {
                d = detect(device_);
                if (!d.firmware || d.locked) break;
                Sleep(20);
            }
            if (!d.firmware) { error = L"采集卡上没有 HDMI 接收固件，请下载带 HDMI 的 FPGA 程序"; return false; }
            if (!d.locked) { error = L"XCAP 未检测到 HDMI 信号，请检查线缆，并在 PS5 上关闭 HDCP"; return false; }
            if (!d.usable) {
                error = std::format(L"XCAP 不支持当前 HDMI 格式（{}x{}，颜色格式 {}，{} 位），请在 PS5 上选 YUV422 或 RGB", d.mode.width, d.mode.height, d.colour, d.bits);
                return false;
            }
            armed_ = d;
            chooseAudioChannels();      // again with a locked signal: at enable() the console may not have sent audio yet
            const bool wantRgb = pixel_ == Pixel::Bgra8;
            if (d.rgb != wantRgb || d.mode.width != mode_.width || d.mode.height != mode_.height || d.mode.id != mode_.id) {
                if (automatic_) { if (sink_) sink_->formatChanged(signalOf(d)); return true; } // the owner re-enables with it
                error = std::format(L"XCAP 实际信号是 {}，与所选制式不同，请选择自动检测", d.mode.name);
                return false;
            }
        }
        xcap::Config c;
        c.width = mode_.width;
        c.height = mode_.height;
        c.pixfmt = pixel_ == Pixel::V210 ? XCAP_PIXFMT_V210 : XCAP_PIXFMT_BGRX8;
        c.slots = kSlots;
        c.sliceLines = kSliceLines;
        c.hdmi = !pattern_;
        c.audio = audio_;
        if (!device_.start(c)) {
            error = std::format(L"XCAP 采集启动失败（错误 {}）", device_.lastError());
            return false;
        }
        log::info("xcap", std::format("start {}x{} pixfmt={} frameBytes={} slices={} ring={} contiguousKB={}", c.width, c.height, c.pixfmt,
            device_.layout().frame_bytes, device_.slicesPerFrame(), c.slots, device_.layout().contiguous_kb));
        quit_ = false;
        audioQuit_ = false;
        if (audio_) {
            try { audioThread_ = std::thread([this] { audioLoop(); }); }
            catch (...) { log::warn("xcap-audio", "audio thread did not start; video continues"); }
        }
        try { thread_ = std::thread([this] { loop(); }); }
        catch (...) { device_.stop(); error = L"XCAP 采集线程启动失败"; return false; }
        return true;
    }

    bool pause(std::wstring&) override {
        quit_ = true;
        if (device_.event()) SetEvent(device_.event());
        audioQuit_ = true;
        if (device_.audioEvent()) SetEvent(device_.audioEvent());
        if (thread_.joinable()) thread_.join();
        if (audioThread_.joinable()) audioThread_.join();
        device_.stop();
        return true;
    }
    bool flush(std::wstring&) override { return true; }
    void stop() noexcept override {
        std::wstring ignored;
        pause(ignored);
        device_.close();
        sink_ = nullptr;
    }

private:
    xcap::Device device_;
    DriverSink* sink_ = nullptr;
    bool pattern_ = false, automatic_ = false;
    Detected armed_;
    Mode mode_;
    Pixel pixel_ = Pixel::V210;
    std::atomic<bool> quit_{false};
    std::thread thread_, audioThread_;
    std::atomic<bool> audioQuit_{false};
    bool audio_ = false;
    // Channels of the PCM handed to the owner. Chosen while the signal is probed and fixed once the owner has asked:
    // its audio session is configured with that answer and lives until the next open().
    std::atomic<unsigned> sessionChannels_{2};
    std::atomic<bool> channelsFixed_{false};

public:
    unsigned audioChannels() override { channelsFixed_ = true; return sessionChannels_; }
    // HDMI PCM is up to 24 bits: hand it over in 32-bit containers so nothing is cut off on the way to the TV.
    unsigned audioBits() override { return 32; }
    // PCM with more than two channels is handed over as 7.1 when the signal has it at connect time.
    void chooseAudioChannels() {
        if (channelsFixed_) return;
        uint32_t a = 0;
        sessionChannels_ = !pattern_ && device_.read(XCAP_REG_HDMI0 + 4 * XCAP_META_AUDIO, a) && ((a >> 16) & 255) > 2 ? 8 : 2;
    }

private:
    void audioLoop() {
        // The passthrough's WASAPI objects are created, used and released while this thread keeps COM initialised.
        const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        DWORD task = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
        std::unique_ptr<CarrierSink> sink;                    // the open passthrough stream
        // Opening an exclusive stream can take long (the receiver locks to the new format) and the card's ring holds
        // a third of a second: a helper thread opens it, this one keeps reading. One attempt at a time.
        struct Opening {
            std::thread thread;
            std::atomic<bool> done{false};
            bool ok = false;
            std::unique_ptr<CarrierSink> sink;
            Codec codec = Codec::Pcm;
            unsigned channels = 0;
            std::wstring why;
        };
        std::unique_ptr<Opening> opening;
        Codec want = Codec::Pcm;                              // the stream the parser reports
        unsigned wantChannels = 0;
        ULONGLONG retryAt = 0;
        bool everOpened = false, noDecoder = false;
        bool rebase = false;                                  // start the audio clock again at the next packet
        uint64_t ticks = 0;                                   // card time of the packet being parsed, 4 ns
        std::unique_ptr<CarrierDecoder> decoder;
        std::vector<int32_t> pcm32;                           // 24 valid bits, left-justified
        std::vector<float> decoded;
        int64_t frames = 0;                                   // PCM frames handed over, the audio clock
        int64_t base100ns = 0;
        const unsigned ch = sessionChannels_;
        // The parser says "PCM" until it has seen a bitstream's sync words: a Dolby stream would reach the PCM
        // renderer as full-scale noise for its first milliseconds. PCM is passed on once it has stayed PCM for 50 ms
        // of card time, whatever the frame rate of the carrier (a MAT preamble comes every 20 ms at 192 kHz). The
        // audio clock keeps counting meanwhile, so the picture/sound alignment is unchanged.
        static constexpr uint64_t kPcmConfirmTicks = 12500000;
        uint64_t pcmHoldUntil = ~uint64_t(0);                 // set at the first packet
        const auto deliver = [&](size_t n) {
            if (sink_ && n) sink_->audio(pcm32.data(), n * ch * 4, base100ns + frames * 10000000 / 48000);
            frames += int64_t(n);
        };
        // CTA order FL FR LFE FC RL RR RLC RRC -> Windows 7.1 FL FR FC LFE BL BR SL SR
        static constexpr unsigned kOrder[8] = {0, 1, 3, 2, 6, 7, 4, 5};
        const auto s24 = [](double v) { return int32_t(uint32_t(int32_t(std::clamp(v, -8388608.0, 8388607.0))) << 8); };
        AudioParser::Output out;
        out.pcm = [&](const int32_t* v, unsigned in, size_t n) {    // v: 24-bit samples
            if (ticks < pcmHoldUntil) { frames += int64_t(n); return; }
            pcm32.assign(n * ch, 0);
            for (size_t i = 0; i < n; ++i) {
                const int32_t* f = v + i * in;
                if (ch == 8 && in == 8) { for (unsigned k = 0; k < 8; ++k) pcm32[i * 8 + k] = int32_t(uint32_t(f[kOrder[k]]) << 8); }
                else if (ch == 2 && in == 8) {
                    // stereo downmix: centre and surrounds at -3 dB, scaled so that full scale on all four inputs
                    // of a side still fits (no clipping); the LFE is left out, as in ITU-R BS.775
                    static constexpr double kDown = 1.0 / (1.0 + 3 * 0.707);
                    const double l = f[0] + 0.707 * (f[3] + f[4] + f[6]), r = f[1] + 0.707 * (f[3] + f[5] + f[7]);
                    pcm32[i * 2] = s24(l * kDown);
                    pcm32[i * 2 + 1] = s24(r * kDown);
                } else for (unsigned k = 0; k < 2; ++k) pcm32[i * ch + k] = int32_t(uint32_t(f[k]) << 8);
            }
            deliver(n);
        };
        out.changed = [&](Codec codec, unsigned channels) {
            log::info("xcap-audio", std::format("input codec={} channels={}", int(codec), channels));
            sink.reset();
            decoder.reset();
            want = codec;
            wantChannels = channels;
            everOpened = noDecoder = false;
            retryAt = 0;                                      // the passthrough is opened right after this packet
            // `frames` stood still while a bitstream was passed through: the clock starts again with the new stream
            rebase = true;
            if (codec == Codec::Pcm) {
                pcmHoldUntil = ticks + kPcmConfirmTicks;
                if (channels > ch) log::info("xcap-audio", std::format("PCM with {} channels into a {}-channel session: mixed down until the next connect", channels, ch));
            }
        };
        // Runs between packets: a lost passthrough stream is closed and opened again every second; a finished
        // attempt is taken over. Without a stream the carrier is decoded to PCM, when a decoder exists.
        const auto passthrough = [&] {
            const ULONGLONG now = GetTickCount64();
            if (sink && sink->failed()) {
                log::warn("xcap-audio", "passthrough endpoint lost; trying to open it again every second");
                sink.reset();
                retryAt = now + 1000;
                rebase = true;
            }
            if (opening && opening->done) {
                if (opening->thread.joinable()) opening->thread.join();
                const bool current = opening->codec == want && opening->channels == wantChannels;
                if (current && opening->ok) {
                    sink = std::move(opening->sink);
                    decoder.reset();
                    everOpened = true;
                    rebase = true;
                } else if (current) {
                    if (!decoder && !noDecoder) {
                        decoder = std::make_unique<CarrierDecoder>();
                        if (!decoder->open(want, wantChannels)) { decoder.reset(); noDecoder = true; log::warn("xcap-audio", "no passthrough endpoint and no decoder for this stream"); }
                        else log::info("xcap-audio", "no passthrough endpoint; decoding to PCM (object metadata is lost)");
                        rebase = true;
                    }
                    // an endpoint that was there comes back within seconds; one that never took the format is asked rarely
                    retryAt = now + (everOpened ? 1000 : 5000);
                } else retryAt = now;                         // the stream changed meanwhile: this attempt is dropped (and closed)
                opening.reset();
            }
            if (want == Codec::Pcm || sink || opening || now < retryAt) return;
            opening = std::make_unique<Opening>();
            Opening* o = opening.get();
            o->codec = want;
            o->channels = wantChannels;
            o->sink = std::make_unique<CarrierSink>();
            try {
                o->thread = std::thread([o] {
                    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
                    o->ok = o->sink->open(o->codec, o->channels, o->why);
                    if (SUCCEEDED(hr)) CoUninitialize();      // the audio thread keeps COM alive for the stream
                    o->done = true;
                });
            } catch (...) { o->done = true; }                 // counts as a failed attempt
        };
        out.carrier = [&](Codec, unsigned, const uint16_t* words, size_t n) {
            if (sink) { sink->push(words, n); return; }
            if (!decoder) return;
            decoded.clear();
            decoder->push(words, n, decoded);
            if (decoded.empty()) return;
            const unsigned in = std::max(1u, decoder->channels());
            const size_t count = decoded.size() / in;
            pcm32.assign(count * ch, 0);
            const auto put = [&](size_t i, unsigned k, float v) { pcm32[i * ch + k] = s24(double(v) * 8388607.0); };
            for (size_t i = 0; i < count; ++i) {
                const float* f = &decoded[i * in];                // FL FR FC LFE BL BR (SL SR)
                if (ch == 2 && in >= 6) {
                    const float side = in >= 8 ? 0.5f : 0.f;
                    const float down = 1.f / (1.f + 0.707f + 0.707f + side);       // full scale everywhere still fits
                    put(i, 0, down * (f[0] + 0.707f * f[2] + 0.707f * f[4] + side * f[6 % in]));
                    put(i, 1, down * (f[1] + 0.707f * f[2] + 0.707f * f[5] + side * f[7 % in]));
                } else for (unsigned k = 0; k < std::min(ch, in); ++k) put(i, k, f[k]);
            }
            deliver(count);
        };
        AudioParser parser(std::move(out));
        uint64_t next = 0, lost = 0;
        int64_t errRef = 0, errSum = 0;
        unsigned settle = 0;
        uint32_t seq = 0;
        bool have = false;
        ULONGLONG lastPacketAt = GetTickCount64();
        while (!audioQuit_) {
            WaitForSingleObject(device_.audioEvent(), 20);
            const uint64_t done = device_.audioPackets();
            // The packet time has 32 bits (17 s): after a second without packets it is read against the card's
            // clock again instead of being continued.
            if (done != next) lastPacketAt = GetTickCount64();
            else if (have && GetTickCount64() - lastPacketAt > 1000) { have = false; parser.resync(); }
            // A gap (ring overrun, lost packet) is a short dropout, not a new stream: the parser keeps its codec, so
            // the passthrough stays open and the receiver does not lock again.
            if (done > next + XCAP_AUD_RING - 8) { lost += done - next; next = done; parser.resync(); have = false; }
            for (; next < done && !audioQuit_; ++next) {
                const xcap_audio_packet* p = device_.audioPacket(next);
                if (p->magic != XCAP_AUD_MAGIC || p->words > XCAP_AUD_WORDS) continue;
                if (have && p->seq != seq) { ++lost; parser.resync(); }
                // The audio clock belongs to the source, the video clock to the card: start on the packet time,
                // then follow it slowly, so that an hour of samples does not walk away from the picture.
                if (!have) {
                    xcap::Clock now;
                    device_.clock(now);
                    ticks = now.ticks - uint32_t(uint32_t(now.ticks) - p->ts);
                    pcmHoldUntil = ticks + kPcmConfirmTicks;
                } else ticks += uint32_t(p->ts - uint32_t(ticks));
                const int64_t err = int64_t(ticks / 25) - (base100ns + frames * 10000000 / 48000);
                // The first second measures the constant part (what the parser or the decoder holds back).
                const int64_t off = err - errRef;
                if (!have || rebase || off > 1000000 || off < -1000000) { base100ns = int64_t(ticks / 25); frames = 0; errRef = errSum = 0; settle = 0; rebase = false; }
                else if (settle < 1000) { errSum += err; if (++settle == 1000) errRef = errSum / 1000; }
                else base100ns += off / 256;
                seq = p->seq + 1;
                have = true;
                parser.push(p->word, p->words);
            }
            passthrough();
        }
        if (opening) {
            if (opening->thread.joinable()) opening->thread.join();
            opening.reset();
        }
        sink.reset();
        if (lost) log::warn("xcap-audio", std::format("lost packets={}", lost));
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
        if (SUCCEEDED(com)) CoUninitialize();
    }

    // Waits in short steps so pause() is never held up by a missing signal.
    xcap::Device::Wait wait(uint64_t slices, unsigned timeoutMs) {
        const auto deadline = GetTickCount64() + timeoutMs;
        for (;;) {
            if (quit_) return xcap::Device::Wait::Stopped;
            const auto r = device_.waitSlices(slices, 50, kSpinUs);
            if (r != xcap::Device::Wait::Timeout) return r;
            if (GetTickCount64() >= deadline) return xcap::Device::Wait::Timeout;
        }
    }

    void loop() {
        DWORD task = 0;
        HANDLE mmcss = AvSetMmThreadCharacteristicsW(L"Pro Audio", &task);
        if (mmcss) AvSetMmThreadPriority(mmcss, AVRT_PRIORITY_HIGH);
        else log::warn("xcap", std::format("MMCSS unavailable error={}", GetLastError()));
        const uint32_t S = device_.slicesPerFrame(), stride = device_.stride();
        uint64_t frame = 0, delivered = 0, skipped = 0, torn = 0, repeats = 0, bad = 0;
        uint32_t lastSeq = 0;
        bool haveSeq = false, noInput = false;
        bool previousWhole = false;         // the frame before was handed over complete
        LARGE_INTEGER freq;
        QueryPerformanceFrequency(&freq);
        // The card's clock follows the PCIe reference (tens of ppm off), so its rate is measured.
        xcap::Clock clock, earlier;
        device_.clock(earlier);
        Sleep(20);
        device_.clock(clock);
        double usPerTick = double(clock.qpc - earlier.qpc) * 1e6 / double(freq.QuadPart) / double(clock.ticks - earlier.ticks);
        double latencySum = 0, latencyMax = 0;
        uint64_t latencyCount = 0;

        while (!quit_) {
            const auto first = wait(frame * S + 1, 1000);
            if (quit_) break;
            if (first == xcap::Device::Wait::Stopped) {
                if (sink_) sink_->failure(std::format(L"XCAP 采集卡停止（状态 0x{:08X}，帧对齐错误 {}）", uint32_t(device_.ctl()->engine_status), uint32_t(device_.ctl()->align_errors)));
                break;
            }
            if (first == xcap::Device::Wait::Timeout) {
                // the format may have changed while no frame fits the old one
                const auto d = pattern_ ? Detected{} : detect(device_);
                if (d.locked && !d.same(armed_)) { armed_ = d; if (sink_) sink_->formatChanged(signalOf(d)); continue; }
                if (!noInput && sink_) { noInput = true; Frame none; none.noInput = true; sink_->video(none); }
                continue;
            }
            noInput = false;
            // Behind by a whole frame: jump to the newest one that has started.
            const uint64_t newest = (device_.slicesDone() - 1) / S;
            if (newest > frame + 1) { skipped += newest - frame; frame = newest; continue; }

            const xcap_frame_header head = *device_.header(frame);
            if (head.magic != XCAP_HDR_MAGIC) { if (sink_) sink_->failure(L"XCAP 帧头无效，正在重新启动采集"); break; }
            // "identical to the previous frame" only helps a consumer that has that previous frame
            const bool consecutive = haveSeq && head.seq == lastSeq + 1 && previousWhole;
            lastSeq = head.seq;
            haveSeq = true;

            Frame f;
            f.width = head.geom & 0xFFFF;
            f.height = head.geom >> 16;
            f.stride = head.stride;
            f.pixel = head.pixfmt == XCAP_PIXFMT_V210 ? Pixel::V210 : Pixel::Bgra8;
            f.data = device_.pixels(frame);
            f.bytes = size_t(stride) * f.height;
            f.pts100ns = int64_t(head.ts_first_pixel / 25);          // 4 ns ticks
            f.duration100ns = mode_.duration * 10000000 / mode_.scale;
            f.color = colorOf(f.pixel, f.height, head.meta);
            f.repeat = -1;
            bool finished = false, whole = false;
            f.awaitRows = [&](uint32_t rows) {
                const uint64_t slice = rows >= f.height ? S : (uint64_t(rows) + kSliceLines - 1) / kSliceLines;
                if (wait(frame * S + slice, 200) != xcap::Device::Wait::Ok || !device_.frameIntact(frame)) { whole = false; return false; }
                if (rows >= f.height && !finished) {
                    finished = true;
                    const xcap_frame_trailer& t = *device_.trailer(frame);
                    LARGE_INTEGER now;
                    QueryPerformanceCounter(&now);
                    const double us = double(now.QuadPart - clock.qpc) * 1e6 / double(freq.QuadPart) - double(int64_t(t.ts_last_pixel - clock.ticks)) * usPerTick;
                    latencySum += us;
                    latencyMax = std::max(latencyMax, us);
                    ++latencyCount;
                    if (t.flags & XCAP_FRAME_BAD) { ++bad; return false; }
                    if (consecutive) f.repeat = (t.flags & XCAP_FRAME_REPEAT) ? 1 : 0;
                    if (f.repeat == 1) ++repeats;
                    whole = true;
                }
                return true;
            };
            if (sink_) sink_->video(f);
            previousWhole = whole;
            if (!device_.frameIntact(frame)) ++torn;
            ++delivered;
            ++frame;
            if (!pattern_ && delivered % 15 == 0) {
                const auto d = detect(device_);
                if (d.locked && !d.same(armed_) && sink_) {
                    log::warn("xcap", std::format("signal changed to {}x{} colour={} bits={}", d.mode.width, d.mode.height, d.colour, d.bits));
                    armed_ = d;
                    sink_->formatChanged(signalOf(d));
                }
            }
            if (delivered % 15 == 0) {
                earlier = clock;
                device_.clock(clock);
                if (clock.ticks > earlier.ticks) usPerTick = double(clock.qpc - earlier.qpc) * 1e6 / double(freq.QuadPart) / double(clock.ticks - earlier.ticks);
            }
            if (delivered % 300 == 0) {
                log::info("xcap", std::format("frames={} skipped={} torn={} bad={} repeats={} lastPixelToHostUs avg={:.0f} max={:.0f}", delivered, skipped, torn, bad,
                    repeats, latencyCount ? latencySum / double(latencyCount) : 0, latencyMax));
                latencySum = latencyMax = 0;
                latencyCount = 0;
            }
        }
        if (mmcss) AvRevertMmThreadCharacteristics(mmcss);
    }
};

} // namespace

bool isPath(std::wstring_view path) { return path.starts_with(kHdmi) || path.starts_with(kPattern); }

std::vector<Device> devices() {
    std::vector<Device> out;
    const auto all = xcap::Device::enumerate();
    for (size_t i = 0; i < all.size(); ++i) {
        const auto suffix = all.size() > 1 ? std::format(L" #{}", i + 1) : std::wstring();
        out.push_back({L"[原生] XCAP HDMI 采集卡" + suffix, std::wstring(kHdmi) + all[i], true, true});
        out.push_back({L"[原生] XCAP 测试图案 4K60" + suffix, std::wstring(kPattern) + all[i], true, true});
    }
    return out;
}

std::vector<Mode> modes(std::wstring_view path) {
    std::vector<Mode> out;
    for (size_t i = 0; i < kTimings.size(); ++i)
        if (!patternPath(path) || i == 0) out.push_back(modeOf(i));
    return out;
}

bool prepare(std::wstring_view path) {
    xcap::Device device;
    if (!isPath(path) || !device.open(devicePath(path))) return false;
    if (patternPath(path)) return true;
    applySinkEdid(device, true);
    return detect(device).locked;
}

std::unique_ptr<Driver> makeDriver() { return std::make_unique<XcapDriver>(); }

} // namespace veyra::source::xcapture
