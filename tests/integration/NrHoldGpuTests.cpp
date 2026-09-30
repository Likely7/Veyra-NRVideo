// Stage-5 output stabiliser acceptance (docs/NR_FG_OPTIMIZATION_PLAN_2026-09-29.md §5).
//
// The plan's criteria, checked here on real D3D12 dispatches:
//   * a static image with tiny per-frame perturbation must show a clear drop in
//     the frame-to-frame output difference,
//   * a moving block must not ghost (edge error no worse than the pass off),
//   * strength 0 must be a no-op the caller never even dispatches, and
//   * the debug layer must stay clean.
//
// It does not measure the GPU cost: that needs a real frame budget, and is
// measured in the product smoke run instead (reported in the WORKLOG).

#include "veyra/pipeline/NrHoldPass.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/CommandSlotRing.h"
#include <DirectXPackedVector.h>
#include <cmath>
#include <d3d12sdklayers.h>
#include <iostream>
#include <limits>
#include <vector>

int main() {
    using namespace veyra;
    using namespace pipeline;
    gfx::D3D12DeviceContext ctx;
    gfx::CommandSlotRing ring;
    Status status = Status::Ok;
    gfx::DeviceContextDesc desc;
    desc.enableDebugLayer = true;
    if (!ctx.initialize(desc, status) ||
        !ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 4, status)) return 2;

    constexpr unsigned width = 64, height = 48;
    constexpr unsigned pitch = width * 8; // RGBA16F
    auto current = makeTexture(ctx.device(), width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    auto base = makeTexture(ctx.device(), width, height, DXGI_FORMAT_R16G16B16A16_FLOAT, true);
    if (!current || !base) return 2;
    auto upload = makeUploadBuffer(ctx.device(), pitch * height);
    if (!upload) return 2;

    D3D12_HEAP_PROPERTIES hp{}; hp.Type = D3D12_HEAP_TYPE_READBACK;
    D3D12_RESOURCE_DESC bd{};
    bd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bd.Width = pitch * height;
    bd.Height = 1; bd.DepthOrArraySize = 1; bd.MipLevels = 1; bd.SampleDesc.Count = 1;
    bd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ComPtr<ID3D12Resource> readback;
    if (FAILED(ctx.device()->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &bd,
        D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback)))) return 2;

    StateTracker states;
    // `pixels` is RGBA8 for readability; expanded to FP16 on upload.
    auto fill = [&](ID3D12Resource* texture, const std::vector<uint8_t>& pixels) {
        void* data = nullptr;
        if (FAILED(upload->Map(0, nullptr, &data))) return false;
        auto* half = static_cast<DirectX::PackedVector::HALF*>(data);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const size_t src = (size_t(y) * width + x) * 4;
                const size_t dst = size_t(y) * pitch / 2 + x * 4;
                for (unsigned c = 0; c < 4; ++c)
                    half[dst + c] = DirectX::PackedVector::XMConvertFloatToHalf(pixels[src + c] / 255.0f);
            }
        upload->Unmap(0, nullptr);
        unsigned slot;
        auto* list = ring.acquireNext(slot, status);
        if (!list) return false;
        states.transition(list, texture, D3D12_RESOURCE_STATE_COPY_DEST);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        dst.pResource = texture; dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        src.pResource = upload.Get(); src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        src.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, pitch};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        return ring.submitAndSignal(slot) && ring.waitIdle();
    };

    NrHoldPass pass;
    if (!pass.initialize(ctx.device(), width, height, current.Get(), base.Get())) return 2;

    // Read the stabilised result the way the graph does.
    auto readResult = [&](std::vector<uint8_t>& out) {
        auto* stabilised = pass.result();
        if (!stabilised) return false;
        unsigned slot;
        auto* list = ring.acquireNext(slot, status);
        if (!list) return false;
        states.transition(list, stabilised, D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION dst{}, src{};
        src.pResource = stabilised; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dst.pResource = readback.Get(); dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        dst.PlacedFootprint.Footprint = {DXGI_FORMAT_R16G16B16A16_FLOAT, width, height, 1, pitch};
        list->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);
        if (!ring.submitAndSignal(slot) || !ring.waitIdle()) return false;
        void* data = nullptr;
        D3D12_RANGE range{0, pitch * height};
        if (FAILED(readback->Map(0, &range, &data))) return false;
        out.resize(pitch * height);
        const auto* half = static_cast<const DirectX::PackedVector::HALF*>(data);
        for (size_t i = 0; i < pitch * height / 2; ++i) {
            const float v = DirectX::PackedVector::XMConvertHalfToFloat(half[i]);
            out[i] = uint8_t(std::clamp(v * 255.0f, 0.0f, 255.0f));
        }
        D3D12_RANGE empty{0, 0};
        readback->Unmap(0, &empty);
        return true;
    };

    bool ok = true;
    auto check = [&](const char* name, bool success) {
        std::cout << name << '=' << (success ? 1 : 0) << std::endl;
        ok &= success;
    };

    // --- static scene, tiny perturbation -------------------------------------
    // The source varies by +-1/255 per frame but stays within tolerance: the
    // hold must damp the output difference substantially versus the raw input.
    auto staticScene = [&](unsigned frame, std::vector<uint8_t>& out) {
        out.resize(size_t(width) * height * 4);
        for (unsigned y = 0; y < height; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const int base = 120 + int((x * 7 + y * 3) % 24);
                const int jitter = ((x + y + frame) % 3) - 1; // -1, 0, +1
                const uint8_t v = uint8_t(std::clamp(base + jitter, 0, 255));
                const size_t i = (size_t(y) * width + x) * 4;
                out[i] = out[i + 1] = out[i + 2] = v; out[i + 3] = 255;
            }
    };
    // The criterion is a *drop* in the frame-to-frame difference, so the held
    // output has to be compared against the unheld input over the same frames.
    // An absolute threshold would only encode this clip's contrast.
    std::vector<uint8_t> input, output, previousOutput, previousInput;
    double rawDelta = 0, heldDelta = 0;
    int samples = 0;
    for (unsigned frame = 0; frame < 8; ++frame) {
        staticScene(frame, input);
        if (!fill(base.Get(), input) || !fill(current.Get(), input)) { ok = false; break; }
        // A reset on the first frame seeds history without holding.
        unsigned slot;
        auto* list = ring.acquireNext(slot, status);
        if (!list) { ok = false; break; }
        pass.run(list, states, frame == 0, 0.8f, 0.02f);
        if (!ring.submitAndSignal(slot) || !ring.waitIdle()) { ok = false; break; }
        if (!readResult(output)) { ok = false; break; }
        if (frame >= 2 && previousOutput.size() == output.size()) {
            // `rawDelta`: how much the source itself moved between these frames.
            // `heldDelta`: how much the stabilised output moved. The hold is
            // only useful if the second is clearly smaller than the first.
            for (size_t i = 0; i < output.size(); ++i) {
                rawDelta += std::abs(int(input[i]) - int(previousInput[i]));
                heldDelta += std::abs(int(output[i]) - int(previousOutput[i]));
            }
            ++samples;
        }
        previousOutput = output;
        previousInput = input;
    }
    const double rawMean = samples ? rawDelta / samples : 0.0;
    const double heldMean = samples ? heldDelta / samples : 0.0;
    const double reduction = rawMean > 0 ? 1.0 - heldMean / rawMean : 0.0;
    std::cout << "static samples=" << samples
              << " rawMean=" << rawMean << " heldMean=" << heldMean
              << " reduction=" << reduction << std::endl;
    check("static scene holds (frame-to-frame difference clearly reduced)",
          samples > 0 && reduction >= 0.5);

    // --- strength 0 is never dispatched --------------------------------------
    // The caller skips run() at 0; verify the pass reports inactive history and
    // that calling it anyway is a no-op (no dispatch, no state change).
    pass.reset();
    check("strength zero leaves history unseeded", !pass.active());
    {
        unsigned slot;
        auto* list = ring.acquireNext(slot, status);
        pass.run(list, states, false, 0.0f, 0.02f);
        check("strength zero dispatches nothing", ring.submitAndSignal(slot) && ring.waitIdle() && !pass.active());
    }

    // --- moving block must not ghost -----------------------------------------
    auto movingScene = [&](unsigned frame, std::vector<uint8_t>& out) {
        out.assign(size_t(width) * height * 4, 30);
        const unsigned left = 4 + frame * 6;
        for (unsigned y = 8; y < 32; ++y)
            for (unsigned x = left; x < left + 12 && x < width; ++x) {
                const size_t i = (size_t(y) * width + x) * 4;
                out[i] = 240; out[i + 1] = 40; out[i + 2] = 40; out[i + 3] = 255;
            }
        for (size_t i = 3; i < out.size(); i += 4) out[i] = 255;
    };
    std::vector<uint8_t> moveInput, moveOutput;
    for (unsigned frame = 0; frame < 6; ++frame) {
        movingScene(frame, moveInput);
        if (!fill(base.Get(), moveInput) || !fill(current.Get(), moveInput)) { ok = false; break; }
        unsigned slot;
        auto* list = ring.acquireNext(slot, status);
        pass.run(list, states, frame == 0, 0.8f, 0.02f);
        if (!ring.submitAndSignal(slot) || !ring.waitIdle()) { ok = false; break; }
        if (!readResult(moveOutput)) { ok = false; break; }
    }
    // The block's leading edge must have advanced with the input, not stayed
    // behind. Find the rightmost "red" column and compare with the input's.
    auto rightmostRed = [&](const std::vector<uint8_t>& px) {
        int found = -1;
        for (unsigned y = 20; y < 21; ++y)
            for (unsigned x = 0; x < width; ++x) {
                const size_t i = (size_t(y) * width + x) * 4;
                if (px[i] > 150 && px[i + 1] < 120) found = std::max(found, int(x));
            }
        return found;
    };
    const int expected = rightmostRed(moveInput);
    const int observed = rightmostRed(moveOutput);
    std::cout << "moving edge expected=" << expected << " observed=" << observed << std::endl;
    check("moving block does not ghost behind its input", observed >= 0 && expected - observed <= 2);

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
        if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
            ++errors;
            std::cerr << message->pDescription << std::endl;
        }
    }
    check("debug layer clean", errors == 0);
    pass.close();
    return ok ? 0 : 1;
}
