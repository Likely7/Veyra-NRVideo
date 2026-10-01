// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/MoonlightInputCapture.h"

#include <QCoreApplication>
#include <QGuiApplication>

#include "veyra/Log.h"
#include "veyra/engine/EngineController.h"

namespace veyra::ui {

MoonlightInputCapture::MoonlightInputCapture(engine::EngineController& engine, QObject* parent)
    : QObject(parent), engine_(engine), router_(*this) {
    QCoreApplication::instance()->installNativeEventFilter(this);
    // Focus lost (Alt+Tab, another window on top): give the keyboard and mouse back at once.
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state != Qt::ApplicationActive && captured_) capture(false);
    });
}

MoonlightInputCapture::~MoonlightInputCapture() {
    capture(false);
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
}

bool MoonlightInputCapture::capture(bool on) {
    if (on == captured_) return true;
    if (on) {
        if (!window_ || GetForegroundWindow() != window_) return false;
        RAWINPUTDEVICE device{0x01, 0x02, 0, window_};   // generic desktop / mouse
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            veyra::log::warn("moonlight-input", "RegisterRawInputDevices failed; mouse capture not started");
            return false;
        }
        captured_ = true;
        router_.reset();
        clipToWindow();
        SetCursor(nullptr);
    } else {
        captured_ = false;
        RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&device, 1, sizeof(device));
        ClipCursor(nullptr);
        SetCursor(LoadCursorW(nullptr, IDC_ARROW));
        router_.reset();
        engine_.moonlightReleaseInput();   // nothing stays pressed on the host
    }
    emit capturedChanged(captured_);
    return true;
}

void MoonlightInputCapture::clipToWindow() {
    RECT client{};
    if (!window_ || !GetClientRect(window_, &client)) return;
    POINT topLeft{client.left, client.top}, bottomRight{client.right, client.bottom};
    ClientToScreen(window_, &topLeft);
    ClientToScreen(window_, &bottomRight);
    const RECT screen{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    ClipCursor(&screen);
}

bool MoonlightInputCapture::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    if (!captured_ || eventType != "windows_generic_MSG") return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (!msg->hwnd || GetAncestor(msg->hwnd, GA_ROOT) != window_) return false;

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
        if (result) *result = 0;
        return true;
    }
    case WM_SETCURSOR:
        SetCursor(nullptr);
        if (result) *result = TRUE;
        return true;
    case WM_SIZE: case WM_MOVE:
        clipToWindow();
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

void MoonlightInputCapture::key(uint32_t virtualKey, uint32_t scanCode, bool extended, bool down) { engine_.moonlightKey(virtualKey, scanCode, extended, down); }
void MoonlightInputCapture::mouseMove(int dx, int dy) { engine_.moonlightMouseMove(dx, dy); }
void MoonlightInputCapture::mouseButton(int button, bool down) { engine_.moonlightMouseButton(button, down); }
void MoonlightInputCapture::scroll(int delta, bool horizontal) { engine_.moonlightScroll(delta, horizontal); }

} // namespace veyra::ui
