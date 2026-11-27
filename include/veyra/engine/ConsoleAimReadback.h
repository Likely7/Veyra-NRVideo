// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "veyra/engine/PresentCapture.h"
#include <memory>

namespace veyra::remoteplay { class AimTest; }

namespace veyra::engine {

// This capture tap records only on request, and never waits for the GPU.
class ConsoleAimReadback final : public PresentCapture {
public:
    ConsoleAimReadback();
    ~ConsoleAimReadback() override;
    void setProbe(std::shared_ptr<remoteplay::AimTest>);
    bool record(ID3D12GraphicsCommandList*, const Frame&) override;
    void submitted(ID3D12Fence*, uint64_t) override;
    void discarded() override;
    void presented(int64_t host100ns) override;

private:
    struct State;
    std::unique_ptr<State> p_;
};

} // namespace veyra::engine
