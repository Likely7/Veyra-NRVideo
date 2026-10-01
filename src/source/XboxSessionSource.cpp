// SPDX-License-Identifier: GPL-3.0-only
// Session flow after Greenlight's streammanager.ts / stream page (unknownskl/greenlight, MIT).
#include "veyra/source/XboxSessionSource.h"

#include <windows.h>
#include <mmreg.h>

#include <opus/opus.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}

#include <algorithm>
#include <chrono>
#include <format>
#include <vector>

#include "veyra/Log.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/sink/AudioFormat.h"
#include "veyra/source/CaptureCompressedDecoder.h"
#include "veyra/xbox/Account.h"
#include "veyra/xbox/StreamApi.h"
#include "veyra/xbox/WebRtcSession.h"

namespace veyra::source {
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

pipeline::SourcePixelFormat pixelFormatOf(const AVFrame& frame) {
    if (frame.format == AV_PIX_FMT_D3D12 && frame.hw_frames_ctx) {
        const auto* context = reinterpret_cast<const AVHWFramesContext*>(frame.hw_frames_ctx->data);
        return context->sw_format == AV_PIX_FMT_NV12 ? pipeline::SourcePixelFormat::NV12
             : context->sw_format == AV_PIX_FMT_P010 ? pipeline::SourcePixelFormat::P010 : pipeline::SourcePixelFormat::Unknown;
    }
    switch (frame.format) {
    case AV_PIX_FMT_NV12: return pipeline::SourcePixelFormat::NV12;
    case AV_PIX_FMT_YUV420P: return pipeline::SourcePixelFormat::Yuv420P;
    default: return pipeline::SourcePixelFormat::Unknown;
    }
}

// Console streams are 8-bit H.264 SDR: BT.709 limited range, shown with the same code values as a capture card.
pipeline::ColorDescription fallbackColor() {
    pipeline::ColorDescription c;
    c.pixelFormat = pipeline::SourcePixelFormat::NV12;
    c.range = pipeline::ColorRange::Limited;
    c.rangeAssumed = true;
    c.matrixAssumed = c.transferAssumed = c.primariesAssumed = true;
    c.matrix = pipeline::YuvMatrix::BT709;
    c.transfer = pipeline::TransferFunction::BT709;
    c.primaries = pipeline::ColorPrimaries::BT709;
    c.preserveSdrCodeValues = true;
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

double movingAverage(double current, double sample) { return current > 0 ? current * 0.95 + sample * 0.05 : sample; }

// True when the access unit carries an IDR slice or a sequence parameter set (a point to start decoding).
bool startsDecoding(const std::vector<uint8_t>& unit) {
    for (size_t i = 0; i + 3 < unit.size(); ++i) {
        if (unit[i] == 0 && unit[i + 1] == 0 && unit[i + 2] == 1) {
            const uint8_t type = unit[i + 3] & 0x1F;
            if (type == 5 || type == 7) return true;
            i += 2;
        }
    }
    return false;
}

std::wstring describeSessionFailure(const std::string& code, const std::string& message) {
    if (message.find("WaitingForServerToRegister") != std::string::npos)
        return L"主机没有连上 Xbox 服务。请确认主机开着，或处于“睡眠（即时启动）”模式，并在主机的 设置 → 设备和连接 → 远程功能 里打开“启用远程功能”。";
    if (code == "ConsoleNotFound" || message.find("not found") != std::string::npos)
        return L"找不到这台主机，可能已从账号里移除。请刷新主机列表。";
    if (message.find("InUse") != std::string::npos || message.find("busy") != std::string::npos)
        return L"这台主机正在被别的设备串流。请先在那边断开。";
    return std::format(L"主机拒绝了串流（{} {}）。", widen(code), widen(message));
}

class ApiSignaling final : public xbox::Signaling {
public:
    ApiSignaling(xbox::StreamApi& api, std::string session) : api_(api), session_(std::move(session)) {}
    std::string exchangeSdp(const std::string& offer) override { return api_.exchangeSdp(session_, offer); }
    std::vector<xbox::IceCandidate> exchangeIce(const std::vector<xbox::IceCandidate>& local) override { return api_.exchangeIce(session_, local); }
private:
    xbox::StreamApi& api_;
    std::string session_;
};

} // namespace

struct XboxSessionSource::Impl {
    XboxConnectDesc desc;
    std::shared_ptr<xbox::HttpsTransport> http;
    std::unique_ptr<xbox::StreamApi> api;
    std::string host, gsToken;
    std::string sessionId;
    xbox::WebRtcSession rtc;
    std::atomic<bool> streaming{false}, ended{false};
    std::wstring endReason;
    std::mutex endMutex;

    // Video: the WebRTC thread queues access units; the decode thread drains them.
    struct Unit { std::vector<uint8_t> data; uint32_t rtp; int64_t arrival; };
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<Unit> queue;
    bool stopDecode = false;
    std::thread decodeThread, keepaliveThread;
    CaptureCompressedDecoder decoder;
    bool decoderOpen = false;
    AVFrame* dummyTarget = nullptr;
    unsigned width = 1920, height = 1080;
    Clock::time_point lastKeyframeRequest{};

    struct Meta { int64_t pts; int64_t arrival; };
    std::deque<Meta> presented;
    std::mutex presentedMutex;

    // Audio
    OpusDecoder* opus = nullptr;
    std::vector<float> pcm;
    uint64_t audioSamples = 0;
    bool audioDiscontinuity = true;
    bool audioReady = false;

    // Input and feedback
    std::mutex inputMutex;
    xbox::GamepadFrame lastPad;
    Clock::time_point lastPadSent{};
    bool padSent = false;
    std::mutex feedbackMutex;
    remoteplay::ControllerFeedback feedback;
    bool feedbackPending = false;
    Clock::time_point rumbleUntil{};
    bool rumbling = false;

    std::atomic<uint64_t> units{0}, decoded{0}, dropped{0}, errors{0};
    std::mutex statsMutex;
    double decodeMs = 0;
    mutable Clock::time_point rateStart = Clock::now();
    mutable uint64_t rateUnits = 0, rateDecoded = 0, rateBytes = 0;
    mutable double receivedFps = 0, decodedFps = 0, videoMbps = 0;

    ~Impl() {
        if (dummyTarget) av_frame_free(&dummyTarget);
        if (opus) opus_decoder_destroy(opus);
    }
};

XboxSessionSource::XboxSessionSource() : p_(std::make_unique<Impl>()) {}

XboxSessionSource::~XboxSessionSource() { close(); }

void XboxSessionSource::setState(XboxStats::State state, std::wstring message) {
    std::lock_guard lock(mutex_);
    stats_.state = state;
    stats_.message = std::move(message);
}

bool XboxSessionSource::connect(XboxConnectDesc desc) {
    close();
    {
        std::lock_guard lock(mutex_);
        initialized_ = started_ = false;
        latest_.reset();
        view_.reset();
        sequence_ = skipped_ = 0;
        openFlagPending_ = true;
        stats_ = {};
        stats_.state = XboxStats::State::Starting;
        info_ = {};
    }
    p_ = std::make_unique<Impl>();
    cancel_ = false;
    owner_ = std::jthread([this, request = std::move(desc)]() mutable { run(std::move(request)); });
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

void XboxSessionSource::run(XboxConnectDesc desc) {
    auto& p = *p_;
    p.desc = std::move(desc);
    const auto fail = [&](std::wstring message) {
        log::error("xbox", "connect failed");
        std::lock_guard lock(mutex_);
        stats_.state = XboxStats::State::Failed;
        stats_.message = std::move(message);
        initialized_ = true;
        started_ = false;
        ready_.notify_all();
    };
    const auto cancelled = [&] { return cancel_.load(); };

    try {
        if (!p.desc.account || !p.desc.account->signedIn()) { fail(L"请先登录 Xbox 账号。"); return; }
        setState(XboxStats::State::Starting, L"正在登录 Xbox 串流服务…");
        const xbox::StreamingAccess access = p.desc.account->streamingAccess();
        p.http = xbox::makeWinHttpTransport(&cancel_);
        p.api = std::make_unique<xbox::StreamApi>(p.http, access.host, access.gsToken);
        p.host = access.host;
        p.gsToken = access.gsToken;
        log::info("xbox", "streaming region " + access.regionName + " host " + access.host);

        setState(XboxStats::State::Starting, L"正在唤醒主机并准备串流…");
        p.sessionId = p.api->play(p.desc.serverId, p.desc.locale);
        log::info("xbox", "session " + p.sessionId);
        bool connectSent = false;
        const auto deadline = Clock::now() + std::chrono::seconds(120);
        for (;;) {
            if (cancelled()) { fail(L"已取消。"); return; }
            if (Clock::now() > deadline) { fail(L"主机长时间没有准备好串流。请确认主机开着并且网络正常。"); return; }
            const xbox::SessionState state = p.api->state(p.sessionId);
            log::info("xbox", "session state " + state.state);
            if (state.state == "Provisioned") break;
            if (state.state == "Failed") { fail(describeSessionFailure(state.errorCode, state.errorMessage)); return; }
            if (state.state == "ReadyToConnect" && !connectSent) {
                p.api->connect(p.sessionId, p.desc.account->transferToken());
                connectSent = true;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        setState(XboxStats::State::Connecting, L"正在建立连接…");
        xbox::WebRtcCallbacks callbacks;
        callbacks.video = [this](std::vector<uint8_t>&& unit, uint32_t rtp, int64_t arrival) {
            auto& q = *p_;
            ++q.units;
            std::lock_guard lock(q.queueMutex);
            // Latency first: keep only a short backlog; a decoder that falls behind drops the oldest.
            while (q.queue.size() >= 8) { q.queue.pop_front(); ++q.dropped; }
            q.queue.push_back({std::move(unit), rtp, arrival});
            q.queueCv.notify_one();
        };
        callbacks.audio = [this](const uint8_t* data, size_t size, uint32_t) {
            auto& q = *p_;
            if (!q.audioReady || !q.opus) return;
            const int frames = opus_decode_float(q.opus, data, opus_int32(size), q.pcm.data(), 5760, 0);
            if (frames <= 0) { q.audioDiscontinuity = true; return; }
            const double ptsMs = double(q.audioSamples) * 1000.0 / 48000.0;
            audio_.push(q.pcm.data(), size_t(frames) * 2 * sizeof(float), ptsMs, q.audioDiscontinuity);
            q.audioDiscontinuity = false;
            q.audioSamples += uint64_t(frames);
        };
        callbacks.vibration = [this](const xbox::Vibration& v) {
            if (v.gamepad != 0) return;
            auto& q = *p_;
            std::lock_guard lock(q.feedbackMutex);
            q.feedback.rumble = true;
            q.feedback.left = uint8_t(std::min(100, int(v.leftMotor)) * 255 / 100);
            q.feedback.right = uint8_t(std::min(100, int(v.rightMotor)) * 255 / 100);
            q.feedbackPending = true;
            q.rumbling = q.feedback.left || q.feedback.right;
            q.rumbleUntil = Clock::now() + std::chrono::milliseconds(v.durationMs ? v.durationMs : 1000);
        };
        callbacks.serverVideoSize = [this](uint32_t w, uint32_t h) {
            std::lock_guard lock(mutex_);
            stats_.width = w;
            stats_.height = h;
        };
        callbacks.ended = [this](const std::string& reason) {
            auto& q = *p_;
            std::lock_guard lock(q.endMutex);
            q.endReason = reason == "the console ended the stream" ? L"主机结束了串流。" : L"与主机的连接中断了。";
            q.ended = true;
        };
        p.rtc.setCallbacks(std::move(callbacks));

        // Audio output: stereo float at 48 kHz.
        int opusError = 0;
        p.opus = opus_decoder_create(48000, 2, &opusError);
        if (p.opus && opusError == OPUS_OK) {
            p.pcm.assign(5760 * 2, 0.f);
            sink::AudioFormat layout;
            layout.channels = 2;
            layout.mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
            const WAVEFORMATEXTENSIBLE wave = sink::floatWave(layout);
            p.audioReady = audio_.configure(wave.Format, sizeof(wave));
            if (!p.audioReady) log::warn("xbox-audio", "the audio output could not be configured; the stream continues without sound");
        }

        ApiSignaling signaling(*p.api, p.sessionId);
        std::string error;
        if (!p.rtc.start(signaling, &error)) {
            fail(L"无法和主机建立连接：" + widen(error) + L"。请确认本机网络没有拦截 UDP，主机与本机最好在同一局域网。");
            return;
        }
        if (!p.rtc.waitReady(std::chrono::seconds(10))) { fail(L"主机连上了，但没有完成握手。请重试。"); return; }

        // Decode and keepalive.
        p.stopDecode = false;
        p.decodeThread = std::thread([this] { decodeLoop(); });
        p.keepaliveThread = std::thread([this] {
            auto& q = *p_;
            auto next = Clock::now() + std::chrono::seconds(30);
            while (!cancel_.load() && !q.ended.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (Clock::now() < next) continue;
                next = Clock::now() + std::chrono::seconds(30);
                try {
                    q.api->keepalive(q.sessionId);
                } catch (const xbox::ServiceError& e) {
                    log::warn("xbox", std::format("keepalive HTTP {}", e.status));
                    if (e.status == 404) { std::lock_guard lock(q.endMutex); q.endReason = L"串流会话已在服务器端结束。"; q.ended = true; }
                } catch (const std::exception& e) {
                    log::warn("xbox", std::string("keepalive: ") + e.what());
                }
            }
        });
        {
            std::lock_guard lock(p.inputMutex);
            p.streaming = true;
        }
        {
            std::lock_guard lock(mutex_);
            stats_.state = XboxStats::State::Streaming;
            stats_.message.clear();
            publishedInfo_ = info_;
            publishedInfo_.opened = true;
            publishedInfo_.kind = pipeline::SourceKind::Xbox;
            publishedInfo_.averageFps = 60.0;
            publishedInfo_.nominalRateNum = 60;
            publishedInfo_.nominalRateDen = 1;
            publishedInfo_.timestampQuantum = 1.0 / 60.0;
            publishedInfo_.duration = pipeline::Rational::unknown();
            publishedInfo_.width = p.width;
            publishedInfo_.height = p.height;
            publishedInfo_.displayAspect = 16.0 / 9.0;
            publishedInfo_.videoCodecName = "H.264";
            publishedInfo_.color = fallbackColor();
            info_ = publishedInfo_;
            initialized_ = true;
            started_ = true;
        }
        ready_.notify_all();
        log::info("xbox", "streaming");
        while (!cancel_.load() && !p.ended.load()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (p.ended.load()) {
            std::wstring reason;
            { std::lock_guard lock(p.endMutex); reason = p.endReason; }
            setState(XboxStats::State::Ended, reason);
        }
    } catch (const xbox::ServiceError& e) {
        log::error("xbox", std::format("service error {} {}", e.status, e.what()));
        std::wstring message = xbox::describeXboxError(e.body);
        if (message.empty()) {
            if (e.status == 401 || e.status == 403 || std::string(e.what()) == "sign-in expired") message = L"Xbox 登录已失效，请重新登录。";
            else message = std::format(L"Xbox 服务返回错误（HTTP {}）。", e.status);
        }
        if (!initialized_) fail(std::move(message));
        else setState(XboxStats::State::Failed, std::move(message));
    } catch (const xbox::NetworkError& e) {
        log::error("xbox", std::string("network: ") + e.what());
        std::wstring message = cancel_ ? L"已取消。" : L"连不上 Xbox 服务，请检查网络。（" + widen(e.what()) + L"）";
        if (!initialized_) fail(std::move(message));
        else setState(XboxStats::State::Failed, std::move(message));
    } catch (const std::exception& e) {
        log::error("xbox", std::string("session failed: ") + e.what());
        if (!initialized_) fail(L"Xbox 串流出错：" + widen(e.what()));
        else setState(XboxStats::State::Failed, L"Xbox 串流出错：" + widen(e.what()));
    }
}

void XboxSessionSource::decodeLoop() {
    auto& p = *p_;
    bool seenKeyframe = false, recovering = false;
    int64_t lastPts = -1;
    uint32_t lastRtp = 0;
    int64_t rtpHigh = 0;
    bool haveRtp = false;
    std::deque<Impl::Meta> metas;
    const auto requestKeyframe = [&] {
        const auto now = Clock::now();
        if (now - p.lastKeyframeRequest < std::chrono::milliseconds(500)) return;
        p.lastKeyframeRequest = now;
        p.rtc.requestKeyframe();
    };
    log::info("xbox", "decode thread started");
    for (;;) {
        Impl::Unit unit;
        {
            std::unique_lock lock(p.queueMutex);
            p.queueCv.wait(lock, [&] { return p.stopDecode || !p.queue.empty(); });
            if (p.stopDecode) break;
            unit = std::move(p.queue.front());
            p.queue.pop_front();
        }
        if (!seenKeyframe && !startsDecoding(unit.data)) { ++p.dropped; requestKeyframe(); continue; }
        if (!p.decoderOpen) {
            p.decoder.setStreamProfile(true);
            if (!p.decoder.open(CaptureCodec::H264, p.width, p.height, nullptr, 0, p.desc.decodeDevice.get(), p.desc.decodeQueue.get())) {
                log::error("xbox", "no usable H.264 decoder");
                setState(XboxStats::State::Failed, L"没有可用的 H.264 解码器。");
                p.ended = true;
                break;
            }
            p.decoderOpen = true;
            if (!p.dummyTarget) p.dummyTarget = allocNv12(16, 16);
            log::info("xbox", std::string("decoder ") + p.decoder.backendName());
        }
        // 90 kHz RTP timestamps, unwrapped, to 100 ns ticks.
        if (haveRtp && unit.rtp < lastRtp && lastRtp - unit.rtp > 0x80000000u) rtpHigh += int64_t(1) << 32;
        lastRtp = unit.rtp;
        haveRtp = true;
        int64_t pts = (rtpHigh + int64_t(unit.rtp)) * 1000 / 9;
        if (pts <= lastPts) pts = lastPts + 166667;
        lastPts = pts;
        metas.push_back({pts, unit.arrival});
        if (metas.size() > 64) metas.pop_front();

        const auto begin = Clock::now();
        const bool needsTarget = p.decoder.framesDecoded() == 0 || !p.decoder.hardwareActive();
        AVFrame* target = needsTarget ? allocNv12(p.width, p.height) : p.dummyTarget;
        AVFrame* out = nullptr;
        bool hardware = false;
        const bool produced = target && p.decoder.decode(unit.data.data(), unit.data.size(), pts, target, &out, hardware);
        const double decodeMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        if (!produced) {
            if (needsTarget && target) av_frame_free(&target);
            if (!p.decoder.waitingForInput()) {
                ++p.errors;
                seenKeyframe = false;
                recovering = true;
                requestKeyframe();
            }
            continue;
        }
        std::shared_ptr<AVFrame> frame;
        if (hardware) {
            AVFrame* cloned = av_frame_clone(out);
            if (cloned) frame.reset(cloned, [](AVFrame* f) { av_frame_free(&f); });
            if (needsTarget && target) av_frame_free(&target);
        } else {
            frame.reset(target, [](AVFrame* f) { av_frame_free(&f); });
        }
        if (!frame) { ++p.errors; continue; }
        seenKeyframe = true;
        if (unsigned(frame->width) != p.width || unsigned(frame->height) != p.height) {
            p.width = unsigned(frame->width);
            p.height = unsigned(frame->height);
        }

        int64_t arrival = unit.arrival;
        const int64_t framePts = frame->pts != AV_NOPTS_VALUE ? frame->pts : pts;
        for (const auto& meta : metas) if (meta.pts == framePts) { arrival = meta.arrival; break; }

        pipeline::FramePacket packet{};
        packet.sourceKind = pipeline::SourceKind::Xbox;
        packet.pts = pipeline::Rational{framePts, 10000000};
        packet.duration = pipeline::Rational{166667, 10000000};
        packet.arrivalHost100ns = arrival;
        packet.decodedHost100ns = host100ns();
        if (recovering) { packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity); recovering = false; }
        auto fallback = fallbackColor();
        packet.colorInfo = pipeline::resolveFrameColor(*frame, fallback);
        packet.colorInfo.pixelFormat = pixelFormatOf(*frame);
        if (packet.colorInfo.pixelFormat == pipeline::SourcePixelFormat::Unknown) { ++p.errors; continue; }

        SourceInfo info;
        {
            std::lock_guard lock(mutex_);
            info = info_;
            stats_.width = unsigned(frame->width);
            stats_.height = unsigned(frame->height);
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
            p.presented.push_back({framePts, arrival});
            if (p.presented.size() > 64) p.presented.pop_front();
        }
        {
            std::lock_guard lock(p.statsMutex);
            p.decodeMs = movingAverage(p.decodeMs, decodeMs);
        }
        ++p.decoded;
        publish(std::move(frame), packet, info);
    }
    log::info("xbox", "decode thread stopped");
}

void XboxSessionSource::publish(std::shared_ptr<AVFrame> frame, const pipeline::FramePacket& packet, const SourceInfo& info) {
    std::lock_guard lock(mutex_);
    Frame item{std::move(frame), packet, info};
    item.packet.sequence = ++sequence_;
    if (openFlagPending_) {
        item.packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Open);
        openFlagPending_ = false;
    }
    if (latest_) {
        ++skipped_;
        item.packet.flags |= latest_->packet.flags;
        item.packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Drop);
    }
    latest_ = std::move(item);
}

SourceReadStatus XboxSessionSource::read(pipeline::FramePacket& packet, const AVFrame** frame) {
    if (frame) *frame = nullptr;
    std::lock_guard lock(mutex_);
    if (stats_.state == XboxStats::State::Failed || stats_.state == XboxStats::State::Ended) return SourceReadStatus::Error;
    if (!latest_) return started_ ? SourceReadStatus::Waiting : SourceReadStatus::Error;
    packet = latest_->packet;
    info_ = latest_->info;
    view_ = std::move(latest_->frame);
    latest_.reset();
    if (frame) *frame = view_.get();
    return SourceReadStatus::Frame;
}

void XboxSessionSource::videoPresented(double ptsMs, int64_t host) {
    std::optional<int64_t> arrival;
    if (p_) {
        std::lock_guard lock(p_->presentedMutex);
        const int64_t pts100 = int64_t(ptsMs * 10000.0);
        for (const auto& meta : p_->presented) if (std::llabs(meta.pts - pts100) < 5) { arrival = meta.arrival; break; }
    }
    audio_.videoPresented(ptsMs, host, arrival);
}

void XboxSessionSource::controller(const remoteplay::ControllerState& state) {
    if (!p_) return;
    auto& p = *p_;
    const xbox::GamepadFrame frame = xbox::gamepadFromController(state);
    std::lock_guard lock(p.inputMutex);
    if (!p.streaming.load() || !p.desc.gamepad) return;
    const auto now = Clock::now();
    if (p.padSent && frame == p.lastPad && now - p.lastPadSent < std::chrono::milliseconds(33)) return;
    p.rtc.sendGamepad(frame);
    p.lastPad = frame;
    p.lastPadSent = now;
    p.padSent = true;
}

remoteplay::ControllerFeedback XboxSessionSource::takeFeedback() {
    remoteplay::ControllerFeedback out;
    if (!p_) return out;
    auto& p = *p_;
    std::lock_guard lock(p.feedbackMutex);
    if (p.feedbackPending) {
        out = p.feedback;
        p.feedbackPending = false;
    } else if (p.rumbling && Clock::now() >= p.rumbleUntil) {
        // The report carries a duration; stop when it runs out (SDL would otherwise keep going for seconds).
        out.rumble = true;
        out.left = out.right = 0;
        p.rumbling = false;
    }
    return out;
}

XboxStats XboxSessionSource::stats() const {
    XboxStats s;
    {
        std::lock_guard lock(mutex_);
        s = stats_;
    }
    if (!p_) return s;
    auto& p = *p_;
    s.units = p.units.load();
    s.decoded = p.decoded.load();
    s.dropped = p.dropped.load() + skipped();
    s.decodeErrors = p.errors.load();
    s.hardwareDecode = p.decoderOpen && p.decoder.hardwareActive();
    const xbox::WebRtcStats r = p.rtc.stats();
    s.keyframeRequests = r.keyframeRequests;
    s.rttMs = r.rttMs;
    std::lock_guard lock(p.statsMutex);
    s.decodeMs = p.decodeMs;
    const auto now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - p.rateStart).count();
    if (seconds >= 1.0) {
        p.receivedFps = double(s.units - p.rateUnits) / seconds;
        p.decodedFps = double(s.decoded - p.rateDecoded) / seconds;
        p.videoMbps = double(r.videoBytes - p.rateBytes) * 8.0 / seconds / 1e6;
        p.rateUnits = s.units;
        p.rateDecoded = s.decoded;
        p.rateBytes = r.videoBytes;
        p.rateStart = now;
    }
    s.receivedFps = p.receivedFps;
    s.decodedFps = p.decodedFps;
    s.videoMbps = p.videoMbps;
    return s;
}

uint64_t XboxSessionSource::unitsReceived() const { return p_ ? p_->units.load() : 0; }

uint64_t XboxSessionSource::skipped() const {
    std::lock_guard lock(mutex_);
    return skipped_;
}

void XboxSessionSource::close() noexcept {
    cancel_ = true;
    if (owner_.joinable()) owner_.join();
    if (p_) {
        auto& p = *p_;
        {
            std::lock_guard lock(p.inputMutex);
            if (p.streaming.exchange(false) && p.padSent) p.rtc.sendGamepad(xbox::GamepadFrame{});   // release everything on the console
        }
        p.rtc.close();
        {
            std::lock_guard lock(p.queueMutex);
            p.stopDecode = true;
        }
        p.queueCv.notify_all();
        if (p.decodeThread.joinable()) p.decodeThread.join();
        if (p.keepaliveThread.joinable()) p.keepaliveThread.join();
        if (p.decoderOpen) { p.decoder.close(); p.decoderOpen = false; }
        // End the console session so the next start does not find it busy (best effort, short timeout).
        // The session's own transport is cancelled by now, and teardown must not wait on the network:
        // a detached request with its own transport and copies of what it needs.
        if (p.api && !p.sessionId.empty()) {
            std::thread([host = p.host, token = p.gsToken, id = p.sessionId] {
                try {
                    xbox::StreamApi(xbox::makeWinHttpTransport(), host, token).stop(id);
                    log::info("xbox", "session " + id + " stopped");
                } catch (const std::exception& e) {
                    log::warn("xbox", std::string("session stop: ") + e.what());
                }
            }).detach();
            p.sessionId.clear();
        }
    }
    audio_.stop();
    std::lock_guard lock(mutex_);
    latest_.reset();
    view_.reset();
    info_ = {};
    started_ = false;
    if (stats_.state == XboxStats::State::Streaming || stats_.state == XboxStats::State::Connecting || stats_.state == XboxStats::State::Starting)
        stats_.state = XboxStats::State::Idle;
}

} // namespace veyra::source
