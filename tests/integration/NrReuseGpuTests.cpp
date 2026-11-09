// NR inference reuse, GPU pass (docs/NR_INFERENCE_REUSE_2026-11-07.md), on
// real D3D12 dispatches with the debug layer:
//   * a reused frame with zero motion shows its source plus the stored change;
//   * with a known translation the change follows the content;
//   * where the carried key source does not match, the source shows unchanged;
//   * chained motion is the sum of the steps and reaches NR's motion inputs;
//   * after an inferred frame the motion output is the flow, bit for bit.
#include "veyra/pipeline/NrReusePass.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include <DirectXPackedVector.h>
#include <algorithm>
#include <cmath>
#include <d3d12sdklayers.h>
#include <iostream>
#include <vector>

int main()
{
    using namespace veyra;
    using namespace pipeline;
    using DirectX::PackedVector::HALF;
    using DirectX::PackedVector::XMConvertFloatToHalf;
    using DirectX::PackedVector::XMConvertHalfToFloat;
    gfx::D3D12DeviceContext ctx;
    gfx::CommandSlotRing ring;
    Status status = Status::Ok;
    gfx::DeviceContextDesc desc;
    desc.enableDebugLayer = true;
    if (!ctx.initialize(desc, status) ||
        !ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 4, status)) return 2;

    // The chain at 64x48, the flow at half size, NR's motion input at 64x48:
    // the scaling paths are exercised, not only the 1:1 case.
    constexpr unsigned W = 64, H = 48, FW = 32, FH = 24;
    auto source = makeTexture(ctx.device(), W, H, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    auto output = makeTexture(ctx.device(), W, H, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    auto flow = makeTexture(ctx.device(), FW, FH, DXGI_FORMAT_R16G16_FLOAT, true);
    auto nrMotion = makeTexture(ctx.device(), W, H, DXGI_FORMAT_R16G16_FLOAT, true);
    auto upload = makeUploadBuffer(ctx.device(), W * H * 8);
    if (!source || !output || !flow || !nrMotion || !upload) return 2;
    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = W * H * 8;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    if (FAILED(ctx.device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return 2;

    StateTracker states;
    auto run = [&](auto&& record) {
        unsigned slot;
        auto* list = ring.acquireNext(slot, status);
        if (!list) return false;
        record(list);
        return ring.submitAndSignal(slot) && ring.waitIdle();
    };
    // Upload `values` (channels per texel) into an FP16 texture.
    // Footprint rows are padded to the 256-byte pitch alignment.
    auto pitchOf = [](unsigned w, unsigned channels) { return (w * channels * 2 + 255u) & ~255u; };
    auto fill = [&](ID3D12Resource* texture, unsigned w, unsigned h, unsigned channels, const std::vector<float>& values) {
        const unsigned pitch = pitchOf(w, channels);
        void* data = nullptr;
        if (FAILED(upload->Map(0, nullptr, &data))) return false;
        for (unsigned y = 0; y < h; ++y) {
            auto* row = reinterpret_cast<HALF*>(static_cast<uint8_t*>(data) + size_t(y) * pitch);
            for (unsigned i = 0; i < w * channels; ++i) row[i] = XMConvertFloatToHalf(values[size_t(y) * w * channels + i]);
        }
        upload->Unmap(0, nullptr);
        return run([&](ID3D12GraphicsCommandList* list) {
            states.transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
            dst.pResource = texture; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
            src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
            src.PlacedFootprint.Footprint = {channels == 4 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R16G16_FLOAT, w, h, 1, pitch};
            list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        });
    };
    auto read = [&](ID3D12Resource* texture, unsigned w, unsigned h, unsigned channels, std::vector<float>& out, std::vector<uint16_t>* raw = nullptr) {
        const unsigned pitch = pitchOf(w, channels);
        if (!run([&](ID3D12GraphicsCommandList* list) {
                states.transition(list, texture, D3D12_RESOURCE_STATE_COPY_SOURCE);
                D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
                src.pResource = texture; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
                dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
                dst.PlacedFootprint.Footprint = {channels == 4 ? DXGI_FORMAT_R16G16B16A16_FLOAT : DXGI_FORMAT_R16G16_FLOAT, w, h, 1, pitch};
                list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
            })) return false;
        void* data = nullptr;
        D3D12_RANGE range{0, size_t(pitch) * h};
        if (FAILED(readback->Map(0, &range, &data))) return false;
        out.resize(size_t(w) * h * channels);
        if (raw) raw->resize(out.size());
        for (unsigned y = 0; y < h; ++y) {
            const auto* row = reinterpret_cast<const HALF*>(static_cast<const uint8_t*>(data) + size_t(y) * pitch);
            for (unsigned i = 0; i < w * channels; ++i) {
                const size_t at = size_t(y) * w * channels + i;
                out[at] = XMConvertHalfToFloat(row[i]);
                if (raw) (*raw)[at] = row[i];
            }
        }
        D3D12_RANGE empty{0, 0};
        readback->Unmap(0, &empty);
        return true;
    };

    bool ok = true;
    auto check = [&](const char* name, bool success) {
        std::cout << (success ? "PASS " : "FAIL ") << name << std::endl;
        ok &= success;
    };

    NrReusePass pass;
    if (!pass.initialize(ctx.device(), W, H, FW, FH, source.Get(), output.Get(), flow.Get(),
                         {{nrMotion.Get(), W, H}})) return 2;

    // A textured gray scene; the "NR change" brightens a 10x10 square.
    auto scene = [&](int shift, float lift, std::vector<float>& px) {
        px.assign(size_t(W) * H * 4, 0);
        for (unsigned y = 0; y < H; ++y)
            for (unsigned x = 0; x < W; ++x) {
                const int sx = int(x) - shift;
                const float v = 0.25f + 0.15f * float(((sx & 7) < 4) ^ ((y & 7) < 4));
                const bool square = sx >= 20 && sx < 30 && y >= 18 && y < 28;
                float* p = &px[(size_t(y) * W + x) * 4];
                p[0] = p[1] = p[2] = v + (square ? lift : 0.f);
                p[3] = 1;
            }
    };
    auto constantFlow = [&](float dx, float dy) {
        std::vector<float> v(size_t(FW) * FH * 2);
        for (size_t i = 0; i < v.size(); i += 2) { v[i] = dx; v[i + 1] = dy; }
        return v;
    };
    auto inferred = [&](int shift) {
        // The inferred frame: chain input and the NR chain's output with its change.
        std::vector<float> in, out;
        scene(shift, 0.f, in);
        scene(shift, 0.2f, out);
        return fill(source.Get(), W, H, 4, in) && fill(output.Get(), W, H, 4, out) &&
               run([&](auto* list) { pass.store(list, states); });
    };
    auto at = [&](const std::vector<float>& px, unsigned x, unsigned y) { return px[(size_t(y) * W + x) * 4]; };

    // --- zero motion: source + stored change ---------------------------------
    {
        std::vector<float> in, result, expected;
        bool steps = inferred(0) && fill(flow.Get(), FW, FH, 2, constantFlow(0, 0));
        scene(0, 0.f, in);
        steps = steps && fill(source.Get(), W, H, 4, in) &&
                run([&](auto* list) { pass.compose(list, states, false); pass.reuse(list, states, {}); }) &&
                read(output.Get(), W, H, 4, result);
        scene(0, 0.2f, expected);
        double worst = 0;
        for (size_t i = 0; steps && i < result.size(); ++i) worst = std::max(worst, double(std::abs(result[i] - expected[i])));
        std::cout << "static worstError=" << worst << std::endl;
        check("zero motion reuses the stored change", steps && worst < 2e-3);
    }
    // --- translation: the change follows the content --------------------------
    {
        // Content moved 4 px right: previous = current + flow, so flow is -4
        // chain pixels, -2 at the half-size flow extent.
        std::vector<float> in, result;
        bool steps = inferred(0) && fill(flow.Get(), FW, FH, 2, constantFlow(-2, 0));
        scene(4, 0.f, in);
        steps = steps && fill(source.Get(), W, H, 4, in) &&
                run([&](auto* list) { pass.compose(list, states, false); pass.reuse(list, states, {}); }) &&
                read(output.Get(), W, H, 4, result);
        // The lifted square now covers x 24..33; x 20..23 is uncovered.
        const float inside = steps ? at(result, 28, 22) - at(in, 28, 22) : 0;
        const float behind = steps ? at(result, 21, 22) - at(in, 21, 22) : 1;
        std::cout << "moved inside=" << inside << " behind=" << behind << std::endl;
        check("the change moves with the content", steps && std::abs(inside - 0.2f) < 0.01f && std::abs(behind) < 0.01f);
    }
    // --- mismatch: unenhanced source ------------------------------------------
    {
        std::vector<float> in, result;
        bool steps = inferred(0) && fill(flow.Get(), FW, FH, 2, constantFlow(0, 0));
        in.assign(size_t(W) * H * 4, 0.9f);
        for (size_t i = 3; i < in.size(); i += 4) in[i] = 1;
        steps = steps && fill(source.Get(), W, H, 4, in) &&
                run([&](auto* list) { pass.compose(list, states, false); pass.reuse(list, states, {}); }) &&
                read(output.Get(), W, H, 4, result);
        double worst = 0;
        for (size_t i = 0; steps && i < result.size(); ++i) worst = std::max(worst, double(std::abs(result[i] - in[i])));
        std::cout << "mismatch worstError=" << worst << std::endl;
        check("content that does not match shows the source unchanged", steps && worst < 1e-3);
    }
    // --- motion outputs ---------------------------------------------------------
    {
        std::vector<float> flowValues = constantFlow(-1.5f, 0.75f), motion, target;
        std::vector<uint16_t> flowBits, motionBits;
        bool steps = fill(flow.Get(), FW, FH, 2, flowValues) &&
                     run([&](auto* list) { pass.compose(list, states, false); }) &&
                     read(pass.motion(), FW, FH, 2, motion, &motionBits) && read(flow.Get(), FW, FH, 2, flowValues, &flowBits);
        check("after an inferred frame the motion is the flow, bit for bit", steps && motionBits == flowBits);
        // Reused frame (above), then the inferred frame after it: -2 then -3
        // flow pixels chain to -5, which NR's 64-wide input sees as -10.
        steps = fill(flow.Get(), FW, FH, 2, constantFlow(-2, 0)) &&
                run([&](auto* list) { pass.compose(list, states, false); }) &&
                fill(flow.Get(), FW, FH, 2, constantFlow(-3, 0)) &&
                run([&](auto* list) { pass.compose(list, states, true); pass.writeMotionTargets(list, states); }) &&
                read(pass.motion(), FW, FH, 2, motion) && read(nrMotion.Get(), W, H, 2, target);
        const size_t mid = (size_t(FH / 2) * FW + FW / 2) * 2, targetMid = (size_t(H / 2) * W + W / 2) * 2;
        std::cout << "chained motion=" << (steps ? motion[mid] : 0) << " nrInput=" << (steps ? target[targetMid] : 0) << std::endl;
        check("chained motion is the sum of the steps", steps && std::abs(motion[mid] + 5.f) < 1e-3f && std::abs(motion[mid + 1]) < 1e-3f);
        check("NR's motion input receives the chained motion at its own scale",
              steps && std::abs(target[targetMid] + 10.f) < 1e-2f && std::abs(target[targetMid + 1]) < 1e-3f);
    }

    ring.drainQueue();
    ComPtr<ID3D12InfoQueue> info;
    if (FAILED(ctx.device()->QueryInterface(IID_PPV_ARGS(&info)))) return 2;
    unsigned errors = 0;
    for (UINT64 i = 0; i < info->GetNumStoredMessages(); ++i) {
        SIZE_T size = 0;
        if (FAILED(info->GetMessage(i, nullptr, &size))) return 2;
        std::vector<uint8_t> bytes(size);
        auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
        if (FAILED(info->GetMessage(i, message, &size))) return 2;
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) { ++errors; std::cerr << message->pDescription << std::endl; }
    }
    check("debug layer clean", errors == 0);
    pass.close();
    std::cout << (ok ? "ALL PASS" : "FAILED") << std::endl;
    return ok ? 0 : 1;
}
