#include "veyra/gfx/FsrFgPresenter.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/pipeline/GpuPassUtils.h"
#include <d3d12sdklayers.h>
#include <cmath>
#include <cstring>
#include <iostream>
#include <vector>

// Real GPU interpolation contract, with no window or swapchain. Test-only
// pixel readback proves a moving square reaches its temporal midpoint.
int wmain(int argc, wchar_t** argv) {
    using namespace veyra;
    constexpr unsigned W = 640, H = 360, pitch = W * 4;
    const bool amd = argc > 1 && std::wstring_view(argv[1]) == L"amd";
    gfx::D3D12DeviceContext ctx; gfx::CommandSlotRing ring; Status st = Status::Ok;
    gfx::DeviceContextDesc desc; desc.enableDebugLayer = true; desc.requiredVendorId = amd ? 0x1002 : 0;
    bool ok = ctx.initialize(desc, st) && ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 4, st);
    if (!ok) return 2;
    Microsoft::WRL::ComPtr<ID3D12InfoQueue> info;
    ctx.device()->QueryInterface(IID_PPV_ARGS(&info));
    auto color = pipeline::makeTexture(ctx.device(), W, H, DXGI_FORMAT_R8G8B8A8_UNORM, false);
    auto motion = pipeline::makeTexture(ctx.device(), W, H, DXGI_FORMAT_R16G16_FLOAT, false);
    auto depth = pipeline::makeTexture(ctx.device(), W, H, DXGI_FORMAT_R32_FLOAT, false);
    auto output = pipeline::makeTexture(ctx.device(), W, H, DXGI_FORMAT_R8G8B8A8_UNORM, true);
    auto upload = pipeline::makeUploadBuffer(ctx.device(), uint64_t(pitch) * H * 3);
    Microsoft::WRL::ComPtr<ID3D12Resource> readback;
    D3D12_RESOURCE_DESC buffer{}; buffer.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    buffer.Width = uint64_t(pitch) * H; buffer.Height = buffer.DepthOrArraySize = buffer.MipLevels = 1;
    buffer.SampleDesc.Count = 1; buffer.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_READBACK;
    ok = color && motion && depth && output && upload &&
        SUCCEEDED(ctx.device()->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &buffer,
            D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)));
    uint8_t* pixels = nullptr;
    ok = ok && SUCCEEDED(upload->Map(0, nullptr, reinterpret_cast<void**>(&pixels)));
    unsigned samples = 0, intermediate = 0;
    double errorSum = 0;
    for (unsigned cycle = 0; ok && cycle < 3; ++cycle) {
        gfx::FsrFgPresenter fg;
        ok = fg.initializeIndependent(ctx.device(), W, H, DXGI_FORMAT_R8G8B8A8_UNORM, false);
        for (unsigned frame = 0; ok && frame < 16; ++frame) {
            const unsigned left = 80 + frame * 16;
            for (unsigned y = 0; y < H; ++y) for (unsigned x = 0; x < W; ++x) {
                const size_t at = size_t(y) * pitch + x * 4;
                const uint8_t value = y >= 140 && y < 220 && x >= left && x < left + 80 ? 240 : 32;
                pixels[at] = pixels[at + 1] = pixels[at + 2] = value; pixels[at + 3] = 255;
                const uint16_t mv[2] = {0xcc00, 0}; // current->previous, -16 pixels
                std::memcpy(pixels + size_t(pitch) * H + at, mv, 4);
                const float z = 0.5f;
                std::memcpy(pixels + size_t(pitch) * H * 2 + at, &z, 4);
            }
            unsigned slot = 0; auto* list = ring.acquireNext(slot, st); if (!list) { ok = false; break; }
            pipeline::StateTracker states;
            ID3D12Resource* inputs[] = {color.Get(), motion.Get(), depth.Get()};
            const DXGI_FORMAT formats[] = {DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R16G16_FLOAT, DXGI_FORMAT_R32_FLOAT};
            for (unsigned i = 0; i < 3; ++i) {
                states.transition(list, inputs[i], D3D12_RESOURCE_STATE_COPY_DEST);
                D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = inputs[i]; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                src.PlacedFootprint.Offset = uint64_t(pitch) * H * i;
                src.PlacedFootprint.Footprint = {formats[i], W, H, 1, pitch};
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
                states.transition(list, inputs[i], D3D12_RESOURCE_STATE_COMMON);
            }
            ok = fg.generate(list, color.Get(), motion.Get(), depth.Get(), output.Get(), frame == 0, 1000.0f / 30);
            if (!ok) { ring.discardRecording(); break; }
            states.transition(list, output.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE);
            D3D12_TEXTURE_COPY_LOCATION src{}; src.pResource = output.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            D3D12_TEXTURE_COPY_LOCATION dst{}; dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R8G8B8A8_UNORM, W, H, 1, pitch};
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            states.transition(list, output.Get(), D3D12_RESOURCE_STATE_COMMON);
            ok = ring.submitAndSignal(slot) && ctx.waitForFenceValue(ring.lastSignaledValue());
            const uint8_t* captured = nullptr; D3D12_RANGE read{0, size_t(pitch) * H};
            ok = ok && SUCCEEDED(readback->Map(0, &read, reinterpret_cast<void**>(const_cast<uint8_t**>(&captured))));
            if (ok && frame >= 3) {
                uint64_t sum = 0, count = 0;
                for (unsigned y = 156; y < 204; ++y) for (unsigned x = 0; x < W; ++x)
                    if (captured[size_t(y) * pitch + x * 4] > 200) { sum += x; ++count; }
                const double center = count ? double(sum) / count : -10000;
                const double expected = left + 39.5 - 8;
                const double error = std::abs(center - expected);
                ++samples; errorSum += error;
                if (error < 4) ++intermediate;
                std::cout << "MIDPOINT cycle=" << cycle << " frame=" << frame << " center=" << center
                    << " expected=" << expected << " error=" << error << " pixels=" << count << std::endl;
            }
            if (captured) { D3D12_RANGE written{0, 0}; readback->Unmap(0, &written); }
        }
        ok = ring.drainQueue() && ok; fg.shutdown();
    }
    if (pixels) upload->Unmap(0, nullptr);
    unsigned errors = 0;
    if (info) for (UINT64 i = 0; i < info->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
        SIZE_T size = 0; info->GetMessage(i, nullptr, &size); std::vector<uint8_t> data(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(data.data());
        if (SUCCEEDED(info->GetMessage(i, message, &size)) && message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
            ++errors; std::cout << "GPU_ERROR " << message->pDescription << std::endl;
        }
    }
    ok = ok && samples >= 30 && intermediate == samples && errorSum / std::max(1u, samples) < 3 && errors == 0;
    std::cout << "FSR_INDEPENDENT pass=" << ok << " samples=" << samples << " intermediate=" << intermediate
        << " meanError=" << errorSum / std::max(1u, samples) << " debugErrors=" << errors << std::endl;
    return ok ? 0 : 1;
}
