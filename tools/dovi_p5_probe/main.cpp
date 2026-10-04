// veyra_dovi_p5_probe — diagnostic harness for the Dolby Vision profile-5
// base-layer conversion (custom addition; diagnostic only, never linked into
// the player).
//
// Why it exists: the profile-5 conversion lives in a compute shader, so "the
// log says the right constants were packed" is not evidence that the picture is
// right. This probe runs the *shipped* YuvToLinearRgb.dxil over one decoded
// frame with the same root constants the player packs and writes the raw
// working signal out, so the GPU result can be diffed against an independent
// reference decode (ffmpeg -vf libplacebo=apply_dolbyvision=1) or against a
// host-side implementation of the same maths.
//
// Output: RGBA16F, linear BT.709 scRGB (1.0 = 80 nits), row-tight, i.e. exactly
// what EnhanceGraph::convertInput leaves in the working texture when the scRGB
// output bit is set. That bit is set here on purpose: it skips tone mapping, so
// the comparison measures the colour conversion and nothing else.
//
//   veyra_dovi_p5_probe --input <file> --time <seconds> --out <rgba16f.raw>
//                       [--shader <dir>] [--no-dovi] [--width <px>]
//
// --no-dovi clears the enable flag, which turns the shader into the previous
// behaviour (base layer read as BT.2020 YCbCr). That is the control run: it
// must reproduce the green reference, otherwise the harness is measuring
// something other than the conversion.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d12.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <bit>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "veyra/pipeline/ColorGradeTables.h"
#include "veyra/pipeline/DolbyVisionP5.h"
#include "veyra/source/DolbyVisionRpu.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/dovi_meta.h>
#include <libavutil/pixdesc.h>
}

using Microsoft::WRL::ComPtr;

namespace {

constexpr int kConstants = 12 + veyra::pipeline::kColorGradeConstantCount +
                           veyra::pipeline::kDolbyVisionP5ConstantCount;

void report(const char* what, HRESULT hr) {
    std::fprintf(stderr, "dovi-p5-probe: %s failed hr=0x%08X\n", what, unsigned(hr));
}

float bitsToFloat(uint32_t bits) { return std::bit_cast<float>(bits); }

// Windows hands a char** main the command line in the ANSI code page, while
// FFmpeg wants UTF-8. Round-trip through UTF-16 and check the filesystem so a
// non-ASCII media path still opens (the profile-5 sample lives in a Chinese
// folder name).
std::string pathForFfmpeg(const std::string& arg) {
    auto toWide = [](const std::string& s, UINT codePage) {
        const int n = MultiByteToWideChar(codePage, 0, s.c_str(), int(s.size()), nullptr, 0);
        std::wstring w(n > 0 ? size_t(n) : 0, L'\0');
        if (n > 0) MultiByteToWideChar(codePage, 0, s.c_str(), int(s.size()), w.data(), n);
        return w;
    };
    auto toUtf8 = [](const std::wstring& w) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
        std::string s(n > 0 ? size_t(n) : 0, '\0');
        if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), s.data(), n, nullptr, nullptr);
        return s;
    };
    const std::wstring fromAcp = toWide(arg, CP_ACP);
    if (!fromAcp.empty() && GetFileAttributesW(fromAcp.c_str()) != INVALID_FILE_ATTRIBUTES) return toUtf8(fromAcp);
    const std::wstring fromUtf8 = toWide(arg, CP_UTF8);
    if (!fromUtf8.empty() && GetFileAttributesW(fromUtf8.c_str()) != INVALID_FILE_ATTRIBUTES) return toUtf8(fromUtf8);
    return arg;
}

// --- decode one frame -------------------------------------------------------
struct DecodedFrame {
    AVFrame* frame = nullptr;
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    double pts = 0.0;
    bool dovi = false;
    veyra::pipeline::DolbyVisionP5 params;
    veyra::source::DolbyVisionP5Build build;
    ~DecodedFrame() {
        if (frame) av_frame_free(&frame);
        if (codec) avcodec_free_context(&codec);
        if (format) avformat_close_input(&format);
    }
};

bool decodeFrame(const std::string& path, double seconds, DecodedFrame& out) {
    if (avformat_open_input(&out.format, path.c_str(), nullptr, nullptr) < 0) {
        std::fprintf(stderr, "dovi-p5-probe: cannot open %s\n", path.c_str());
        return false;
    }
    if (avformat_find_stream_info(out.format, nullptr) < 0) return false;
    const int streamIndex = av_find_best_stream(out.format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) { std::fprintf(stderr, "dovi-p5-probe: no video stream\n"); return false; }
    AVStream* stream = out.format->streams[streamIndex];
    const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
    if (decoder == nullptr) return false;
    out.codec = avcodec_alloc_context3(decoder);
    if (out.codec == nullptr || avcodec_parameters_to_context(out.codec, stream->codecpar) < 0) return false;
    out.codec->thread_count = 0;
    if (avcodec_open2(out.codec, decoder, nullptr) < 0) return false;

    const int64_t target = int64_t(seconds / av_q2d(stream->time_base));
    if (av_seek_frame(out.format, streamIndex, target, AVSEEK_FLAG_BACKWARD) < 0) {
        std::fprintf(stderr, "dovi-p5-probe: seek failed\n");
        return false;
    }
    avcodec_flush_buffers(out.codec);

    AVPacket* packet = av_packet_alloc();
    bool done = false;
    while (!done && av_read_frame(out.format, packet) >= 0) {
        if (packet->stream_index != streamIndex) { av_packet_unref(packet); continue; }
        if (avcodec_send_packet(out.codec, packet) < 0) { av_packet_unref(packet); continue; }
        av_packet_unref(packet);
        for (;;) {
            AVFrame* frame = av_frame_alloc();
            const int got = avcodec_receive_frame(out.codec, frame);
            if (got == AVERROR(EAGAIN)) { av_frame_free(&frame); break; }
            if (got < 0) { av_frame_free(&frame); done = true; break; }
            const double pts = frame->pts == AV_NOPTS_VALUE ? 0.0 : frame->pts * av_q2d(stream->time_base);
            if (pts + 1e-6 >= seconds) {
                out.frame = frame;
                out.pts = pts;
                done = true;
                break;
            }
            av_frame_free(&frame);
        }
    }
    av_packet_free(&packet);
    if (out.frame == nullptr) { std::fprintf(stderr, "dovi-p5-probe: no frame at %.3fs\n", seconds); return false; }
    if (out.frame->format != AV_PIX_FMT_YUV420P10LE) {
        std::fprintf(stderr, "dovi-p5-probe: expected yuv420p10le, got %s\n",
                     av_get_pix_fmt_name(static_cast<AVPixelFormat>(out.frame->format)));
        return false;
    }
    const AVFrameSideData* side = av_frame_get_side_data(out.frame, AV_FRAME_DATA_DOVI_METADATA);
    if (side != nullptr && side->size >= sizeof(AVDOVIMetadata)) {
        const auto* meta = reinterpret_cast<const AVDOVIMetadata*>(side->data);
        out.build = veyra::source::buildDolbyVisionP5(*meta, out.params);
        out.dovi = out.build.ok;
    }
    return true;
}

// The build directory can itself sit on a non-ASCII path, and VEYRA_SHADER_DIR
// is a UTF-8 literal under /utf-8, so open through UTF-16 rather than ANSI.
HANDLE openReadUtf8(const std::string& path) {
    const int n = MultiByteToWideChar(CP_UTF8, 0, path.c_str(), int(path.size()), nullptr, 0);
    std::wstring wide(n > 0 ? size_t(n) : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, path.c_str(), int(path.size()), wide.data(), n);
    return CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
}

// --- D3D12 ------------------------------------------------------------------
struct Gpu {
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12CommandAllocator> allocator;
    ComPtr<ID3D12GraphicsCommandList> list;
    ComPtr<ID3D12Fence> fence;
    HANDLE event = nullptr;
    uint64_t fenceValue = 1;
};

bool createList(Gpu& gpu) {
    if (FAILED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, IID_PPV_ARGS(&gpu.device)))) return false;
    D3D12_COMMAND_QUEUE_DESC queueDesc{};
    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    if (FAILED(gpu.device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&gpu.queue)))) return false;
    if (FAILED(gpu.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&gpu.allocator)))) return false;
    if (FAILED(gpu.device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, gpu.allocator.Get(), nullptr,
                                             IID_PPV_ARGS(&gpu.list)))) return false;
    if (FAILED(gpu.device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&gpu.fence)))) return false;
    gpu.event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    return gpu.event != nullptr;
}

bool submitAndWait(Gpu& gpu) {
    if (FAILED(gpu.list->Close())) { report("Close", E_FAIL); return false; }
    ID3D12CommandList* lists[] = { gpu.list.Get() };
    gpu.queue->ExecuteCommandLists(1, lists);
    if (FAILED(gpu.queue->Signal(gpu.fence.Get(), gpu.fenceValue))) return false;
    if (gpu.fence->GetCompletedValue() < gpu.fenceValue) {
        if (FAILED(gpu.fence->SetEventOnCompletion(gpu.fenceValue, gpu.event))) return false;
        WaitForSingleObject(gpu.event, 60000);
    }
    ++gpu.fenceValue;
    return true;
}

uint64_t aligned(uint64_t value, uint64_t alignment) { return (value + alignment - 1) / alignment * alignment; }

// Lays out one plane in an upload buffer using the standard copyable footprint.
struct Upload {
    ComPtr<ID3D12Resource> resource;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    uint8_t* mapped = nullptr;
};

bool makeUpload(Gpu& gpu, const D3D12_RESOURCE_DESC& desc, Upload& out) {
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};
    UINT rows = 0;
    uint64_t rowSize = 0, total = 0;
    gpu.device->GetCopyableFootprints(&desc, 0, 1, 0, &layout, &rows, &rowSize, &total);
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC buffer{};
    buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = total;
    buffer.Height = 1;
    buffer.DepthOrArraySize = 1;
    buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1;
    buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&out.resource)))) return false;
    if (FAILED(out.resource->Map(0, nullptr, reinterpret_cast<void**>(&out.mapped)))) return false;
    out.footprint = layout;
    return true;
}

bool makeTexture(Gpu& gpu, uint32_t width, uint32_t height, DXGI_FORMAT format,
                 D3D12_RESOURCE_FLAGS flags, ComPtr<ID3D12Resource>& out) {
    D3D12_HEAP_PROPERTIES heap{};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Flags = flags;
    return SUCCEEDED(gpu.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&out)));
}

void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* resource,
             D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER b{};
    b.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition.pResource = resource;
    b.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    b.Transition.StateBefore = before;
    b.Transition.StateAfter = after;
    list->ResourceBarrier(1, &b);
}

} // namespace

// --- RPU scan and playback-history comparison (no GPU work) ------------------
// Two review questions need evidence that does not go through the shader:
// does every frame of the sample carry usable conversion parameters, and does a
// flat (placeholder) frame convert the same way no matter how playback reached
// it (fresh open, sequential playback, or a backward seek into the opening).

struct Decoder {
    AVFormatContext* format = nullptr;
    AVCodecContext* codec = nullptr;
    AVStream* stream = nullptr;
    int index = -1;
    bool flushed = false;
    ~Decoder() {
        if (codec) avcodec_free_context(&codec);
        if (format) avformat_close_input(&format);
    }
    bool open(const std::string& path) {
        if (avformat_open_input(&format, path.c_str(), nullptr, nullptr) < 0) return false;
        if (avformat_find_stream_info(format, nullptr) < 0) return false;
        index = av_find_best_stream(format, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (index < 0) return false;
        stream = format->streams[index];
        const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (decoder == nullptr) return false;
        codec = avcodec_alloc_context3(decoder);
        if (codec == nullptr || avcodec_parameters_to_context(codec, stream->codecpar) < 0) return false;
        codec->thread_count = 0;
        return avcodec_open2(codec, decoder, nullptr) >= 0;
    }
    bool seekTo(double seconds) {
        const int64_t stamp = int64_t(seconds / av_q2d(stream->time_base));
        if (av_seek_frame(format, index, stamp, AVSEEK_FLAG_BACKWARD) < 0) return false;
        avcodec_flush_buffers(codec);
        flushed = false;
        return true;
    }
    double ptsOf(const AVFrame* frame) const {
        return frame->pts == AV_NOPTS_VALUE ? 0.0 : frame->pts * av_q2d(stream->time_base);
    }
    // Next decoded video frame (caller frees) or nullptr at EOF.
    AVFrame* nextFrame() {
        for (;;) {
            AVFrame* frame = av_frame_alloc();
            const int got = avcodec_receive_frame(codec, frame);
            if (got == 0) return frame;
            av_frame_free(&frame);
            if (got != AVERROR(EAGAIN)) return nullptr;
            if (flushed) return nullptr;
            AVPacket* packet = av_packet_alloc();
            int read = av_read_frame(format, packet);
            while (read >= 0 && packet->stream_index != index) {
                av_packet_unref(packet);
                read = av_read_frame(format, packet);
            }
            if (read < 0) {
                av_packet_free(&packet);
                avcodec_send_packet(codec, nullptr); // drain
                flushed = true;
                continue;
            }
            avcodec_send_packet(codec, packet);
            av_packet_free(&packet);
        }
    }
};

struct RpuFrame {
    double pts = 0.0;
    bool hasSide = false;
    bool usable = false;
    veyra::pipeline::DolbyVisionP5 params;
    veyra::source::DolbyVisionP5Build build;
};

bool readRpu(const AVFrame* frame, RpuFrame& out) {
    const AVFrameSideData* side = av_frame_get_side_data(frame, AV_FRAME_DATA_DOVI_METADATA);
    out.hasSide = side != nullptr && side->size >= sizeof(AVDOVIMetadata);
    if (!out.hasSide) return false;
    out.build = veyra::source::buildDolbyVisionP5(
        *reinterpret_cast<const AVDOVIMetadata*>(side->data), out.params);
    out.usable = out.build.usable;
    return true;
}

void printRpu(const char* tag, const RpuFrame& rpu) {
    std::printf("%s pts=%.3f side=%d usable=%d active=%d missing=%d unsupported=%d placeholder=%d "
                "curve0=[%.6f,%.6f,%.6f] curve1=[%.6f,%.6f,%.6f] curve2=[%.6f,%.6f,%.6f]\n",
                tag, rpu.pts, rpu.hasSide ? 1 : 0, rpu.usable ? 1 : 0, rpu.params.active ? 1 : 0,
                rpu.build.colorMetadataMissing ? 1 : 0, rpu.build.unsupportedMapping ? 1 : 0,
                rpu.build.placeholderMapping ? 1 : 0,
                rpu.params.curve[0][0], rpu.params.curve[0][1], rpu.params.curve[0][2],
                rpu.params.curve[1][0], rpu.params.curve[1][1], rpu.params.curve[1][2],
                rpu.params.curve[2][0], rpu.params.curve[2][1], rpu.params.curve[2][2]);
}

// Per-frame RPU report for `count` frames from `seconds`. With `sequential` the
// file is decoded from the start instead of seeking, which is the other half of
// the history-independence check.
bool scanRpu(const std::string& path, double seconds, int count, bool sequential) {
    Decoder decoder;
    if (!decoder.open(path)) { std::fprintf(stderr, "dovi-p5-probe: cannot open %s\n", path.c_str()); return false; }
    if (!sequential && !decoder.seekTo(seconds)) { std::fprintf(stderr, "dovi-p5-probe: seek failed\n"); return false; }
    int seen = 0, usable = 0, noSide = 0, missing = 0, unsupported = 0, placeholder = 0;
    while (seen < count) {
        AVFrame* frame = decoder.nextFrame();
        if (frame == nullptr) break;
        RpuFrame rpu;
        rpu.pts = decoder.ptsOf(frame);
        if (sequential && rpu.pts + 1e-6 < seconds) { av_frame_free(&frame); continue; }
        readRpu(frame, rpu);
        ++seen;
        if (!rpu.hasSide) ++noSide;
        if (rpu.build.colorMetadataMissing) ++missing;
        if (rpu.build.unsupportedMapping) ++unsupported;
        if (rpu.build.placeholderMapping) ++placeholder;
        if (rpu.usable) ++usable;
        printRpu("scan", rpu);
        av_frame_free(&frame);
    }
    std::printf("scan summary frames=%d usable=%d noSideData=%d missingColour=%d unsupported=%d placeholder=%d\n",
                seen, usable, noSide, missing, unsupported, placeholder);
    return seen > 0;
}

// Reaches the frame at `target` three ways and reports whether the conversion
// parameters are identical, i.e. independent of playback history.
bool compareHistory(const std::string& path, double target, double later) {
    RpuFrame reached[3];
    const char* names[3] = {"fresh-seek", "sequential", "seek-back"};
    for (int mode = 0; mode < 3; ++mode) {
        Decoder decoder;
        if (!decoder.open(path)) { std::fprintf(stderr, "dovi-p5-probe: cannot open %s\n", path.c_str()); return false; }
        if (mode == 2) {
            // Play a later part first, then seek back to the target.
            if (!decoder.seekTo(later)) return false;
            for (int warm = 0; warm < 8; ++warm) {
                AVFrame* frame = decoder.nextFrame();
                if (frame == nullptr) break;
                av_frame_free(&frame);
            }
        }
        if (mode != 1 && !decoder.seekTo(target)) return false;
        for (;;) {
            AVFrame* frame = decoder.nextFrame();
            if (frame == nullptr) break;
            if (decoder.ptsOf(frame) + 1e-6 >= target) {
                reached[mode].pts = decoder.ptsOf(frame);
                readRpu(frame, reached[mode]);
                av_frame_free(&frame);
                break;
            }
            av_frame_free(&frame);
        }
        printRpu(names[mode], reached[mode]);
    }
    bool same = reached[0].hasSide == reached[1].hasSide && reached[1].hasSide == reached[2].hasSide &&
                reached[0].params.active == reached[1].params.active &&
                reached[1].params.active == reached[2].params.active;
    for (int i = 0; same && i < 9; ++i)
        same = reached[0].params.yccToRgb[i] == reached[1].params.yccToRgb[i] &&
               reached[1].params.yccToRgb[i] == reached[2].params.yccToRgb[i] &&
               reached[0].params.lmsToRgb[i] == reached[1].params.lmsToRgb[i] &&
               reached[1].params.lmsToRgb[i] == reached[2].params.lmsToRgb[i];
    for (int c = 0; same && c < 3; ++c)
        for (int k = 0; k < 3; ++k)
            same = reached[0].params.curve[c][k] == reached[1].params.curve[c][k] &&
                   reached[1].params.curve[c][k] == reached[2].params.curve[c][k];
    std::printf("history independent=%d target=%.3f later=%.3f\n", same ? 1 : 0, target, later);
    return same;
}

int main(int argc, char** argv) {
    std::string input, output, shaderDir = VEYRA_SHADER_DIR;
    double seconds = 90.0;
    double later = 0.0;
    int scan = 0;
    bool sequential = false, history = false;
    bool useDovi = true;
    bool sdrOutput = false;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--input" && hasValue) input = argv[++i];
        else if (arg == "--out" && hasValue) output = argv[++i];
        else if (arg == "--shader" && hasValue) shaderDir = argv[++i];
        else if (arg == "--time" && hasValue) seconds = std::atof(argv[++i]);
        else if (arg == "--scan" && hasValue) scan = std::atoi(argv[++i]);
        else if (arg == "--sequential") sequential = true;
        else if (arg == "--history") history = true;
        else if (arg == "--later" && hasValue) later = std::atof(argv[++i]);
        else if (arg == "--no-dovi") useDovi = false;
        else if (arg == "--sdr") sdrOutput = true;
        else { std::fprintf(stderr, "dovi-p5-probe: unknown argument %s\n", arg.c_str()); return 2; }
    }
    if (input.empty()) {
        std::fprintf(stderr,
                     "usage: veyra_dovi_p5_probe --input <file> --time <s> --out <raw> [--shader dir] [--no-dovi] [--sdr]\n"
                     "       veyra_dovi_p5_probe --input <file> --time <s> --scan <frames> [--sequential]\n"
                     "       veyra_dovi_p5_probe --input <file> --time <s> --history [--later <s>]\n");
        return 2;
    }
    input = pathForFfmpeg(input);
    if (scan > 0) return scanRpu(input, seconds, scan, sequential) ? 0 : 3;
    if (history) return compareHistory(input, seconds, later > 0.0 ? later : seconds + 600.0) ? 0 : 3;
    if (output.empty()) {
        std::fprintf(stderr, "dovi-p5-probe: --out is required unless --scan/--history is used\n");
        return 2;
    }
    shaderDir = pathForFfmpeg(shaderDir); // the build directory may sit on a non-ASCII path

    DecodedFrame decoded;
    if (!decodeFrame(input, seconds, decoded)) return 3;
    AVFrame* frame = decoded.frame;
    const uint32_t width = uint32_t(frame->width), height = uint32_t(frame->height);
    std::printf("frame pts=%.3f %ux%u dovi=%d", decoded.pts, width, height, decoded.dovi ? 1 : 0);
    if (decoded.dovi) {
        const auto& p = decoded.params;
        std::printf(" ycc=[%.6f,%.6f,%.6f|%.6f,%.6f,%.6f|%.6f,%.6f,%.6f]",
                    p.yccToRgb[0], p.yccToRgb[1], p.yccToRgb[2], p.yccToRgb[3], p.yccToRgb[4],
                    p.yccToRgb[5], p.yccToRgb[6], p.yccToRgb[7], p.yccToRgb[8]);
        std::printf(" lms=[%.6f,%.6f,%.6f|%.6f,%.6f,%.6f|%.6f,%.6f,%.6f]",
                    p.lmsToRgb[0], p.lmsToRgb[1], p.lmsToRgb[2], p.lmsToRgb[3], p.lmsToRgb[4],
                    p.lmsToRgb[5], p.lmsToRgb[6], p.lmsToRgb[7], p.lmsToRgb[8]);
        std::printf(" curve0=[%.6f,%.6f,%.6f] curve1=[%.6f,%.6f,%.6f] curve2=[%.6f,%.6f,%.6f]",
                    p.curve[0][0], p.curve[0][1], p.curve[0][2], p.curve[1][0], p.curve[1][1],
                    p.curve[1][2], p.curve[2][0], p.curve[2][1], p.curve[2][2]);
        if (decoded.build.placeholderMapping) std::printf(" placeholder=1");
    }
    std::printf("\n");

    // Root constants exactly as EnhanceGraph::convertInput packs them for a
    // profile-5 file with the scRGB working output.
    std::vector<float> constants(kConstants, 0.0f);
    constants[0] = 0.0f;                       // full range: IPT-PQ-C2 is full range
    constants[1] = 2.0f;                       // BT.2020 non-constant luminance
    constants[2] = 4.0f;                       // ST2084
    constants[3] = 1.0f;                       // ten-bit sample selector (P010-style)
    constants[4] = bitsToFloat(width);
    constants[5] = bitsToFloat(height);
    constants[6] = bitsToFloat(sdrOutput ? 2u : (1u | 2u));
    constants[7] = bitsToFloat(0u);            // chroma location 0 -> point sampling
    constants[8] = 1000.0f;                    // tone-map peak (unused with scRGB)
    constants[9] = 203.0f;
    if (useDovi && decoded.dovi)
        veyra::pipeline::packDolbyVisionP5Constants(decoded.params, constants.data() + 12 +
                                                    veyra::pipeline::kColorGradeConstantCount);
    std::printf("dv_enable=%.0f\n", constants[12 + veyra::pipeline::kColorGradeConstantCount + 27]);

    // Shader.
    std::string shaderPath = shaderDir + "/YuvToLinearRgb.dxil";
    HANDLE file = openReadUtf8(shaderPath);
    if (file == INVALID_HANDLE_VALUE) { std::fprintf(stderr, "dovi-p5-probe: shader missing %s\n", shaderPath.c_str()); return 4; }
    LARGE_INTEGER shaderSize{};
    GetFileSizeEx(file, &shaderSize);
    std::vector<uint8_t> shaderBytes(size_t(shaderSize.QuadPart));
    DWORD read = 0;
    ReadFile(file, shaderBytes.data(), DWORD(shaderBytes.size()), &read, nullptr);
    CloseHandle(file);
    std::printf("shader=%s bytes=%zu\n", shaderPath.c_str(), shaderBytes.size());

    Gpu gpu;
    if (!createList(gpu)) { std::fprintf(stderr, "dovi-p5-probe: device/queue creation failed\n"); return 5; }

    D3D12_DESCRIPTOR_RANGE1 srvRange{};
    srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    srvRange.NumDescriptors = 3;
    srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    D3D12_DESCRIPTOR_RANGE1 uavRange{};
    uavRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    uavRange.NumDescriptors = 1;
    uavRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    // YuvToLinearRgb.hlsl also declares the colour-grade tables at t8..t11
    // (curve, hue, luma, 3D LUT). Grading is disabled for this probe, but the
    // registers still have to exist or the PSO refuses to build.
    D3D12_DESCRIPTOR_RANGE1 gradeRange{};
    gradeRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    gradeRange.NumDescriptors = 4;
    gradeRange.BaseShaderRegister = 8;
    gradeRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE;
    D3D12_ROOT_PARAMETER1 params[4]{};
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[0].Constants.ShaderRegister = 0;
    params[0].Constants.Num32BitValues = UINT(kConstants);
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &srvRange;
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[2].DescriptorTable.NumDescriptorRanges = 1;
    params[2].DescriptorTable.pDescriptorRanges = &uavRange;
    params[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    params[3].DescriptorTable.NumDescriptorRanges = 1;
    params[3].DescriptorTable.pDescriptorRanges = &gradeRange;
    D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootDesc{};
    rootDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
    rootDesc.Desc_1_1.NumParameters = 4;
    rootDesc.Desc_1_1.pParameters = params;
    ComPtr<ID3DBlob> signature, signatureError;
    if (FAILED(D3D12SerializeVersionedRootSignature(&rootDesc, &signature, &signatureError))) {
        std::fprintf(stderr, "dovi-p5-probe: root signature serialize failed: %s\n", signatureError ?
            static_cast<const char*>(signatureError->GetBufferPointer()) : "");
        return 6;
    }
    ComPtr<ID3D12RootSignature> rootSignature;
    const HRESULT rootHr = gpu.device->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(),
            IID_PPV_ARGS(&rootSignature));
    if (FAILED(rootHr)) { report("CreateRootSignature", rootHr); return 6; }
    D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc{};
    psoDesc.pRootSignature = rootSignature.Get();
    psoDesc.CS.pShaderBytecode = shaderBytes.data();
    psoDesc.CS.BytecodeLength = shaderBytes.size();
    ComPtr<ID3D12PipelineState> pso;
    const HRESULT psoHr = gpu.device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&pso));
    if (FAILED(psoHr)) { report("CreateComputePipelineState", psoHr); return 6; }

    D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
    heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    heapDesc.NumDescriptors = 8;
    heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> heap;
    if (FAILED(gpu.device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&heap)))) return 7;
    const UINT increment = gpu.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu = heap->GetCPUDescriptorHandleForHeapStart();
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle = heap->GetGPUDescriptorHandleForHeapStart();

    ComPtr<ID3D12Resource> luma, chroma, target, readback;
    if (!makeTexture(gpu, width, height, DXGI_FORMAT_R16_UNORM, D3D12_RESOURCE_FLAG_NONE, luma)) return 7;
    if (!makeTexture(gpu, width / 2, height / 2, DXGI_FORMAT_R16G16_UNORM, D3D12_RESOURCE_FLAG_NONE, chroma)) return 7;
    if (!makeTexture(gpu, width, height, DXGI_FORMAT_R16G16B16A16_FLOAT,
                     D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, target)) return 7;

    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    srv.Format = DXGI_FORMAT_R16_UNORM;
    gpu.device->CreateShaderResourceView(luma.Get(), &srv, cpu);
    D3D12_CPU_DESCRIPTOR_HANDLE cpu1{ cpu.ptr + increment }, cpu2{ cpu.ptr + 2ull * increment };
    srv.Format = DXGI_FORMAT_R16G16_UNORM;
    gpu.device->CreateShaderResourceView(chroma.Get(), &srv, cpu1);
    // The shader declares a reserved second chroma view at t2; keep the slot
    // holding a valid descriptor even though nothing samples it.
    srv.Format = DXGI_FORMAT_R16_UNORM;
    gpu.device->CreateShaderResourceView(luma.Get(), &srv, cpu2);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uav{};
    uav.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    uav.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu{ cpu.ptr + 3ull * increment };
    gpu.device->CreateUnorderedAccessView(target.Get(), nullptr, &uav, uavCpu);
    // Placeholder descriptors for the colour-grade table (slots 4..7 = t8..t11);
    // grading is off, so nothing samples them.
    srv.Format = DXGI_FORMAT_R16_UNORM;
    for (UINT slot = 4; slot < 8; ++slot) {
        D3D12_CPU_DESCRIPTOR_HANDLE handle{ cpu.ptr + size_t(slot) * increment };
        gpu.device->CreateShaderResourceView(luma.Get(), &srv, handle);
    }

    // Upload the planes. The shader's ten-bit path reads R16_UNORM and rescales
    // with 65535/64, so a 10-bit code must arrive in the P010 position (<<6),
    // which is exactly what a hardware P010 surface gives the player.
    Upload lumaUpload, chromaUpload;
    D3D12_RESOURCE_DESC lumaDesc = luma->GetDesc(), chromaDesc = chroma->GetDesc();
    if (!makeUpload(gpu, lumaDesc, lumaUpload) || !makeUpload(gpu, chromaDesc, chromaUpload)) return 8;
    for (uint32_t y = 0; y < height; ++y) {
        const uint16_t* src = reinterpret_cast<const uint16_t*>(frame->data[0] + ptrdiff_t(y) * frame->linesize[0]);
        uint16_t* dst = reinterpret_cast<uint16_t*>(lumaUpload.mapped + size_t(y) * lumaUpload.footprint.Footprint.RowPitch);
        for (uint32_t x = 0; x < width; ++x) dst[x] = uint16_t(src[x] << 6);
    }
    // Chroma must be interleaved the way a P010 surface presents it: the
    // decoder hands the planes separately (Cb, then Cr), the shader samples one
    // R16G16 texel per chroma pair.
    for (uint32_t y = 0; y < height / 2; ++y) {
        const uint16_t* cb = reinterpret_cast<const uint16_t*>(frame->data[1] + ptrdiff_t(y) * frame->linesize[1]);
        const uint16_t* cr = reinterpret_cast<const uint16_t*>(frame->data[2] + ptrdiff_t(y) * frame->linesize[2]);
        uint16_t* dst = reinterpret_cast<uint16_t*>(chromaUpload.mapped + size_t(y) * chromaUpload.footprint.Footprint.RowPitch);
        for (uint32_t x = 0; x < width / 2; ++x) {
            dst[2 * x] = uint16_t(cb[x] << 6);
            dst[2 * x + 1] = uint16_t(cr[x] << 6);
        }
    }

    // Readback for the working texture.
    D3D12_RESOURCE_DESC targetDesc = target->GetDesc();
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT targetLayout{};
    UINT targetRows = 0;
    uint64_t targetRowSize = 0, targetTotal = 0;
    gpu.device->GetCopyableFootprints(&targetDesc, 0, 1, 0, &targetLayout, &targetRows, &targetRowSize, &targetTotal);
    D3D12_HEAP_PROPERTIES readbackHeap{};
    readbackHeap.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC readbackDesc{};
    readbackDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    readbackDesc.Width = targetTotal;
    readbackDesc.Height = 1;
    readbackDesc.DepthOrArraySize = 1;
    readbackDesc.MipLevels = 1;
    readbackDesc.SampleDesc.Count = 1;
    readbackDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    if (FAILED(gpu.device->CreateCommittedResource(&readbackHeap, D3D12_HEAP_FLAG_NONE, &readbackDesc,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return 8;

    ID3D12GraphicsCommandList* list = gpu.list.Get();
    barrier(list, luma.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    barrier(list, chroma.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    dst.pResource = luma.Get(); src.pResource = lumaUpload.resource.Get(); src.PlacedFootprint = lumaUpload.footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    dst.pResource = chroma.Get(); src.pResource = chromaUpload.resource.Get(); src.PlacedFootprint = chromaUpload.footprint;
    list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
    barrier(list, luma.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barrier(list, chroma.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    barrier(list, target.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    ID3D12DescriptorHeap* heaps[] = { heap.Get() };
    list->SetDescriptorHeaps(1, heaps);
    list->SetComputeRootSignature(rootSignature.Get());
    list->SetPipelineState(pso.Get());
    list->SetComputeRoot32BitConstants(0, UINT(kConstants), constants.data(), 0);
    list->SetComputeRootDescriptorTable(1, gpuHandle);
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpu{ gpuHandle.ptr + 3ull * increment };
    list->SetComputeRootDescriptorTable(2, uavGpu);
    D3D12_GPU_DESCRIPTOR_HANDLE gradeGpu{ gpuHandle.ptr + 4ull * increment };
    list->SetComputeRootDescriptorTable(3, gradeGpu);
    list->Dispatch((width + 15) / 16, (height + 15) / 16, 1);

    barrier(list, target.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    D3D12_TEXTURE_COPY_LOCATION back{};
    back.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    back.pResource = readback.Get();
    back.PlacedFootprint = targetLayout;
    D3D12_TEXTURE_COPY_LOCATION from{};
    from.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    from.pResource = target.Get();
    list->CopyTextureRegion(&back, 0, 0, 0, &from, nullptr);
    if (!submitAndWait(gpu)) { std::fprintf(stderr, "dovi-p5-probe: submit failed\n"); return 9; }

    void* mapped = nullptr;
    if (FAILED(readback->Map(0, nullptr, &mapped))) return 9;
    FILE* out = std::fopen(output.c_str(), "wb");
    if (out == nullptr) { std::fprintf(stderr, "dovi-p5-probe: cannot write %s\n", output.c_str()); return 9; }
    const size_t rowBytes = size_t(width) * 8;
    for (uint32_t y = 0; y < height; ++y)
        std::fwrite(static_cast<const uint8_t*>(mapped) + size_t(y) * targetLayout.Footprint.RowPitch, 1, rowBytes, out);
    std::fclose(out);
    readback->Unmap(0, nullptr);
    std::printf("wrote %s (%ux%u RGBA16F scRGB linear BT.709, 1.0=80 nits)\n", output.c_str(), width, height);
    return 0;
}
