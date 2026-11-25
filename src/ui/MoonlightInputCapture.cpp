// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/MoonlightInputCapture.h"
#include "veyra/engine/EngineController.h"
namespace veyra::ui {
MoonlightInputCapture::MoonlightInputCapture(engine::EngineController& engine,QObject* parent)
    : WindowInputCapture(*this,false,parent), engine_(engine) {
    connect(this,&WindowInputCapture::capturedChanged,this,[this](bool on){ if(!on) engine_.moonlightReleaseInput(); });
}
void MoonlightInputCapture::key(uint32_t virtualKey, uint32_t scanCode, bool extended, bool down) { engine_.moonlightKey(virtualKey, scanCode, extended, down); }
void MoonlightInputCapture::mouseMove(int dx, int dy) { engine_.moonlightMouseMove(dx, dy); }
void MoonlightInputCapture::mouseButton(int button, bool down) { engine_.moonlightMouseButton(button, down); }
void MoonlightInputCapture::scroll(int delta, bool horizontal) { engine_.moonlightScroll(delta, horizontal); }

} // namespace veyra::ui
