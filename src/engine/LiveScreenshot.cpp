// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/engine/LiveScreenshot.h"
#include "veyra/Log.h"

#include <chrono>
#include <filesystem>
#include <format>
#include <objbase.h>
#include <utility>

namespace veyra::engine {
namespace {
LiveScreenshot::Result writeScreenshot(std::wstring path, sink::ImageReadback readback) {
    const bool hdr = readback.format != DXGI_FORMAT_R8G8B8A8_UNORM;
    const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(result)) {
        log::warn("image-save", std::format("live screenshot COM initialization failed hr=0x{:08X}", unsigned(result)));
        return {std::move(path), hdr, false};
    }
    struct ComScope { ~ComScope() { CoUninitialize(); } } com;
    try {
        if (hdr) path = std::filesystem::path(path).replace_extension(L".jxr").wstring();
        sink::RgbaImage raw;
        if (!sink::readImagePixels(readback, raw)) return {std::move(path), hdr, false};
        readback.buffer.Reset();
        const auto extension = std::filesystem::path(path).extension();
        const bool saved = hdr ? sink::saveHdrScreenshot(path, raw, readback.format) :
            sink::saveImage(path, raw, extension == L".jpg" || extension == L".jpeg", sink::ImageAlpha::Opaque);
        if (saved) log::info("image-save", std::format("live screenshot saved extent={}x{} hdr={}", raw.width, raw.height, hdr));
        return {std::move(path), hdr, saved};
    } catch (const std::exception& e) {
        log::warn("image-save", std::format("live screenshot write error: {}", e.what()));
        return {std::move(path), hdr, false};
    }
}
}

void LiveScreenshot::request(std::wstring path) {
    path_ = std::move(path);
}

bool LiveScreenshot::active() const {
    return !path_.empty() || writer_.valid() || failed_.has_value();
}

bool LiveScreenshot::needsFrame() const {
    return !path_.empty() && !readback_.buffer;
}

bool LiveScreenshot::record(ID3D12GraphicsCommandList* list, const Frame& frame) {
    if (!needsFrame() || frame.generated) return false;
    Microsoft::WRL::ComPtr<ID3D12Device> device;
    if (!frame.texture || FAILED(frame.texture->GetDevice(IID_PPV_ARGS(&device))) ||
        !sink::prepareImageReadback(device.Get(), frame.texture, readback_)) {
        failed_ = Result{std::exchange(path_, {}), frame.hdrMode != 0, false};
        return false;
    }
    sink::recordImageReadback(list, frame.texture, readback_);
    return true;
}

void LiveScreenshot::submitted(ID3D12Fence* fence, uint64_t value) {
    if (readback_.buffer && !fence_) {
        fence_ = fence;
        fenceValue_ = value;
    }
}

void LiveScreenshot::discarded() {
    if (!fence_) readback_ = {};
}

std::optional<LiveScreenshot::Result> LiveScreenshot::poll() {
    if (failed_) return std::exchange(failed_, std::nullopt);
    if (fence_) {
        const auto completed = fence_->GetCompletedValue();
        if (completed == UINT64_MAX) {
            const bool hdr = readback_.format != DXGI_FORMAT_R8G8B8A8_UNORM;
            readback_ = {};
            fence_.Reset();
            return Result{std::exchange(path_, {}), hdr, false};
        }
        if (completed >= fenceValue_) {
            fence_.Reset();
            const bool hdr = readback_.format != DXGI_FORMAT_R8G8B8A8_UNORM;
            auto captured = std::exchange(readback_, {});
            auto path = std::exchange(path_, {});
            try {
                writer_ = std::async(std::launch::async, writeScreenshot, std::move(path), std::move(captured));
            } catch (const std::exception& e) {
                log::warn("image-save", std::format("live screenshot writer start failed: {}", e.what()));
                return Result{{}, hdr, false};
            }
        }
    }
    if (writer_.valid() && writer_.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        return writer_.get();
    }
    return {};
}

std::optional<LiveScreenshot::Result> LiveScreenshot::finish() {
    if (auto result = poll()) return result;
    if (writer_.valid()) return writer_.get();
    return {};
}

} // namespace veyra::engine
