#include "veyra/engine/LiveRecorder.h"
#include "veyra/pipeline/EnhanceGraph.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/sink/VideoEncoder.h"
#include "veyra/Log.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <deque>
#include <format>
#include <filesystem>
#include <thread>
#include <vector>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/mathematics.h>
}

namespace veyra::engine {
namespace {

std::string utf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size, 0);
    WideCharToMultiByte(CP_UTF8, 0, text.data(), int(text.size()), out.data(), size, nullptr, nullptr);
    return out;
}

std::wstring fileStamp() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04d-%02d-%02d-%02d%02d%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
               local.tm_hour, local.tm_min, local.tm_sec);
    return buffer;
}

} // namespace

struct LiveRecorder::Impl {
    struct Packet {
        std::vector<uint8_t> data;
        int64_t ptsUs = 0;
        int64_t durationUs = 0;
        bool key = false;
    };

    // The engine thread produces packets; the writer thread owns the file.
    // The queue is bounded so a slow disk costs frames, never playback.
    static constexpr size_t kQueueLimit = 240;      // ~4 s of 60 fps video
    static constexpr size_t kQueueHighWater = 160;  // start dropping here

    std::unique_ptr<sink::VideoEncoder> encoder;
    AVFormatContext* mux = nullptr;
    AVStream* stream = nullptr;
    std::thread writer;

    std::mutex queueMutex;
    std::condition_variable queueSignal;
    std::deque<Packet> queue;
    bool writerStop = false;
    std::atomic<bool> writerFailed{false};
    std::atomic<bool> headerWritten{false};

    mutable std::mutex statusMutex;
    std::wstring path;
    std::wstring error;
    std::wstring encoderName;

    uint32_t width = 0, height = 0, fps = 60;
    bool hevc = false, hdr = false;
    int64_t frameDurationUs = 16667;
    int64_t lastPtsUs = -1;
    std::atomic<uint64_t> frames{0};
    std::atomic<uint64_t> dropped{0};
    std::atomic<uint64_t> recordedUs{0};
    std::chrono::steady_clock::time_point startTime{};

    void failWriter(const std::wstring& reason) {
        writerFailed.store(true, std::memory_order_relaxed);
        {
            std::lock_guard lock(statusMutex);
            if (error.empty()) error = reason;
        }
        veyra::log::error("live-recorder", std::format("writer failed: {}", utf8(error)));
    }

    bool writePacket(const Packet& packet) {
        AVPacket* raw = av_packet_alloc();
        if (!raw) return false;
        bool ok = av_new_packet(raw, int(packet.data.size())) >= 0;
        if (ok) {
            std::memcpy(raw->data, packet.data.data(), packet.data.size());
            raw->stream_index = stream->index;
            raw->pts = raw->dts = packet.ptsUs;
            raw->duration = packet.durationUs;
            if (packet.key) raw->flags |= AV_PKT_FLAG_KEY;
            ok = av_interleaved_write_frame(mux, raw) >= 0;
        }
        av_packet_free(&raw);
        return ok;
    }

    void writerLoop() {
        int result = avio_open(&mux->pb, utf8(path).c_str(), AVIO_FLAG_WRITE);
        if (result < 0) { failWriter(L"无法创建录制文件"); return; }
        result = avformat_write_header(mux, nullptr);
        if (result < 0) {
            avio_closep(&mux->pb);
            failWriter(L"无法写入录制文件头");
            return;
        }
        headerWritten.store(true, std::memory_order_relaxed);
        veyra::log::info("live-recorder", std::format("container header written path={}", utf8(path)));
        for (;;) {
            Packet packet;
            {
                std::unique_lock lock(queueMutex);
                queueSignal.wait(lock, [this] { return writerStop || !queue.empty(); });
                if (queue.empty()) {
                    if (writerStop) break;
                    continue;
                }
                packet = std::move(queue.front());
                queue.pop_front();
            }
            if (!writePacket(packet)) { failWriter(L"写入录制帧失败"); break; }
        }
        if (headerWritten.load(std::memory_order_relaxed)) av_write_trailer(mux);
        if (mux && mux->pb) avio_closep(&mux->pb);
        veyra::log::info("live-recorder", "container closed");
    }
};

LiveRecorder::LiveRecorder() : p_(std::make_unique<Impl>()) {}
LiveRecorder::~LiveRecorder() { end(); }

bool LiveRecorder::begin(gfx::D3D12DeviceContext& ctx, gfx::CommandSlotRing& ring, pipeline::EnhanceGraph& graph,
                         const RecordingSettings& settings, uint32_t nominalFps, std::wstring& error) {
    auto& p = *p_;
    error.clear();
    if (state_.load(std::memory_order_relaxed) != State::Idle) { error = L"录制已在进行中"; return false; }
    if (settings.directory.empty()) { error = L"未设置录制保存位置"; return false; }
    std::error_code code;
    std::filesystem::create_directories(settings.directory, code);
    if (code) { error = L"无法创建录制目录：" + settings.directory; return false; }

    const auto file = (std::filesystem::path(settings.directory) / (L"Veyra-" + fileStamp() + L".mp4")).wstring();
    if (std::filesystem::exists(file)) { error = L"录制文件已存在，请更换保存位置"; return false; }

    // The recorded picture is the processed one: the graph's working extent.
    p.width = graph.workWidth();
    p.height = graph.workHeight();
    if (!p.width || !p.height) { error = L"当前画面尺寸不可用"; return false; }
    if ((p.width & 1) || (p.height & 1)) { error = L"当前画面尺寸为奇数，无法录制"; return false; }
    p.hdr = graph.hdrOutput();
    // HDR output needs HEVC Main10; the encoder refuses H.264 for 10-bit.
    p.hevc = settings.hevc || p.hdr;
    p.fps = settings.fpsLimit ? settings.fpsLimit : (nominalFps ? nominalFps : 60);
    if (p.fps > 240) p.fps = 240;
    p.frameDurationUs = 1000000 / p.fps;
    {
        std::lock_guard lock(p.statusMutex);
        p.path = file;
        p.error.clear();
    }

    if (avformat_alloc_output_context2(&p.mux, nullptr, "mp4", nullptr) < 0 || !p.mux) {
        error = L"无法创建录制封装器";
        return false;
    }
    p.stream = avformat_new_stream(p.mux, nullptr);
    if (!p.stream) { error = L"无法创建录制视频流"; return false; }
    p.stream->time_base = AVRational{1, 1000000};
    p.stream->avg_frame_rate = AVRational{int(p.fps), 1};
    auto* parameters = p.stream->codecpar;
    parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    parameters->codec_id = p.hevc ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
    parameters->width = p.width;
    parameters->height = p.height;
    parameters->format = p.hdr ? AV_PIX_FMT_YUV420P10LE : AV_PIX_FMT_YUV420P;
    parameters->color_range = AVCOL_RANGE_MPEG;
    parameters->color_space = p.hdr ? AVCOL_SPC_BT2020_NCL : AVCOL_SPC_BT709;
    parameters->color_primaries = p.hdr ? AVCOL_PRI_BT2020 : AVCOL_PRI_BT709;
    parameters->color_trc = p.hdr ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_IEC61966_2_1;
    if (p.hdr) parameters->profile = AV_PROFILE_HEVC_MAIN_10;

    // The engine thread produces packets; the writer thread owns the file.
    auto producer = [this](const uint8_t* bytes, size_t size, int64_t pts, bool key) {
        Impl::Packet packet;
        packet.data.assign(bytes, bytes + size);
        packet.ptsUs = pts;
        packet.durationUs = p_->frameDurationUs;
        packet.key = key;
        {
            std::lock_guard lock(p_->queueMutex);
            if (p_->queue.size() >= Impl::kQueueLimit) {
                p_->dropped.fetch_add(1, std::memory_order_relaxed);
                return false;  // the encoder stops instead of writing a broken file
            }
            p_->queue.push_back(std::move(packet));
        }
        p_->queueSignal.notify_one();
        return true;
    };

    sink::EncoderConfig config;
    config.hevc = p.hevc;
    config.fpsNum = p.fps;
    config.fpsDen = 1;
    config.bitrateMbps = settings.bitrateMbps;
    config.rateControl = sink::ExportRateControl::Vbr;
    // The live loop may not wait long for the encoder: a bounded drain keeps
    // a slow GPU from stalling playback behind the recorder.
    config.drainTimeoutMs = 2000;
    std::wstring detail;
    p.encoder = sink::openVideoEncoder(ctx, ring, graph, config, producer, detail);
    if (!p.encoder) {
        error = detail.empty() ? L"没有可用的视频编码器" : detail;
        if (p.mux) { avformat_free_context(p.mux); p.mux = nullptr; }
        return false;
    }
    {
        std::lock_guard lock(p.statusMutex);
        p.encoderName = p.encoder->describe();
    }
    const auto headers = p.encoder->headers();
    if (!headers.empty()) {
        parameters->extradata = static_cast<uint8_t*>(av_mallocz(headers.size() + AV_INPUT_BUFFER_PADDING_SIZE));
        if (!parameters->extradata) { error = L"无法写入录制文件头"; return false; }
        std::memcpy(parameters->extradata, headers.data(), headers.size());
        parameters->extradata_size = int(headers.size());
    }

    p.frames.store(0, std::memory_order_relaxed);
    p.dropped.store(0, std::memory_order_relaxed);
    p.recordedUs.store(0, std::memory_order_relaxed);
    p.lastPtsUs = -1;
    p.startTime = std::chrono::steady_clock::now();
    p.writer = std::thread([this] { p_->writerLoop(); });
    state_.store(State::Recording, std::memory_order_release);
    veyra::log::info("live-recorder", std::format(
        "started path={} extent={}x{} codec={} hdr={} fps={} bitrateMbps={} encoder={}",
        utf8(file), p.width, p.height, p.hevc ? "HEVC" : "H264",
        p.hdr ? 1 : 0, p.fps, settings.bitrateMbps, utf8(p.encoderName)));
    return true;
}

bool LiveRecorder::encodeFrame(uint32_t frameSlot, bool generated) {
    auto& p = *p_;
    if (state_.load(std::memory_order_relaxed) != State::Recording) return false;
    if (p.writerFailed.load(std::memory_order_relaxed)) { end(); return false; }

    // A slow disk costs frames, never playback: above the high-water mark the
    // frame is skipped and counted.
    if (p.queue.size() >= Impl::kQueueHighWater) {
        p.dropped.fetch_add(1, std::memory_order_relaxed);
        return true;
    }
    const auto now = std::chrono::steady_clock::now();
    const int64_t ptsUs = std::chrono::duration_cast<std::chrono::microseconds>(now - p.startTime).count();
    if (p.lastPtsUs >= 0 && ptsUs <= p.lastPtsUs) return true;  // monotonic only
    p.lastPtsUs = ptsUs;
    p.recordedUs.store(uint64_t(ptsUs), std::memory_order_relaxed);
    if (!p.encoder->encode(frameSlot, generated, ptsUs)) {
        {
            std::lock_guard lock(p.statusMutex);
            if (p.error.empty()) p.error = p.encoder->lastError();
        }
        veyra::log::error("live-recorder", "encode failed; closing the recording");
        end();
        return false;
    }
    p.frames.fetch_add(1, std::memory_order_relaxed);
    return true;
}

void LiveRecorder::end() {
    auto& p = *p_;
    const State state = state_.load(std::memory_order_relaxed);
    if (state == State::Idle) {
        // A failed begin() may still own a container context.
        if (p.mux) { avformat_free_context(p.mux); p.mux = nullptr; }
        return;
    }
    state_.store(State::Idle, std::memory_order_release);
    // Drain the encoder first: the last bitstreams reach the writer through
    // the same queue, and the trailer may only be written after them.
    if (p.encoder) {
        if (!p.encoder->finish()) {
            std::lock_guard lock(p.statusMutex);
            if (p.error.empty()) p.error = p.encoder->lastError();
        }
        p.encoder.reset();
    }
    {
        std::lock_guard lock(p.queueMutex);
        p.writerStop = true;
    }
    p.queueSignal.notify_all();
    if (p.writer.joinable()) p.writer.join();
    if (p.mux) {
        avformat_free_context(p.mux);
        p.mux = nullptr;
        p.stream = nullptr;
    }
    const bool failed = p.writerFailed.load(std::memory_order_relaxed) || !p.error.empty();
    state_.store(failed ? State::Failed : State::Idle, std::memory_order_release);
    veyra::log::info("live-recorder", std::format(
        "stopped path={} frames={} dropped={} seconds={:.2f} failed={}",
        utf8(p.path), p.frames.load(), p.dropped.load(),
        double(p.recordedUs.load()) / 1000000.0, failed ? 1 : 0));
}

RecordingStatus LiveRecorder::status() const {
    auto& p = *p_;
    RecordingStatus out;
    out.active = state_.load(std::memory_order_relaxed) == State::Recording;
    out.frames = p.frames.load(std::memory_order_relaxed);
    out.dropped = p.dropped.load(std::memory_order_relaxed);
    out.seconds = double(p.recordedUs.load(std::memory_order_relaxed)) / 1000000.0;
    std::lock_guard lock(p.statusMutex);
    out.path = p.path;
    out.error = p.error;
    out.encoder = p.encoderName;
    out.failed = !p.error.empty();
    return out;
}

} // namespace veyra::engine
