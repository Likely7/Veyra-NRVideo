// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include "WindowInputCapture.h"
namespace veyra::engine { class EngineController; }
namespace veyra::ui {
class MoonlightInputCapture final : private moonlight::InputSink, public WindowInputCapture {
public:
    explicit MoonlightInputCapture(engine::EngineController& engine, QObject* parent=nullptr);
    ~MoonlightInputCapture() override { capture(false); }
private:
    void key(uint32_t virtualKey, uint32_t scanCode, bool extended, bool down) override;
    void mouseMove(int dx, int dy) override;
    void mouseButton(int button, bool down) override;
    void scroll(int delta, bool horizontal) override;
    engine::EngineController& engine_;
};
}
