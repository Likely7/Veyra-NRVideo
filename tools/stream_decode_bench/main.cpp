// Stream decode bench: replays a recorded H.264/HEVC/AV1 stream through the same
// CaptureCompressedDecoder (D3D12VA on a shared device) that the PC/Xbox stream
// sources use, at the stream's frame rate, and reports how long each decode takes.
// It keeps decoded pictures alive the way the engine does (one waiting in the
// mailbox, one being read, two held by the graph) so pool pressure is realistic.
//
// usage: veyra_stream_decode_bench <file> [fps=60] [seconds=0 (whole file)] [--burst] [--software]
//   --burst  feeds frames back to back instead of at the frame rate (throughput).
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
}

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <deque>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "veyra/source/CaptureCompressedDecoder.h"

using Microsoft::WRL::ComPtr;
using Clock = std::chrono::steady_clock;

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::printf("usage: veyra_stream_decode_bench <file> [fps] [seconds] [--burst]\n"); return 2; }
    // FFmpeg takes UTF-8 file names on Windows.
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, nullptr, 0, nullptr, nullptr);
    std::string path(size_t(std::max(bytes, 1)) - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, argv[1], -1, path.data(), bytes, nullptr, nullptr);
    const double fps = argc > 2 ? _wtof(argv[2]) : 60.0;
    const double seconds = argc > 3 ? _wtof(argv[3]) : 0.0;
    bool burst = false;
    bool software = false;
    for (int i = 1; i < argc; ++i) {
        if (std::wstring(argv[i]) == L"--burst") burst = true;
        if (std::wstring(argv[i]) == L"--software") software = true;   // the fallback path
    }

    AVFormatContext* input = nullptr;
    if (avformat_open_input(&input, path.c_str(), nullptr, nullptr) < 0 || avformat_find_stream_info(input, nullptr) < 0) {
        std::printf("cannot open %s\n", path.c_str());
        return 1;
    }
    const int stream = av_find_best_stream(input, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (stream < 0) { std::printf("no video stream\n"); return 1; }
    const AVCodecParameters* par = input->streams[stream]->codecpar;
    veyra::source::CaptureCodec codec = par->codec_id == AV_CODEC_ID_H264 ? veyra::source::CaptureCodec::H264
        : par->codec_id == AV_CODEC_ID_HEVC ? veyra::source::CaptureCodec::Hevc
        : par->codec_id == AV_CODEC_ID_AV1 ? veyra::source::CaptureCodec::Av1 : veyra::source::CaptureCodec::None;
    if (codec == veyra::source::CaptureCodec::None) { std::printf("unsupported codec\n"); return 1; }

    // Annex-B for H.264/HEVC, like the stream sources receive it.
    const AVBitStreamFilter* filter = av_bsf_get_by_name(par->codec_id == AV_CODEC_ID_H264 ? "h264_mp4toannexb"
                                                       : par->codec_id == AV_CODEC_ID_HEVC ? "hevc_mp4toannexb" : "null");
    AVBSFContext* bsf = nullptr;
    av_bsf_alloc(filter, &bsf);
    avcodec_parameters_copy(bsf->par_in, par);
    bsf->time_base_in = input->streams[stream]->time_base;
    av_bsf_init(bsf);

    std::vector<std::vector<uint8_t>> units;
    size_t totalBytes = 0;
    AVPacket* packet = av_packet_alloc();
    while (av_read_frame(input, packet) >= 0) {
        if (packet->stream_index == stream) {
            av_bsf_send_packet(bsf, packet);
            while (av_bsf_receive_packet(bsf, packet) == 0) {
                units.emplace_back(packet->data, packet->data + packet->size);
                totalBytes += size_t(packet->size);
                av_packet_unref(packet);
            }
        }
        av_packet_unref(packet);
    }
    av_packet_free(&packet);
    av_bsf_free(&bsf);
    const unsigned width = unsigned(par->width), height = unsigned(par->height);
    avformat_close_input(&input);
    if (units.empty()) { std::printf("no packets\n"); return 1; }
    std::printf("%s: %zu units, %ux%u, average %.1f KB/unit (%.1f Mbps at %.0f fps)\n", path.c_str(), units.size(), width, height,
                double(totalBytes) / units.size() / 1024.0, double(totalBytes) * 8.0 / units.size() * fps / 1e6, fps);

    ComPtr<IDXGIFactory6> factory;
    CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    ComPtr<IDXGIAdapter1> adapter;
    factory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
    ComPtr<ID3D12Device> device;
    if (FAILED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) { std::printf("no D3D12 device\n"); return 1; }
    D3D12_COMMAND_QUEUE_DESC qd{};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ComPtr<ID3D12CommandQueue> queue;
    device->CreateCommandQueue(&qd, IID_PPV_ARGS(&queue));

    veyra::source::CaptureCompressedDecoder decoder;
    decoder.setStreamProfile(true);
    if (!decoder.open(codec, width, height, nullptr, 0, software ? nullptr : device.Get(), software ? nullptr : queue.Get())) { std::printf("decoder open failed\n"); return 1; }
    std::printf("backend=%s\n", decoder.backendName());

    // The engine-side holders: mailbox, current read, two graph parities.
    std::deque<std::shared_ptr<AVFrame>> held;
    std::vector<double> decodeMs;
    const auto interval = std::chrono::duration<double>(1.0 / fps);
    const size_t limit = seconds > 0 ? std::min(units.size(), size_t(seconds * fps)) : units.size();
    const auto start = Clock::now();
    size_t produced = 0, late = 0;
    for (size_t i = 0; i < limit; ++i) {
        if (!burst) {
            const auto due = start + std::chrono::duration_cast<Clock::duration>(interval * double(i));
            if (Clock::now() < due) std::this_thread::sleep_until(due);
            else if (Clock::now() - due > interval) ++late;
        }
        AVFrame* target = av_frame_alloc();
        target->format = 23; // NV12; only used if the decoder falls back to software
        target->width = int(width);
        target->height = int(height);
        av_frame_get_buffer(target, 32);
        AVFrame* out = nullptr;
        bool hardware = false;
        const auto begin = Clock::now();
        const bool ok = decoder.decode(units[i].data(), units[i].size(), int64_t(i) * 166667, target, &out, hardware);
        const double ms = std::chrono::duration<double, std::milli>(Clock::now() - begin).count();
        decodeMs.push_back(ms);
        if (ok && out) {
            ++produced;
            AVFrame* clone = hardware ? av_frame_clone(out) : target;
            if (hardware) av_frame_free(&target);
            held.emplace_back(clone, [](AVFrame* f) { av_frame_free(&f); });
            while (held.size() > 4) held.pop_front();
        } else {
            av_frame_free(&target);
        }
    }
    const double wall = std::chrono::duration<double>(Clock::now() - start).count();
    held.clear();
    std::vector<double> sorted = decodeMs;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0;
    for (double v : decodeMs) sum += v;
    const auto pct = [&](double p) { return sorted[std::min(sorted.size() - 1, size_t(p * double(sorted.size())))]; };
    std::printf("frames=%zu produced=%zu wall=%.2fs throughput=%.1f fps late=%zu\n", limit, produced, wall, double(limit) / wall, late);
    std::printf("decode ms: avg=%.2f p50=%.2f p95=%.2f p99=%.2f max=%.2f\n", sum / double(decodeMs.size()), pct(0.5), pct(0.95), pct(0.99), sorted.back());
    int over = 0;
    for (double v : decodeMs) if (v > 1000.0 / fps) ++over;
    std::printf("decodes slower than one frame interval: %d\n", over);
    std::printf("final backend=%s fallbacks=%llu failures=%llu\n", decoder.backendName(),
                (unsigned long long)decoder.hardwareFallbacks(), (unsigned long long)decoder.decodeFailures());
    return 0;
}
