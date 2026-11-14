#pragma once
// Low-level hook direction from the user's fan patch (2026-10-16).
// Veyra adaptation: a dedicated message thread, bounded callback work, queued UI
// events, paired releases and autorepeat suppression. No input injection into games.
#include <windows.h>
#include <QKeySequence>
#include <array>
#include <atomic>
#include <future>
#include <thread>

namespace veyra::ui {
struct GameKey {
    unsigned vk = 0, mods = 0; // Ctrl=1, Alt=2, Shift=4 (Win reserved)
    bool operator==(const GameKey&) const = default;
};
inline GameKey gameKey(const QKeySequence& sequence) {
    if (sequence.isEmpty() || sequence.count() != 1) return {};
    const auto c = sequence[0]; const int k = c.key(); const auto m = c.keyboardModifiers();
    if (m & ~(Qt::ControlModifier | Qt::AltModifier | Qt::ShiftModifier)) return {};
    unsigned vk = 0;
    if ((k >= 'A' && k <= 'Z') || (k >= '0' && k <= '9')) vk = unsigned(k);
    else if (k >= Qt::Key_F1 && k <= Qt::Key_F24) vk = VK_F1 + unsigned(k - Qt::Key_F1);
    else switch (k) {
    case Qt::Key_Home: vk = VK_HOME; break; case Qt::Key_End: vk = VK_END; break;
    case Qt::Key_PageUp: vk = VK_PRIOR; break; case Qt::Key_PageDown: vk = VK_NEXT; break;
    case Qt::Key_Insert: vk = VK_INSERT; break; case Qt::Key_Delete: vk = VK_DELETE; break;
    case Qt::Key_Space: vk = VK_SPACE; break; case Qt::Key_Tab: vk = VK_TAB; break;
    case Qt::Key_Backspace: vk = VK_BACK; break; case Qt::Key_Return: case Qt::Key_Enter: vk = VK_RETURN; break;
    default: break;
    }
    return {vk, unsigned((m & Qt::ControlModifier ? 1 : 0) | (m & Qt::AltModifier ? 2 : 0) | (m & Qt::ShiftModifier ? 4 : 0))};
}

class GameKeyState {
public:
    struct Result { bool consume = false; int action = -1; };
    void seed(unsigned vk, bool down) { if (vk < keys_.size()) keys_[vk] = down; }
    // Actions: 0 panel, 1 exit, 2 screenshot, 3 record, 4 save replay. Everything
    // but exit only while the picture owns the keys (panelOwned), so Veyra in
    // front never sees a key twice and other programs keep theirs. An unbound
    // key has vk 0.
    Result input(unsigned vk, bool down, GameKey panel, GameKey exit, bool panelOwned, GameKey shot = {}, GameKey record = {}, GameKey replay = {}) {
        if (vk >= keys_.size()) return {};
        const bool repeat = keys_[vk];
        const bool claimed = claimed_[vk];
        // Remember modifiers and hotkey trigger state only, never other typed keys.
        const bool modifier = vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
            vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU || vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
            vk == VK_LWIN || vk == VK_RWIN;
        const bool bound = vk == panel.vk || vk == exit.vk || (shot.vk && vk == shot.vk) || (record.vk && vk == record.vk) ||
            (replay.vk && vk == replay.vk);
        if (modifier || bound || claimed || repeat) keys_[vk] = down;
        if (!down) { claimed_[vk] = false; return {claimed, -1}; }
        if (claimed) return {true, -1};
        if (repeat || modifier) return {};
        const unsigned mods = ((keys_[VK_CONTROL] || keys_[VK_LCONTROL] || keys_[VK_RCONTROL]) ? 1 : 0) |
            ((keys_[VK_MENU] || keys_[VK_LMENU] || keys_[VK_RMENU]) ? 2 : 0) |
            ((keys_[VK_SHIFT] || keys_[VK_LSHIFT] || keys_[VK_RSHIFT]) ? 4 : 0) |
            ((keys_[VK_LWIN] || keys_[VK_RWIN]) ? 8 : 0);
        const GameKey pressed{vk, mods};
        const int action = pressed == exit ? 1 : !panelOwned ? -1 : pressed == panel ? 0 :
            shot.vk && pressed == shot ? 2 : record.vk && pressed == record ? 3 : replay.vk && pressed == replay ? 4 : -1;
        if (action < 0) return {};
        claimed_[vk] = true;
        return {true, action};
    }
private:
    std::array<bool, 256> keys_{}, claimed_{};
};

class GameHotkeyListener {
public:
    static constexpr UINT message = WM_APP + 0x347;
    ~GameHotkeyListener() { stop(); }
    // Five 12-bit slots (vk, mods << 8): panel, exit, screenshot, record, replay.
    static constexpr uint64_t pack(GameKey panel, GameKey exit, GameKey shot = {}, GameKey record = {}, GameKey replay = {}) {
        const auto bits = [](GameKey k) { return uint64_t(k.vk & 255) | (uint64_t(k.mods & 15) << 8); };
        return bits(panel) | (bits(exit) << 12) | (bits(shot) << 24) | (bits(record) << 36) | (bits(replay) << 48);
    }
    static constexpr GameKey slot(uint64_t config, unsigned index) {
        const unsigned at = index * 12;
        return {unsigned((config >> at) & 255), unsigned((config >> (at + 8)) & 15)};
    }
    void bindings(GameKey panel, GameKey exit, GameKey shot = {}, GameKey record = {}, GameKey replay = {}) {
        config_.store(pack(panel, exit, shot, record, replay));
        ++epoch_;
    }
    void panelOwned(bool on) { panelOwned_.store(on); }
    bool running() const { return running_.load(); }
    DWORD takeDeliveryError() { return deliveryError_.exchange(0); }
    bool current(LPARAM epoch) const { return running() && uint64_t(epoch) == epoch_.load(); }
    bool start(HWND target) {
        if (running()) return true;
        stop(); target_ = target;
        deliveryError_.store(0);
        if (!IsWindow(target_)) return false;
        stopEvent_ = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        if (!stopEvent_) return false;
        std::promise<bool> ready; auto initialized = ready.get_future();
        try {
            thread_ = std::thread([this, ready = std::move(ready)]() mutable {
                self_ = this; state_ = {};
                for (unsigned k : {VK_LCONTROL, VK_RCONTROL, VK_LMENU, VK_RMENU, VK_LSHIFT, VK_RSHIFT, VK_LWIN, VK_RWIN})
                    state_.seed(k, (GetAsyncKeyState(int(k)) & 0x8000) != 0);
                const HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, callback, GetModuleHandleW(nullptr), 0);
                running_.store(hook != nullptr); ready.set_value(hook != nullptr);
                if (hook) {
                    while (MsgWaitForMultipleObjects(1, &stopEvent_, FALSE, INFINITE, QS_ALLINPUT) == WAIT_OBJECT_0 + 1) {
                        MSG msg; while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
                    }
                    UnhookWindowsHookEx(hook);
                }
                running_.store(false); self_ = nullptr;
            });
        } catch (...) { CloseHandle(stopEvent_); stopEvent_ = nullptr; return false; }
        const bool ok = initialized.get();
        if (!ok) stop();
        return ok;
    }
    void stop() {
        running_.store(false); ++epoch_;
        if (stopEvent_) SetEvent(stopEvent_);
        if (thread_.joinable()) thread_.join();
        if (stopEvent_) { CloseHandle(stopEvent_); stopEvent_ = nullptr; }
        panelOwned_.store(false);
    }
private:
    HWND target_ = nullptr;
    HANDLE stopEvent_ = nullptr;
    std::thread thread_;
    std::atomic<bool> running_{false}, panelOwned_{false};
    std::atomic<DWORD> deliveryError_{0};
    std::atomic<uint64_t> config_{pack({VK_HOME, 0}, {VK_HOME, 3})}, epoch_{0};
    GameKeyState state_;
    static inline thread_local GameHotkeyListener* self_ = nullptr;
    static LRESULT CALLBACK callback(int code, WPARAM wp, LPARAM lp) {
        if (code == HC_ACTION && self_ && self_->running()) {
            const auto* key = reinterpret_cast<const KBDLLHOOKSTRUCT*>(lp);
            const bool down = wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN;
            const bool up = wp == WM_KEYUP || wp == WM_SYSKEYUP;
            if (key && (down || up)) {
                auto& self = *self_; const auto epoch = self.epoch_.load(); const auto config = self.config_.load();
                const auto result = self.state_.input(key->vkCode, down, slot(config, 0), slot(config, 1), self.panelOwned_.load(),
                                                      slot(config, 2), slot(config, 3), slot(config, 4));
                if (result.action >= 0 && epoch == self.epoch_.load() &&
                    !PostMessageW(self.target_, message, WPARAM(result.action), LPARAM(epoch))) {
                    const DWORD error = GetLastError();
                    self.deliveryError_.store(error ? error : ERROR_INVALID_WINDOW_HANDLE);
                    return CallNextHookEx(nullptr, code, wp, lp);
                }
                if (result.consume) return 1;
            }
        }
        return CallNextHookEx(nullptr, code, wp, lp);
    }
};
}
