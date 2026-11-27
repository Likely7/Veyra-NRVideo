#pragma once

#include "veyra/media/FFmpegVideoDecoder.h"
#include "veyra/pipeline/ColorMetadata.h"
#include <array>
#include <filesystem>
#include <memory>
extern "C" {
#include <libavformat/avformat.h>
}

inline bool d3d11FrameLeaseFixture(const wchar_t* inputPath, const std::wstring& outputPrefix,
                                   veyra::gfx::D3D12DeviceContext& ctx, veyra::gfx::CommandSlotRing& ring) {
    using namespace veyra;
    using media::DecodeReceiveStatus;
    AVFormatContext* raw = nullptr;
    const auto path = std::filesystem::path(inputPath).u8string();
    if (avformat_open_input(&raw, reinterpret_cast<const char*>(path.c_str()), nullptr, nullptr) < 0) return false;
    const auto freeInput = [](AVFormatContext* format) { avformat_close_input(&format); };
    std::unique_ptr<AVFormatContext, decltype(freeInput)> input(raw, freeInput);
    if (avformat_find_stream_info(input.get(), nullptr) < 0) return false;
    const int streamIndex = av_find_best_stream(input.get(), AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (streamIndex < 0) return false;
    const auto* stream = input->streams[streamIndex];
    if (stream->codecpar->codec_id != AV_CODEC_ID_H264) return false;

    media::FFmpegVideoDecoder decoder;
    const auto open = [&] {
        return decoder.openD3D11VA(stream->codecpar, stream->time_base.num, stream->time_base.den,
            ctx.adapter().luid, ctx.device(), true, media::D3D11SurfaceOwnership::RetainedByFrame);
    };
    if (!open()) return false;
    const auto freePacket = [](AVPacket* packet) { av_packet_free(&packet); };
    std::unique_ptr<AVPacket, decltype(freePacket)> packet(av_packet_alloc(), freePacket);
    if (!packet) return false;
    const auto nextOutput = [&]() -> const AVFrame* {
        for (;;) {
            if (const auto* frame = decoder.receiveFrame()) return frame;
            if (decoder.receiveStatus() != DecodeReceiveStatus::NeedInput) return nullptr;
            if (av_read_frame(input.get(), packet.get()) < 0) return nullptr;
            const bool ok = packet->stream_index != streamIndex || decoder.sendPacket(packet.get());
            av_packet_unref(packet.get());
            if (!ok) return nullptr;
        }
    };

    struct HeldPicture {
        std::shared_ptr<AVFrame> frame;
        pipeline::HardwareSurfaceInput surface;
    };
    const auto retain = [&](const AVFrame* source) -> HeldPicture {
        if (!source || !source->opaque_ref || !decoder.hardwareFrameImportable()) return {};
        auto* frame = av_frame_clone(source);
        if (!frame) return {};
        const auto& view = decoder.hardwareSurface();
        return {std::shared_ptr<AVFrame>(frame, [](AVFrame* p) { av_frame_free(&p); }),
                {view.texture, view.subresourceIndex, view.waitFence, view.waitValue, view.textureWidth, view.textureHeight}};
    };
    std::array<HeldPicture, 8> held;
    for (auto& picture : held) {
        picture = retain(nextOutput());
        if (!picture.frame) return false;
    }

    pipeline::EnhanceGraph graph(ctx, ring);
    pipeline::EnhanceGraphDesc desc;
    desc.sourceWidth = desc.workWidth = unsigned(held[0].frame->width);
    desc.sourceHeight = desc.workHeight = unsigned(held[0].frame->height);
    desc.enableNr = false;
    desc.enableSr = false;
    desc.enableFg = false;
    if (!graph.initialize(desc) || !graph.createViews()) return false;
    unsigned sequence = 0;
    const auto submit = [&](const HeldPicture& picture, pipeline::EnhanceGraph::FrameOutputs& out) {
        auto color = pipeline::resolveFrameColor(*picture.frame);
        color.pixelFormat = pipeline::SourcePixelFormat::NV12;
        color.preserveSdrCodeValues = true;
        ++sequence;
        return graph.process(picture.frame.get(), sequence * 16.67, true, out, sequence, &color, &picture.surface);
    };
    const auto render = [&](const HeldPicture& picture, sink::RgbaImage& image) {
        pipeline::EnhanceGraph::FrameOutputs out;
        return submit(picture, out) && sink::readRgba8(ctx, ring, graph.videoFrameResource(out.videoSlot), image);
    };
    sink::RgbaImage first, second;
    if (!render(held[0], first) || !render(held[1], second) || first.pixels == second.pixels || !ring.drainQueue()) return false;

    gfx::ComPtr<ID3D12Fence> holdGpu;
    if (FAILED(ctx.device()->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&holdGpu))) ||
        FAILED(ctx.directQueue()->Wait(holdGpu.Get(), 1))) return false;
    struct ReleaseGpu {
        ID3D12Fence* gate;
        ~ReleaseGpu() { if (gate) gate->Signal(1); }
    } release{holdGpu.Get()};
    pipeline::EnhanceGraph::FrameOutputs queued;
    if (!submit(held[0], queued)) return false;
    const auto firstTexture = held[0].surface.texture;
    held[0] = {}; // The queue and graph now hold the first source frame.
    unsigned busy = 0;
    for (unsigned skip = 0; skip < 12; ++skip) {
        if (nextOutput() || decoder.receiveStatus() != DecodeReceiveStatus::SurfaceBusy) return false;
        ++busy;
    }
    if (FAILED(holdGpu->Signal(1))) return false;
    release.gate = nullptr;
    sink::RgbaImage afterPressure;
    if (!sink::readRgba8(ctx, ring, graph.videoFrameResource(queued.videoSlot), afterPressure) ||
        afterPressure.pixels != first.pixels) return false;
    queued = {};
    sink::RgbaImage recycled;
    if (!render(held[1], recycled) || !render(held[1], recycled)) return false;
    const auto recycling = retain(nextOutput());
    if (!recycling.frame || recycling.surface.texture != firstTexture) return false;

    decoder.close();
    if (av_seek_frame(input.get(), streamIndex, 0, AVSEEK_FLAG_BACKWARD) < 0 || !open()) return false;
    const auto nextGeneration = retain(nextOutput());
    if (!nextGeneration.frame || nextGeneration.surface.texture == held[1].surface.texture) return false;
    sink::RgbaImage afterReopen;
    const bool pass = render(held[1], afterReopen) && afterReopen.pixels == second.pixels &&
        sink::saveImage(outputPrefix + L"-held.png", afterPressure) &&
        sink::saveImage(outputPrefix + L"-old-generation.png", afterReopen);
    std::cout << "D3D11_FRAME_LEASE pass=" << pass << " pending_slots=8 skipped_without_reset=" << busy
              << " reused_after_gpu=1 old_generation_equal=" << (afterReopen.pixels == second.pixels) << '\n';
    return pass;
}
