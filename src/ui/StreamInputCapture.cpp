// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/StreamInputCapture.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QCursor>
#include <QQuickItem>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <chrono>
#include <format>
#include <future>
#include <thread>
#pragma comment(lib,"imm32.lib")

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
    case VK_LEFT:return text+QStringLiteral("←");
    case VK_RIGHT:return text+QStringLiteral("→");
    case VK_UP:return text+QStringLiteral("↑");
    case VK_DOWN:return text+QStringLiteral("↓");
    case VK_LSHIFT:return text+QStringLiteral("LShift");
    case VK_RSHIFT:return text+QStringLiteral("RShift");
    case VK_LCONTROL:return text+QStringLiteral("LCtrl");
    case VK_RCONTROL:return text+QStringLiteral("RCtrl");
    case VK_LMENU:return text+QStringLiteral("LAlt");
    case VK_RMENU:return text+QStringLiteral("RAlt");
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
            {QStringLiteral("turnBoost"), t.turnBoost}, {QStringLiteral("inputDeadzone"), t.inputDeadzone},
            {"dpi",t.dpi},{"turnSpeed",t.turnSpeed},{"adsScale",t.adsScale},{"adsDelayMs",t.adsDelayMs},{"adsAction",t.adsAction},
            {"deadzoneShape",t.deadzoneShape},{"walkScale",t.walkScale},{"unlock",t.unlock},{"gyro",t.gyro},{"carry",t.carry},
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
    t.dpi=bounded("dpi",t.dpi,200,6400);t.turnSpeed=bounded("turnSpeed",t.turnSpeed,1000,6000);t.adsScale=bounded("adsScale",t.adsScale,.1,3.);
    t.adsDelayMs=int(bounded("adsDelayMs",0,0,300));t.adsAction=int(bounded("adsAction",16,0,17));
    t.deadzoneShape=bounded("deadzoneShape",0,0,1);t.walkScale=bounded("walkScale",.45,.2,.8);
    t.unlock=int(bounded("unlock",0,0,4));t.gyro=int(bounded("gyro",0,0,2));t.carry=map.value("carry").toBool();
    t.horizontal = bounded("horizontal", t.horizontal, 0.1, 6.0);
    t.vertical = bounded("vertical", t.vertical, 0.1, 6.0);
    t.turnBoost = bounded("turnBoost", t.turnBoost, 1.0, 6.0);
    t.inputDeadzone = bounded("inputDeadzone", t.inputDeadzone, 0.0, 0.10);
    t.curve = bounded("curve", t.curve, 0.3, 2.5);
    t.deadzone = bounded("deadzone", t.deadzone, 0.0, 0.30);
    t.smoothingMs = bounded("smoothing", t.smoothingMs, 0, 32);
    t.invertY = map.value(QStringLiteral("invertY"), t.invertY).toBool();
    t.mouseLook = map.value(QStringLiteral("mouseLook"), t.mouseLook).toBool();
    return t;
}
} // namespace

// Mouse-only Raw Input registration for PS5. The foreground requirement is
// checked again here because the message-only window uses INPUTSINK.
struct StreamInputCapture::Ps5MouseInput {
    explicit Ps5MouseInput(HWND target, remoteplay::MouseStick& mouse)
        : target(target), mouse(mouse) {}

    bool start() {
        std::promise<bool> ready;
        auto started = ready.get_future();
        thread = std::thread([this, ready = std::move(ready)]() mutable {
            constexpr wchar_t className[] = L"VeyraPs5MouseInput";
            const auto instance = GetModuleHandleW(nullptr);
            const WNDCLASSW wc{.lpfnWndProc = dispatch, .hInstance = instance, .lpszClassName = className};
            if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                ready.set_value(false);
                return;
            }
            const auto hwnd = CreateWindowExW(0, className, nullptr, 0, 0, 0, 0, 0,
                                             HWND_MESSAGE, nullptr, instance, this);
            threadId = GetCurrentThreadId();
            RAWINPUTDEVICE device{0x01, 0x02, RIDEV_INPUTSINK, hwnd};
            const bool registered = hwnd && RegisterRawInputDevices(&device, 1, sizeof(device));
            ready.set_value(registered);
            if (registered) {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                MSG message{};
                while (GetMessageW(&message, nullptr, 0, 0) > 0)
                    DispatchMessageW(&message);
                device = {0x01, 0x02, RIDEV_REMOVE, nullptr};
                RegisterRawInputDevices(&device, 1, sizeof(device));
            }
            if (hwnd) DestroyWindow(hwnd);
        });
        return started.get();
    }

    ~Ps5MouseInput() {
        if (!thread.joinable()) return;
        if (threadId) PostThreadMessageW(threadId, WM_QUIT, 0, 0);
        thread.join();
        veyra::log::info("ps5-input", std::format(
            "mouse worker reports={} raw queue maxMs={} staleDropped={} maxDeliveryGapUs={}",
            reports, maxQueueMs, staleReports, maxDeliveryGapUs));
    }

private:
    static LRESULT CALLBACK dispatch(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        if (message == WM_INPUT) {
            auto* self = reinterpret_cast<Ps5MouseInput*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self) self->input(lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void input(LPARAM data) {
        if (GetAncestor(GetForegroundWindow(), GA_ROOT) != target) return;
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(data), RID_INPUT, &raw, &size,
                            sizeof(RAWINPUTHEADER)) == UINT(-1) ||
            raw.header.dwType != RIM_TYPEMOUSE || (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE)) return;
        const auto dx = raw.data.mouse.lLastX, dy = raw.data.mouse.lLastY;
        if (!dx && !dy) return;
        const DWORD lag = GetTickCount() - DWORD(GetMessageTime());
        if (lag < 0x80000000u) maxQueueMs = std::max(maxQueueMs, lag);
        if (lag > 40 && lag < 0x80000000u) {
            ++staleReports;
            return;
        }
        const auto now = nowUs();
        if (lastDeliveryUs && now - lastDeliveryUs <= 100000)
            maxDeliveryGapUs = std::max(maxDeliveryGapUs, now - lastDeliveryUs);
        lastDeliveryUs = now;
        ++reports;
        mouse.move(dx, dy, now);
    }

    HWND target;
    remoteplay::MouseStick& mouse;
    std::thread thread;
    DWORD threadId = 0;
    DWORD maxQueueMs = 0;
    unsigned reports = 0, staleReports = 0;
    int64_t lastDeliveryUs = 0, maxDeliveryGapUs = 0;
};

StreamInputCapture::StreamInputCapture(engine::EngineController& engine, QString profilePath,
                                       std::function<uint64_t()> ps5Target)
    : engine_(engine), router_(*this), profilePath_(std::move(profilePath)), ps5Target_(std::move(ps5Target)) {
    load();
    profiles_[profile_]=activeProfile();
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
    cancelLearning();
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
}

void StreamInputCapture::setWindow(HWND window) {
    if (window_ != window) {end();cancelLearning();}
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
    if (route == Route::Moonlight) {
        RAWINPUTDEVICE device{0x01, 0x02, 0, window_};
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            veyra::log::warn("stream-input", "RegisterRawInputDevices failed");
            return false;
        }
    } else {
        auto mouse = std::make_shared<remoteplay::MouseStick>(mapper_.tuning());
        auto worker = std::make_unique<Ps5MouseInput>(window_, *mouse);
        if (!worker->start()) {
            veyra::log::warn("ps5-input", "Could not start PS5 mouse input");
            return false;
        }
        mouse_ = std::move(mouse);
        ps5MouseInput_ = std::move(worker);
    }
    router_.reset();
    mapper_.reset();
    route_ = route;
    target_ = route_ == Route::Ps5 ? ps5Target_() : 0;
    if(route_!=Route::Moonlight){
        restoreCursor_=GetCursorPos(&savedCursor_)!=FALSE;
        suspendIme();
    }
    clipToWindow();
    SetCursor(nullptr);
    if (route_ != Route::Moonlight) {
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
    ps5MouseInput_.reset();
    mouse_.reset();
    if (old == Route::Moonlight) {
        RAWINPUTDEVICE device{0x01, 0x02, RIDEV_REMOVE, nullptr};
        RegisterRawInputDevices(&device, 1, sizeof(device));
    }
    ClipCursor(nullptr);
    if(old!=Route::Moonlight){
        restoreIme();
        if(restoreCursor_ && MonitorFromPoint(savedCursor_,MONITOR_DEFAULTTONULL))SetCursorPos(savedCursor_.x,savedCursor_.y);
        restoreCursor_=false;
    }
    wheelQueue_.clear();wheelGapUs_=0;
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
    if(route_!=Route::Moonlight){
        POINT center{(topLeft.x+bottomRight.x)/2,(topLeft.y+bottomRight.y)/2};
        RECT point{center.x,center.y,center.x+1,center.y+1};
        ClipCursor(&point);
        return;
    }
    const RECT screen{topLeft.x, topLeft.y, bottomRight.x, bottomRight.y};
    ClipCursor(&screen);
}

bool StreamInputCapture::nativeEventFilter(const QByteArray& eventType, void* message, qintptr* result) {
    if ((route_ == Route::None && learning_ < 0) || eventType != "windows_generic_MSG") return false;
    const MSG* msg = static_cast<const MSG*>(message);
    if (!msg->hwnd || GetAncestor(msg->hwnd, GA_ROOT) != window_) return false;
    WPARAM keyParam=msg->wParam;
    const bool keyMessage = msg->message == WM_KEYDOWN || msg->message == WM_KEYUP ||
        msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP;
    const auto consume = [result] { if (result) *result = 0; return true; };
    if(!captured() && keyMessage){
        if(keyParam==VK_PROCESSKEY){
            UINT sc=(msg->lParam>>16)&0xff;
            if(msg->lParam&(1<<24))sc|=0xe000;
            keyParam=MapVirtualKeyW(sc,MAPVK_VSC_TO_VK_EX);
            if(!keyParam || keyParam==VK_PROCESSKEY)return consume();
        }
        const int setting=mapper_.tuning().unlock;
        const auto mods=mapper_.modifiers();
        const bool unlock=setting==0?keyParam==VK_ESCAPE:setting==1?keyParam=='Z'&&(mods&6)==6:
                          setting==2?keyParam=='Q'&&(mods&4):setting==3?keyParam==VK_F1:keyParam==VK_OEM_3;
        if(unlock){
            if(learning_>=0)cancelLearning();else end();
            return consume();
        }
    }
    if(learning_>=0 && (msg->message==WM_KILLFOCUS||(msg->message==WM_ACTIVATE&&LOWORD(msg->wParam)==WA_INACTIVE))){cancelLearning();return false;}
    if (learning_ >= 0) {
        if (msg->message == WM_LBUTTONDOWN) {
            auto* button = qobject_cast<QQuickItem*>(cancelButton_.data());
            if (button && button->isVisible() && button->contains(button->mapFromGlobal(QCursor::pos())))
                return false;
        }
        if (!keyMessage && msg->message != WM_MOUSEWHEEL && msg->message != WM_MOUSEHWHEEL &&
            msg->message != WM_LBUTTONDOWN && msg->message != WM_RBUTTONDOWN &&
            msg->message != WM_MBUTTONDOWN && msg->message != WM_XBUTTONDOWN) return false;
        return router_.message(msg->message, keyParam, msg->lParam, false) && consume();
    }
    if (!captured() && keyMessage && msg->wParam >= VK_F7 && msg->wParam <= VK_F10) return false;
    switch (msg->message) {
    case WM_INPUT: {
        if (!captured()) return false;
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(msg->lParam), RID_INPUT, &raw, &size,
                            sizeof(RAWINPUTHEADER)) != UINT(-1) &&
            raw.header.dwType == RIM_TYPEMOUSE && (raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
            const auto dx = raw.data.mouse.lLastX, dy = raw.data.mouse.lLastY;
            router_.rawMouse(dx, dy);
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
    const bool consumed = router_.message(msg->message, keyParam, msg->lParam, captured());
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
    if(!delta)return;
    const auto steps=std::clamp(std::abs(delta)/120,1,16);
    for(int i=0;i<steps&&wheelQueue_.size()<16;++i)wheelQueue_.push_back(key);
    tick();
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
    const bool previousDriving = driving();
    if (!mapper_.digital(key, down)) return;
    if (previousDriving != driving()) {
#ifdef VEYRA_ENABLE_REMOTEPLAY
        if (ps5Captured()) engine_.remotePlayController({});
#endif
        if (mouse_) mouse_->resetMotion();
        wheelQueue_.clear();
        wheelKey_ = 0;
        wheelGapUs_ = 0;
        emit drivingChanged();
    }
    emitPad();
}

void StreamInputCapture::tick() {
    if (wheelKey_ && nowUs() >= wheelReleaseUs_) {
        mapper_.digital(wheelKey_, false);
        wheelKey_ = 0;wheelGapUs_=nowUs()+32000;
    }
    if(!wheelKey_ && !wheelQueue_.empty() && nowUs()>=wheelGapUs_){
        wheelKey_=wheelQueue_.front();wheelQueue_.pop_front();
        mapper_.digital(wheelKey_,true);wheelReleaseUs_=nowUs()+55000;
    }
    emitPad();
}

void StreamInputCapture::emitPad() {
    bool useMouse = false;
    auto pad = mapper_.state(&useMouse);
    if(mouse_)mouse_->menu(mapper_.menuActive());
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (ps5Captured()) {
        if (!target_ || ps5Target_() != target_) { end(); return; }
        engine_.remotePlayController(pad, useMouse ? std::weak_ptr(mouse_) : std::weak_ptr<remoteplay::MouseStick>{});
    }
#endif
    if (previewing()) {
        if (useMouse) {
            mouse_->apply(pad,nowUs());
        }
        const double speed=mouse_?std::round(mouse_->speed()):0.;
        if (pad != previewState_||speed!=previewSpeed_) { previewSpeed_=speed;previewState_ = pad; emit padStateChanged(); }
    }
}

QVariantList StreamInputCapture::bindings() const {
    QVariantList list;
    for (const int binding : mapper_.bindings()) list.push_back(bindingLabel(binding));
    return list;
}
QVariantList StreamInputCapture::drivingBindings() const {
    QVariantList list;
    for (const int binding : mapper_.bindings(true)) list.push_back(bindingLabel(binding));
    return list;
}
QString StreamInputCapture::driveKeyLabel() const { return bindingLabel(mapper_.driveKey()); }
void StreamInputCapture::selectDriving(bool on) {
    if (driving() == on) return;
    mapper_.selectDriving(on);
    if (mouse_) mouse_->resetMotion();
    emit drivingChanged();
    if (ps5Captured() || previewing()) emitPad();
}
void StreamInputCapture::learnDriveKey(QObject* cancelButton) { learn(Mapper::ActionCount, cancelButton); }
QVariantMap StreamInputCapture::tuning() const { return tuningMap(mapper_.tuning()); }
QVariantMap StreamInputCapture::padState() const {
    const int a=mapper_.tuning().adsAction;
    const bool ads=a==16?previewState_.l2!=0:a==17?previewState_.r2!=0:(previewState_.buttons&(1u<<a))!=0;
    return {{"ads",ads},{"speed",previewing()?previewSpeed_:0.}, {QStringLiteral("buttons"), previewState_.buttons}, {QStringLiteral("leftX"), previewState_.leftX / 32767.0},
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
void StreamInputCapture::learn(int action, QObject* cancelButton, bool drivingLayout) {
    if (action < 0 || action > Mapper::ActionCount) return;
    end();
    cancelLearning();
    // The editor can be opened from Home before any video HWND exists.
    window_ = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(window_, &pid);
    if (pid != GetCurrentProcessId()) return;
    mapper_.reset();
    router_.reset();
    suspendIme();
    cancelButton_ = cancelButton;
    learningDriving_ = drivingLayout;
    learning_ = action;
    learningModifier_ = 0;
    feedback_.clear();
    emit mappingChanged();
}
void StreamInputCapture::bind(int key, int modifiers) {
    const int encoded = Mapper::encode(key, modifiers);
    if (!Mapper::validBinding(encoded,mapper_.tuning().unlock!=0)) {
        feedback_ = tr("此键用于系统或 Veyra 快捷键，请换一个键。");
        emit mappingChanged();
        return;
    }
    const auto& all = mapper_.bindings(learningDriving_);
    const auto used = std::find(all.begin(), all.end(), encoded);
    const bool moved = used != all.end() && int(used - all.begin()) != learning_;
    const bool isDriveKey = learning_ == Mapper::ActionCount;
    const bool assigned = isDriveKey ? mapper_.setDriveKey(encoded)
                                     : mapper_.setBinding(learning_, encoded, learningDriving_);
    if (!assigned) {
        feedback_ = tr("此键已被另一套键位或布局切换占用。");
        emit mappingChanged();
        return;
    }
    feedback_ = !isDriveKey && moved ?
        tr("已将 %1 从原按键移到此处。").arg(bindingLabel(encoded)) : QString{};
    restoreIme();
    learning_ = -1;
    save();
    emit mappingChanged();
}
void StreamInputCapture::clearBinding(int action, bool drivingLayout) {
    releasePs5();
    if (mapper_.setBinding(action, 0, drivingLayout)) { save(); emit mappingChanged(); }
}
void StreamInputCapture::cancelLearning() {
    if (learning_ < 0) return;
    restoreIme();
    learning_ = -1;
    mapper_.reset();
    router_.reset();
    emit mappingChanged();
}
void StreamInputCapture::applyDefaults(bool drivingLayout) {
    releasePs5();
    if (drivingLayout) mapper_.resetDrivingBindings();
    else {
        mapper_.resetBindings();
        mapper_.setToggleMask(0);
    }
    const auto toggle = mapper_.driveKey();
    if (!mapper_.setDriveKey(toggle)) mapper_.setDriveKey(0);
    feedback_.clear();
    save();
    emit mappingChanged();
}
void StreamInputCapture::resetMouseTuning() {
    Mapper::Tuning defaults;
    defaults.mouseLook = mapper_.tuning().mouseLook;
    applyMouseTuning(defaults);
}
void StreamInputCapture::setTuning(const QString& key, const QVariant& value) {
    auto map = tuning();
    if (!map.contains(key)) return;
    map[key] = value;
    applyMouseTuning(readTuning(map));
}
void StreamInputCapture::applyMouseTuning(const Mapper::Tuning& next) {
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
    profiles_=json.value("profiles").toObject();
    profile_=json.value("profile").toString(QStringLiteral("Default"));
    keyboardEnabled_=json.value("enabled").toBool();
    readProfile(json);
    profiles_[profile_]=activeProfile();
}
void StreamInputCapture::suspendIme(){
    if(!imeWindow_&&window_){
        imeWindow_=window_;
        oldIme_=ImmAssociateContext(window_,nullptr);
        const auto current=ImmGetContext(window_);
        veyra::log::info("ps5-input",std::format("ime suspended={} previousContext={}",current==nullptr,oldIme_!=nullptr));
        if(current)ImmReleaseContext(window_,current);
    }
}
void StreamInputCapture::restoreIme(){
    if(imeWindow_&&IsWindow(imeWindow_)){
        ImmAssociateContext(imeWindow_,oldIme_);
        const auto current=ImmGetContext(imeWindow_);
        veyra::log::info("ps5-input",std::format("ime restored={}",current==oldIme_));
        if(current)ImmReleaseContext(imeWindow_,current);
    }
    imeWindow_=nullptr;oldIme_=nullptr;
}
QString StreamInputCapture::unlockLabel()const{
    static const QStringList values={"Esc","Ctrl+Alt+Z","Alt+Q","F1","~"};
    return values.value(mapper_.tuning().unlock);
}
void StreamInputCapture::readProfile(const QJsonObject& json){
    mapper_.setTuning(readTuning(json.value("tuning").toObject().toVariantMap()));
    mapper_.setDriveKey(0);
    mapper_.resetBindings();
    const auto readBindings = [this](const QJsonArray& bindings, bool driving) {
        if (bindings.size() < 26 || bindings.size() > Mapper::ActionCount) return;
        for (int i = 0; i < bindings.size(); ++i) mapper_.setBinding(i, 0, driving);
        for (int i = 0; i < bindings.size(); ++i)
            mapper_.setBinding(i, bindings[i].toInt(), driving);
    };
    readBindings(json.value("bindings").toArray(), false);
    mapper_.resetDrivingBindings();
    readBindings(json.value("drivingBindings").toArray(), true);
    mapper_.setDriveKey(json.value("driveKey").toInt(0x75));
    mapper_.setToggleMask(uint64_t(json.value("toggles").toInteger()));
}
QJsonObject StreamInputCapture::activeProfile()const{
    QJsonArray walking, driving;
    for (const int key : mapper_.bindings()) walking.append(key);
    for (const int key : mapper_.bindings(true)) driving.append(key);
    return {{"bindings",walking},{"drivingBindings",driving},{"driveKey",mapper_.driveKey()},
            {"tuning",QJsonObject::fromVariantMap(tuning())},{"toggles",qint64(mapper_.toggleMask())}};
}
bool StreamInputCapture::toggleAction(int action)const{return action>=0&&action<Mapper::ActionCount&&(mapper_.toggleMask()&(uint64_t{1}<<action));}
void StreamInputCapture::setToggleAction(int action,bool on){
    if(action!=Mapper::L2&&action!=Mapper::L3&&action!=Mapper::Circle&&action!=Mapper::MenuStick)return;
    releasePs5();auto bit=uint64_t{1}<<action;
    mapper_.setToggleMask(on?mapper_.toggleMask()|bit:mapper_.toggleMask()&~bit);save();emit mappingChanged();
}
void StreamInputCapture::selectProfile(const QString& value,bool create){
    const auto name=value.trimmed();if(name.isEmpty()||name.size()>40||name==profile_)return;
    if(create?(profiles_.contains(name)||profiles_.size()>=16):!profiles_.contains(name))return;
    releasePs5();profiles_[profile_]=activeProfile();profile_=name;
    if(!create)readProfile(profiles_.value(name).toObject());
    selectDriving(false);
    save();emit mappingChanged();
}
void StreamInputCapture::useTemplate(int index){
    if(index<0||index>2)return;
    auto t=mapper_.tuning();
    t.deadzone=index==1?.18:index==2?.05:.08;t.curve=index==1?.60:index==2?1.:.65;
    t.deadzoneShape=index==1?.5:0.;t.turnBoost=index==1?1.5:1.;
    applyMouseTuning(t);
}
void StreamInputCapture::save() {
    auto json=activeProfile();profiles_[profile_]=json;
    json["version"]=1;json["enabled"]=keyboardEnabled_;json["profile"]=profile_;json["profiles"]=profiles_;
    QDir().mkpath(QFileInfo(profilePath_).absolutePath());
    QSaveFile file(profilePath_);
    const auto data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        feedback_ = tr("无法保存键鼠映射。");
        veyra::log::warn("stream-input", "saving PS5 keyboard mapping failed");
    }
}

} // namespace veyra::ui
