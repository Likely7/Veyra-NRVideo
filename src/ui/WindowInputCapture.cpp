// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/WindowInputCapture.h"

#include <QCoreApplication>
#include <QGuiApplication>

#include <QDebug>

namespace veyra::ui {

WindowInputCapture::WindowInputCapture(moonlight::InputSink& sink, bool releaseOnly, QObject* parent, MouseGrab mouseGrab)
    : QObject(parent), mouseGrab_(mouseGrab ? std::move(mouseGrab) : grabNativeMouse), releaseOnly_(releaseOnly), router_(sink, releaseOnly) {
    QCoreApplication::instance()->installNativeEventFilter(this);
    // Focus lost (Alt+Tab, another window on top): give the keyboard and mouse back at once.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive && captured_) capture(false);
    });
}

WindowInputCapture::~WindowInputCapture() {
    capture(false);
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
}

bool WindowInputCapture::capture(bool on) {
    if (on == captured_) return true;
    if (!mouseGrab_(window_, on)) return false;
    captured_ = on;
    router_.reset();
    emit capturedChanged(on);
    return true;
}

bool WindowInputCapture::grabNativeMouse(HWND window, bool on) {
    if (on) {
        if (!window || GetForegroundWindow() != window) return false;
        RAWINPUTDEVICE device{0x01, 0x02, 0, window};   // generic desktop / mouse
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            qWarning("RegisterRawInputDevices failed; mouse capture not started");
            return false;
        }
        clipToWindow(window);
        SetCursor(nullptr);
    } else {
        RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&device, 1, sizeof(device));
        ClipCursor(nullptr);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    }
    return true;
}

void WindowInputCapture::clipToWindow(HWND window) {
    RECT client{};
    if (!window || !GetClientRect(window, &client)) return;
    POINT topLeft{client.left, client.top}, bottomRight{client.right, client.bottom};
    ClientToScreen(window, &topLeft);
    ClientToScreen(window, &bottomRight);
    const RECT screen{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    ClipCursor(&screen);
}

bool WindowInputCapture::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    if ((!captured_ && !listenForLocalKeys_) || eventType != "windows_generic_MSG") return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (!msg->hwnd || GetAncestor(msg->hwnd, GA_ROOT) != window_) return false;
    if (!captured_) {
        // Windows sends an unmodified F10 as a system key. Leave Alt chords
        // and other system keys to the focused window.
        const bool bareF10 = msg->wParam == VK_F10 && !(msg->lParam & (LPARAM{1} << 29)) &&
                             (msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP);
        if (!bareF10 && msg->message != WM_KEYDOWN && msg->message != WM_KEYUP) return false;
        if (GetKeyState(VK_CONTROL) < 0 || GetKeyState(VK_SHIFT) < 0 || GetKeyState(VK_MENU) < 0 ||
            GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) return false;
        const bool down = msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN;
        const bool consumed = router_.localKey(msg->lParam, down);
        if (consumed && result) *result = 0;
        return consumed;
    }

    switch (msg->message) {
    case WM_INPUT: {
        alignas(8) BYTE buffer[128];
        UINT size = sizeof(buffer);
        const UINT got = GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, buffer, &size, sizeof(RAWINPUTHEADER));
        if (got != UINT(-1)) {
            const auto* raw = reinterpret_cast<const RAWINPUT*>(buffer);
            if (raw->header.dwType == RIM_TYPEMOUSE && (raw->data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0)
                router_.rawMouse(raw->data.mouse.lLastX, raw->data.mouse.lLastY);
        }
        // A foreground WM_INPUT must reach DefWindowProc once to release the
        // system's raw input buffer. Qt will not call it for a consumed message.
        if (GET_RAWINPUT_CODE_WPARAM(msg->wParam) == RIM_INPUT)
            DefWindowProcW(msg->hwnd, msg->message, msg->wParam, msg->lParam);
        if (result) *result = 0;
        return true;
    }
    case WM_SETCURSOR:
        SetCursor(nullptr);
        if (result) *result = TRUE;
        return true;
    case WM_SIZE: case WM_MOVE:
        clipToWindow(window_);
        return false;   // the UI still needs the resize
    case WM_ACTIVATE:
        if (LOWORD(msg->wParam) == WA_INACTIVE) capture(false);
        return false;
    case WM_KILLFOCUS:
        capture(false);
        return false;
    default:
        break;
    }

    const bool altTab = msg->wParam == VK_TAB
        && ((msg->message == WM_SYSKEYDOWN && (msg->lParam & (1L << 29))) || (GetKeyState(VK_MENU) & 0x8000));
    if (releaseOnly_ && (msg->message == WM_SYSKEYDOWN || msg->message == WM_KEYDOWN) &&
        (altTab || msg->wParam == VK_LWIN || msg->wParam == VK_RWIN)) {
        capture(false);
        return false;
    }
    const bool consumed = router_.message(msg->message, msg->wParam, msg->lParam);
    if (consumed && result) *result = 0;
    if (const auto reserved = router_.takeReserved(); reserved != moonlight::Reserved::None) {
        switch (reserved) {
        case moonlight::Reserved::ReleaseCapture: capture(false); emit releaseRequested(); break;
        case moonlight::Reserved::Quit: capture(false); emit quitRequested(); break;
        case moonlight::Reserved::ToggleStats: emit statsRequested(); break;
        case moonlight::Reserved::None: break;
        }
    }
    return consumed;
}

} // namespace veyra::ui
