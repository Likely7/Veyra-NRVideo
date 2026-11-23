// SPDX-License-Identifier: GPL-3.0-only
// Xbox session flow: provisioning through Microsoft's xHome service; the media
// plane is the reference Chromium player inside WebView2. The player exports
// complete Annex-B H.264 access units and Opus packets via RTCRtpScriptTransform
// (probe-verified on WebView2 142), so the decode and audio chains below need
// no bitstream conversion.
#include "veyra/source/XboxWebSource.h"
#include "veyra/xbox/DisconnectPolicy.h"
#include "veyra/xbox/VideoDecodeRecovery.h"
#include "veyra/xbox/WebView2Host.h"

#include <windows.h>
#include <mmreg.h>

#include <opus/opus.h>

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/hwcontext.h>
}

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <format>
#include <vector>

#include "veyra/Log.h"
#include "veyra/pipeline/ColorMetadata.h"
#include "veyra/sink/AudioFormat.h"
#include "veyra/source/CaptureCompressedDecoder.h"
#include "veyra/xbox/Account.h"
#include "veyra/xbox/Protocol.h"
#include "veyra/xbox/StreamApi.h"

namespace veyra::source {
namespace {

using Clock = std::chrono::steady_clock;
using json = nlohmann::json;

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
    if ((frame.format == AV_PIX_FMT_D3D12 || frame.format == AV_PIX_FMT_D3D11) && frame.hw_frames_ctx) {
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

// Console streams are 8-bit H.264 SDR: BT.709 limited range (v1 contract).
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

// True when the access unit carries an IDR slice or a sequence parameter set.
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

// SDL controller state -> the player's InputFrame JSON (the inverse of
// Protocol.h gamepadFromController; the player's Packet.ts serializes it into
// the same xcloud input report as v1). Axes: SDL is down-positive, the
// InputFrame convention is up-positive (Packet.ts negates Y before writing).
json gamepadStateJson(const remoteplay::ControllerState& s) {
    using B = remoteplay::ControllerState;
    const auto button = [&](uint32_t mask) { return (s.inputActive && (s.buttons & mask)) ? 1 : 0; };
    const auto axis = [](int16_t v) {
        if (v == INT16_MIN) v = INT16_MAX;
        if (v < -32767) v = -32767;
        return double(v) / 32767.0;
    };
    const bool active = s.inputActive;
    return json{
        {"GamepadIndex", 0},
        {"A", button(B::Cross)}, {"B", button(B::Circle)}, {"X", button(B::Square)}, {"Y", button(B::Triangle)},
        {"DPadUp", button(B::Up)}, {"DPadDown", button(B::Down)},
        {"DPadLeft", button(B::Left)}, {"DPadRight", button(B::Right)},
        {"LeftShoulder", button(B::L1)}, {"RightShoulder", button(B::R1)},
        {"LeftThumb", button(B::L3)}, {"RightThumb", button(B::R3)},
        {"Menu", button(B::Options)}, {"View", button(B::Share)}, {"Nexus", button(B::PS)},
        {"LeftThumbXAxis", active ? axis(s.leftX) : 0.0},
        {"LeftThumbYAxis", active ? -axis(s.leftY) : 0.0},
        {"RightThumbXAxis", active ? axis(s.rightX) : 0.0},
        {"RightThumbYAxis", active ? -axis(s.rightY) : 0.0},
        {"LeftTrigger", active ? double(s.l2) / 255.0 : 0.0},
        {"RightTrigger", active ? double(s.r2) / 255.0 : 0.0},
    };
}

} // namespace

struct XboxWebSource::Impl {
    XboxConnectDesc desc;
    std::shared_ptr<xbox::HttpsTransport> http;
    std::unique_ptr<xbox::StreamApi> api;
    std::string host, gsToken;
    std::string sessionId;
    xbox::WebView2Host web;
    std::atomic<bool> streaming{false}, ended{false}, connected{false};
    std::wstring endReason;
    std::string disconnectReason;
    std::mutex endMutex;
    bool firstExchange = true;           // guarded by signalTaskMutex
    // Signalling runs off the STA thread: the WebView2 callbacks only enqueue,
    // one worker performs the blocking REST exchanges in arrival order.
    struct SignalTask { bool offer = false; std::string sdp; std::vector<xbox::IceCandidate> ice; };
    std::mutex signalTaskMutex;
    std::condition_variable signalTaskCv;
    std::deque<SignalTask> signalTasks;
    bool stopSignal = false;
    std::thread signalThread;
    std::mutex stateMutex;
    std::condition_variable stateCv;
    std::string lastConnectionState;

    // Video: the WebView2 STA thread queues access units; the decode thread drains.
    struct Unit { std::vector<uint8_t> data; uint32_t rtp; int64_t arrival; bool keyframe = false; bool inputLoss = false; };
    std::mutex queueMutex;
    std::condition_variable queueCv;
    std::deque<Unit> queue;
    bool stopDecode = false;
    std::thread decodeThread, keepaliveThread;
    CaptureCompressedDecoder decoder;
    bool decoderOpen = false;
    std::atomic<bool> hardwareDecodeStatus{false};
    std::atomic<bool> softwareDecodeActive{false};
    AVFrame* dummyTarget = nullptr;
    unsigned width = 1920, height = 1080;
    int keepaliveSeconds = 30;
    Clock::time_point lastKeyframeRequest{};
    std::mutex keyframeMutex;
    std::atomic<uint64_t> keyframeRequests{0};

    struct Meta { int64_t pts, arrival; int64_t decoded = 0; };
    std::deque<Meta> presented;
    std::mutex presentedMutex;

    // Audio
    OpusDecoder* opus = nullptr;
    std::vector<float> pcm;
    uint64_t audioSamples = 0;
    int64_t audioRtpTicks = 0;           // Opus RTP clock (48 kHz), unwrapped
    uint32_t lastAudioRtp = 0;
    bool haveAudioRtp = false;
    bool audioDiscontinuity = true;
    bool audioReady = false;

    // Input and feedback
    std::mutex inputMutex;
    std::string lastPadJson;
    Clock::time_point lastPadSent{};
    bool padSent = false;
    std::mutex feedbackMutex;
    remoteplay::ControllerFeedback feedback;
    bool feedbackPending = false;
    Clock::time_point rumbleUntil{};
    bool rumbling = false;

    std::atomic<uint64_t> units{0}, decoded{0}, dropped{0}, errors{0}, videoBytes{0};
    std::mutex statsMutex;
    double decodeMs = 0;
    mutable Clock::time_point rateStart = Clock::now();
    mutable uint64_t rateUnits = 0, rateDecoded = 0, rateBytes = 0;
    mutable double receivedFps = 0, decodedFps = 0, videoMbps = 0;

    void end(const std::string& reason, std::wstring text) {
        std::lock_guard lock(endMutex);
        if (ended.load()) return;        // preserve the first, specific reason
        disconnectReason = reason;
        endReason = std::move(text);
        ended = true;
    }

    ~Impl() {
        if (dummyTarget) av_frame_free(&dummyTarget);
        if (opus) opus_decoder_destroy(opus);
    }
};

XboxWebSource::XboxWebSource() : p_(std::make_unique<Impl>()) {}

XboxWebSource::~XboxWebSource() { close(); }

void XboxWebSource::setState(XboxStats::State state, std::wstring message) {
    std::lock_guard lock(mutex_);
    stats_.state = state;
    stats_.message = std::move(message);
}

void XboxWebSource::requestKeyframe() {
    if (!p_) return;
    auto& p = *p_;
    {
        std::lock_guard lock(p.keyframeMutex);
        const auto now = Clock::now();
        if (now - p.lastKeyframeRequest < std::chrono::milliseconds(500)) return;
        p.lastKeyframeRequest = now;
    }
    ++p.keyframeRequests;
    // The player's Control channel also pulses every 5 s on its own; this is
    // the loss/decode-recovery request on top of that cadence.
    p.web.sendKeyframeRequest();
}

bool XboxWebSource::connect(XboxConnectDesc desc) {
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

void XboxWebSource::run(XboxConnectDesc desc) {
    auto& p = *p_;
    p.desc = std::move(desc);
    const auto fail = [&](std::wstring message) {
        log::error("xbox-web", "connect failed");
        std::lock_guard lock(mutex_);
        stats_.state = XboxStats::State::Failed;
        stats_.message = std::move(message);
        initialized_ = true;
        started_ = false;
        ready_.notify_all();
    };
    const auto cancelled = [&] { return cancel_.load() || (p.desc.stopRequested && p.desc.stopRequested->load()); };

    try {
        if (!p.desc.account || !p.desc.account->signedIn()) { fail(L"请先登录 Xbox 账号。"); return; }
        setState(XboxStats::State::Starting, L"正在登录 Xbox 串流服务…");
        const xbox::StreamingAccess access = p.desc.account->streamingAccess();
        p.http = xbox::makeWinHttpTransport(&cancel_);
        p.api = std::make_unique<xbox::StreamApi>(p.http, access.host, access.gsToken);
        p.api->setProfile(p.desc.profile);
        p.api->setTokenRefresh([&account = p.desc.account] { return account->refreshStreamingAccess().gsToken; });
        p.host = access.host;
        p.gsToken = access.gsToken;
        log::info("xbox-web", "streaming region " + access.regionName + " host " + access.host);

        setState(XboxStats::State::Starting, L"正在唤醒主机并准备串流…");
        p.sessionId = p.api->play(p.desc.serverId, p.desc.locale, p.desc.profile);
        {
            std::lock_guard lock(mutex_);
            stats_.qualityTier = int(p.desc.profile.tier);
        }
        p.softwareDecodeActive.store(p.desc.softwareDecode);
        log::info("xbox-web", std::format("profile tier={} bitrateKbps={} fullTeredo={} softwareDecode={} decodeBackend={}",
            int(p.desc.profile.tier), p.desc.profile.videoBitrateKbps,
            p.desc.profile.fullTeredo, p.desc.softwareDecode, int(p.desc.profile.decodeBackend)));
        log::info("xbox-web", "session " + p.sessionId);
        bool connectSent = false;
        const auto deadline = Clock::now() + std::chrono::seconds(120);
        for (;;) {
            if (cancelled()) { fail(L"已取消。"); return; }
            if (Clock::now() > deadline) { fail(L"主机长时间没有准备好串流。请确认主机开着并且网络正常。"); return; }
            const xbox::SessionState state = p.api->state(p.sessionId);
            log::info("xbox-web", "session state " + state.state);
            if (state.state == "Provisioned") break;
            if (state.state == "Failed") {
                log::error("xbox-web", "session failed code=" + (state.errorCode.empty() ? std::string("?") : state.errorCode) +
                                   " message=" + (state.errorMessage.empty() ? std::string("?") : state.errorMessage));
                fail(describeSessionFailure(state.errorCode, state.errorMessage));
                return;
            }
            if (state.state == "ReadyToConnect" && !connectSent) {
                p.api->connect(p.sessionId, p.desc.account->transferToken());
                connectSent = true;
                continue;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        setState(XboxStats::State::Connecting, L"正在建立连接…");

        // Audio output: stereo float at 48 kHz (v1 plumbing).
        int opusError = 0;
        p.opus = opus_decoder_create(48000, 2, &opusError);
        if (p.opus && opusError == OPUS_OK) {
            p.pcm.assign(5760 * 2, 0.f);
            sink::AudioFormat layout;
            layout.channels = 2;
            layout.mask = SPEAKER_FRONT_LEFT | SPEAKER_FRONT_RIGHT;
            const WAVEFORMATEXTENSIBLE wave = sink::floatWave(layout);
            p.audioReady = audio_.configure(wave.Format, sizeof(wave)) && audio_.start();
            if (!p.audioReady) log::warn("xbox-web-audio", "the audio output could not be started; the stream continues without sound");
        } else {
            log::error("xbox-web-audio", std::format("Opus decoder creation failed code={}", opusError));
        }

        // The WebView2 media callbacks. Everything here runs on the host's STA
        // thread: queue and return, never block on the network or the decoder.
        xbox::WebView2HostCallbacks callbacks;
        callbacks.videoFrame = [this](std::vector<uint8_t>&& unit, uint32_t rtp, int64_t /*timestampUs*/,
                                      bool keyframe, int64_t arrival) {
            auto& q = *p_;
            ++q.units;
            q.videoBytes += unit.size();
            std::lock_guard lock(q.queueMutex);
            // Latency first (v1): a decoder that falls behind drops the oldest.
            const auto lost = xbox::trimVideoInputs(q.queue, 8);
            q.dropped += lost;
            q.queue.push_back({std::move(unit), rtp, arrival, keyframe, lost > 0 && q.queue.empty()});
            q.queueCv.notify_one();
        };
        callbacks.audioFrame = [this](std::vector<uint8_t>&& packet, uint32_t rtp, int64_t /*timestampUs*/, int64_t /*arrival*/) {
            auto& q = *p_;
            if (!q.audioReady || !q.opus) return;
            // Time comes from RTP, not from counting decoded samples: a loss
            // leaves a gap instead of pulling audio ahead (v1 contract).
            if (q.haveAudioRtp) {
                const auto advance = int32_t(rtp - q.lastAudioRtp);
                if (advance <= 0) return;
                q.audioRtpTicks += advance;
            }
            q.lastAudioRtp = rtp;
            q.haveAudioRtp = true;
            const int frames = opus_decode_float(q.opus, packet.data(), opus_int32(packet.size()), q.pcm.data(), 5760, 0);
            if (frames <= 0) { q.audioDiscontinuity = true; return; }
            const double ptsMs = double(q.audioRtpTicks) / 48.0;
            const bool accepted = audio_.push(q.pcm.data(), size_t(frames) * 2 * sizeof(float), ptsMs, q.audioDiscontinuity);
            if (!q.audioSamples) log::info("xbox-web-audio", std::format("first Opus block samples={} accepted={}", frames, accepted));
            q.audioDiscontinuity = false;
            q.audioSamples += uint64_t(frames);
        };
        callbacks.offer = [this](std::string sdp) {
            auto& q = *p_;
            std::lock_guard lock(q.signalTaskMutex);
            q.signalTasks.push_back({true, std::move(sdp), {}});
            q.signalTaskCv.notify_one();
        };
        callbacks.ice = [this](std::vector<xbox::IceCandidate> local) {
            auto& q = *p_;
            std::lock_guard lock(q.signalTaskMutex);
            q.signalTasks.push_back({false, {}, std::move(local)});
            q.signalTaskCv.notify_one();
        };
        callbacks.state = [this](std::string state) {
            auto& q = *p_;
            log::info("xbox-web", "connection state " + state);
            {
                std::lock_guard lock(q.stateMutex);
                q.lastConnectionState = state;
            }
            q.stateCv.notify_all();
            if (state == "connected") q.connected = true;
            else if (state == "failed" || state == "closed") {
                q.connected = false;
                q.end("connection lost", L"与主机的连接中断了。");
            }
        };
        callbacks.rumble = [this](const xbox::WebRumble& r) {
            auto& q = *p_;
            const auto level = [](double v) { return uint8_t(std::clamp(v, 0.0, 1.0) * 255.0); };
            std::lock_guard lock(q.feedbackMutex);
            q.feedback.rumble = true;
            q.feedback.left = level(r.high);     // high = strong (left) motor
            q.feedback.right = level(r.low);     // low  = weak (right) motor
            q.feedbackPending = true;
            q.rumbling = q.feedback.left || q.feedback.right;
            q.rumbleUntil = Clock::now() + std::chrono::milliseconds(r.durationMs > 0 ? r.durationMs : 1000);
        };
        callbacks.failed = [this](std::string reason) {
            p_->end("webview failed", L"WebView2 运行出错（" + widen(reason) + L"）。请确认已安装 WebView2 运行时。");
        };

        // The signalling worker: blocking REST exchanges run here, in arrival
        // order, never on the WebView2 STA thread.
        p.signalThread = std::thread([this] {
            auto& q = *p_;
            for (;;) {
                Impl::SignalTask task;
                {
                    std::unique_lock lock(q.signalTaskMutex);
                    q.signalTaskCv.wait(lock, [&] { return q.stopSignal || !q.signalTasks.empty(); });
                    if (q.stopSignal) return;
                    task = std::move(q.signalTasks.front());
                    q.signalTasks.pop_front();
                }
                if (task.offer) {
                    try {
                        const std::string answer = q.api->exchangeSdp(q.sessionId, task.sdp);
                        q.web.sendAnswer(answer);
                    } catch (const std::exception& e) {
                        log::error("xbox-web", std::string("SDP exchange: ") + e.what());
                        bool first;
                        { std::lock_guard lock(q.signalTaskMutex); first = q.firstExchange; }
                        // A chat renegotiation failure must not tear down a running stream.
                        if (first) q.end("sdp exchange failed", L"Xbox 连接失败：SDP 交换出错（" + widen(e.what()) + L"）。");
                    }
                } else {
                    try {
                        const std::vector<xbox::IceCandidate> remote = q.api->exchangeIce(q.sessionId, task.ice);
                        { std::lock_guard lock(q.signalTaskMutex); q.firstExchange = false; }
                        q.web.sendRemoteIce(remote);
                    } catch (const std::exception& e) {
                        log::error("xbox-web", std::string("ICE exchange: ") + e.what());
                        q.end("ice exchange failed", L"Xbox 连接失败：ICE 交换出错（" + widen(e.what()) + L"）。");
                    }
                }
            }
        });

        if (!p.web.start(nullptr, std::move(callbacks), std::chrono::seconds(30))) {
            const std::string why = p.web.startFailureReason();
            fail(L"WebView2 启动失败" + (why.empty()
                ? std::wstring(L"（超时）。请确认已安装 Microsoft Edge WebView2 运行时。")
                : L"：" + widen(why)));
            return;
        }
        if (cancelled()) { fail(L"已取消。"); return; }

        // Player config: force H264 (the decode chain is FFmpeg H264), carry
        // the profile's bitrate cap and the first TURN server, if configured.
        json config{
            {"codec", "video/H264"},
            {"codecProfiles", json::array({"4d", "42e", "420"})},
            {"pollingRate", 250},
            {"vibration", true},
            {"deadzone", 0.0},   // SDL already applies the user's deadzone
        };
        if (p.desc.profile.videoBitrateKbps > 0) config["videoBitrate"] = p.desc.profile.videoBitrateKbps;
        if (p.desc.profile.audioBitrateKbps > 0) config["audioBitrate"] = p.desc.profile.audioBitrateKbps;
        for (const auto& server : p.desc.profile.iceServers) {
            if (server.url.rfind("turn:", 0) == 0 || server.url.rfind("turns:", 0) == 0) {
                config["turnServer"] = {{"url", server.url}, {"username", server.username}, {"credential", server.credential}};
                break;
            }
        }
        p.web.startSession(config.dump());

        // Wait for the WebRTC connection (offer/ICE exchange runs in the callbacks).
        {
            std::unique_lock lock(p.stateMutex);
            const bool up = p.stateCv.wait_for(lock, std::chrono::seconds(60),
                [&] { return p.connected.load() || p.ended.load() || cancelled(); });
            if (!up || (!p.connected.load() && !cancelled())) {
                fail(L"主机连上了，但没有完成握手。请重试。");
                return;
            }
        }
        if (cancelled()) { fail(L"已取消。"); return; }
        if (p.ended.load()) {
            std::lock_guard lock(p.endMutex);
            fail(p.endReason.empty() ? L"Xbox 连接失败。" : p.endReason);
            return;
        }

        // The service's advisory pulse; keep the historical 30 s unless faster.
        try {
            const int pulse = p.api->keepAlivePulseInSeconds(p.sessionId);
            if (pulse > 0) p.keepaliveSeconds = std::min(30, std::max(1, pulse / 2));
        } catch (const std::exception& e) {
            log::warn("xbox-web", std::string("configuration: ") + e.what());
        }
        {
            int vw = 1920, vh = 1080;
            xbox::viewportForTier(p.desc.profile.tier, &vw, &vh);
            p.width = unsigned(vw);
            p.height = unsigned(vh);
        }
        p.stopDecode = false;
        p.decodeThread = std::thread([this] { decodeLoop(); });
        p.keepaliveThread = std::thread([this] {
            auto& q = *p_;
            auto next = Clock::now() + std::chrono::seconds(q.keepaliveSeconds);
            while (!cancel_.load() && !q.ended.load()) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                if (Clock::now() < next) continue;
                next = Clock::now() + std::chrono::seconds(q.keepaliveSeconds);
                try {
                    q.api->keepalive(q.sessionId);
                } catch (const xbox::ServiceError& e) {
                    log::warn("xbox-web", std::format("keepalive HTTP {}", e.status));
                    if (e.status == 404) q.end("session expired", L"串流会话已在服务器端结束。");
                } catch (const std::exception& e) {
                    log::warn("xbox-web", std::string("keepalive: ") + e.what());
                }
            }
        });
        // The player's Control channel requests a keyframe every 5 s itself;
        // no C++ cadence thread is needed here (requestKeyframe covers recovery).
        {
            std::lock_guard lock(p.inputMutex);
            p.streaming = true;
        }
        {
            std::lock_guard lock(mutex_);
            if (stats_.state != XboxStats::State::Failed) {
                stats_.state = XboxStats::State::Streaming;
                stats_.message.clear();
            }
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
        if (!p.ended.load()) log::info("xbox-web", "streaming");
        while (!cancelled() && !p.ended.load()) std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (p.ended.load()) {
            std::wstring reason;
            std::string code;
            { std::lock_guard lock(p.endMutex); reason = p.endReason; code = p.disconnectReason; }
            std::lock_guard lock(mutex_);
            if (stats_.state != XboxStats::State::Failed) {
                stats_.state = XboxStats::State::Ended;
                stats_.message = std::move(reason);
                stats_.disconnectReason = code;
                stats_.reconnectable = xbox::reconnectableDisconnect(code);
            }
        }
    } catch (const xbox::ServiceError& e) {
        log::error("xbox-web", std::format("service error {} {}", e.status, e.what()));
        std::wstring message = xbox::describeXboxError(e.body);
        if (message.empty()) {
            if (e.status == 401 || e.status == 403 || std::string(e.what()) == "sign-in expired") message = L"Xbox 登录已失效，请重新登录。";
            else message = std::format(L"Xbox 服务返回错误（HTTP {}）。", e.status);
        }
        if (!initialized_) fail(std::move(message));
        else setState(XboxStats::State::Failed, std::move(message));
    } catch (const xbox::NetworkError& e) {
        log::error("xbox-web", std::string("network: ") + e.what());
        std::wstring message = cancel_ ? L"已取消。" : L"连不上 Xbox 服务，请检查网络。（" + widen(e.what()) + L"）";
        if (!initialized_) fail(std::move(message));
        else setState(XboxStats::State::Failed, std::move(message));
    } catch (const std::exception& e) {
        log::error("xbox-web", std::string("session failed: ") + e.what());
        if (!initialized_) fail(L"Xbox 串流出错：" + widen(e.what()));
        else setState(XboxStats::State::Failed, L"Xbox 串流出错：" + widen(e.what()));
    }
}

void XboxWebSource::decodeLoop() {
    auto& p = *p_;
    bool seenKeyframe = false, recovering = false, forceSoftware = p.desc.softwareDecode;
    p.softwareDecodeActive.store(forceSoftware);
    xbox::VideoDecodeRecovery recovery;
    int64_t lastPts = -1;
    uint32_t lastRtp = 0;
    int64_t rtpHigh = 0;
    bool haveRtp = false;
    std::deque<Impl::Meta> metas;
    log::info("xbox-web", "decode thread started");
    for (;;) {
        Impl::Unit unit;
        {
            std::unique_lock lock(p.queueMutex);
            p.queueCv.wait(lock, [&] { return p.stopDecode || !p.queue.empty(); });
            if (p.stopDecode) break;
            unit = std::move(p.queue.front());
            p.queue.pop_front();
        }
        if (unit.inputLoss) {
            // A missing AU invalidates H.264's reference chain; an IDR request
            // alone does not repair the decode context (v1 recovery contract).
            if (p.decoderOpen) p.decoder.recoverAtKeyframe();
            seenKeyframe = false;
            recovering = true;
            metas.clear();
            requestKeyframe();
            log::warn("xbox-web-decode-recovery", "input queue lost access units; history reset, waiting for keyframe");
        }
        // The transform marks keyframes (type key); the NAL scan is the
        // belt-and-braces check, identical to v1.
        if (!seenKeyframe && !unit.keyframe && !startsDecoding(unit.data)) { ++p.dropped; requestKeyframe(); continue; }
        if (!p.decoderOpen) {
            p.decoder.setStreamProfile(true);
            p.decoder.setPreferD3D11VA(p.desc.profile.decodeBackend == xbox::DecodeBackend::D3D11VA);
            if (!p.decoder.open(CaptureCodec::H264, p.width, p.height, nullptr, 0,
                                forceSoftware ? nullptr : p.desc.decodeDevice.get(),
                                forceSoftware ? nullptr : p.desc.decodeQueue.get())) {
                log::error("xbox-web", "no usable H.264 decoder");
                setState(XboxStats::State::Failed, L"没有可用的 H.264 解码器。");
                p.ended = true;
                break;
            }
            p.decoderOpen = true;
            p.hardwareDecodeStatus.store(p.decoder.hardwareActive(), std::memory_order_relaxed);
            p.softwareDecodeActive.store(!p.decoder.hardwareActive(), std::memory_order_relaxed);
            if (!p.dummyTarget) p.dummyTarget = allocNv12(16, 16);
            log::info("xbox-web", std::string("decoder ") + p.decoder.backendName());
        }
        // 90 kHz RTP timestamps, unwrapped, to 100 ns ticks (v1 media clock).
        if (haveRtp && unit.rtp < lastRtp && lastRtp - unit.rtp > 0x80000000u) rtpHigh += int64_t(1) << 32;
        lastRtp = unit.rtp;
        haveRtp = true;
        int64_t pts = (rtpHigh + int64_t(unit.rtp)) * 1000 / 9;
        if (pts <= lastPts) pts = lastPts + 166667;
        lastPts = pts;
        metas.push_back({pts, unit.arrival, 0});
        if (metas.size() > 64) metas.pop_front();

        const auto begin = Clock::now();
        const bool needsTarget = p.decoder.framesDecoded() == 0 || !p.decoder.hardwareActive();
        AVFrame* target = needsTarget ? allocNv12(p.width, p.height) : p.dummyTarget;
        AVFrame* out = nullptr;
        bool hardware = false;
        const bool produced = target && p.decoder.decode(unit.data.data(), unit.data.size(), pts, target, &out, hardware);
        p.hardwareDecodeStatus.store(p.decoder.hardwareActive(), std::memory_order_relaxed);
        p.softwareDecodeActive.store(!p.decoder.hardwareActive(), std::memory_order_relaxed);
        const double decodeMs = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        if (!produced) {
            if (needsTarget && target) av_frame_free(&target);
            if (!p.decoder.waitingForInput()) {
                ++p.errors;
                seenKeyframe = false;
                recovering = true;
                metas.clear();
                const std::string error = p.decoder.lastError();
                const bool wasHardware = p.decoder.hardwareActive();
                const auto action = recovery.failed(wasHardware);
                const char* name = action == xbox::VideoDecodeRecoveryAction::ResetHistory ? "reset-history" :
                    action == xbox::VideoDecodeRecoveryAction::ReopenHardware ? "reopen-hardware" :
                    action == xbox::VideoDecodeRecoveryAction::UseSoftware ? "use-software" : "stop";
                log::warn("xbox-web-decode-recovery", std::format("action={} backend={} errors={} detail={}",
                    name, p.decoder.backendName(), p.errors.load(), error));
                if (action == xbox::VideoDecodeRecoveryAction::Stop) {
                    setState(XboxStats::State::Failed, L"Xbox 视频解码连续失败，恢复后仍无法出画。请重连，并保留日志。");
                    p.ended = true;
                    break;
                }
                if (action == xbox::VideoDecodeRecoveryAction::ResetHistory) p.decoder.recoverAtKeyframe();
                else {
                    p.hardwareDecodeStatus.store(false, std::memory_order_relaxed);
                    p.decoder.close();
                    p.decoderOpen = false;
                    if (action == xbox::VideoDecodeRecoveryAction::UseSoftware) { forceSoftware = true; p.softwareDecodeActive.store(true); }
                }
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
        recovery.picture();
        seenKeyframe = true;
        if (unsigned(frame->width) != p.width || unsigned(frame->height) != p.height) {
            p.width = unsigned(frame->width);
            p.height = unsigned(frame->height);
        }

        Impl::Meta frameMeta{pts, unit.arrival, host100ns()};
        int64_t arrival = unit.arrival;
        const int64_t framePts = frame->pts != AV_NOPTS_VALUE ? frame->pts : pts;
        for (const auto& meta : metas) if (meta.pts == framePts) { frameMeta = meta; arrival = meta.arrival; break; }
        frameMeta.pts = framePts;
        frameMeta.decoded = host100ns();

        pipeline::FramePacket packet{};
        packet.sourceKind = pipeline::SourceKind::Xbox;
        packet.pts = pipeline::Rational{framePts, 10000000};
        packet.duration = pipeline::Rational{166667, 10000000};
        packet.arrivalHost100ns = arrival;
        packet.decodedHost100ns = host100ns();
        if (recovering) {
            packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Discontinuity);
            log::info("xbox-web-decode-recovery", std::format("picture restored backend={} pts100ns={} errors={}",
                p.decoder.backendName(), framePts, p.errors.load()));
            recovering = false;
        }
        auto fallback = fallbackColor();
        packet.colorInfo = pipeline::resolveFrameColor(*frame, fallback);
        packet.colorInfo.pixelFormat = pixelFormatOf(*frame);
        if (packet.colorInfo.pixelFormat == pipeline::SourcePixelFormat::Unknown) { ++p.errors; continue; }
        if (hardware && frame->format == AV_PIX_FMT_D3D11 && !p.decoder.hardwareSurface(packet.hardwareSurface)) {
            ++p.errors;
            continue;
        }

        SourceInfo info;
        {
            std::lock_guard lock(mutex_);
            info = info_;
            stats_.width = unsigned(frame->width);
            stats_.height = unsigned(frame->height);
        }
        info.color = packet.colorInfo;
        info.hardwareDecodeActive = frame->format == AV_PIX_FMT_D3D12 || frame->format == AV_PIX_FMT_D3D11;
        info.videoDecodePath = p.decoder.backendName();
        if (info.width != unsigned(frame->width) || info.height != unsigned(frame->height)) {
            info.width = unsigned(frame->width);
            info.height = unsigned(frame->height);
            info.displayAspect = frame->height > 0 ? double(frame->width) / double(frame->height) : 0.0;
            packet.flags |= static_cast<pipeline::FrameFlags>(pipeline::FrameFlagBits::Resize);
        }
        {
            std::lock_guard lock(p.presentedMutex);
            p.presented.push_back(frameMeta);
            if (p.presented.size() > 64) p.presented.pop_front();
        }
        {
            std::lock_guard lock(p.statsMutex);
            p.decodeMs = movingAverage(p.decodeMs, decodeMs);
        }
        ++p.decoded;
        publish(std::move(frame), packet, info);
    }
    log::info("xbox-web", "decode thread stopped");
}

void XboxWebSource::publish(std::shared_ptr<AVFrame> frame, const pipeline::FramePacket& packet, const SourceInfo& info) {
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

SourceReadStatus XboxWebSource::read(pipeline::FramePacket& packet, const AVFrame** frame) {
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

void XboxWebSource::videoPresented(double ptsMs, int64_t host, bool /*sourceFrame*/) {
    // Chromium's receiver pipeline has no per-frame feedback report channel
    // exposed to the player fork; presentation timing drives the audio clock
    // only. (v1 sends the five-stage input report over its own datachannel.)
    std::optional<int64_t> arrival;
    if (p_) {
        std::lock_guard lock(p_->presentedMutex);
        const int64_t pts100 = int64_t(ptsMs * 10000.0);
        for (const auto& meta : p_->presented) if (std::llabs(meta.pts - pts100) < 5) { arrival = meta.arrival; break; }
    }
    audio_.videoPresented(ptsMs, host, arrival);
}

void XboxWebSource::controller(const remoteplay::ControllerState& state) {
    if (!p_) return;
    auto& p = *p_;
    const std::string message = gamepadStateJson(state).dump();
    std::lock_guard lock(p.inputMutex);
    if (!p.streaming.load() || !p.desc.gamepad) return;
    const auto now = Clock::now();
    if (p.padSent && message == p.lastPadJson && now - p.lastPadSent < std::chrono::milliseconds(33)) return;
    p.web.sendGamepad(message);
    p.lastPadJson = message;
    p.lastPadSent = now;
    p.padSent = true;
}

remoteplay::ControllerFeedback XboxWebSource::takeFeedback() {
    remoteplay::ControllerFeedback out;
    if (!p_) return out;
    auto& p = *p_;
    std::lock_guard lock(p.feedbackMutex);
    if (p.feedbackPending) {
        out = p.feedback;
        p.feedbackPending = false;
    } else if (p.rumbling && Clock::now() >= p.rumbleUntil) {
        // The report carries a duration; stop when it runs out (v1 cadence).
        out.rumble = true;
        out.left = out.right = 0;
        p.rumbling = false;
    }
    return out;
}

XboxStats XboxWebSource::stats() const {
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
    s.hardwareDecode = p.hardwareDecodeStatus.load(std::memory_order_relaxed);
    s.softwareDecodeActive = p.softwareDecodeActive.load(std::memory_order_relaxed);
    s.keyframeRequests = p.keyframeRequests.load();
    // Chromium owns NACK/reorder inside its receiver; those counters are not
    // exposed through the encoded transform. Zero, never fabricated.
    std::lock_guard lock(p.statsMutex);
    s.decodeMs = p.decodeMs;
    const auto now = Clock::now();
    const double seconds = std::chrono::duration<double>(now - p.rateStart).count();
    if (seconds >= 1.0) {
        p.receivedFps = double(s.units - p.rateUnits) / seconds;
        p.decodedFps = double(s.decoded - p.rateDecoded) / seconds;
        const uint64_t bytes = p.videoBytes.load();
        p.videoMbps = double(bytes - p.rateBytes) * 8.0 / seconds / 1e6;
        p.rateUnits = s.units;
        p.rateDecoded = s.decoded;
        p.rateBytes = bytes;
        p.rateStart = now;
    }
    s.receivedFps = p.receivedFps;
    s.decodedFps = p.decodedFps;
    s.videoMbps = p.videoMbps;
    return s;
}

uint64_t XboxWebSource::unitsReceived() const { return p_ ? p_->units.load() : 0; }

uint64_t XboxWebSource::skipped() const {
    std::lock_guard lock(mutex_);
    return skipped_;
}

void XboxWebSource::close() noexcept {
    cancel_ = true;
    if (owner_.joinable()) owner_.join();
    if (p_) {
        auto& p = *p_;
        {
            std::lock_guard lock(p.inputMutex);
            if (p.streaming.exchange(false) && p.padSent)
                p.web.sendGamepad(gamepadStateJson(remoteplay::ControllerState{}).dump());   // release everything
        }
        {
            std::lock_guard lock(p.queueMutex);
            p.stopDecode = true;
        }
        p.queueCv.notify_all();
        p.stateCv.notify_all();
        {
            std::lock_guard lock(p.signalTaskMutex);
            p.stopSignal = true;
        }
        p.signalTaskCv.notify_all();
        if (p.decodeThread.joinable()) p.decodeThread.join();
        if (p.keepaliveThread.joinable()) p.keepaliveThread.join();
        if (p.signalThread.joinable()) p.signalThread.join();
        p.web.stop();
        p.hardwareDecodeStatus.store(false, std::memory_order_relaxed);
        if (p.decoderOpen) { p.decoder.close(); p.decoderOpen = false; }
        // End the console session off-thread (v1 teardown contract).
        if (p.api && !p.sessionId.empty()) {
            std::thread([host = p.host, token = p.gsToken, id = p.sessionId] {
                try {
                    xbox::StreamApi(xbox::makeWinHttpTransport(), host, token).stop(id);
                    log::info("xbox-web", "session " + id + " stopped");
                } catch (const std::exception& e) {
                    log::warn("xbox-web", std::string("session stop: ") + e.what());
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