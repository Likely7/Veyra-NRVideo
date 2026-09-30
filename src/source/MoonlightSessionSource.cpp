// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/source/MoonlightSessionSource.h"

#include <windows.h>
#include <mmreg.h>

#include <Limelight.h>
#include <opus/opus_multistream.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <format>
#include <vector>

#include "veyra/Log.h"
#include "veyra/moonlight/Crypto.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/sink/AudioFormat.h"
#include "veyra/source/CaptureCompressedDecoder.h"

namespace veyra::source {

// The protocol layer repeats a few constants of Limelight.h so it can be tested without the C
// library; if the library ever changes them, fail here rather than negotiate the wrong format.
static_assert(moonlight::kFormatH264 == VIDEO_FORMAT_H264 && moonlight::kFormatH265 == VIDEO_FORMAT_H265 &&
              moonlight::kFormatH265Main10 == VIDEO_FORMAT_H265_MAIN10 && moonlight::kFormatAv1Main8 == VIDEO_FORMAT_AV1_MAIN8 &&
              moonlight::kFormatAv1Main10 == VIDEO_FORMAT_AV1_MAIN10 &&
              (VIDEO_FORMAT_MASK_10BIT & moonlight::kFormatMask10Bit) == moonlight::kFormatMask10Bit,
              "VIDEO_FORMAT constants differ from moonlight-common-c");
static_assert(moonlight::kServerH264 == SCM_H264 && moonlight::kServerHevc == SCM_HEVC && moonlight::kServerHevcMain10 == SCM_HEVC_MAIN10 &&
              moonlight::kServerAv1Main8 == SCM_AV1_MAIN8 && moonlight::kServerAv1Main10 == SCM_AV1_MAIN10,
              "SCM constants differ from moonlight-common-c");
static_assert(moonlight::kPadUp == UP_FLAG && moonlight::kPadDown == DOWN_FLAG && moonlight::kPadLeft == LEFT_FLAG && moonlight::kPadRight == RIGHT_FLAG &&
              moonlight::kPadStart == PLAY_FLAG && moonlight::kPadBack == BACK_FLAG && moonlight::kPadLeftStick == LS_CLK_FLAG && moonlight::kPadRightStick == RS_CLK_FLAG &&
              moonlight::kPadLeftBumper == LB_FLAG && moonlight::kPadRightBumper == RB_FLAG && moonlight::kPadGuide == SPECIAL_FLAG &&
              moonlight::kPadA == A_FLAG && moonlight::kPadB == B_FLAG && moonlight::kPadX == X_FLAG && moonlight::kPadY == Y_FLAG &&
              moonlight::kPadTouchpad == TOUCHPAD_FLAG && moonlight::kPadMisc == MISC_FLAG,
              "controller button flags differ from moonlight-common-c");
static_assert(moonlight::kModShift == MODIFIER_SHIFT && moonlight::kModCtrl == MODIFIER_CTRL && moonlight::kModAlt == MODIFIER_ALT && moonlight::kModMeta == MODIFIER_META,
              "keyboard modifier flags differ from moonlight-common-c");

namespace {

using Clock = std::chrono::steady_clock;

int64_t host100ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count() / 100;
}

std::wstring widen(const std::string& text) {
    if (text.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0);
    std::wstring out(size_t(std::max(size, 0)), L'\0');
    if (size > 0) MultiByteToWideChar(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size);
    return out;
}

const char* codecLabel(int videoFormat) {
    if (videoFormat & VIDEO_FORMAT_MASK_H264) return "H.264";
    if (videoFormat & VIDEO_FORMAT_MASK_H265) return (videoFormat & VIDEO_FORMAT_MASK_10BIT) ? "HEVC 10-bit" : "HEVC";
    if (videoFormat & VIDEO_FORMAT_MASK_AV1) return (videoFormat & VIDEO_FORMAT_MASK_10BIT) ? "AV1 10-bit" : "AV1";
    return "?";
}

// What a connection failure or termination means to the user.
std::wstring describeTermination(int code) {
    switch (code) {
    case ML_ERROR_GRACEFUL_TERMINATION: return L"主机上的游戏已经退出，串流结束。";
    case ML_ERROR_NO_VIDEO_TRAFFIC: return L"没有收到视频数据。通常是防火墙或端口转发没有放行 UDP 47998–48010。";
    case ML_ERROR_NO_VIDEO_FRAME: return L"长时间收不到完整画面。网络很不稳定，或码率设得太高。";
    case ML_ERROR_UNEXPECTED_EARLY_TERMINATION: return L"串流刚开始就被主机结束，常见原因是主机无法抓取画面（受保护内容或显示器未连接）。";
    case ML_ERROR_PROTECTED_CONTENT: return L"主机上正在显示受保护的内容，无法串流。";
    case ML_ERROR_FRAME_CONVERSION: return L"主机转换画面失败。开启 HDR 时，主机桌面分辨率与串流分辨率不兼容会这样。";
    default: return std::format(L"连接中断（错误码 {}）。请检查网络和主机。", code);
    }
}

pipeline::SourcePixelFormat pixelFormatOf(const AVFrame& frame) {
    if (frame.format == AV_PIX_FMT_D3D12 && frame.hw_frames_ctx) {
        const auto* context = reinterpret_cast<const AVHWFramesContext*>(frame.hw_frames_ctx->data);
        return context->sw_format == AV_PIX_FMT_NV12 ? pipeline::SourcePixelFormat::NV12
             : context->sw_format == AV_PIX_FMT_P010 ? pipeline::SourcePixelFormat::P010 : pipeline::SourcePixelFormat::Unknown;
    }
    switch (frame.format) {
    case AV_PIX_FMT_NV12: return pipeline::SourcePixelFormat::NV12;
    case AV_PIX_FMT_YUV420P10LE: case AV_PIX_FMT_P010: return pipeline::SourcePixelFormat::P010;
    case AV_PIX_FMT_YUV420P: return pipeline::SourcePixelFormat::Yuv420P;
    default: return pipeline::SourcePixelFormat::Unknown;
    }
}

// A PC desktop or game: sRGB content carried as BT.709, displayed with the same code
// values (like capture cards and files); 10-bit streams are HDR10 (BT.2020 / PQ).
pipeline::ColorDescription fallbackColor(bool hdr) {
    pipeline::ColorDescription c;
    c.pixelFormat = hdr ? pipeline::SourcePixelFormat::P010 : pipeline::SourcePixelFormat::NV12;
    c.range = pipeline::ColorRange::Limited;
    c.rangeAssumed = true;
    c.matrixAssumed = c.transferAssumed = c.primariesAssumed = true;
    if (hdr) {
        c.matrix = pipeline::YuvMatrix::BT2020NCL;
        c.transfer = pipeline::TransferFunction::PQ;
        c.primaries = pipeline::ColorPrimaries::BT2020;
    } else {
        c.matrix = pipeline::YuvMatrix::BT709;
        c.transfer = pipeline::TransferFunction::BT709;
        c.primaries = pipeline::ColorPrimaries::BT709;
        c.preserveSdrCodeValues = true;
    }
    return c;
}

AVFrame* allocNv12(unsigned width, unsigned height) {
    AVFrame* frame = av_frame_alloc();
    if (!frame) return nullptr;
    frame->format = AV_PIX_FMT_NV12;
    frame->width = int(width);
    frame->height = int(height);
    if (av_frame_get_buffer(frame, 32) < 0) { av_frame_free(&frame); return nullptr; }
    return frame;
}

std::atomic<MoonlightSessionSource*> g_active{nullptr};

double movingAverage(double current, double sample) { return current > 0 ? current * 0.95 + sample * 0.05 : sample; }

} // namespace

struct MoonlightSessionSource::Impl {
    MoonlightSessionSource* owner = nullptr;
    MoonlightConnectDesc desc;            // device/queue only after the launch (identity wiped)

    // Video
    CaptureCompressedDecoder decoder;
    CaptureCodec codec = CaptureCodec::None;
    unsigned width = 0, height = 0;
    int videoFormat = 0;
    bool hdrStream = false;
    std::thread decodeThread;
    std::atomic<bool> decodeStop{false};
    AVFrame* dummyTarget = nullptr;       // passed while the hardware path owns the pictures
    struct Meta { int64_t pts; int64_t arrival; };
    std::deque<Meta> presented;           // pts -> arrival of recent frames (for the audio sync)
    std::mutex presentedMutex;

    // Audio
    OpusMSDecoder* opus = nullptr;
    int audioChannels = 0, samplesPerFrame = 0;
    uint64_t audioSamples = 0;
    bool audioDiscontinuity = true;
    std::vector<float> pcm;

    // Counters written by the library threads
    std::atomic<uint64_t> units{0}, decoded{0}, dropped{0}, errors{0}, idr{0};
    std::atomic<int64_t> lastFrameNumber{0};
    std::atomic<bool> terminated{false};
    std::atomic<int> terminationCode{0};
    std::atomic<bool> streaming{false};
    // The library allows its statistics calls only between LiStartConnection and LiStopConnection:
    // stats() holds this while it calls them, close() takes it to clear `streaming` first.
    std::mutex libMutex;
    // Input state, guarded by libMutex as well: a send must not overlap LiStopConnection.
    moonlight::HeldKeys keys;
    unsigned mouseButtons = 0;            // bit per button 1..5
    moonlight::PadFrame pad;
    bool padArrived = false;
    std::mutex feedbackMutex;
    remoteplay::ControllerFeedback feedback;
    bool feedbackPending = false;
    uint16_t rumbleLow = 0, rumbleHigh = 0;
    Clock::time_point feedbackSent{};
    std::mutex statsMutex;
    double hostLatencyMs = 0, receiveMs = 0, queueMs = 0, decodeMs = 0, receivedFps = 0, decodedFps = 0;
    std::chrono::steady_clock::time_point rateStart = Clock::now();
    uint64_t rateUnits = 0, rateDecoded = 0;

    ~Impl() {
        if (dummyTarget) av_frame_free(&dummyTarget);
        if (opus) opus_multistream_decoder_destroy(opus);
    }
};

struct MoonlightSessionSource::Callbacks {
    // Releases what is held on the host. The caller holds libMutex and has checked `streaming`.
    static void releaseInputLocked(Impl& impl) {
        for (int i = 0; i < impl.keys.count(); ++i)
            LiSendKeyboardEvent(impl.keys.at(i), KEY_ACTION_UP, 0);
        impl.keys.clear();
        for (int button = 1; button <= 5; ++button)
            if (impl.mouseButtons & (1u << button)) LiSendMouseButtonEvent(BUTTON_ACTION_RELEASE, button);
        impl.mouseButtons = 0;
        if (impl.padArrived && !(impl.pad == moonlight::PadFrame{})) {
            LiSendMultiControllerEvent(0, int16_t(impl.desc.options.gamepadMask), 0, 0, 0, 0, 0, 0, 0);
            impl.pad = {};
        }
    }

    // -- connection -------------------------------------------------------------------------
    static void stageStarting(int stage) { log::info("moonlight", std::format("stage starting: {}", LiGetStageName(stage))); }
    static void stageComplete(int stage) { log::info("moonlight", std::format("stage complete: {}", LiGetStageName(stage))); }
    static void stageFailed(int stage, int error) {
        log::warn("moonlight", std::format("stage failed: {} error={}", LiGetStageName(stage), error));
        if (auto* self = g_active.load()) {
            std::lock_guard lock(self->mutex_);
            self->stats_.errorCode = error;
            self->stats_.message = std::format(L"连接在“{}”阶段失败（错误码 {}）。请确认主机上的 Sunshine 正在运行，并且防火墙放行了 TCP 47984/47989/48010 与 UDP 47998–48010。",
                                               widen(LiGetStageName(stage)), error);
        }
    }
    static void started() { log::info("moonlight", "connection started"); }
    static void terminated(int error) {
        log::warn("moonlight", std::format("connection terminated code={}", error));
        if (auto* self = g_active.load()) {
            self->p_->terminationCode = error;
            self->p_->terminated = true;
            std::lock_guard lock(self->mutex_);
            self->stats_.errorCode = error;
            self->stats_.message = describeTermination(error);
            self->stats_.state = error == ML_ERROR_GRACEFUL_TERMINATION ? MoonlightStats::State::Ended : MoonlightStats::State::Failed;
        }
    }
    static void logMessage(const char* format, ...) {
        char line[512];
        va_list args;
        va_start(args, format);
        std::vsnprintf(line, sizeof(line), format, args);
        va_end(args);
        std::string text(line);
        while (!text.empty() && (text.back() == '\n' || text.back() == '\r')) text.pop_back();
        if (!text.empty()) log::info("moonlight-lib", text);
    }
    static void rumble(unsigned short controller, unsigned short low, unsigned short high) {
        if (controller != 0) return;
        auto* self = g_active.load();
        if (!self || !self->p_) return;
        std::lock_guard lock(self->p_->feedbackMutex);
        self->p_->rumbleLow = low;
        self->p_->rumbleHigh = high;
        self->p_->feedbackPending = true;
    }
    static void connectionStatus(int status) {
        log::info("moonlight", status == CONN_STATUS_POOR ? "connection status: poor" : "connection status: okay");
    }
    static void setHdrMode(bool enabled) { log::info("moonlight", std::format("host HDR mode: {}", enabled)); }

    // -- video (pull renderer) ---------------------------------------------------------------
    static int drSetup(int videoFormat, int width, int height, int redrawRate, void*, int) {
        auto* self = g_active.load();
        if (!self) return -1;
        auto& p = *self->p_;
        p.videoFormat = videoFormat;
        p.width = unsigned(width);
        p.height = unsigned(height);
        p.hdrStream = (videoFormat & VIDEO_FORMAT_MASK_10BIT) != 0;
        p.codec = (videoFormat & VIDEO_FORMAT_MASK_H264) ? CaptureCodec::H264
                : (videoFormat & VIDEO_FORMAT_MASK_H265) ? CaptureCodec::Hevc
                : (videoFormat & VIDEO_FORMAT_MASK_AV1) ? CaptureCodec::Av1 : CaptureCodec::None;
        if (p.codec == CaptureCodec::None) return -1;
        if (!p.decoder.open(p.codec, p.width, p.height, nullptr, 0, p.desc.decodeDevice.get(), p.desc.decodeQueue.get())) {
            log::error("moonlight", std::format("no usable decoder for {}", codecLabel(videoFormat)));
            return -1;
        }
        if (!p.dummyTarget) p.dummyTarget = allocNv12(16, 16);
        log::info("moonlight", std::format("video setup {} {}x{} redraw={} backend={}", codecLabel(videoFormat), width, height, redrawRate, p.decoder.backendName()));
        {
            std::lock_guard lock(self->mutex_);
            self->stats_.codec = codecLabel(videoFormat);
            self->stats_.hdr = p.hdrStream;
            self->stats_.width = p.width;
            self->stats_.height = p.height;
            self->info_.width = p.width;
            self->info_.height = p.height;
            self->info_.displayAspect = height > 0 ? double(width) / double(height) : 0.0;
            self->info_.videoCodecName = codecLabel(videoFormat);
            self->info_.color = fallbackColor(p.hdrStream);
        }
        return 0;
    }
    static void drStart() {
        auto* self = g_active.load();
        if (!self) return;
        auto& p = *self->p_;
        p.decodeStop = false;
        p.decodeThread = std::thread([self] { decodeLoop(*self); });
    }
    static void drStop() {
        auto* self = g_active.load();
        if (!self) return;
        self->p_->decodeStop = true;
        LiWakeWaitForVideoFrame();
    }
    static void drCleanup() {
        auto* self = g_active.load();
        if (!self) return;
        auto& p = *self->p_;
        p.decodeStop = true;
        LiWakeWaitForVideoFrame();
        if (p.decodeThread.joinable()) p.decodeThread.join();
        p.decoder.close();
    }

    static void decodeLoop(MoonlightSessionSource& self) {
        auto& p = *self.p_;
        std::vector<uint8_t> unit;
        bool seenIdr = false, recovering = false;
        int64_t lastPts = -1;
        const int64_t nominal = std::max<int64_t>(1, int64_t(10000000.0 / std::max(1.0, self.stats_.fps)));
        std::deque<Impl::Meta> metas;
        log::info("moonlight", "decode thread started");
        while (!p.decodeStop.load()) {
            VIDEO_FRAME_HANDLE handle = nullptr;
            PDECODE_UNIT du = nullptr;
            if (!LiWaitForNextVideoFrame(&handle, &du)) break;
            int status = DR_OK;
            ++p.units;

            const int64_t nowHost = host100ns();
            const uint64_t nowLib = LiGetMicroseconds();
            const uint64_t ageUs = nowLib >= du->receiveTimeUs ? nowLib - du->receiveTimeUs : 0;
            const int64_t arrival = nowHost - int64_t(ageUs) * 10;

            if (!seenIdr && du->frameType != FRAME_TYPE_IDR) {
                LiCompleteVideoFrame(handle, DR_NEED_IDR);
                continue;
            }
            // A gap in frame numbers is a frame the network lost (FEC could not repair).
            const int64_t previousNumber = p.lastFrameNumber.exchange(du->frameNumber);
            if (previousNumber != 0 && du->frameNumber > previousNumber + 1) p.dropped += uint64_t(du->frameNumber - previousNumber - 1);

            unit.clear();
            for (PLENTRY entry = du->bufferList; entry; entry = entry->next)
                unit.insert(unit.end(), reinterpret_cast<const uint8_t*>(entry->data), reinterpret_cast<const uint8_t*>(entry->data) + entry->length);

            // The host's capture clock is the stream's timeline; it must still strictly increase.
            int64_t pts = int64_t(du->presentationTimeUs) * 10;
            if (pts <= lastPts) pts = lastPts + nominal;
            lastPts = pts;
            metas.push_back({pts, arrival});
            if (metas.size() > 64) metas.pop_front();

            const auto decodeBegin = Clock::now();
            // While the hardware path owns the pictures the target is never written; before the first
            // hardware picture (the decoder may still fall back to software) it must be a real frame.
            const bool needsTarget = p.decoder.framesDecoded() == 0 || !p.decoder.hardwareActive();
            AVFrame* target = needsTarget ? allocNv12(p.width, p.height) : p.dummyTarget;
            AVFrame* out = nullptr;
            bool hardware = false;
            const bool produced = target && p.decoder.decode(unit.data(), unit.size(), pts, target, &out, hardware);
            const double decodeMs = std::chrono::duration<double, std::milli>(Clock::now() - decodeBegin).count();

            if (!produced) {
                if (needsTarget && target) av_frame_free(&target);
                if (!p.decoder.waitingForInput()) {
                    ++p.errors;
                    ++p.idr;
                    recovering = true;
                    status = DR_NEED_IDR;
                    seenIdr = false;
                }
                LiCompleteVideoFrame(handle, status);
                continue;
            }

            std::shared_ptr<AVFrame> frame;
            if (hardware) {
                AVFrame* cloned = av_frame_clone(out);
                if (cloned) frame.reset(cloned, [](AVFrame* f) { av_frame_free(&f); });
                if (needsTarget && target) av_frame_free(&target);
            } else {
                frame.reset(target, [](AVFrame* f) { av_frame_free(&f); });   // the picture was converted into it
            }
            if (!frame) { ++p.errors; LiCompleteVideoFrame(handle, DR_OK); continue; }
            seenIdr = true;

            int64_t matchedArrival = arrival;
            const int64_t framePts = frame->pts != AV_NOPTS_VALUE ? frame->pts : pts;
            for (const auto& meta : metas) if (meta.pts == framePts) { matchedArrival = meta.arrival; break; }

            pipeline::FramePacket packet{};
            packet.sourceKind = pipeline::SourceKind::Moonlight;
            packet.pts = pipeline::Rational{framePts, 10000000};
            packet.duration = pipeline::Rational{nominal, 10000000};
            packet.arrivalHost100ns = matchedArrival;
            packet.decodedHost100ns = host100ns();
            if (recovering) { packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity); recovering = false; }

            auto fallback = fallbackColor(p.hdrStream);
            packet.colorInfo = pipeline::resolveFrameColor(*frame, fallback);
            packet.colorInfo.pixelFormat = pixelFormatOf(*frame);
            if (packet.colorInfo.pixelFormat == pipeline::SourcePixelFormat::Unknown) {
                ++p.errors;
                LiCompleteVideoFrame(handle, DR_OK);
                continue;
            }
            SourceInfo info;
            {
                std::lock_guard lock(self.mutex_);
                info = self.info_;
            }
            info.color = packet.colorInfo;
            info.hardwareDecodeActive = frame->format == AV_PIX_FMT_D3D12;
            info.videoDecodePath = info.hardwareDecodeActive ? "d3d12va" : "software";
            if (info.width != unsigned(frame->width) || info.height != unsigned(frame->height)) {
                info.width = unsigned(frame->width);
                info.height = unsigned(frame->height);
                info.displayAspect = frame->height > 0 ? double(frame->width) / double(frame->height) : 0.0;
                packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Resize);
            }
            {
                std::lock_guard lock(p.presentedMutex);
                p.presented.push_back({framePts, matchedArrival});
                if (p.presented.size() > 64) p.presented.pop_front();
            }

            // Statistics: the host reports its capture+encode time in 0.1 ms; the library timestamps
            // the first packet of the frame, its completion and our dequeue.
            {
                std::lock_guard lock(p.statsMutex);
                if (du->frameHostProcessingLatency) p.hostLatencyMs = movingAverage(p.hostLatencyMs, du->frameHostProcessingLatency / 10.0);
                p.receiveMs = movingAverage(p.receiveMs, double(int64_t(du->enqueueTimeUs) - int64_t(du->receiveTimeUs)) / 1000.0);
                p.queueMs = movingAverage(p.queueMs, double(int64_t(nowLib) - int64_t(du->enqueueTimeUs)) / 1000.0);
                p.decodeMs = movingAverage(p.decodeMs, decodeMs);
                ++p.decoded;
            }
            self.publish(std::move(frame), packet, info);
            LiCompleteVideoFrame(handle, DR_OK);
        }
        log::info("moonlight", "decode thread stopped");
    }

    // -- audio ---------------------------------------------------------------------------------
    static int arInit(int audioConfiguration, const POPUS_MULTISTREAM_CONFIGURATION config, void*, int) {
        auto* self = g_active.load();
        if (!self) return -1;
        auto& p = *self->p_;
        int error = 0;
        p.opus = opus_multistream_decoder_create(config->sampleRate, config->channelCount, config->streams, config->coupledStreams, config->mapping, &error);
        if (!p.opus || error != OPUS_OK) {
            log::error("moonlight-audio", std::format("Opus decoder creation failed: {}", error));
            return -1;
        }
        p.audioChannels = config->channelCount;
        p.samplesPerFrame = config->samplesPerFrame;
        p.pcm.assign(size_t(p.samplesPerFrame) * size_t(p.audioChannels), 0.f);
        p.audioSamples = 0;
        p.audioDiscontinuity = true;
        sink::AudioFormat layout;
        layout.channels = unsigned(config->channelCount);
        layout.mask = uint32_t(CHANNEL_MASK_FROM_AUDIO_CONFIGURATION(audioConfiguration));
        if (!layout.valid() || config->sampleRate != 48000) {
            log::error("moonlight-audio", std::format("unsupported audio layout channels={} mask=0x{:X} rate={}", layout.channels, layout.mask, config->sampleRate));
            return -1;
        }
        const WAVEFORMATEXTENSIBLE wave = sink::floatWave(layout);
        if (!self->audio_.configure(wave.Format, sizeof(wave))) {
            log::error("moonlight-audio", "the audio output could not be configured; the stream continues without sound");
            // Video stays available: a missing audio device must not end the session.
            opus_multistream_decoder_destroy(p.opus);
            p.opus = nullptr;
            return 0;
        }
        log::info("moonlight-audio", std::format("Opus {} ch, {} streams, {} coupled, {} samples per frame", config->channelCount, config->streams, config->coupledStreams, config->samplesPerFrame));
        return 0;
    }
    static void arStart() {
        if (auto* self = g_active.load(); self && self->p_->opus) self->audio_.start();
    }
    static void arStop() {
        if (auto* self = g_active.load(); self && self->p_->opus) self->audio_.stop();
    }
    static void arCleanup() {
        auto* self = g_active.load();
        if (!self) return;
        auto& p = *self->p_;
        if (p.opus) { opus_multistream_decoder_destroy(p.opus); p.opus = nullptr; }
    }
    static void arDecode(char* sample, int length) {
        auto* self = g_active.load();
        if (!self) return;
        auto& p = *self->p_;
        if (!p.opus) return;
        const int frames = opus_multistream_decode_float(p.opus, reinterpret_cast<const unsigned char*>(sample), length, p.pcm.data(), p.samplesPerFrame, 0);
        if (frames <= 0) { p.audioDiscontinuity = true; return; }
        const double ptsMs = double(p.audioSamples) * 1000.0 / 48000.0;
        self->audio_.push(p.pcm.data(), size_t(frames) * size_t(p.audioChannels) * sizeof(float), ptsMs, p.audioDiscontinuity);
        p.audioDiscontinuity = false;
        p.audioSamples += uint64_t(frames);
    }
};

MoonlightSessionSource::MoonlightSessionSource() : p_(std::make_unique<Impl>()) { p_->owner = this; }

MoonlightSessionSource::~MoonlightSessionSource() { close(); }

bool MoonlightSessionSource::connect(MoonlightConnectDesc desc) {
    close();
    MoonlightSessionSource* expected = nullptr;
    if (!g_active.compare_exchange_strong(expected, this)) {
        log::error("moonlight", "another Moonlight session is already active");
        return false;
    }
    {
        std::lock_guard lock(mutex_);
        initialized_ = started_ = false;
        latest_.reset();
        view_.reset();
        sequence_ = skipped_ = 0;
        openFlagPending_ = true;
        stats_ = {};
        stats_.state = MoonlightStats::State::Launching;
        stats_.fps = desc.options.fps;
        info_ = {};
    }
    p_ = std::make_unique<Impl>();
    p_->owner = this;
    cancel_ = false;
    owner_ = std::jthread([this, request = std::move(desc)](std::stop_token stop) mutable { run(stop, std::move(request)); });
    std::unique_lock lock(mutex_);
    ready_.wait(lock, [&] { return initialized_; });
    if (!started_) {
        lock.unlock();
        close();
        return false;
    }
    info_ = publishedInfo_;
    return true;
}

void MoonlightSessionSource::run(std::stop_token, MoonlightConnectDesc desc) {
    const auto fail = [&](std::wstring message, int code = 0) {
        log::error("moonlight", std::format("connect failed: code={}", code));
        std::lock_guard lock(mutex_);
        stats_.state = MoonlightStats::State::Failed;
        stats_.message = std::move(message);
        stats_.errorCode = code;
        initialized_ = true;
        started_ = false;
        ready_.notify_all();
    };
    try {
        const auto& o = desc.options;
        const int formats = moonlight::chooseVideoFormats(o.codec, o.hdr, desc.host.serverCodecModeSupport, o.av1HardwareDecode);
        if (!formats) {
            fail(o.hdr ? L"主机不支持所选的 HDR 编码。HDR 需要主机支持 HEVC 10-bit 或 AV1 10-bit。" : L"主机不支持所选的编码。");
            return;
        }

        STREAM_CONFIGURATION config;
        LiInitializeStreamConfiguration(&config);
        config.width = o.width;
        config.height = o.height;
        config.fps = o.fps;
        config.bitrate = o.bitrateKbps > 0 ? o.bitrateKbps : moonlight::defaultBitrateKbps(o.width, o.height, o.fps);
        config.packetSize = 1392;
        config.streamingRemotely = STREAM_CFG_AUTO;
        config.audioConfiguration = o.audioChannels >= 8 ? AUDIO_CONFIGURATION_71_SURROUND
                                  : o.audioChannels >= 6 ? AUDIO_CONFIGURATION_51_SURROUND : AUDIO_CONFIGURATION_STEREO;
        config.supportedVideoFormats = formats;
        config.clientRefreshRateX100 = o.displayRefreshHz > 0 ? o.displayRefreshHz * 100 : 0;
        config.colorSpace = (formats & VIDEO_FORMAT_MASK_10BIT) ? COLORSPACE_REC_2020 : COLORSPACE_REC_709;
        config.colorRange = COLOR_RANGE_LIMITED;
        config.encryptionFlags = ENCFLG_ALL;
        const std::string key = moonlight::crypto::randomBytes(16);
        const std::string ivPrefix = moonlight::crypto::randomBytes(4);
        std::memcpy(config.remoteInputAesKey, key.data(), sizeof(config.remoteInputAesKey));
        std::memcpy(config.remoteInputAesIv, ivPrefix.data(), 4);

        moonlight::LaunchRequest launch;
        launch.appId = desc.appId;
        launch.width = o.width;
        launch.height = o.height;
        launch.fps = o.fps;
        std::memcpy(launch.remoteInputKey.data(), key.data(), 16);
        std::memcpy(launch.remoteInputKeyId.data(), ivPrefix.data(), 4);
        launch.hdr = (formats & VIDEO_FORMAT_MASK_10BIT) != 0;
        launch.sops = o.sops;
        launch.localAudio = o.localAudio;
        launch.surroundAudioInfo = SURROUNDAUDIOINFO_FROM_AUDIO_CONFIGURATION(config.audioConfiguration);
        launch.gamepadMask = o.gamepadMask;
        launch.extraQuery = LiGetLaunchUrlQueryParameters();

        moonlight::ServerClient client(desc.identity, desc.address, desc.serverCertPem, desc.httpsPort, !desc.host.nvidiaServerSoftware);
        client.setCancel(&cancel_);
        std::string rtspUrl;
        try {
            rtspUrl = client.launch(desc.resume ? "resume" : "launch", launch, desc.host.nvidiaServerSoftware);
        } catch (const moonlight::StatusError& e) {
            fail(e.status == 401 ? L"主机不认识这台电脑，请重新配对。" : std::format(L"主机拒绝了启动请求（{}）。", widen(e.what())), e.status);
            return;
        } catch (const moonlight::TransportError& e) {
            if (e.kind == moonlight::TransportError::Kind::Cancelled) { fail(L"已取消。"); return; }
            fail(e.kind == moonlight::TransportError::Kind::Tls ? L"与主机的加密连接失败，主机的证书可能已改变，请重新配对。" : L"连不上主机，请检查网络和主机是否开机。");
            return;
        }
        // The identity (the client's private key) is not needed any more.
        SecureZeroMemory(desc.identity.keyPem.data(), desc.identity.keyPem.size());
        desc.identity = {};
        if (rtspUrl.empty()) { fail(L"主机没有返回串流地址。"); return; }
        if (cancel_.load()) { fail(L"已取消。"); return; }

        {
            std::lock_guard lock(mutex_);
            stats_.state = MoonlightStats::State::Connecting;
            stats_.encrypted = true;
        }
        p_->desc = std::move(desc);
        const auto& d = p_->desc;

        SERVER_INFORMATION server;
        LiInitializeServerInformation(&server);
        const std::string address = d.address.host;
        server.address = address.c_str();
        server.serverInfoAppVersion = d.host.appVersion.c_str();
        server.serverInfoGfeVersion = d.host.gfeVersion.empty() ? nullptr : d.host.gfeVersion.c_str();
        server.rtspSessionUrl = rtspUrl.c_str();
        server.serverCodecModeSupport = d.host.serverCodecModeSupport;

        CONNECTION_LISTENER_CALLBACKS listener;
        LiInitializeConnectionCallbacks(&listener);
        listener.stageStarting = Callbacks::stageStarting;
        listener.stageComplete = Callbacks::stageComplete;
        listener.stageFailed = Callbacks::stageFailed;
        listener.connectionStarted = Callbacks::started;
        listener.connectionTerminated = Callbacks::terminated;
        listener.logMessage = Callbacks::logMessage;
        listener.rumble = Callbacks::rumble;
        listener.connectionStatusUpdate = Callbacks::connectionStatus;
        listener.setHdrMode = Callbacks::setHdrMode;

        DECODER_RENDERER_CALLBACKS video;
        LiInitializeVideoCallbacks(&video);
        video.setup = Callbacks::drSetup;
        video.start = Callbacks::drStart;
        video.stop = Callbacks::drStop;
        video.cleanup = Callbacks::drCleanup;
        video.submitDecodeUnit = nullptr;
        video.capabilities = CAPABILITY_PULL_RENDERER;

        AUDIO_RENDERER_CALLBACKS audio;
        LiInitializeAudioCallbacks(&audio);
        audio.init = Callbacks::arInit;
        audio.start = Callbacks::arStart;
        audio.stop = Callbacks::arStop;
        audio.cleanup = Callbacks::arCleanup;
        audio.decodeAndPlaySample = Callbacks::arDecode;
        // Sunshine sends 5 ms packets; the audio path copes with any duration.
        audio.capabilities = CAPABILITY_SUPPORTS_ARBITRARY_AUDIO_DURATION;

        log::info("moonlight", std::format("connecting {}x{}@{} {} kbps formats=0x{:X} audio={}ch", o.width, o.height, o.fps, config.bitrate, formats, o.audioChannels));
        const int result = LiStartConnection(&server, &config, &listener, &video, &audio, nullptr, 0, nullptr, 0);
        // The session key material is no longer needed here either.
        SecureZeroMemory(config.remoteInputAesKey, sizeof(config.remoteInputAesKey));
        if (result != 0) {
            std::wstring message;
            {
                std::lock_guard lock(mutex_);
                message = stats_.message;
            }
            fail(message.empty() ? std::format(L"无法建立串流连接（错误码 {}）。", result) : message, result);
            return;
        }
        {
            std::lock_guard lock(mutex_);
            stats_.state = MoonlightStats::State::Streaming;
            publishedInfo_ = info_;
            publishedInfo_.opened = true;
            publishedInfo_.kind = pipeline::SourceKind::Moonlight;
            publishedInfo_.averageFps = double(o.fps);
            publishedInfo_.nominalRateNum = o.fps;
            publishedInfo_.nominalRateDen = 1;
            publishedInfo_.timestampQuantum = 1.0 / double(std::max(1, o.fps));
            publishedInfo_.duration = pipeline::Rational::unknown();
            info_ = publishedInfo_;
            initialized_ = true;
            started_ = true;
        }
        p_->streaming = true;
        ready_.notify_all();
        // From here the library's own threads run the stream; this thread only waits to be stopped.
        while (!cancel_.load() && !p_->terminated.load()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    } catch (const std::exception& e) {
        log::error("moonlight", std::format("session failed: {}", e.what()));
        fail(L"串流处理异常，已停止，请查看日志。");
    }
}

void MoonlightSessionSource::publish(std::shared_ptr<AVFrame> frame, const pipeline::FramePacket& packet, const SourceInfo& info) {
    std::lock_guard lock(mutex_);
    Frame item{std::move(frame), packet, info};
    item.packet.sequence = ++sequence_;
    if (openFlagPending_) {
        item.packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Open);
        openFlagPending_ = false;
    }
    // A picture the engine never asked for is a skipped source frame: flag it so temporal
    // effects reset their history, exactly like the capture mailbox does.
    if (latest_) {
        ++skipped_;
        item.packet.flags |= latest_->packet.flags;
        item.packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Drop);
    }
    latest_ = std::move(item);
}

SourceReadStatus MoonlightSessionSource::read(pipeline::FramePacket& packet, const AVFrame** frame) {
    if (frame) *frame = nullptr;
    std::lock_guard lock(mutex_);
    if (stats_.state == MoonlightStats::State::Failed || stats_.state == MoonlightStats::State::Ended) return SourceReadStatus::Error;
    if (!latest_) return started_ ? SourceReadStatus::Waiting : SourceReadStatus::Error;
    packet = latest_->packet;
    info_ = latest_->info;
    view_ = std::move(latest_->frame);
    latest_.reset();
    if (frame) *frame = view_.get();
    return SourceReadStatus::Frame;
}

void MoonlightSessionSource::videoPresented(double ptsMs, int64_t host) {
    std::optional<int64_t> arrival;
    {
        std::lock_guard lock(p_->presentedMutex);
        const int64_t pts100 = int64_t(ptsMs * 10000.0);
        for (const auto& meta : p_->presented) if (std::llabs(meta.pts - pts100) < 5) { arrival = meta.arrival; break; }
    }
    audio_.videoPresented(ptsMs, host, arrival);
}

void MoonlightSessionSource::disconnect(bool quitApp) {
    // The HTTP request needs the identity the session has already wiped; quitting the app on the
    // host is therefore left to the caller (bridge), which holds the host record. Here it only ends the stream.
    (void)quitApp;
    cancel_ = true;
}

void MoonlightSessionSource::close() noexcept {
    cancel_ = true;
    if (owner_.joinable()) owner_.request_stop();
    // LiStopConnection joins the library threads; the decode thread ends through the stop callback.
    if (g_active.load() == this) {
        LiInterruptConnection();
        bool wasStreaming = false;
        {
            std::lock_guard lib(p_->libMutex);
            wasStreaming = p_->streaming.load();
            if (wasStreaming) Callbacks::releaseInputLocked(*p_);
            p_->streaming = false;
        }
        if (wasStreaming) LiStopConnection();
    }
    if (owner_.joinable()) owner_.join();
    if (p_ && p_->decodeThread.joinable()) { p_->decodeStop = true; LiWakeWaitForVideoFrame(); p_->decodeThread.join(); }
    audio_.stop();
    {
        std::lock_guard lock(mutex_);
        latest_.reset();
        view_.reset();
        info_ = {};
        started_ = false;
        if (stats_.state == MoonlightStats::State::Streaming || stats_.state == MoonlightStats::State::Connecting || stats_.state == MoonlightStats::State::Launching)
            stats_.state = MoonlightStats::State::Idle;
    }
    MoonlightSessionSource* self = this;
    g_active.compare_exchange_strong(self, nullptr);
}

MoonlightStats MoonlightSessionSource::stats() const {
    MoonlightStats s;
    {
        std::lock_guard lock(mutex_);
        s = stats_;
        s.dropped = 0;
    }
    if (!p_) return s;
    s.units = p_->units.load();
    s.decoded = p_->decoded.load();
    s.dropped = p_->dropped.load() + skipped();
    s.decodeErrors = p_->errors.load();
    s.idrRequests = p_->idr.load();
    s.hardwareDecode = p_->decoder.hardwareActive();
    {
        std::lock_guard lock(p_->statsMutex);
        s.hostLatencyMs = p_->hostLatencyMs;
        s.receiveMs = p_->receiveMs;
        s.queueMs = p_->queueMs;
        s.decodeMs = p_->decodeMs;
    }
    std::lock_guard lib(p_->libMutex);
    if (p_->streaming.load() && s.state == MoonlightStats::State::Streaming) {
        uint32_t rtt = 0, variance = 0;
        if (LiGetEstimatedRttInfo(&rtt, &variance)) { s.rttMs = double(rtt); s.rttVarianceMs = double(variance); }
        if (const RTP_VIDEO_STATS* video = LiGetRTPVideoStats()) {
            s.videoPackets = video->packetCountVideo;
            s.fecPackets = video->packetCountFec;
            s.fecRecovered = video->packetCountFecRecovered;
            s.fecFailed = video->packetCountFecFailed;
            s.outOfSequence = video->packetCountOOS;
        }
    }
    return s;
}

uint64_t MoonlightSessionSource::unitsReceived() const { return p_ ? p_->units.load() : 0; }

// --- input ----------------------------------------------------------------------------------
// Every send happens under libMutex after checking `streaming`: close() clears `streaming` under the
// same mutex before LiStopConnection, so no send can overlap the teardown.

namespace {
constexpr uint32_t kPadSupported = moonlight::kPadUp | moonlight::kPadDown | moonlight::kPadLeft | moonlight::kPadRight |
    moonlight::kPadStart | moonlight::kPadBack | moonlight::kPadLeftStick | moonlight::kPadRightStick |
    moonlight::kPadLeftBumper | moonlight::kPadRightBumper | moonlight::kPadGuide | moonlight::kPadA | moonlight::kPadB | moonlight::kPadX | moonlight::kPadY;
}

void MoonlightSessionSource::controller(const remoteplay::ControllerState& state) {
    if (!p_) return;
    const moonlight::PadFrame frame = moonlight::mapPad(state);
    std::lock_guard lib(p_->libMutex);
    if (!p_->streaming.load() || (p_->desc.options.gamepadMask & 1) == 0) return;
    if (!p_->padArrived) {
        // Older hosts answer "unsupported" and work without the arrival event.
        LiSendControllerArrivalEvent(0, uint16_t(p_->desc.options.gamepadMask), LI_CTYPE_XBOX, kPadSupported, LI_CCAP_ANALOG_TRIGGERS | LI_CCAP_RUMBLE);
        p_->padArrived = true;
        p_->pad = {};
        p_->pad.buttons = ~frame.buttons;   // forces the first frame out
    }
    if (frame == p_->pad) return;
    p_->pad = frame;
    LiSendMultiControllerEvent(0, int16_t(p_->desc.options.gamepadMask), int(frame.buttons), frame.leftTrigger, frame.rightTrigger,
                               frame.leftX, frame.leftY, frame.rightX, frame.rightY);
}

remoteplay::ControllerFeedback MoonlightSessionSource::takeFeedback() {
    remoteplay::ControllerFeedback out;
    if (!p_) return out;
    std::lock_guard lock(p_->feedbackMutex);
    const auto now = Clock::now();
    const bool active = p_->rumbleLow != 0 || p_->rumbleHigh != 0;
    if (p_->feedbackPending || (active && now - p_->feedbackSent >= std::chrono::seconds(2))) {
        out.rumble = true;
        out.left = uint8_t((p_->rumbleLow + 128) / 257);
        out.right = uint8_t((p_->rumbleHigh + 128) / 257);
        p_->feedbackPending = false;
        p_->feedbackSent = now;
    }
    return out;
}

void MoonlightSessionSource::keyboard(uint32_t virtualKey, uint32_t scanCode, bool extended, bool down) {
    if (!p_) return;
    const moonlight::HostKey key = moonlight::hostKey(virtualKey, scanCode, extended);
    std::lock_guard lib(p_->libMutex);
    if (!p_->streaming.load()) return;
    if (down) {
        if (!p_->keys.press(key.code, key.modifier)) return;   // auto-repeat: the host repeats by itself
        LiSendKeyboardEvent(key.code, KEY_ACTION_DOWN, char(p_->keys.modifiers()));
    } else {
        if (!p_->keys.release(key.code)) return;
        LiSendKeyboardEvent(key.code, KEY_ACTION_UP, char(p_->keys.modifiers()));
    }
}

void MoonlightSessionSource::mouseMove(int dx, int dy) {
    if (!p_ || (dx == 0 && dy == 0)) return;
    std::lock_guard lib(p_->libMutex);
    if (!p_->streaming.load()) return;
    LiSendMouseMoveEvent(moonlight::clampDelta(dx), moonlight::clampDelta(dy));
}

void MoonlightSessionSource::mouseButton(int button, bool down) {
    if (!p_ || button < 1 || button > 5) return;
    std::lock_guard lib(p_->libMutex);
    if (!p_->streaming.load()) return;
    const unsigned bit = 1u << button;
    if (down == ((p_->mouseButtons & bit) != 0)) return;
    p_->mouseButtons ^= bit;
    LiSendMouseButtonEvent(down ? BUTTON_ACTION_PRESS : BUTTON_ACTION_RELEASE, button);
}

void MoonlightSessionSource::scroll(int delta, bool horizontal) {
    if (!p_ || delta == 0) return;
    std::lock_guard lib(p_->libMutex);
    if (!p_->streaming.load()) return;
    const short amount = short(std::clamp(delta, -32767, 32767));
    if (horizontal) LiSendHighResHScrollEvent(amount);
    else LiSendHighResScrollEvent(amount);
}

void MoonlightSessionSource::releaseInput() {
    if (!p_) return;
    std::lock_guard lib(p_->libMutex);
    if (!p_->streaming.load()) return;
    Callbacks::releaseInputLocked(*p_);
}

uint64_t MoonlightSessionSource::skipped() const {
    std::lock_guard lock(mutex_);
    return skipped_;
}

} // namespace veyra::source
