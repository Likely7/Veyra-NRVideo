#pragma once
#include <QStringList>
#include <Qt>

namespace veyra::ui {

// QKeyEvent on Windows uses bit 8 for E0. Physical names are shared with
// Stickmouse profiles, so a layout change does not move a player shortcut.
struct Ps5PlayerKey {
    const char* name;
    unsigned scan;
    Qt::Key qtKey;
};
inline constexpr Ps5PlayerKey ps5PlayerKeys[]{
    {"F1", 0x3b, Qt::Key_F1}, {"F2", 0x3c, Qt::Key_F2},
    {"F3", 0x3d, Qt::Key_F3}, {"F4", 0x3e, Qt::Key_F4},
    {"F5", 0x3f, Qt::Key_F5}, {"F6", 0x40, Qt::Key_F6},
    {"F7", 0x41, Qt::Key_F7}, {"F8", 0x42, Qt::Key_F8},
    {"F9", 0x43, Qt::Key_F9}, {"F10", 0x44, Qt::Key_F10},
    {"F11", 0x57, Qt::Key_F11}, {"F12", 0x58, Qt::Key_F12},
    {"F13", 0x64, Qt::Key_F13}, {"F14", 0x65, Qt::Key_F14},
    {"F15", 0x66, Qt::Key_F15}, {"F16", 0x67, Qt::Key_F16},
    {"F17", 0x68, Qt::Key_F17}, {"F18", 0x69, Qt::Key_F18},
    {"F19", 0x6a, Qt::Key_F19}, {"F20", 0x6b, Qt::Key_F20},
    {"F21", 0x6c, Qt::Key_F21}, {"F22", 0x6d, Qt::Key_F22},
    {"F23", 0x6e, Qt::Key_F23}, {"F24", 0x76, Qt::Key_F24},
    {"KeyA", 0x1e, Qt::Key_A}, {"KeyB", 0x30, Qt::Key_B},
    {"KeyC", 0x2e, Qt::Key_C}, {"KeyD", 0x20, Qt::Key_D},
    {"KeyE", 0x12, Qt::Key_E}, {"KeyF", 0x21, Qt::Key_F},
    {"KeyG", 0x22, Qt::Key_G}, {"KeyH", 0x23, Qt::Key_H},
    {"KeyI", 0x17, Qt::Key_I}, {"KeyJ", 0x24, Qt::Key_J},
    {"KeyK", 0x25, Qt::Key_K}, {"KeyL", 0x26, Qt::Key_L},
    {"KeyM", 0x32, Qt::Key_M}, {"KeyN", 0x31, Qt::Key_N},
    {"KeyO", 0x18, Qt::Key_O}, {"KeyP", 0x19, Qt::Key_P},
    {"KeyQ", 0x10, Qt::Key_Q}, {"KeyR", 0x13, Qt::Key_R},
    {"KeyS", 0x1f, Qt::Key_S}, {"KeyT", 0x14, Qt::Key_T},
    {"KeyU", 0x16, Qt::Key_U}, {"KeyV", 0x2f, Qt::Key_V},
    {"KeyW", 0x11, Qt::Key_W}, {"KeyX", 0x2d, Qt::Key_X},
    {"KeyY", 0x15, Qt::Key_Y}, {"KeyZ", 0x2c, Qt::Key_Z},
    {"Digit0", 0x0b, Qt::Key_0}, {"Digit1", 0x02, Qt::Key_1},
    {"Digit2", 0x03, Qt::Key_2}, {"Digit3", 0x04, Qt::Key_3},
    {"Digit4", 0x05, Qt::Key_4}, {"Digit5", 0x06, Qt::Key_5},
    {"Digit6", 0x07, Qt::Key_6}, {"Digit7", 0x08, Qt::Key_7},
    {"Digit8", 0x09, Qt::Key_8}, {"Digit9", 0x0a, Qt::Key_9},
    {"Space", 0x39, Qt::Key_Space}, {"Tab", 0x0f, Qt::Key_Tab},
    {"Escape", 0x01, Qt::Key_Escape}, {"Enter", 0x1c, Qt::Key_Return},
    {"Backspace", 0x0e, Qt::Key_Backspace}, {"CapsLock", 0x3a, Qt::Key_CapsLock},
    {"PageUp", 0x149, Qt::Key_PageUp}, {"PageDown", 0x151, Qt::Key_PageDown},
    {"ArrowUp", 0x148, Qt::Key_Up}, {"ArrowDown", 0x150, Qt::Key_Down},
    {"ArrowLeft", 0x14b, Qt::Key_Left}, {"ArrowRight", 0x14d, Qt::Key_Right},
    {"Home", 0x147, Qt::Key_Home}, {"Insert", 0x152, Qt::Key_Insert},
    {"Delete", 0x153, Qt::Key_Delete}, {"Numpad0", 0x52, Qt::Key_0},
    {"Numpad1", 0x4f, Qt::Key_1}, {"Numpad2", 0x50, Qt::Key_2},
    {"Numpad3", 0x51, Qt::Key_3}, {"Numpad4", 0x4b, Qt::Key_4},
    {"Numpad5", 0x4c, Qt::Key_5}, {"Numpad6", 0x4d, Qt::Key_6},
    {"Numpad7", 0x47, Qt::Key_7}, {"Numpad8", 0x48, Qt::Key_8},
    {"Numpad9", 0x49, Qt::Key_9},
    {"ShiftLeft", 0x2a, Qt::Key_Shift}, {"ShiftRight", 0x36, Qt::Key_Shift},
    {"ControlLeft", 0x1d, Qt::Key_Control}, {"ControlRight", 0x11d, Qt::Key_Control},
    {"AltLeft", 0x38, Qt::Key_Alt}, {"AltRight", 0x138, Qt::Key_AltGr}
};

inline QString ps5PlayerKeyName(unsigned scan, bool extended) {
    const auto physical = scan | (extended ? 0x100u : 0u);
    for (const auto& key : ps5PlayerKeys)
        if (key.scan == physical) return QString::fromLatin1(key.name);
    return {};
}

inline QStringList ps5PlayerShortcutKeys() {
    QStringList names;
    for (const auto& key : ps5PlayerKeys) {
        if (key.qtKey == Qt::Key_Shift || key.qtKey == Qt::Key_Control ||
            key.qtKey == Qt::Key_Alt || key.qtKey == Qt::Key_AltGr) continue;
        names.append(QString::fromLatin1(key.name));
    }
    return names;
}

inline QString ps5EventKeyName(int qtKey, int nativeScanCode) {
    if (nativeScanCode)
        return ps5PlayerKeyName(nativeScanCode & 0xff, (nativeScanCode & 0xff00) != 0);
    // Synthetic Qt events have no scan code.
    for (const auto& key : ps5PlayerKeys)
        if (key.qtKey == qtKey) return QString::fromLatin1(key.name);
    return {};
}
} // namespace veyra::ui
