#pragma once
// 游戏模式 (屏幕捕获 → "游戏模式"): Veyra's fullscreen picture stays above what it
// shows while that program keeps the foreground, so a game - or, for a monitor
// capture, the whole desktop - gets the real keyboard, mouse and gamepad (Raw Input
// and DirectInput only report to the foreground window; posted input does not work).
//
// The 1:1 main window becomes click-through through Qt's own flags
// (Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus, bound in Main.qml to
// passthroughActive), so Qt keeps WS_EX_LAYERED | WS_EX_TRANSPARENT itself - with the
// software interface it drops a layered bit set behind its back. Qt does not set
// WS_EX_NOACTIVATE on it, so this class adds that bit, makes the window topmost,
// Scaled output retains keyboard/gamepad foreground but blocks mouse clicks.
// This class hands the foreground over, owns the hotkeys and the watchdog, and makes any other
// visible Veyra window click-through too. The configurable panel key while the picture is
// in front, opens the adjust panel and takes the input back; closing the panel hands
// it to the program again. The configurable exit key leaves. The keyboard hook has
// a dedicated message thread and only posts recognized actions to the GUI thread.
//
// Everything is in-process on the GUI thread, so a crash takes the windows with it
// and no click-through window can be left behind. A GUI thread that stops answering
// for 3 s gets the picture cloaked by a watchdog thread, so the screen is never left
// covered by a picture nobody can dismiss. The idea of the four styles comes from a
// user's external VeyraOverlay tool (2026-10-09); this is an independent implementation.
#include <QAbstractNativeEventFilter>
#include <QByteArray>
#include <windows.h>
#include <dwmapi.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <format>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>
#include "veyra/Log.h"
#include "veyra/source/ScreenCaptureSource.h"
#include "veyra/ui/GameHotkeys.h"
#include "veyra/ui/GameModePolicy.h"

namespace veyra::ui {

class GamePassthrough final : public QAbstractNativeEventFilter {
public:
    // Off: a normal window. Panel: on top and interactive (Home panel open).
    // Through: exact click-through. Scaled: blocks mouse, target retains keyboard/gamepad.
    using Mode = GameInputMode;
    enum class Event { PanelHotkey, ExitHotkey, ExitUnavailable, HangReleased };
    std::function<void(Event)> onEvent;

    GamePassthrough() { s_instance = this; }
    ~GamePassthrough() override {
        stopWatchdog();
        apply(Mode::Off);
        keys_.stop();
        if (receiver_) DestroyWindow(receiver_);
        if (s_instance == this) s_instance = nullptr;
    }
    GamePassthrough(const GamePassthrough&) = delete;
    GamePassthrough& operator=(const GamePassthrough&) = delete;

    void attach(HWND main) {
        if (main_ == main) return;
        const Mode previous = mode_;
        apply(Mode::Off);
        main_ = main;
        watchedMain_.store(main);
        if (previous != Mode::Off) apply(previous);
    }
    // window: the captured window (window capture), else nullptr. monitor: where the
    // picture is shown (the captured monitor, or the monitor of the captured window).
    void setTarget(HWND window, HMONITOR monitor, bool monitorCapture) {
        target_ = window; monitor_ = monitor; monitorCapture_ = monitorCapture;
    }
    Mode mode() const { return mode_; }

    void setHotkeys(GameKey panel, GameKey exit) { keys_.bindings(panel, exit); }
    // WGC captures the visible window frame, not an enlarged movie viewport.
    // Coordinates are physical pixels in the fullscreen host's client space.
    RECT pictureBounds() const {
        RECT r{}; POINT origin{};
        if (!main_ || !ClientToScreen(main_, &origin)) return {};
        if (monitorCapture_) {
            MONITORINFO m{}; m.cbSize = sizeof(m);
            if (!GetMonitorInfoW(monitor_, &m)) return {};
            r = m.rcMonitor;
        } else {
            if (!target_ || !IsWindow(target_) || IsIconic(target_)) return {};
            if (FAILED(DwmGetWindowAttribute(target_, DWMWA_EXTENDED_FRAME_BOUNDS, &r, sizeof(r))) &&
                !GetWindowRect(target_, &r)) return {};
        }
        OffsetRect(&r, -origin.x, -origin.y);
        return r;
    }

    // The listener must exist before anything may become click-through.
    bool arm() {
        if (!main_ || !IsWindow(main_)) return false;
        if (keys_.running()) return true;
        if (!ensureReceiver() || !keys_.start(receiver_)) {
            veyra::log::warn("game-mode", "keyboard listener unavailable; click-through refused");
            if (onEvent) onEvent(Event::ExitUnavailable);
            return false;
        }
        veyra::log::info("game-mode", "keyboard hook installed on dedicated message thread");
        return true;
    }
    // The caller publishes the new mode first, so Qt has applied (or removed) the
    // main window's click-through flags before the foreground moves.
    void apply(Mode next) {
        if (!main_ || next == mode_) return;
        const Mode previous = mode_;
        if (next == Mode::Off) {
            setTransparent(false);
            noActivate(false);
            sunk_ = false;
            topmost(false);
            setPanelHotkey(false);
            keys_.stop();
            if (hook_) { UnhookWinEvent(hook_); hook_ = nullptr; }
            armed_.store(false);
            mode_ = Mode::Off;
            keepTaskbarButton();
            focus(main_, "Veyra");
            log("off");
            return;
        }
        if (previous == Mode::Off) {
            if (!arm()) return;
            topmostBefore_ = (GetWindowLongPtrW(main_, GWL_EXSTYLE) & WS_EX_TOPMOST) != 0;
            hook_ = SetWinEventHook(EVENT_SYSTEM_FOREGROUND, EVENT_SYSTEM_FOREGROUND, nullptr, &GamePassthrough::winEvent, 0, 0, WINEVENT_OUTOFCONTEXT);
            startWatchdog();
        }
        mode_ = next;
        sunk_ = false;
        topmost(true);
        keepTaskbarButton();
        if (next == Mode::Through || next == Mode::Scaled) {
            setTransparent(next == Mode::Through);
            noActivate(true);
            focus(focusTarget(), "program");
        } else {
            setTransparent(false);
            noActivate(false);
            focus(main_, "Veyra");
        }
        updatePanelHotkey(GetForegroundWindow());
        log(next == Mode::Through ? "click-through" : next == Mode::Scaled ? "scaled keyboard/gamepad" : "panel");
    }

    // GUI thread, every 250 ms while the bridge runs.
    void heartbeat() {
        beat_.store(GetTickCount64());
        if (const DWORD error = keys_.takeDeliveryError(); error && mode_ != Mode::Off) {
            veyra::log::warn("game-mode", std::format("hotkey delivery failed err={}; releasing input", error));
            if (onEvent) onEvent(Event::ExitUnavailable);
            return;
        }
        if (cloaked_.exchange(false)) {
            BOOL cloak = FALSE;
            DwmSetWindowAttribute(main_, DWMWA_CLOAK, &cloak, sizeof cloak);
            veyra::log::warn("game-mode", "GUI answering again: picture uncloaked, leaving game mode");
            if (onEvent) onEvent(Event::HangReleased);   // the bridge publishes Off, then calls apply(Off)
            return;
        }
        // Something (Qt, another topmost program) may have taken the top back.
        if (mode_ != Mode::Off && !sunk_ && main_ && !(GetWindowLongPtrW(main_, GWL_EXSTYLE) & WS_EX_TOPMOST)) topmost(true);
    }

    bool nativeEventFilter(const QByteArray& type, void* message, qintptr*) override {
        if (type != "windows_generic_MSG") return false;
        const auto* msg = static_cast<const MSG*>(message);
        switch (msg->message) {
        case WM_SHOWWINDOW:
            if (msg->wParam) shown(msg->hwnd);
            return false;
        case WM_WINDOWPOSCHANGING: {
            const auto* pos = reinterpret_cast<const WINDOWPOS*>(msg->lParam);
            if (pos && (pos->flags & SWP_SHOWWINDOW)) shown(msg->hwnd);
            return false;
        }
        default:
            return false;
        }
    }

private:
    static constexpr ULONGLONG kHangMs = 3000;
    static inline GamePassthrough* s_instance = nullptr;

    HWND main_ = nullptr, target_ = nullptr;
    HWND receiver_ = nullptr; // message-only HWND; unaffected by Qt flags/fullscreen
    std::atomic<HWND> watchedMain_{nullptr};
    HMONITOR monitor_ = nullptr;
    bool monitorCapture_ = false;
    Mode mode_ = Mode::Off;
    bool panelOwned_ = false;
    GameHotkeyListener keys_;
    bool topmostBefore_ = false, sunk_ = false;
    HWINEVENTHOOK hook_ = nullptr;
    std::vector<std::pair<HWND, LONG_PTR>> changed_;   // windows made click-through, with their original style

    std::thread watchdog_;
    std::mutex watchMutex_;
    std::condition_variable watchWake_;
    bool watchStop_ = false;
    std::atomic<ULONGLONG> beat_{0};
    std::atomic<bool> armed_{false}, cloaked_{false};

    static LRESULT CALLBACK receiverProc(HWND window, UINT message, WPARAM wp, LPARAM lp) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        auto* self = reinterpret_cast<GamePassthrough*>(GetWindowLongPtrW(window, GWLP_USERDATA));
        if (self && message == GameHotkeyListener::message) {
            if (self->keys_.current(lp) && self->mode_ != Mode::Off && wp <= 1 &&
                (wp == 1 || self->panelOwned_)) {
                veyra::log::info("game-mode", std::format("hotkey received action={} epoch={}", wp, lp));
                if (self->onEvent) self->onEvent(wp == 0 ? Event::PanelHotkey : Event::ExitHotkey);
            }
            return 0;
        }
        return DefWindowProcW(window, message, wp, lp);
    }
    bool ensureReceiver() {
        if (receiver_ && IsWindow(receiver_)) return true;
        WNDCLASSW wc{};
        wc.lpfnWndProc = &GamePassthrough::receiverProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.lpszClassName = L"Veyra.GameHotkeyReceiver";
        if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) return false;
        receiver_ = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0,
                                    HWND_MESSAGE, nullptr, wc.hInstance, this);
        return receiver_ != nullptr;
    }

    void log(const char* what) {
        const LONG_PTR ex = main_ ? GetWindowLongPtrW(main_, GWL_EXSTYLE) : 0;
        veyra::log::info("game-mode", std::format("{} monitorCapture={} target=0x{:X} exstyle=0x{:X} windows={} panelOwned={} exitHook={}",
            what, monitorCapture_, uint64_t(uintptr_t(target_)), uint64_t(ex), changed_.size(), panelOwned_, keys_.running()));
    }
    static bool ours(HWND w) {
        DWORD pid = 0;
        return w && GetWindowThreadProcessId(w, &pid) && pid == GetCurrentProcessId();
    }
    bool isTarget(HWND w) const {
        if (!w || !target_) return false;
        if (GetAncestor(w, GA_ROOT) == target_) return true;
        DWORD a = 0, b = 0;
        GetWindowThreadProcessId(w, &a); GetWindowThreadProcessId(target_, &b);
        return a && a == b;
    }
    // The Start menu, Alt+Tab, the taskbar, notifications and the desktop come and
    // go above or below everything; they never move the picture.
    static bool shellSurface(HWND w) {
        if (!w || w == GetShellWindow() || w == GetDesktopWindow()) return true;
        if (GetWindowLongPtrW(w, GWL_EXSTYLE) & (WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE)) return true;
        wchar_t name[64]{};
        GetClassNameW(w, name, 64);
        const std::wstring_view cls(name);
        return cls == L"Progman" || cls == L"WorkerW" || cls == L"Shell_TrayWnd" || cls == L"Shell_SecondaryTrayWnd" ||
               cls == L"Windows.UI.Core.CoreWindow" || cls == L"XamlExplorerHostIslandWindow" || cls == L"ForegroundStaging" ||
               cls == L"MultitaskingViewFrame" || cls == L"TaskSwitcherWnd";
    }
    void topmost(bool on) {
        const bool want = on || topmostBefore_;
        SetWindowPos(main_, want ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    }
    // Qt::WindowDoesNotAcceptFocus stops Qt's own activation but does not set
    // WS_EX_NOACTIVATE on this window; without it Veyra could still take the
    // foreground from the game (a shell activation, a raise). Set after Qt's flags.
    void noActivate(bool on) {
        const LONG_PTR ex = GetWindowLongPtrW(main_, GWL_EXSTYLE);
        if (on == ((ex & WS_EX_NOACTIVATE) != 0)) return;
        SetWindowLongPtrW(main_, GWL_EXSTYLE, on ? ex | WS_EX_NOACTIVATE : ex & ~LONG_PTR(WS_EX_NOACTIVATE));
    }
    // A Qt flag change rebuilds the window style; the taskbar button's minimise
    // bits (main.cpp, allowTaskbarMinimise) are put back as on a fullscreen switch.
    void keepTaskbarButton() {
        const LONG_PTR style = GetWindowLongPtrW(main_, GWL_STYLE);
        if ((style & (WS_MINIMIZEBOX | WS_SYSMENU)) != (WS_MINIMIZEBOX | WS_SYSMENU))
            SetWindowLongPtrW(main_, GWL_STYLE, style | WS_MINIMIZEBOX | WS_SYSMENU);
    }
    // Windows other than the main one (normally hidden while the picture is in
    // front): only a layered window lets clicks through, so a plain one is layered
    // here at full opacity and gets its exact style back afterwards.
    void markTransparent(HWND w, bool requireVisible) {
        if (w == main_ || !ours(w) || (requireVisible && !IsWindowVisible(w)) || (GetWindowLongPtrW(w, GWL_STYLE) & WS_CHILD)) return;
        for (const auto& c : changed_) if (c.first == w) return;
        const LONG_PTR ex = GetWindowLongPtrW(w, GWL_EXSTYLE);
        changed_.emplace_back(w, ex);
        SetWindowLongPtrW(w, GWL_EXSTYLE, ex | WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE);
        if (!(ex & WS_EX_LAYERED) && !SetLayeredWindowAttributes(w, 0, 255, LWA_ALPHA))
            veyra::log::warn("game-mode", std::format("layered attributes failed hwnd=0x{:X} err={}", uint64_t(uintptr_t(w)), GetLastError()));
    }
    void setTransparent(bool on) {
        if (on) {
            EnumWindows([](HWND w, LPARAM self) -> BOOL {
                reinterpret_cast<GamePassthrough*>(self)->markTransparent(w, true);
                return TRUE;
            }, reinterpret_cast<LPARAM>(this));
            return;
        }
        constexpr LONG_PTR bits = WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE;
        for (const auto& [w, ex] : changed_)
            if (IsWindow(w)) SetWindowLongPtrW(w, GWL_EXSTYLE, (GetWindowLongPtrW(w, GWL_EXSTYLE) & ~bits) | (ex & bits));
        changed_.clear();
    }
    void shown(HWND w) {
        if (GetWindowLongPtrW(w, GWL_STYLE) & WS_CHILD) return;
        source::excludeWindowFromScreenCapture(w);
        if (mode_ == Mode::Through) markTransparent(w, false);
    }
    // Window capture: the captured window. Monitor capture: the first ordinary window
    // on that monitor in z-order (what the user was using before Veyra's dialog), else
    // the desktop.
    HWND focusTarget() const {
        if (!monitorCapture_) return target_ && IsWindow(target_) ? target_ : nullptr;
        struct Find { HMONITOR monitor; HWND hit; } find{monitor_, nullptr};
        EnumWindows([](HWND w, LPARAM p) -> BOOL {
            auto& f = *reinterpret_cast<Find*>(p);
            if (ours(w) || !IsWindowVisible(w) || IsIconic(w) || shellSurface(w) || GetWindowTextLengthW(w) == 0) return TRUE;
            DWORD cloaked = 0;
            DwmGetWindowAttribute(w, DWMWA_CLOAKED, &cloaked, sizeof cloaked);
            if (cloaked || MonitorFromWindow(w, MONITOR_DEFAULTTONULL) != f.monitor) return TRUE;
            f.hit = w;
            return FALSE;
        }, reinterpret_cast<LPARAM>(&find));
        return find.hit ? find.hit : GetShellWindow();
    }
    void focus(HWND w, const char* what) {
        if (!w || !IsWindow(w)) return;
        if (IsIconic(w)) ShowWindow(w, SW_RESTORE);
        bool ok = SetForegroundWindow(w) && GetForegroundWindow() == w;
        if (!ok) {
            // Refused unless this thread owns the foreground: borrowing the
            // foreground thread's input state lifts that.
            const HWND fg = GetForegroundWindow();
            const DWORD fgThread = fg ? GetWindowThreadProcessId(fg, nullptr) : 0, self = GetCurrentThreadId();
            const bool attached = fgThread && fgThread != self && AttachThreadInput(fgThread, self, TRUE);
            BringWindowToTop(w);
            SetForegroundWindow(w);
            if (attached) AttachThreadInput(fgThread, self, FALSE);
            ok = GetForegroundWindow() == w;
        }
        veyra::log::info("game-mode", std::format("focus -> {} 0x{:X} ok={}", what, uint64_t(uintptr_t(w)), ok));
    }
    void setPanelHotkey(bool on) {
        if (on == panelOwned_) return;
        panelOwned_ = on;
        keys_.panelOwned(on);
    }
    // Home belongs to Veyra while the user is "inside" the picture: the captured
    // program (window capture) or anything on that monitor (monitor capture). In
    // other programs it stays theirs.
    void updatePanelHotkey(HWND fg) {
        bool want = mode_ == Mode::Panel || ours(fg);
        if (!want && (mode_ == Mode::Through || mode_ == Mode::Scaled))
            want = monitorCapture_ ? MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) == monitor_ : isTarget(fg);
        setPanelHotkey(want);
    }
    static void CALLBACK winEvent(HWINEVENTHOOK, DWORD event, HWND hwnd, LONG object, LONG, DWORD, DWORD) {
        if (event == EVENT_SYSTEM_FOREGROUND && object == OBJID_WINDOW && s_instance) s_instance->foregroundChanged(hwnd);
    }
    void foregroundChanged(HWND fg) {
        if (mode_ == Mode::Off) return;
        updatePanelHotkey(fg);
        if ((mode_ != Mode::Through && mode_ != Mode::Scaled) || monitorCapture_ || ours(fg)) return;
        if (isTarget(fg)) {
            if (sunk_) { sunk_ = false; topmost(true); log("program back in front: picture on top"); }
            return;
        }
        // Another program on this monitor came forward (Alt+Tab, a chat window): the
        // picture steps behind it, still above the game, until the game is back.
        if (sunk_ || shellSurface(fg) || MonitorFromWindow(fg, MONITOR_DEFAULTTONEAREST) != monitor_) return;
        sunk_ = true;
        const HWND front = GetAncestor(fg, GA_ROOT);
        SetWindowPos(main_, front ? front : fg, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        if (target_ && IsWindow(target_)) SetWindowPos(target_, main_, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        log("another program in front: picture behind it");
    }
    void startWatchdog() {
        beat_.store(GetTickCount64());
        armed_.store(true);
        if (watchdog_.joinable()) return;
        watchdog_ = std::thread([this] {
            std::unique_lock lock(watchMutex_);
            while (!watchStop_) {
                watchWake_.wait_for(lock, std::chrono::milliseconds(250));
                if (watchStop_) break;
                if (!armed_.load() || cloaked_.load()) continue;
                const ULONGLONG stale = GetTickCount64() - beat_.load();
                if (stale < kHangMs) continue;
                // DWM cloaking needs nothing from the stuck GUI thread.
                BOOL cloak = TRUE;
                const HWND main = watchedMain_.load();
                const HRESULT hr = main && ours(main) ? DwmSetWindowAttribute(main, DWMWA_CLOAK, &cloak, sizeof cloak) : E_HANDLE;
                cloaked_.store(SUCCEEDED(hr));
                veyra::log::warn("game-mode", std::format("GUI silent for {} ms: picture cloaked hr=0x{:08X}", stale, unsigned(hr)));
            }
        });
    }
    void stopWatchdog() {
        { std::lock_guard lock(watchMutex_); watchStop_ = true; }
        watchWake_.notify_all();
        if (watchdog_.joinable()) watchdog_.join();
    }
};

}  // namespace veyra::ui
