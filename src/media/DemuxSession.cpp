#include "veyra/media/DemuxSession.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <format>
#include <limits>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/dict.h>
#include <libavutil/error.h>
#include <libavutil/mathematics.h>
}

#include "veyra/Log.h"

namespace veyra::media {
namespace {
using Clock = std::chrono::steady_clock;

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now().time_since_epoch()).count();
}
std::string utf8(std::wstring_view text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(std::max(size, 0)), '\0');
    if (size > 0) WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}
std::string errorText(int code) {
    char text[AV_ERROR_MAX_STRING_SIZE]{};
    av_strerror(code, text, sizeof(text));
    return text;
}
double envNumber(const wchar_t* name, double fallback) {
    wchar_t text[48]{};
    const DWORD n = GetEnvironmentVariableW(name, text, 48);
    if (n == 0 || n >= 48) return fallback;
    wchar_t* end = nullptr;
    const double value = std::wcstod(text, &end);
    return end != text ? value : fallback;
}
int64_t toUs(int64_t ts, AVRational base) {
    return ts == AV_NOPTS_VALUE ? AV_NOPTS_VALUE : av_rescale_q(ts, base, AV_TIME_BASE_Q);
}
constexpr int64_t kStreamEnded = INT64_MIN;
}  // namespace

bool remoteMediaPath(std::wstring_view path) {
    if (path.size() >= 2 && path[0] == L'\\' && path[1] == L'\\') {
        // \\?\C:\... and \\.\... are local; \\?\UNC\server\share is a server.
        if (path.size() >= 4 && (path[2] == L'?' || path[2] == L'.') && path[3] == L'\\') {
            const auto rest = path.substr(4);
            return rest.size() >= 4 && _wcsnicmp(rest.data(), L"UNC\\", 4) == 0;
        }
        return true;
    }
    if (path.size() >= 3 && std::iswalpha(path[0]) && path[1] == L':' && (path[2] == L'\\' || path[2] == L'/')) {
        const wchar_t root[4] = {path[0], L':', L'\\', 0};
        return GetDriveTypeW(root) == DRIVE_REMOTE;
    }
    return false;
}

bool forceDemuxSessionForTest() {
    static const bool forced = GetEnvironmentVariableW(L"VEYRA_TEST_FORCE_DEMUX_SESSION", nullptr, 0) > 0;
    return forced;
}

void CodecParametersDeleter::operator()(AVCodecParameters* parameters) const { avcodec_parameters_free(&parameters); }

// ---------------------------------------------------------------------------
// Interrupt: every blocking FFmpeg call asks this. The owner's stop flag, the
// session's own shutdown, a pending command (seek / forced reconnect) and the
// per-operation deadline all abort it at once.
// ---------------------------------------------------------------------------
struct DemuxSession::Interrupt {
    const std::atomic<bool>* owner = nullptr;
    std::atomic<bool> stop{false}, command{false};
    std::atomic<int64_t> deadline{0};
    bool cancelled() const { return (owner && owner->load()) || stop.load(); }
    bool should() const {
        if (cancelled() || command.load()) return true;
        const auto limit = deadline.load();
        return limit && nowNs() >= limit;
    }
    bool timedOut() const {
        const auto limit = deadline.load();
        return limit && nowNs() >= limit;
    }
    static int callback(void* opaque) { return static_cast<Interrupt*>(opaque)->should() ? 1 : 0; }
    void arm(int ms) { deadline.store(nowNs() + int64_t(ms) * 1000000ll); }
    void disarm() { deadline.store(0); }
    // Sleeps up to ms, returning false as soon as a stop / command arrives.
    bool sleep(int ms) const {
        for (int waited = 0; waited < ms; waited += 20) {
            if (cancelled() || command.load()) return false;
            Sleep(DWORD(std::min(20, ms - waited)));
        }
        return !(cancelled() || command.load());
    }
};

// ---------------------------------------------------------------------------
// FileAvio: reads a file (local test, UNC, mapped drive) through our own IO so
// the transfer is counted, cancellable and, for tests, throttled
// (VEYRA_TEST_SLOW_IO=<MB/s>[,<latency ms per read>] stands in for a slow NAS).
// ---------------------------------------------------------------------------
struct DemuxSession::FileAvio {
    static constexpr int kBuffer = 1 << 20;
    HANDLE handle = INVALID_HANDLE_VALUE;
    AVIOContext* avio = nullptr;
    const Interrupt* irq = nullptr;
    int64_t position = 0, size = -1;
    double mbps = 0;
    int latencyMs = 0;
    int64_t throttleStartNs = 0;
    double throttleBytes = 0;

    ~FileAvio() {
        if (avio) {
            av_freep(&avio->buffer);
            avio_context_free(&avio);
        }
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
    bool open(const std::wstring& path, const Interrupt* interrupt) {
        irq = interrupt;
        handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
        if (handle == INVALID_HANDLE_VALUE) return false;
        LARGE_INTEGER length{};
        if (GetFileSizeEx(handle, &length)) size = length.QuadPart;
        wchar_t text[64]{};
        if (const DWORD n = GetEnvironmentVariableW(L"VEYRA_TEST_SLOW_IO", text, 64); n > 0 && n < 64) {
            wchar_t* end = nullptr;
            mbps = std::wcstod(text, &end);
            if (end && *end == L',') latencyMs = std::max(0, _wtoi(end + 1));
            if (mbps > 0 || latencyMs > 0)
                log::warn("net-session", std::format("test slow IO active: {:.2f} MB/s, {} ms per read (VEYRA_TEST_SLOW_IO)", mbps, latencyMs));
        }
        auto* buffer = static_cast<uint8_t*>(av_malloc(kBuffer));
        if (!buffer) return false;
        avio = avio_alloc_context(buffer, kBuffer, 0, this, &FileAvio::readPacket, nullptr, &FileAvio::seek);
        if (!avio) {
            av_free(buffer);
            return false;
        }
        throttleStartNs = nowNs();
        return true;
    }
    bool wait(int ms) const {
        for (int waited = 0; waited < ms; waited += 10) {
            if (irq && irq->should()) return false;
            Sleep(DWORD(std::min(10, ms - waited)));
        }
        return !(irq && irq->should());
    }
    static int readPacket(void* opaque, uint8_t* buffer, int size) {
        auto& self = *static_cast<FileAvio*>(opaque);
        if (self.irq && self.irq->should()) return AVERROR_EXIT;
        if (self.latencyMs > 0 && !self.wait(self.latencyMs)) return AVERROR_EXIT;
        DWORD got = 0;
        if (!ReadFile(self.handle, buffer, DWORD(std::min(size, kBuffer)), &got, nullptr)) return AVERROR(EIO);
        if (got == 0) return AVERROR_EOF;
        self.position += got;
        if (self.mbps > 0) {
            // Token bucket: the bytes delivered so far may not run ahead of the allowed rate.
            self.throttleBytes += got;
            const double allowedSeconds = self.throttleBytes / (self.mbps * 1048576.0);
            const double elapsed = double(nowNs() - self.throttleStartNs) / 1e9;
            if (allowedSeconds > elapsed && !self.wait(int((allowedSeconds - elapsed) * 1000.0))) return AVERROR_EXIT;
            if (elapsed > allowedSeconds + 1.0) {   // idle time earns no credit beyond a second
                self.throttleStartNs = nowNs();
                self.throttleBytes = 0;
            }
        }
        return int(got);
    }
    static int64_t seek(void* opaque, int64_t offset, int whence) {
        auto& self = *static_cast<FileAvio*>(opaque);
        if (whence & AVSEEK_SIZE) return self.size;
        whence &= ~AVSEEK_FORCE;
        if (self.latencyMs > 0) self.wait(self.latencyMs);
        LARGE_INTEGER target{}, result{};
        target.QuadPart = offset;
        const DWORD method = whence == SEEK_SET ? FILE_BEGIN : whence == SEEK_CUR ? FILE_CURRENT : FILE_END;
        if (!SetFilePointerEx(self.handle, target, &result, method)) return AVERROR(EIO);
        self.position = result.QuadPart;
        return result.QuadPart;
    }
};

void DemuxSession::Queue::clear() {
    for (auto& item : items) av_packet_free(&item.packet);
    items.clear();
    bytes = 0;
    durationUs = 0;
    firstUs = lastEndUs = lastDtsUs = INT64_MIN;
    ended = false;
    dedupe = false;
}

DemuxSession::DemuxSession() = default;

DemuxSession::~DemuxSession() {
    stop_.store(true);
    if (irq_) irq_->stop.store(true);
    producerCv_.notify_all();
    consumerCv_.notify_all();
    seekCv_.notify_all();
    if (thread_.joinable()) thread_.join();
    video_.clear();
    audio_.clear();
    for (auto& item : pendingAudio_) av_packet_free(&item.packet);
    closeContext();
}

// ---------------------------------------------------------------------------
// Opening
// ---------------------------------------------------------------------------
void DemuxSession::closeContext() {
    // AVFMT_FLAG_CUSTOM_IO keeps FFmpeg from closing our AVIO; it is freed with file_.
    if (context_) avformat_close_input(&context_);
    file_.reset();
}

// ---------------------------------------------------------------------------
// HLS master playlists. FFmpeg's HLS demuxer opens every variant (its playlist and the start of its first
// segment) before the first picture: on a Twitch VOD that is seven playlists of 180 KB and about 20 MB of
// segments, longer than a slow link can deliver in the connect deadline, and all of it thrown away. The best
// variant is chosen here, like other players do (mpv: hls-bitrate=max), and only that one is opened.
// ---------------------------------------------------------------------------
namespace {

struct HlsVariant {
    int64_t bandwidth = 0;
    int64_t pixels = 0;
    bool audioGroup = false;
    std::string uri;
};

std::string hlsAttribute(const std::string& line, const char* key) {
    const std::string needle = std::string(key) + "=";
    for (size_t at = line.find(needle); at != std::string::npos; at = line.find(needle, at + 1)) {
        if (at != 0 && line[at - 1] != ',' && line[at - 1] != ':') continue;   // not a longer name that ends the same way
        size_t begin = at + needle.size(), end;
        if (begin < line.size() && line[begin] == '"') {
            ++begin;
            end = line.find('"', begin);
        } else {
            end = line.find(',', begin);
        }
        return line.substr(begin, end == std::string::npos ? std::string::npos : end - begin);
    }
    return {};
}

std::string absoluteUrl(const std::string& base, const std::string& ref) {
    if (ref.starts_with("http://") || ref.starts_with("https://")) return ref;
    const auto scheme = base.find("://");
    if (scheme == std::string::npos) return {};
    const auto authorityEnd = base.find('/', scheme + 3);
    const std::string origin = authorityEnd == std::string::npos ? base : base.substr(0, authorityEnd);
    if (ref.starts_with("//")) return base.substr(0, scheme + 1) + ref;
    if (ref.starts_with("/")) return origin + ref;
    std::string directory = base.substr(0, std::min(base.find('?'), base.find('#')));
    const auto slash = directory.rfind('/');
    directory = slash > scheme + 2 ? directory.substr(0, slash + 1) : origin + "/";
    return directory + ref;
}

// The URL of the best variant when `url` is a master playlist whose variants carry their own audio; empty when it is
// not (a media playlist, another protocol, separate audio renditions, a failed fetch): the caller opens `url` itself.
std::string bestHlsVariant(const std::string& url, AVDictionary* networkOptions, const AVIOInterruptCB& interrupt, std::string* note) {
    const std::string path = url.substr(0, url.find('?'));
    std::string lower = path;
    for (auto& ch : lower) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    if (!lower.ends_with(".m3u8")) return {};
    AVDictionary* options = nullptr;
    av_dict_copy(&options, networkOptions, 0);
    AVIOContext* io = nullptr;
    const int opened = avio_open2(&io, url.c_str(), AVIO_FLAG_READ, &interrupt, &options);
    av_dict_free(&options);
    if (opened < 0) return {};
    std::string text;
    uint8_t buffer[16384];
    for (int n; (n = avio_read(io, buffer, sizeof buffer)) > 0 && text.size() < (512u << 10);) text.append(reinterpret_cast<const char*>(buffer), size_t(n));
    avio_closep(&io);
    std::vector<HlsVariant> variants;
    bool separateAudio = false;
    HlsVariant pending;
    bool expectUri = false;
    size_t begin = 0;
    while (begin < text.size()) {
        size_t end = text.find('\n', begin);
        if (end == std::string::npos) end = text.size();
        std::string line = text.substr(begin, end - begin);
        begin = end + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.starts_with("#EXT-X-STREAM-INF:")) {
            pending = {};
            const auto bandwidth = hlsAttribute(line, "BANDWIDTH");
            pending.bandwidth = bandwidth.empty() ? 0 : std::atoll(bandwidth.c_str());
            const auto resolution = hlsAttribute(line, "RESOLUTION");
            if (const auto x = resolution.find('x'); x != std::string::npos) pending.pixels = std::atoll(resolution.c_str()) * std::atoll(resolution.c_str() + x + 1);
            pending.audioGroup = !hlsAttribute(line, "AUDIO").empty();
            expectUri = true;
        } else if (line.starts_with("#EXT-X-MEDIA:")) {
            if (hlsAttribute(line, "TYPE") == "AUDIO" && !hlsAttribute(line, "URI").empty()) separateAudio = true;
        } else if (expectUri && !line.empty() && line[0] != '#') {
            pending.uri = line;
            variants.push_back(pending);
            expectUri = false;
        }
    }
    if (variants.size() < 2 || separateAudio) return {};
    const HlsVariant* best = nullptr;
    for (const auto& v : variants)
        if (!best || v.pixels > best->pixels || (v.pixels == best->pixels && v.bandwidth > best->bandwidth)) best = &v;
    if (!best || best->audioGroup) return {};
    const std::string chosen = absoluteUrl(url, best->uri);
    if (chosen.empty()) return {};
    if (note) *note = std::format("hls master playlist: {} variants, opening only the best ({} bit/s, {} pixels)", variants.size(), best->bandwidth, best->pixels);
    return chosen;
}

}  // namespace

bool DemuxSession::openContext(std::wstring* error, bool initial) {
    closeContext();
    AVFormatContext* context = avformat_alloc_context();
    if (!context) {
        if (error) *error = L"无法分配媒体上下文";
        return false;
    }
    context->interrupt_callback = {&Interrupt::callback, irq_.get()};
    AVDictionary* options = nullptr;
    std::string name = utf8(path_);
    if (network_) {
        networkMediaOptions(&options);
        // Short drops are retried inside FFmpeg's HTTP layer first; this session's
        // reconnect (recover()) only takes over when they run out.
        av_dict_set(&options, "reconnect", "1", 0);
        av_dict_set(&options, "reconnect_streamed", "1", 0);
        av_dict_set(&options, "reconnect_on_network_error", "1", 0);
        av_dict_set(&options, "reconnect_delay_max", "2", 0);
        av_dict_set(&options, "seg_max_retry", "3", 0);   // HLS segments
        av_dict_set(&options, "multiple_requests", "1", 0);
        if (!initial && live_) {
            // A live stream that comes back is probed briefly: the streams are known from the first open, and the
            // full analysis (20 pictures for the frame rate) only delayed the first picture by a second or more.
            av_dict_set(&options, "analyzeduration", "400000", 0);
            av_dict_set(&options, "probesize", "1048576", 0);
            av_dict_set(&options, "fpsprobesize", "0", 0);
        }
    } else {
        file_ = std::make_unique<FileAvio>();
        if (!file_->open(path_, irq_.get())) {
            file_.reset();
            avformat_free_context(context);
            av_dict_free(&options);
            if (error) *error = L"无法打开视频文件，请检查路径和网络共享是否可访问";
            return false;
        }
        context->pb = file_->avio;
        context->flags |= AVFMT_FLAG_CUSTOM_IO;
    }
    irq_->arm(kConnectTimeoutMs);
    if (network_) {
        std::string note;
        const std::string best = bestHlsVariant(name, options, {&Interrupt::callback, irq_.get()}, &note);
        if (!best.empty()) {
            log::info("net-session", note);
            name = best;
        }
    }
    int result = avformat_open_input(&context, name.c_str(), nullptr, &options);
    av_dict_free(&options);
    const bool timedOut = irq_->timedOut();
    irq_->disarm();
    if (result < 0) {   // avformat_open_input freed the context
        log::error("net-session", std::format("open failed code={} text={} timedOut={} initial={}", result, errorText(result), timedOut, initial));
        file_.reset();
        if (error) *error = irq_->cancelled() ? L"已取消" : timedOut ? L"连接服务器超时（40 秒内没有完成），请检查地址和网络" : L"无法连接到服务器或地址不可用，请检查网络";
        return false;
    }
    irq_->arm(kProbeTimeoutMs);
    result = avformat_find_stream_info(context, nullptr);
    const bool probeTimedOut = irq_->timedOut();
    irq_->disarm();
    if (result < 0) {
        log::error("net-session", std::format("find_stream_info failed code={} text={} timedOut={}", result, errorText(result), probeTimedOut));
        avformat_close_input(&context);
        file_.reset();
        if (error) *error = irq_->cancelled() ? L"已取消" : probeTimedOut ? L"读取媒体信息超时（30 秒），服务器太慢或不是视频流" : L"无法读取媒体信息，地址可能不是视频";
        return false;
    }
    context_ = context;
    ++connections_;
    return true;
}

std::shared_ptr<DemuxSession> DemuxSession::open(const OpenParams& params, std::wstring* error) {
    std::shared_ptr<DemuxSession> session(new DemuxSession());
    auto& s = *session;
    s.path_ = params.path;
    s.settings_ = params.settings;
    s.cancelled_ = params.cancelled;
    s.network_ = networkMediaInput(params.path);
    s.irq_ = std::make_unique<Interrupt>();
    s.irq_->owner = params.cancelled;
    s.reconnectBudgetMs_ = int64_t(envNumber(L"VEYRA_TEST_RECONNECT_BUDGET_MS", 60000));
    std::wstring failure;
    if (!s.openContext(&failure, true)) {
        if (error) *error = failure;
        return nullptr;
    }
    AVFormatContext* context = s.context_;
    int video = av_find_best_stream(context, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (video < 0) {
        log::error("net-session", "no video stream");
        if (error) *error = L"该地址没有视频流";
        return nullptr;
    }
    // An HLS master playlist lists every quality as streams of one context. Like other players
    // (mpv: hls-bitrate=max) take the best one rather than whichever FFmpeg met first; the bandwidth
    // check later says so when the server cannot keep up, instead of silently switching down.
    if (context->iformat && context->iformat->name && std::strstr(context->iformat->name, "hls")) {
        int64_t bestScore = -1;
        for (unsigned i = 0; i < context->nb_streams; ++i) {
            const auto* p = context->streams[i]->codecpar;
            if (p->codec_type != AVMEDIA_TYPE_VIDEO) continue;
            const int64_t score = int64_t(p->width) * p->height * 1000 + std::min<int64_t>(p->bit_rate / 1000, 999);
            if (score > bestScore) {
                bestScore = score;
                video = int(i);
            }
        }
    }
    int audio = params.audioStream;
    if (audio == -2) audio = av_find_best_stream(context, AVMEDIA_TYPE_AUDIO, -1, video, nullptr, 0);
    if (audio < 0 || unsigned(audio) >= context->nb_streams || context->streams[audio]->codecpar->codec_type != AVMEDIA_TYPE_AUDIO) audio = -1;
    s.videoIndex_ = video;
    s.audioIndex_.store(audio);
    s.streams_.resize(context->nb_streams);
    for (unsigned i = 0; i < context->nb_streams; ++i) {
        const AVStream* st = context->streams[i];
        auto& info = s.streams_[i];
        info.index = int(i);
        info.type = st->codecpar->codec_type;
        info.parameters.reset(avcodec_parameters_alloc());
        if (info.parameters) avcodec_parameters_copy(info.parameters.get(), st->codecpar);
        info.timeBaseNum = st->time_base.num;
        info.timeBaseDen = std::max(st->time_base.den, 1);
        info.avgRateNum = st->avg_frame_rate.num;
        info.avgRateDen = st->avg_frame_rate.den;
        info.nominalRateNum = st->r_frame_rate.num;
        info.nominalRateDen = st->r_frame_rate.den;
        info.sarNum = st->sample_aspect_ratio.num;
        info.sarDen = st->sample_aspect_ratio.den;
        info.duration = st->duration;
        if (const auto* e = av_dict_get(st->metadata, "language", nullptr, 0)) info.language = e->value;
        if (const auto* e = av_dict_get(st->metadata, "title", nullptr, 0)) info.title = e->value;
        if (const auto* e = av_dict_get(st->metadata, "rotate", nullptr, 0)) info.rotate = e->value;
    }
    s.durationUs_ = context->duration == AV_NOPTS_VALUE ? 0 : context->duration;
    s.containerBitRate_ = context->bit_rate;
    s.formatName_ = context->iformat && context->iformat->name ? context->iformat->name : "";
    const bool byteSeekable = context->pb && (context->pb->seekable & AVIO_SEEKABLE_NORMAL);
    const bool hls = s.formatName_.find("hls") != std::string::npos || s.formatName_.find("applehttp") != std::string::npos;
    // A source with a known length that can be repositioned is on demand; anything
    // else (RTSP, HTTP-TS without length, live HLS) is live: no seek, no resume point.
    s.live_ = !(s.durationUs_ > 0 && (byteSeekable || hls));
    if (s.network_ && s.formatName_.find("rtsp") != std::string::npos) s.live_ = true;
    s.plan_ = networkBufferPlan(s.settings_, s.live_);
    {
        std::lock_guard lock(s.m_);
        s.targetSeconds_ = s.plan_.holds ? s.plan_.startSeconds : 0.0;
        s.phase_ = s.targetSeconds_ > 0 ? Phase::Prebuffering : Phase::Playing;
        s.phaseSinceNs_ = nowNs();
    }
    // Streams nobody reads must not be downloaded: an HLS master playlist lists every
    // quality as streams of the same context, and the demuxer fetches all that are
    // not discarded.
    for (unsigned i = 0; i < context->nb_streams; ++i)
        context->streams[i]->discard = (int(i) == video || int(i) == audio) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
    log::info("net-session", std::format("opened format={} live={} durationUs={} containerBitRate={} videoStream={} audioStream={} streams={} startSeconds={:.1f} aheadSeconds={:.1f} aheadMiB={} holds={} autoReconnect={}",
        s.formatName_, s.live_, s.durationUs_, s.containerBitRate_, video, audio, context->nb_streams, s.plan_.startSeconds,
        s.plan_.aheadSeconds, s.plan_.aheadBytes >> 20, s.plan_.holds, s.settings_.autoReconnect));
    s.thread_ = std::thread([raw = &s] { raw->threadMain(); });
    return session;
}

// ---------------------------------------------------------------------------
// Shared-state arithmetic (m_ held)
// ---------------------------------------------------------------------------
bool DemuxSession::endedLocked() const { return eof_ || failed_; }

double DemuxSession::aheadSecondsLocked() const {
    return settings_.level == NetworkBufferLevel::Auto && bandwidthLow_ ? plan_.growSeconds : plan_.aheadSeconds;
}
uint64_t DemuxSession::aheadBytesLocked() const {
    return settings_.level == NetworkBufferLevel::Auto && bandwidthLow_ ? plan_.growBytes : plan_.aheadBytes;
}

// A packet's share of the buffered content. Estimated durations only: timestamps are not trusted here (see below).
static int64_t itemSpanUs(int64_t startUs, int64_t endUs) { return std::clamp<int64_t>(endUs - startUs, 0, 5000000); }

double DemuxSession::leadSecondsLocked() const {
    // Seconds of content downloaded beyond the playback position: what waits in a queue (the packets' durations)
    // plus what the consumers already took but playback has not reached yet. Never lastEnd - position: a damaged
    // or restarted stream makes timestamps leap (field tests: an HTTP-TS stream that starts over jumps back by
    // minutes), and a lead computed from them stayed "full" while the queues were empty, so nothing was read again.
    const int64_t position = hasHint_ ? playbackUs_ : consumedUs_;
    double best = std::numeric_limits<double>::infinity();
    auto consider = [&](const Queue& q) {
        if (q.lastEndUs == INT64_MIN) {   // nothing queued since open / seek
            best = 0;
            return;
        }
        double taken = 0;
        if (position >= 0) {
            const int64_t head = q.items.empty() ? q.lastEndUs : q.items.front().startUs;
            taken = std::clamp(double(head - position) / 1e6, 0.0, 5.0);   // the two may sit on different timelines: bounded
        }
        best = std::min(best, double(q.durationUs) / 1e6 + taken);
    };
    consider(video_);
    if (audioIndex_.load() >= 0 && !audio_.ended) consider(audio_);
    return std::isfinite(best) ? best : 0.0;
}

void DemuxSession::noteArrivalLocked(size_t bytes) {
    const int64_t now = nowNs();
    arrivals_.push_back({now, bytes});
    while (!arrivals_.empty() && now - arrivals_.front().ns > 3000000000ll) arrivals_.pop_front();
    totalBytes_ += bytes;
}

double DemuxSession::serverRateLocked() const {
    return readSecondsDecayed_ >= 0.2 ? readBytesDecayed_ / readSecondsDecayed_ : 0.0;
}

double DemuxSession::bitrateLocked(double* measured) const {
    double rate = 0;
    if (measured) *measured = 0;
    if (rateSamples_.size() >= 2) {
        const auto& first = rateSamples_.front();
        const auto& last = rateSamples_.back();
        const double seconds = double(last.contentUs - first.contentUs) / 1e6;
        if (seconds >= 3.0) {
            rate = double(last.cumulativeBytes - first.cumulativeBytes) / seconds;
            if (measured) *measured = rate;
        }
    }
    if (rate <= 0 && containerBitRate_ > 0) rate = double(containerBitRate_) / 8.0;
    return rate;
}

double DemuxSession::resumeTargetLocked() const {
    double target = std::max(plan_.startSeconds, 1.0);
    if (bandwidthLow_) {
        const double bitrate = bitrateLocked(nullptr), server = serverRateLocked();
        if (bitrate > 0 && server > 0) {
            // Playing at the stream's speed drains the buffer by (1 - server/bitrate) per second:
            // collect enough to play about a minute before the next stop.
            const double ratio = std::clamp(server / bitrate, 0.02, 1.0);
            target = std::max(target, 60.0 * (1.0 - ratio));
        }
    }
    // A slow server needs as long to fill a big window as to play it: cap the wait per level.
    const double cap = settings_.level == NetworkBufferLevel::Large ? 45.0 : 20.0;
    target = std::min(target, std::max(cap, plan_.startSeconds));
    // A live stream that has just come back restarts with half a second: the jitter buffer refills while it plays.
    if (liveBackCap_ > 0) target = std::min(target, liveBackCap_);
    return std::min(target, std::max(1.0, aheadSecondsLocked() - 0.5));
}

void DemuxSession::enterPhaseLocked(Phase phase, const char* why) {
    if (phase == phase_) return;
    const int64_t now = nowNs();
    log::info("net-session", std::format("phase {} -> {} ({}) lead={:.2f}s target={:.2f}s bandwidthLow={} stalls={}",
        phase_ == Phase::Playing ? "playing" : phase_ == Phase::Buffering ? "buffering" : "prebuffering",
        phase == Phase::Playing ? "playing" : phase == Phase::Buffering ? "buffering" : "prebuffering", why,
        leadSecondsLocked(), targetSeconds_, bandwidthLow_, stalls_));
    phase_ = phase;
    if (phase == Phase::Playing) liveBackCap_ = 0.0;
    phaseSinceNs_ = now;
    consumerCv_.notify_all();
    producerCv_.notify_all();
}

void DemuxSession::updatePhaseLocked() {
    // Bandwidth verdict (hysteresis): the server delivers less than the stream needs.
    {
        const double bitrate = bitrateLocked(nullptr), server = serverRateLocked();
        // Judged on enough evidence: 3 s of reading that delivered at least two seconds of the stream.
        if (bitrate > 0 && readSecondsDecayed_ >= 3.0 && readBytesDecayed_ >= 2.0 * bitrate) {
            if (!bandwidthLow_ && server < bitrate * 0.95) {
                bandwidthLow_ = true;
                log::warn("net-session", std::format("server slower than the stream: server {:.2f} MB/s < bitrate {:.2f} MB/s", server / 1048576.0, bitrate / 1048576.0));
            } else if (bandwidthLow_ && server > bitrate * 1.1) {
                bandwidthLow_ = false;
                log::info("net-session", std::format("server fast enough again: server {:.2f} MB/s, bitrate {:.2f} MB/s", server / 1048576.0, bitrate / 1048576.0));
            }
        }
    }
    const double lead = leadSecondsLocked();
    const bool ended = endedLocked();
    const uint64_t queued = video_.bytes + audio_.bytes;
    const bool capacity = (!videoStarved_ && lead >= aheadSecondsLocked() - 0.05) || queued >= aheadBytesLocked();
    switch (phase_) {
    case Phase::Prebuffering:
        if (!plan_.holds || ended || lead >= targetSeconds_ || capacity) enterPhaseLocked(Phase::Playing, ended ? "end of data" : capacity ? "window full" : "target reached");
        break;
    case Phase::Playing:
        if (plan_.holds && !ended && lead < plan_.lowWaterSeconds) {
            ++stalls_;
            targetSeconds_ = resumeTargetLocked();
            enterPhaseLocked(Phase::Buffering, "underflow");
        }
        break;
    case Phase::Buffering:
        if (!plan_.holds || ended || lead >= targetSeconds_ || capacity) enterPhaseLocked(Phase::Playing, ended ? "end of data" : capacity ? "window full" : "target reached");
        else targetSeconds_ = resumeTargetLocked();
        break;
    }
}

bool DemuxSession::flowBlockedLocked() const {
    if (endedLocked()) return true;
    const uint64_t queued = video_.bytes + audio_.bytes;
    if (queued >= aheadBytesLocked()) return true;
    if (videoStarved_) return false;
    return leadSecondsLocked() >= aheadSecondsLocked();
}

void DemuxSession::logSummaryLocked(bool force) {
    const int64_t now = nowNs();
    if (!force && now - lastSummaryNs_ < 1000000000ll) return;
    lastSummaryNs_ = now;
    double measured = 0;
    const double bitrate = bitrateLocked(&measured);
    double arrivalBytes = 0;
    for (const auto& a : arrivals_) arrivalBytes += double(a.bytes);
    log::info("net-buffer", std::format("state={} leadSec={:.2f} targetSec={:.2f} videoQ={} audioQ={} queuedMiB={:.1f} downloadMBps={:.2f} serverMBps={:.2f} bitrateMBps={:.2f} bandwidthLow={} reconnects={} attempt={} stalls={} corruptPackets={} totalMiB={:.1f} eof={} failed={}",
        reconnecting_ ? "reconnecting" : phase_ == Phase::Playing ? "playing" : phase_ == Phase::Buffering ? "buffering" : "prebuffering",
        leadSecondsLocked(), targetSeconds_, video_.items.size(), audio_.items.size(), double(video_.bytes + audio_.bytes) / 1048576.0,
        arrivalBytes / 3.0 / 1048576.0, serverRateLocked() / 1048576.0, bitrate / 1048576.0, bandwidthLow_, reconnects_, reconnectAttempt_, stalls_,
        corruptPackets_, double(totalBytes_) / 1048576.0, eof_, failed_));
}

// ---------------------------------------------------------------------------
// Consumers
// ---------------------------------------------------------------------------
DemuxSession::Pop DemuxSession::popVideo(AVPacket* dst, Meta& meta, int waitMs, bool honourHold) {
    std::unique_lock lock(m_);
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(waitMs, 0));
    for (;;) {
        if (stop_.load() || (cancelled_ && cancelled_->load())) return Pop::Cancelled;
        updatePhaseLocked();
        const bool hold = honourHold && phase_ != Phase::Playing;
        if (!hold && !video_.items.empty()) {
            Item item = std::move(video_.items.front());
            video_.items.pop_front();
            video_.bytes -= item.bytes;
            video_.durationUs -= itemSpanUs(item.startUs, item.endUs);
            consumedUs_ = item.startUs;
            av_packet_move_ref(dst, item.packet);
            av_packet_free(&item.packet);
            meta.discontinuity = item.discontinuity;
            meta.generation = item.generation;
            producerCv_.notify_one();
            return Pop::Packet;
        }
        if (video_.items.empty() && endedLocked()) return failed_ ? Pop::Failed : Pop::Eof;
        if (video_.items.empty() && !hold && !videoStarved_) {
            // Out of pictures: whatever the lead says, the producer has to read on.
            videoStarved_ = true;
            producerCv_.notify_one();
        }
        if (waitMs <= 0 || Clock::now() >= deadline) return Pop::Empty;
        consumerCv_.wait_until(lock, deadline);
    }
}

DemuxSession::Pop DemuxSession::popAudio(AVPacket* dst, Meta& meta, int waitMs) {
    std::unique_lock lock(m_);
    const auto deadline = Clock::now() + std::chrono::milliseconds(std::max(waitMs, 0));
    for (;;) {
        if (stop_.load() || (cancelled_ && cancelled_->load())) return Pop::Cancelled;
        if (audioIndex_.load() < 0) return Pop::Eof;
        if (!audio_.items.empty()) {
            Item item = std::move(audio_.items.front());
            audio_.items.pop_front();
            audio_.bytes -= item.bytes;
            audio_.durationUs -= itemSpanUs(item.startUs, item.endUs);
            av_packet_move_ref(dst, item.packet);
            av_packet_free(&item.packet);
            meta.discontinuity = item.discontinuity;
            meta.generation = item.generation;
            producerCv_.notify_one();
            return Pop::Packet;
        }
        if (endedLocked()) return failed_ ? Pop::Failed : Pop::Eof;
        if (waitMs <= 0 || Clock::now() >= deadline) return Pop::Empty;
        consumerCv_.wait_until(lock, deadline);
    }
}

bool DemuxSession::selectAudioStream(int index) {
    if (index >= 0 && (size_t(index) >= streams_.size() || streams_[size_t(index)].type != AVMEDIA_TYPE_AUDIO)) return false;
    std::lock_guard lock(m_);
    if (audioIndex_.load() == index) return true;
    audioIndex_.store(index);
    audio_.clear();
    discardDirty_ = true;
    producerCv_.notify_all();
    return true;
}

void DemuxSession::setPlaybackPositionUs(int64_t us) {
    if (us < 0) return;
    std::lock_guard lock(m_);
    playbackUs_ = us;
    hasHint_ = true;
    updatePhaseLocked();
    producerCv_.notify_one();
}

bool DemuxSession::seek(int64_t targetUs) {
    if (live_) return false;
    std::unique_lock lock(m_);
    if (failed_ && !settings_.autoReconnect) return false;
    seekTargetUs_ = std::max<int64_t>(0, targetUs);
    seekPending_ = true;
    seekDone_ = false;
    seekOk_ = false;
    irq_->command.store(true);
    producerCv_.notify_all();
    // Poll the owner's stop flag: nothing notifies this wait when it is set.
    while (!seekDone_) {
        if (stop_.load() || (cancelled_ && cancelled_->load())) return false;
        seekCv_.wait_for(lock, std::chrono::milliseconds(50));
    }
    return seekOk_;
}

void DemuxSession::requestReconnect(const char* reason) {
    std::lock_guard lock(m_);
    if (reconnecting_ || reconnectPending_ || failed_) return;
    reconnectPending_ = true;
    reconnectReason_ = reason ? reason : "requested";
    irq_->command.store(true);
    producerCv_.notify_all();
}

bool DemuxSession::holding() const {
    std::lock_guard lock(m_);
    const_cast<DemuxSession*>(this)->updatePhaseLocked();
    return phase_ != Phase::Playing;
}

void DemuxSession::addCorruptFrames(uint64_t flagged, uint64_t skipped) {
    std::lock_guard lock(m_);
    corruptFrames_ += flagged;
    skippedFrames_ += skipped;
}

std::wstring DemuxSession::failureMessage() const {
    std::lock_guard lock(m_);
    return failure_;
}

bool DemuxSession::eof() const {
    std::lock_guard lock(m_);
    return eof_;
}

NetworkStats DemuxSession::stats() const {
    std::lock_guard lock(m_);
    NetworkStats out;
    out.active = true;
    out.live = live_;
    out.bufferedSeconds = leadSecondsLocked();
    out.targetSeconds = targetSeconds_;
    out.bufferedBytes = video_.bytes + audio_.bytes;
    out.capBytes = aheadBytesLocked();
    double arrivalBytes = 0;
    int64_t oldest = nowNs();
    for (const auto& a : arrivals_) {
        arrivalBytes += double(a.bytes);
        oldest = std::min(oldest, a.ns);
    }
    const double window = std::clamp(double(nowNs() - oldest) / 1e9, 1.0, 3.0);
    out.downloadBytesPerSecond = arrivalBytes / window;
    out.serverBytesPerSecond = serverRateLocked();
    out.bitrateBytesPerSecond = bitrateLocked(nullptr);
    out.bandwidthLow = bandwidthLow_;
    out.reconnects = reconnects_;
    out.reconnectAttempt = reconnecting_ ? reconnectAttempt_ : 0;
    out.stalls = stalls_;
    out.corruptPackets = corruptPackets_;
    out.corruptFrames = corruptFrames_;
    out.skippedFrames = skippedFrames_;
    out.sessionBytes = totalBytes_;
    out.connections = connections_;
    if (failed_) {
        out.state = NetworkStats::State::Failed;
        out.message = failure_;
    } else if (reconnecting_) {
        out.state = NetworkStats::State::Reconnecting;
        out.message = std::format(L"网络中断，正在重连（第 {} 次）", reconnectAttempt_ ? reconnectAttempt_ : 1u);
    } else if (eof_ && video_.items.empty()) {
        out.state = NetworkStats::State::Ended;
    } else if (phase_ == Phase::Prebuffering) {
        out.state = NetworkStats::State::Prebuffering;
        out.message = std::format(L"正在缓冲 {:.1f}/{:.1f} 秒", out.bufferedSeconds, targetSeconds_);
    } else if (phase_ == Phase::Buffering) {
        out.state = NetworkStats::State::Buffering;
        out.message = std::format(L"网络缓冲中 {:.1f}/{:.1f} 秒", out.bufferedSeconds, targetSeconds_);
    } else {
        out.state = NetworkStats::State::Playing;
    }
    if (bandwidthLow_ && (out.state == NetworkStats::State::Buffering || out.state == NetworkStats::State::Prebuffering)) {
        // Separate piece (the UI translates "；"-separated parts on their own).
        out.message += L"；" + std::format(L"服务器速度 {:.1f} MB/s 低于视频码率 {:.1f} MB/s，建议加大缓冲或下载后播放",
                                           out.serverBytesPerSecond / 1048576.0, out.bitrateBytesPerSecond / 1048576.0);
    }
    return out;
}

// ---------------------------------------------------------------------------
// Demux thread
// ---------------------------------------------------------------------------
void DemuxSession::fail(const std::wstring& message) {
    log::error("net-session", std::format("session failed: {}", utf8(message)));
    std::lock_guard lock(m_);
    failed_ = true;
    reconnecting_ = false;
    failure_ = message;
    updatePhaseLocked();
    consumerCv_.notify_all();
    seekCv_.notify_all();
}

void DemuxSession::applyDiscardFlags() {
    if (!context_) return;
    const int video = videoIndex_, audio = audioIndex_.load();
    for (unsigned i = 0; i < context_->nb_streams; ++i)
        context_->streams[i]->discard = (int(i) == video || int(i) == audio) ? AVDISCARD_DEFAULT : AVDISCARD_ALL;
}

// The reconnected source must offer the streams the consumers already rely on.
bool DemuxSession::verifyStreams() {
    if (!context_ || unsigned(videoIndex_) >= context_->nb_streams) return false;
    auto matches = [&](int index) {
        if (index < 0) return true;
        if (unsigned(index) >= context_->nb_streams || size_t(index) >= streams_.size()) return false;
        const auto* parameters = context_->streams[index]->codecpar;
        const auto& known = streams_[size_t(index)];
        return parameters->codec_type == known.type && known.parameters && parameters->codec_id == known.parameters->codec_id;
    };
    if (!matches(videoIndex_) || !matches(audioIndex_.load())) {
        log::warn("net-session", "reconnected source does not offer the same video/audio streams");
        return false;
    }
    const auto* now = context_->streams[videoIndex_]->codecpar;
    const auto* before = streams_[size_t(videoIndex_)].parameters.get();
    // (a short probe may not have seen the picture size yet: only a size that is known can differ)
    if (before && now->width > 0 && now->height > 0 && (now->width != before->width || now->height != before->height)) {
        log::warn("net-session", std::format("reconnected source changed picture size {}x{} -> {}x{}", before->width, before->height, now->width, now->height));
        return false;
    }
    applyDiscardFlags();
    return true;
}

void DemuxSession::threadMain() {
    AVPacket* packet = av_packet_alloc();
    while (!stop_.load()) {
        enum class Work { Read, Seek, Reconnect, Idle, Stop } work = Work::Idle;
        {
            std::unique_lock lock(m_);
            for (;;) {
                if (stop_.load()) {
                    work = Work::Stop;
                    break;
                }
                if (cancelled_ && cancelled_->load()) {   // the engine stopped: nothing will read any more
                    consumerCv_.notify_all();
                    producerCv_.wait_for(lock, std::chrono::milliseconds(100));
                    continue;
                }
                if (seekPending_) {
                    work = Work::Seek;
                    break;
                }
                if (reconnectPending_) {
                    work = Work::Reconnect;
                    break;
                }
                if (discardDirty_) {
                    discardDirty_ = false;
                    lock.unlock();
                    applyDiscardFlags();
                    lock.lock();
                    continue;
                }
                updatePhaseLocked();
                logSummaryLocked(false);
                if (!flowBlockedLocked() && context_) {
                    work = Work::Read;
                    break;
                }
                if (!context_ && !endedLocked()) {   // disconnected and nothing is pending: reconnect now
                    work = Work::Reconnect;
                    reconnectReason_ = "no connection";
                    break;
                }
                producerCv_.wait_for(lock, std::chrono::milliseconds(100));
            }
        }
        if (work == Work::Stop) break;
        if (work == Work::Seek) {
            doSeek();
            continue;
        }
        if (work == Work::Reconnect) {
            std::string why;
            {
                std::lock_guard lock(m_);
                why = reconnectReason_;
                reconnectPending_ = false;
            }
            irq_->command.store(false);
            recover(0, why.c_str());
            continue;
        }
        // Read one packet.
        irq_->arm(live_ ? kReadTimeoutLiveMs : kReadTimeoutVodMs);
        const auto begin = Clock::now();
        const int result = av_read_frame(context_, packet);
        const bool timedOut = irq_->timedOut();
        irq_->disarm();
        const double seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        if (result >= 0 && (packet->flags & AV_PKT_FLAG_CORRUPT) && context_->pb && context_->pb->error < 0) {
            // The connection broke in the middle of a packet: FFmpeg hands the partial data over flagged
            // "corrupt" and reports the error only on the next read. That is a lost connection, not
            // damaged media - the packet is dropped and the link re-established.
            const int code = context_->pb->error;
            av_packet_unref(packet);
            log::warn("net-session", std::format("packet cut short by a transport error code={} text={}", code, errorText(code)));
            if (stop_.load()) break;
            recover(code, "truncated packet");
            continue;
        }
        if (result >= 0) {
            {
                std::lock_guard lock(m_);
                // The history ages with reading time, not with the idle time while the window is full: after a
                // pause one slow call (the first byte of a new HLS segment can take seconds) must not outweigh
                // everything read before it.
                const double keep = std::exp(-seconds / 6.0);
                readBytesDecayed_ = readBytesDecayed_ * keep + double(packet->size);
                readSecondsDecayed_ = readSecondsDecayed_ * keep + seconds;
            }
            onPacket(packet);
            continue;
        }
        if (stop_.load()) break;
        {
            std::lock_guard lock(m_);
            if (seekPending_ || reconnectPending_) continue;   // our own command interrupted the read
        }
        if (cancelled_ && cancelled_->load()) continue;
        if (result == AVERROR_EOF) {
            std::lock_guard lock(m_);
            eof_ = true;
            video_.ended = true;
            audio_.ended = true;
            log::info("net-session", std::format("end of stream: video queued {} audio queued {}", video_.items.size(), audio_.items.size()));
            updatePhaseLocked();
            consumerCv_.notify_all();
            continue;
        }
        log::warn("net-session", std::format("read failed code={} text={} timedOut={}", result, errorText(result), timedOut));
        recover(result, timedOut ? "read timeout" : "read error");
    }
    av_packet_free(&packet);
}

void DemuxSession::flushPendingAudio(int64_t anchorUs) {
    // Called when the live gate opens: pending audio starts at the keyframe time.
    const int audio = audioIndex_.load();
    if (audio < 0 || size_t(audio) >= streams_.size()) {
        for (auto& item : pendingAudio_) av_packet_free(&item.packet);
        pendingAudio_.clear();
        pendingAudioBytes_ = 0;
        return;
    }
    const AVRational tb{streams_[size_t(audio)].timeBaseNum, streams_[size_t(audio)].timeBaseDen};
    const int64_t offsetTb = av_rescale_q(offsetUs_, AV_TIME_BASE_Q, tb);
    if (!pendingAudio_.empty())
        log::info("net-session", std::format("live reconnect: {} audio packets waited for the keyframe, raw {}..{} ms", pendingAudio_.size(),
            pendingAudio_.front().startUs / 1000, pendingAudio_.back().startUs / 1000));
    for (auto& item : pendingAudio_) {
        // Before the keyframe it is not needed; far after it, it belongs to another timeline (late audio of a restarted stream).
        if (item.startUs + 5000 < anchorUs || item.startUs > anchorUs + 5000000) {
            av_packet_free(&item.packet);
            continue;
        }
        if (item.packet->pts != AV_NOPTS_VALUE) item.packet->pts += offsetTb;
        if (item.packet->dts != AV_NOPTS_VALUE) item.packet->dts += offsetTb;
        item.startUs += offsetUs_;
        item.endUs += offsetUs_;
        if (item.dtsUs != AV_NOPTS_VALUE) item.dtsUs += offsetUs_;
        item.generation = generation_.load();
        item.discontinuity = audioDiscontinuityPending_;
        audioDiscontinuityPending_ = false;
        std::lock_guard lock(m_);
        pushLocked(std::move(item), false);
    }
    pendingAudio_.clear();
    pendingAudioBytes_ = 0;
}

void DemuxSession::onPacket(AVPacket* pkt) {
    const int index = pkt->stream_index;
    const bool isVideo = index == videoIndex_;
    const bool isAudio = index == audioIndex_.load();
    const SessionStreamInfo* info = stream(index);
    if (!info || (!isVideo && !isAudio)) {
        av_packet_unref(pkt);
        return;
    }
    const AVRational tb{info->timeBaseNum, info->timeBaseDen};
    const int64_t stamp = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
    int64_t startUs = toUs(stamp, tb);
    int64_t dtsUs = toUs(pkt->dts != AV_NOPTS_VALUE ? pkt->dts : pkt->pts, tb);
    // Estimated duration when the container leaves it out.
    int64_t durationUs = pkt->duration > 0 ? av_rescale_q(pkt->duration, tb, AV_TIME_BASE_Q) : 0;
    if (durationUs <= 0) {
        if (isVideo) {
            const int num = info->avgRateNum > 0 ? info->avgRateNum : info->nominalRateNum;
            const int den = info->avgRateDen > 0 ? info->avgRateDen : info->nominalRateDen;
            durationUs = num > 0 && den > 0 ? int64_t(1e6 * double(den) / double(num)) : 40000;
        } else {
            const auto* params = info->parameters.get();
            durationUs = params && params->frame_size > 0 && params->sample_rate > 0 ? int64_t(1e6 * params->frame_size / params->sample_rate) : 21333;
        }
    }
    Queue& queue = isVideo ? video_ : audio_;
    // ---- a timeline that leaps: a restarted stream, damaged stamps ----
    // Live streams are re-anchored like a reconnect: the next video keyframe continues the old timeline, so the
    // consumers (picture clock, audio) never see time run backwards or jump ahead; before this a restarted stream
    // froze the picture until its new timestamps caught up with the clock.
    int64_t lastDtsUs;
    {
        std::lock_guard lock(m_);   // a consumer's selectAudioStream() clears the audio queue
        lastDtsUs = queue.lastDtsUs;
    }
    if (!gateKeyframe_ && dtsUs != AV_NOPTS_VALUE && lastDtsUs != INT64_MIN) {
        const int64_t step = dtsUs + offsetUs_ - lastDtsUs;
        if (step < -1000000 || step > 3000000) {
            if (timestampJumpLogs_ < 24) {
                ++timestampJumpLogs_;
                log::warn("net-session", std::format("timestamp jump {} {:.3f}s -> {:.3f}s (step {:.3f}s) key={} corrupt={} size={} live={}", isVideo ? "video" : "audio",
                    double(lastDtsUs) / 1e6, double(dtsUs + offsetUs_) / 1e6, double(step) / 1e6, (pkt->flags & AV_PKT_FLAG_KEY) != 0,
                    (pkt->flags & AV_PKT_FLAG_CORRUPT) != 0, pkt->size, live_));
            }
            if (live_) {
                // Video: only a keyframe starts a timeline; anything else with a leap is a damaged stamp, or the
                // start of a new stream that begins mid-GOP, and waits for the keyframe. Audio right after a
                // re-anchor: a packet of the old timeline that arrived late. (A leap in audio alone is a gap in the
                // sound, not a new timeline: it passes.)
                if (isVideo) {
                    if (!(pkt->flags & AV_PKT_FLAG_KEY)) {
                        av_packet_unref(pkt);
                        return;
                    }
                    gateKeyframe_ = true;
                } else if (nowNs() - lastAnchorNs_ < 3000000000ll) {
                    av_packet_unref(pkt);
                    return;
                }
            }
        }
    }
    // ---- live reconnect: wait for a video keyframe, then continue the old timeline ----
    if (gateKeyframe_) {
        if (isVideo) {
            if (!(pkt->flags & AV_PKT_FLAG_KEY) || startUs == AV_NOPTS_VALUE) {
                av_packet_unref(pkt);
                return;
            }
            int64_t previousEnd;
            {
                std::lock_guard lock(m_);
                previousEnd = std::max(video_.lastEndUs, audio_.lastEndUs);   // INT64_MIN: nothing was ever queued
            }
            offsetUs_ = previousEnd == INT64_MIN ? 0 : previousEnd - startUs;
            gateKeyframe_ = false;
            lastAnchorNs_ = nowNs();
            videoDiscontinuityPending_ = true;
            audioDiscontinuityPending_ = true;
            int64_t videoEnd, audioEnd;
            {
                std::lock_guard lock(m_);
                videoEnd = video_.lastEndUs;
                audioEnd = audio_.lastEndUs;
            }
            log::info("net-session", std::format("live reconnect: keyframe at {} ms (raw), previous end video {} ms audio {} ms, timeline offset {} ms, pending audio {} packets",
                startUs / 1000, videoEnd / 1000, audioEnd / 1000, offsetUs_ / 1000, pendingAudio_.size()));
            flushPendingAudio(startUs);
        } else {
            if (startUs == AV_NOPTS_VALUE || pendingAudioBytes_ > (8 << 20)) {
                av_packet_unref(pkt);
                return;
            }
            Item item;
            item.packet = av_packet_alloc();
            if (!item.packet) {
                av_packet_unref(pkt);
                return;
            }
            av_packet_move_ref(item.packet, pkt);
            item.startUs = startUs;
            item.endUs = startUs + durationUs;
            item.dtsUs = dtsUs;
            item.bytes = size_t(item.packet->size);
            pendingAudioBytes_ += int64_t(item.bytes);
            pendingAudio_.push_back(std::move(item));
            return;
        }
    }
    if (offsetUs_) {
        const int64_t offsetTb = av_rescale_q(offsetUs_, AV_TIME_BASE_Q, tb);
        if (pkt->pts != AV_NOPTS_VALUE) pkt->pts += offsetTb;
        if (pkt->dts != AV_NOPTS_VALUE) pkt->dts += offsetTb;
        if (startUs != AV_NOPTS_VALUE) startUs += offsetUs_;
        if (dtsUs != AV_NOPTS_VALUE) dtsUs += offsetUs_;
    }
    // ---- VOD reconnect: skip what was queued before the connection dropped ----
    if (queue.dedupe) {
        if (dtsUs != AV_NOPTS_VALUE && queue.lastDtsUs != INT64_MIN && dtsUs <= queue.lastDtsUs) {
            av_packet_unref(pkt);
            return;
        }
        queue.dedupe = false;
    }
    Item item;
    item.packet = av_packet_alloc();
    if (!item.packet) {
        av_packet_unref(pkt);
        return;
    }
    av_packet_move_ref(item.packet, pkt);
    if (startUs == AV_NOPTS_VALUE) startUs = queue.lastEndUs != INT64_MIN ? queue.lastEndUs : 0;
    item.startUs = startUs;
    item.endUs = startUs + durationUs;
    item.bytes = size_t(item.packet->size);
    item.generation = generation_.load();
    if (isVideo && videoDiscontinuityPending_) {
        item.discontinuity = true;
        videoDiscontinuityPending_ = false;
    } else if (isAudio && audioDiscontinuityPending_) {
        item.discontinuity = true;
        audioDiscontinuityPending_ = false;
    }
    item.dtsUs = dtsUs;
    std::lock_guard lock(m_);
    pushLocked(std::move(item), isVideo);
}

bool DemuxSession::pushLocked(Item&& item, bool video) {
    Queue& queue = video ? video_ : audio_;
    if (item.packet->flags & AV_PKT_FLAG_CORRUPT) {
        if (video) ++corruptPackets_;
    }
    if (queue.firstUs == INT64_MIN) queue.firstUs = item.startUs;
    // The newest end, except that a timeline which starts over (a restarted stream) is followed, not out-waited.
    queue.lastEndUs = item.endUs < queue.lastEndUs - 2000000 ? item.endUs : std::max(queue.lastEndUs, item.endUs);
    queue.durationUs += itemSpanUs(item.startUs, item.endUs);
    if (video) videoStarved_ = false;
    if (item.dtsUs != AV_NOPTS_VALUE) queue.lastDtsUs = item.dtsUs;
    queue.bytes += item.bytes;
    noteArrivalLocked(item.bytes);
    contentBytes_ += item.bytes;
    if (video) {
        if (rateSamples_.empty() || item.endUs - rateSamples_.back().contentUs >= 500000) rateSamples_.push_back({item.endUs, contentBytes_});
        while (rateSamples_.size() > 2 && rateSamples_.back().contentUs - rateSamples_[1].contentUs > 12000000) rateSamples_.pop_front();
    }
    queue.items.push_back(std::move(item));
    updatePhaseLocked();
    consumerCv_.notify_all();
    return true;
}

// ---------------------------------------------------------------------------
// Seek and reconnect (demux thread)
// ---------------------------------------------------------------------------
void DemuxSession::doSeek() {
    int64_t target;
    {
        std::lock_guard lock(m_);
        target = seekTargetUs_;
        seekPending_ = false;
    }
    irq_->command.store(false);
    bool ok = false;
    {
        std::lock_guard lock(m_);
        video_.clear();
        audio_.clear();
        for (auto& item : pendingAudio_) av_packet_free(&item.packet);
        pendingAudio_.clear();
        pendingAudioBytes_ = 0;
        generation_.fetch_add(1);
        eof_ = false;
        playbackUs_ = target;
        consumedUs_ = target;
        rateSamples_.clear();
        contentBytes_ = 0;
        afterSeek_ = true;
        targetSeconds_ = plan_.holds ? std::min(plan_.startSeconds, 5.0) : 0.0;
        phase_ = targetSeconds_ > 0 ? Phase::Prebuffering : Phase::Playing;
        phaseSinceNs_ = nowNs();
    }
    gateKeyframe_ = false;
    videoDiscontinuityPending_ = audioDiscontinuityPending_ = false;
    offsetUs_ = 0;
    for (int attempt = 0; attempt < 2 && !ok && !stop_.load() && !(cancelled_ && cancelled_->load()); ++attempt) {
        if (!context_) {
            std::wstring error;
            if (!openContext(&error, false) || !verifyStreams()) {
                closeContext();
                break;
            }
        }
        const auto& v = streams_[size_t(videoIndex_)];
        const int64_t ts = av_rescale_q(target, AV_TIME_BASE_Q, AVRational{v.timeBaseNum, v.timeBaseDen});
        irq_->arm(kConnectTimeoutMs + kReadTimeoutVodMs);
        const int result = av_seek_frame(context_, videoIndex_, ts, AVSEEK_FLAG_BACKWARD);
        irq_->disarm();
        ok = result >= 0;
        if (!ok) {
            log::warn("net-session", std::format("seek to {} ms failed code={} text={} attempt={}", target / 1000, result, errorText(result), attempt));
            if (irq_->command.load()) break;
            closeContext();   // the next attempt reconnects
        }
    }
    {
        std::lock_guard lock(m_);
        seekOk_ = ok;
        seekDone_ = true;
        seekCv_.notify_all();
        updatePhaseLocked();
        consumerCv_.notify_all();
    }
    log::info("net-session", std::format("seek to {} ms ok={} generation={}", target / 1000, ok, generation_.load()));
}

// Re-establishes the source after a failure, keeping everything already queued.
bool DemuxSession::recover(int code, const char* why) {
    log::warn("net-session", std::format("connection lost reason={} code={} text={}", why, code, code ? errorText(code) : std::string()));
    closeContext();
    if (!settings_.autoReconnect) {
        fail(L"网络连接中断（断线自动重连已关闭）");
        return false;
    }
    {
        std::lock_guard lock(m_);
        reconnecting_ = true;
        reconnectAttempt_ = 0;
        updatePhaseLocked();
    }
    const auto begin = Clock::now();
    int delayMs = 1000;
    uint32_t attempt = 0;
    while (!stop_.load() && !(cancelled_ && cancelled_->load())) {
        if (std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count() >= reconnectBudgetMs_) break;
        // Back off, but give way at once to a seek or a stop.
        if (!irq_->sleep(delayMs)) {
            std::lock_guard lock(m_);
            if (seekPending_) {   // doSeek() reconnects as part of positioning
                reconnecting_ = false;
                return false;
            }
            if (stop_.load() || (cancelled_ && cancelled_->load())) break;
        }
        {
            std::lock_guard lock(m_);
            ++attempt;
            reconnectAttempt_ = attempt;
        }
        std::wstring error;
        bool ok = openContext(&error, false) && verifyStreams();
        if (ok) {
            if (!live_) {
                // On demand: continue after the last queued packet.
                int64_t resume;
                {
                    std::lock_guard lock(m_);
                    resume = video_.lastDtsUs != INT64_MIN ? video_.lastDtsUs : audio_.lastDtsUs;
                }
                if (resume != INT64_MIN) {
                    const auto& v = streams_[size_t(videoIndex_)];
                    const int64_t ts = av_rescale_q(std::max<int64_t>(0, resume), AV_TIME_BASE_Q, AVRational{v.timeBaseNum, v.timeBaseDen});
                    irq_->arm(kConnectTimeoutMs + kReadTimeoutVodMs);
                    const int result = av_seek_frame(context_, videoIndex_, ts, AVSEEK_FLAG_BACKWARD);
                    irq_->disarm();
                    if (result < 0) {
                        log::warn("net-session", std::format("resume seek failed code={} text={}", result, errorText(result)));
                        ok = false;
                    } else {
                        std::lock_guard lock(m_);
                        video_.dedupe = video_.lastDtsUs != INT64_MIN;
                        audio_.dedupe = audio_.lastDtsUs != INT64_MIN;
                    }
                }
            } else {
                gateKeyframe_ = true;
                for (auto& item : pendingAudio_) av_packet_free(&item.packet);
                pendingAudio_.clear();
                pendingAudioBytes_ = 0;
            }
        }
        if (ok) {
            std::lock_guard lock(m_);
            reconnecting_ = false;
            reconnectAttempt_ = 0;
            ++reconnects_;
            eof_ = false;
            liveBackCap_ = live_ && plan_.holds ? 0.5 : 0.0;
            log::info("net-session", std::format("reconnected after {} attempt(s), {} ms; reconnects={} live={}", attempt,
                std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count(), reconnects_, live_));
            updatePhaseLocked();
            return true;
        }
        closeContext();
        gateKeyframe_ = false;
        // Every second for the first 15 s, then every 2 s: whenever the server comes back, the next try is at most
        // a second away, which leaves room for the keyframe wait and the start buffer inside 5 s of an outage's end.
        delayMs = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - begin).count() < 15000 ? 1000 : 2000;
    }
    if (stop_.load() || (cancelled_ && cancelled_->load())) return false;
    fail(std::format(L"网络连接中断，已重试 {} 秒仍无法恢复", reconnectBudgetMs_ / 1000));
    return false;
}

}  // namespace veyra::media
