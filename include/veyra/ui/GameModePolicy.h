#pragma once

namespace veyra::ui {
// Scaled output must not pass absolute mouse positions to an unscaled window.
// Keyboard/gamepad input can still belong to the foreground capture target.
enum class GameInputMode { Off, Panel, Through, Scaled };

constexpr bool gameViewportScaled(bool fill, int aspectMode) {
    return fill || aspectMode != 0;
}

constexpr GameInputMode gameInputMode(bool session, bool fullscreen, bool refused,
                                     bool panel, bool scaled) {
    // Opening a source is sufficient; feature initialization is not an entry delay.
    if (!session || !fullscreen || refused) return GameInputMode::Off;
    if (panel) return GameInputMode::Panel;
    return scaled ? GameInputMode::Scaled : GameInputMode::Through;
}
}
