// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/StreamInputCapture.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <chrono>

#include "veyra/Log.h"
#include "veyra/engine/EngineController.h"

namespace veyra::ui {
namespace {
using Mapper = remoteplay::KeyboardMousePad;

int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

QString bindingLabel(int binding) {
    if (!binding) return QStringLiteral("—");
    QString text;
    const int mods = binding >> 9;
    if (mods & Mapper::Ctrl) text += QStringLiteral("Ctrl+");
    if (mods & Mapper::Alt) text += QStringLiteral("Alt+");
    if (mods & Mapper::Shift) text += QStringLiteral("Shift+");
    const int key = binding & Mapper::KeyMask;
    using T = StreamInputCapture;
    switch (key) {
    case Mapper::MouseLeft: return text + T::tr("鼠标左键");
    case Mapper::MouseMiddle: return text + T::tr("鼠标中键");
    case Mapper::MouseRight: return text + T::tr("鼠标右键");
    case Mapper::MouseBack: return text + T::tr("鼠标侧键 1");
    case Mapper::MouseForward: return text + T::tr("鼠标侧键 2");
    case Mapper::WheelUp: return text + T::tr("滚轮向上");
    case Mapper::WheelDown: return text + T::tr("滚轮向下");
    case Mapper::WheelLeft: return text + T::tr("滚轮向左");
    case Mapper::WheelRight: return text + T::tr("滚轮向右");
    default: break;
    }
    const UINT scan = MapVirtualKeyW(UINT(key), MAPVK_VK_TO_VSC_EX);
    const LONG type = LONG((scan & 255) << 16) | ((scan & 0xff00) ? 1L << 24 : 0);
    wchar_t name[64]{};
    if (const int size = GetKeyNameTextW(type, name, 64); size > 0)
        return text + QString::fromWCharArray(name, size);
    return text + QStringLiteral("VK %1").arg(key, 2, 16, QLatin1Char('0')).toUpper();
}

QVariantMap tuningMap(const Mapper::Tuning& t) {
    return {{QStringLiteral("horizontal"), t.horizontal}, {QStringLiteral("vertical"), t.vertical},
            {QStringLiteral("curve"), t.curve}, {QStringLiteral("deadzone"), t.deadzone},
            {QStringLiteral("smoothing"), t.smoothingMs}, {QStringLiteral("invertY"), t.invertY},
            {QStringLiteral("mouseLook"), t.mouseLook}};
}

Mapper::Tuning readTuning(const QVariantMap& map) {
    const auto bounded = [&map](const char* key, double fallback, double lo, double hi) {
        bool ok = false;
        const double value = map.value(QString::fromLatin1(key)).toDouble(&ok);
        return ok && std::isfinite(value) ? std::clamp(value, lo, hi) : fallback;
    };
    Mapper::Tuning t;
    t.horizontal = bounded("horizontal", 1.0, 0.1, 6.0);
    t.vertical = bounded("vertical", 1.0, 0.1, 6.0);
    t.curve = bounded("curve", 1.0, 0.5, 2.5);
    t.deadzone = bounded("deadzone", 0.08, 0.0, 0.30);
    t.smoothingMs = bounded("smoothing", 0, 0, 32);
    t.invertY = map.value(QStringLiteral("invertY"), false).toBool();
    t.mouseLook = map.value(QStringLiteral("mouseLook"), true).toBool();
    return t;
}
} // namespace

StreamInputCapture::StreamInputCapture(engine::EngineController& engine, QString profilePath,
                                       std::function<uint64_t()> ps5Target)
    : engine_(engine), router_(*this), profilePath_(std::move(profilePath)), ps5Target_(std::move(ps5Target)) {
    load();
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(8);
    connect(&timer_, &QTimer::timeout, this, &StreamInputCapture::tick);
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) return;
        end();
        cancelLearning();
    });
}

StreamInputCapture::~StreamInputCapture() {
    end();
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
}

void StreamInputCapture::setWindow(HWND window) {
    if (window_ != window) end();
    window_ = window;
}

bool StreamInputCapture::capture(bool on) {
    if (!on) {
        if (captured()) end();
        return true;
    }
    return begin(Route::Moonlight);
}

bool StreamInputCapture::capturePs5(bool on) {
    if (!on) {
        if (ps5Captured()) end();
        return true;
    }
    if (!keyboardEnabled_ || !ps5Target_ || !ps5Target_()) return false;
    return begin(Route::Ps5);
}

bool StreamInputCapture::begin(Route route) {
    if (route_ == route) return true;
    if (!window_ || GetForegroundWindow() != window_) return false;
    end();
    cancelLearning();
    RAWINPUTDEVICE device{0x01, 0x02, 0, window_};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        veyra::log::warn("stream-input", "RegisterRawInputDevices failed");
        return false;
    }
    router_.reset();
    mapper_.reset();
    route_ = route;
    target_ = route_ == Route::Ps5 ? ps5Target_() : 0;
    clipToWindow();
    SetCursor(nullptr);
    if (route_ != Route::Moonlight) {
        mouse_ = std::make_shared<remoteplay::MouseStick>(mapper_.tuning());
        timer_.start();
        emitPad();
        if (route_ != route) return false;
    }
    if (captured()) emit capturedChanged(true);
    emit stateChanged();
    return true;
}

void StreamInputCapture::end() {
    if (route_ == Route::None) return;
    const Route old = route_;
    route_ = Route::None;
    timer_.stop();
    mouse_.reset();
    RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};
    RegisterRawInputDevices(&device, 1, sizeof(device));
    ClipCursor(nullptr);
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    router_.reset();
    mapper_.reset();
    wheelKey_ = 0;
#ifdef VEYRA_ENABLE_MOONLIGHT
    if (old == Route::Moonlight) engine_.moonlightReleaseInput();
#endif
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (old == Route::Ps5 && ps5Target_ && ps5Target_() == target_) engine_.remotePlayController({});
#endif
    target_ = 0;
    if (old == Route::Moonlight) emit capturedChanged(false);
    if (old == Route::Preview) { previewState_ = {}; emit padStateChanged(); }
    emit stateChanged();
}

void StreamInputCapture::clipToWindow() {
    RECT client{};
    if (!window_ || !GetClientRect(window_, &client)) return;
    POINT topLeft{client.left, client.top}, bottomRight{client.right, client.bottom};
    ClientToScreen(window_, &topLeft);
    ClientToScreen(window_, &bottomRight);
    const RECT screen{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    ClipCursor(&screen);
}

bool StreamInputCapture::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    if ((route_ == Route::None && learning_ < 0) || eventType != "windows_generic_MSG") return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (!msg->hwnd || GetAncestor(msg->hwnd, GA_ROOT) != window_) return false;
    const bool keyMessage = msg->message == WM_KEYDOWN || msg->message == WM_KEYUP ||
        msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP;
    const auto consume = [result] { if (result) *result = 0; return true; };
    if (!captured() && keyMessage && msg->wParam == VK_ESCAPE) {
        if (learning_ >= 0) cancelLearning();
        else end();
        return consume();
    }
    if (learning_ >= 0) {
        if (!keyMessage && msg->message != WM_MOUSEWHEEL && msg->message != WM_MOUSEHWHEEL &&
            msg->message != WM_LBUTTONDOWN && msg->message != WM_RBUTTONDOWN &&
            msg->message != WM_MBUTTONDOWN && msg->message != WM_XBUTTONDOWN) return false;
        return router_.message(msg->message, msg->wParam, msg->lParam, false) && consume();
    }
    if (!captured() && keyMessage && msg->wParam >= VK_F7 && msg->wParam <= VK_F10) return false;
    switch (msg->message) {
    case WM_INPUT: {
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, &raw, &size,
                            sizeof(RAWINPUTHEADER)) != UINT(-1) &&
            raw.header.dwType == RIM_TYPEMOUSE && (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            const auto dx = raw.data.mouse.lLastX, dy = raw.data.mouse.lLastY;
            if (mouse_ && (dx || dy)) {
                // MSG.time uses the coarse uptime clock. Allow one tick of
                // skew and date queued reports so a GUI stall cannot replay
                // a large movement as one new instantaneous turn.
                const DWORD queueMs = GetTickCount() - msg->time;
                if (queueMs <= 40 || queueMs >= 0x80000000u) {
                    const auto reportMs = nowUs() / 1000 - (queueMs > 16 && queueMs <= 40 ? queueMs : 0);
                    mouse_->move(dx, dy, reportMs * 1000);
                }
            } else if (captured()) {
                router_.rawMouse(dx, dy);
            }
        }
        // Let the default window procedure complete foreground Raw Input cleanup.
        return false;
    }
    case WM_SETCURSOR:
        SetCursor(nullptr);
        if (result) *result = TRUE;
        return true;
    case WM_SIZE: case WM_MOVE: clipToWindow(); return false;
    case WM_ACTIVATE:
        if (LOWORD(msg->wParam) == WA_INACTIVE) end();
        return false;
    case WM_KILLFOCUS: end(); return false;
    default: break;
    }
    const bool consumed = router_.message(msg->message, msg->wParam, msg->lParam, captured());
    if (captured()) {
        switch (router_.takeReserved()) {
        case moonlight::Reserved::ReleaseCapture: end(); emit releaseRequested(); break;
        case moonlight::Reserved::Quit: end(); emit quitRequested(); break;
        case moonlight::Reserved::ToggleStats: emit statsRequested(); break;
        case moonlight::Reserved::None: break;
        }
    }
    return consumed && consume();
}

void StreamInputCapture::key(uint32_t virtualKey, uint32_t scanCode, bool extended, bool down) {
    if (captured()) {
#ifdef VEYRA_ENABLE_MOONLIGHT
        engine_.moonlightKey(virtualKey, scanCode, extended, down);
#endif
        return;
    }
    digital(uint16_t(moonlight::hostKey(virtualKey, scanCode, extended).code) & 255, down);
}
void StreamInputCapture::mouseMove(int dx, int dy) {
#ifdef VEYRA_ENABLE_MOONLIGHT
    if (captured()) engine_.moonlightMouseMove(dx, dy);
#else
    (void)dx;
    (void)dy;
#endif
}
void StreamInputCapture::mouseButton(int button, bool down) {
    if (captured()) {
#ifdef VEYRA_ENABLE_MOONLIGHT
        engine_.moonlightMouseButton(button, down);
#endif
    } else {
        digital(Mapper::MouseLeft + button - 1, down);
    }
}
void StreamInputCapture::scroll(int delta, bool horizontal) {
    if (captured()) {
#ifdef VEYRA_ENABLE_MOONLIGHT
        engine_.moonlightScroll(delta, horizontal);
#endif
        return;
    }
    const int key = horizontal ? (delta > 0 ? Mapper::WheelRight : Mapper::WheelLeft) :
        (delta > 0 ? Mapper::WheelUp : Mapper::WheelDown);
    if (learning_ >= 0) { bind(key, mapper_.modifiers()); return; }
    if (wheelKey_) digital(wheelKey_, false);
    digital(key, true);
    wheelKey_ = key;
    wheelReleaseUs_ = nowUs() + 24000;
}

void StreamInputCapture::digital(int key, bool down) {
    if (learning_ >= 0) {
        const int mod = Mapper::modifier(key);
        const int before = mapper_.modifiers();
        mapper_.digital(key, down);
        if (mod && down) learningModifier_ = key;
        else if (mod && key == learningModifier_) bind(key, before);
        else if (down) bind(key, before);
        return;
    }
    if (mapper_.digital(key, down)) emitPad();
}

void StreamInputCapture::tick() {
    if (wheelKey_ && nowUs() >= wheelReleaseUs_) {
        mapper_.digital(wheelKey_, false);
        wheelKey_ = 0;
    }
    emitPad();
}

void StreamInputCapture::emitPad() {
    bool useMouse = false;
    auto pad = mapper_.state(&useMouse);
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (ps5Captured()) {
        if (!target_ || ps5Target_() != target_) { end(); return; }
        engine_.remotePlayController(pad, useMouse ? std::weak_ptr(mouse_) : std::weak_ptr<remoteplay::MouseStick>{});
    }
#endif
    if (previewing()) {
        if (useMouse) {
            const auto [x, y] = mouse_->sample(nowUs());
            pad.rightX = x;
            pad.rightY = y;
        }
        if (pad != previewState_) { previewState_ = pad; emit padStateChanged(); }
    }
}

QVariantList StreamInputCapture::bindings() const {
    QVariantList list;
    for (const int binding : mapper_.bindings()) list.push_back(bindingLabel(binding));
    return list;
}
QVariantMap StreamInputCapture::tuning() const { return tuningMap(mapper_.tuning()); }
QVariantMap StreamInputCapture::padState() const {
    return {{QStringLiteral("buttons"), previewState_.buttons}, {QStringLiteral("leftX"), previewState_.leftX / 32767.0},
            {QStringLiteral("leftY"), previewState_.leftY / 32767.0}, {QStringLiteral("rightX"), previewState_.rightX / 32767.0},
            {QStringLiteral("rightY"), previewState_.rightY / 32767.0}, {QStringLiteral("l2"), previewState_.l2},
            {QStringLiteral("r2"), previewState_.r2}};
}
void StreamInputCapture::setKeyboardEnabled(bool enabled) {
    if (enabled == keyboardEnabled_) return;
    releasePs5();
    keyboardEnabled_ = enabled;
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (enabled && ps5Target_ && ps5Target_()) engine_.remotePlayController({});
#endif
    save();
    emit mappingChanged();
}
void StreamInputCapture::learn(int action) {
    if (action < 0 || action >= Mapper::ActionCount) return;
    end();
    // The editor can be opened from Home before any video HWND exists.
    window_ = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(window_, &pid);
    if (pid != GetCurrentProcessId()) return;
    mapper_.reset();
    router_.reset();
    learning_ = action;
    learningModifier_ = 0;
    feedback_.clear();
    emit mappingChanged();
}
void StreamInputCapture::bind(int key, int modifiers) {
    const int encoded = Mapper::encode(key, modifiers);
    if (!Mapper::validBinding(encoded)) {
        feedback_ = tr("此键用于系统或 Veyra 快捷键，请换一个键。");
        emit mappingChanged();
        return;
    }
    const auto& all = mapper_.bindings();
    const auto used = std::find(all.begin(), all.end(), encoded);
    feedback_ = used != all.end() && int(used - all.begin()) != learning_ ?
        tr("已将 %1 从原按键移到此处。").arg(bindingLabel(encoded)) : QString{};
    mapper_.setBinding(learning_, encoded);
    learning_ = -1;
    save();
    emit mappingChanged();
}
void StreamInputCapture::clearBinding(int action) {
    releasePs5();
    if (mapper_.setBinding(action, 0)) { save(); emit mappingChanged(); }
}
void StreamInputCapture::cancelLearning() {
    if (learning_ < 0) return;
    learning_ = -1;
    mapper_.reset();
    router_.reset();
    emit mappingChanged();
}
void StreamInputCapture::applyDefaults() {
    releasePs5();
    mapper_.resetBindings();
    mapper_.setTuning({});
    feedback_.clear();
    save();
    emit mappingChanged();
}
void StreamInputCapture::setTuning(const QString& key, const QVariant& value) {
    auto map = tuning();
    if (!map.contains(key)) return;
    map[key] = value;
    const auto next = readTuning(map);
    if (next == mapper_.tuning()) return;
    mapper_.setTuning(next);
    if (mouse_) mouse_->setTuning(next);
    save();
    emit mappingChanged();
}
void StreamInputCapture::preview(bool on) {
    if (!on) { if (previewing()) end(); return; }
    const HWND window = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) return;
    setWindow(window);
    begin(Route::Preview);
}
void StreamInputCapture::releasePs5() {
    if (ps5Captured() || previewing()) end();
    cancelLearning();
}
void StreamInputCapture::load() {
    QFile file(profilePath_);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 64 * 1024) return;
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    if (json.value(QStringLiteral("version")).toInt() != 1) return;
    const auto list = json.value(QStringLiteral("bindings")).toArray();
    if (list.size() == Mapper::ActionCount) {
        for (int i = 0; i < Mapper::ActionCount; ++i) mapper_.setBinding(i, 0);
        for (int i = 0; i < Mapper::ActionCount; ++i)
            mapper_.setBinding(i, list[i].toInt(Mapper::Defaults[i]));
    }
    keyboardEnabled_ = json.value(QStringLiteral("enabled")).toBool();
    mapper_.setTuning(readTuning(json.value(QStringLiteral("tuning")).toObject().toVariantMap()));
}
void StreamInputCapture::save() {
    QJsonArray list;
    for (const int binding : mapper_.bindings()) list.append(binding);
    const QJsonObject json{{QStringLiteral("version"), 1}, {QStringLiteral("enabled"), keyboardEnabled_},
        {QStringLiteral("bindings"), list}, {QStringLiteral("tuning"), QJsonObject::fromVariantMap(tuning())}};
    QDir().mkpath(QFileInfo(profilePath_).absolutePath());
    QSaveFile file(profilePath_);
    const auto data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        feedback_ = tr("无法保存键鼠映射。");
        veyra::log::warn("stream-input", "saving PS5 keyboard mapping failed");
    }
}

} // namespace veyra::ui
