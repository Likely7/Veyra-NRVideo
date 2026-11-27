// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "veyra/engine/PresentCapture.h"
#include "veyra/sink/ImageExportSink.h"
#include <future>
#include <optional>

namespace veyra::engine {

// One explicit screenshot of a real live frame. The presenter records the
// copy before releasing the frame lease. Only the writer maps CPU pixels.
class LiveScreenshot final : public PresentCapture {
public:
    struct Result {
        std::wstring path;
        bool hdr = false;
        bool saved = false;
    };

    void request(std::wstring path);
    bool active() const;
    bool needsFrame() const override;
    bool record(ID3D12GraphicsCommandList* list, const Frame& frame) override;
    void submitted(ID3D12Fence* fence, uint64_t value) override;
    void discarded() override;
    std::optional<Result> poll();
    std::optional<Result> finish();

private:
    std::wstring path_;
    sink::ImageReadback readback_;
    Microsoft::WRL::ComPtr<ID3D12Fence> fence_;
    uint64_t fenceValue_ = 0;
    std::future<Result> writer_;
    std::optional<Result> failed_;
};

} // namespace veyra::engine
