// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/ui/StreamInputCapture.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickWindow>
#include <windowsx.h>
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

bool clicksLearningControl(const QQuickItem* item, const QPointF& cursor) {
    if (!item || !item->isVisible() || !item->isEnabled()) return false;
    const bool contains = item->contains(item->mapFromScene(cursor));
    // Buttons in the dialog keep their normal click, including its Close and
    // Back buttons. Non-button targets such as the diagram opt in explicitly.
    if (contains && (item->metaObject()->indexOfSignal("clicked()") >= 0 ||
                     item->property("ps5LearningControl").toBool())) return true;
    if (!contains && item->clip()) return false;
    for (const auto* child : item->childItems())
        if (clicksLearningControl(child, cursor)) return true;
    return false;
}

int64_t nowUs() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

bool unlockChord(int chord, int setting) {
    const int key = chord & Mapper::KeyMask;
    const int mods = chord >> 9;
    switch (setting) {
    case 0: return key == VK_ESCAPE;
    case 1: return key == 'Z' && (mods & (Mapper::Ctrl | Mapper::Alt)) == (Mapper::Ctrl | Mapper::Alt);
    case 2: return key == 'Q' && (mods & Mapper::Alt);
    case 3: return key == VK_F1;
    case 4: return key == VK_OEM_3;
    default: return false;
    }
}

int windowModifiers() {
    return ((GetKeyState(VK_SHIFT) & 0x8000) ? Mapper::Shift : 0) |
           ((GetKeyState(VK_CONTROL) & 0x8000) ? Mapper::Ctrl : 0) |
           ((GetKeyState(VK_MENU) & 0x8000) ? Mapper::Alt : 0);
}

QString shortNote(const QString& text) {
    const auto codepoints = text.trimmed().toUcs4();
    return QString::fromUcs4(codepoints.constData(), std::min<qsizetype>(12, codepoints.size()));
}

QString bindingLabel(int binding) {
    if (!binding) return QStringLiteral("—");
    QString text;
    const int mods = binding >> 9;
    if (mods & Mapper::Ctrl) text += QStringLiteral("Ctrl+");
    if (mods & Mapper::Alt) text += QStringLiteral("Alt+");
    if (mods & Mapper::Shift) text += QStringLiteral("Shift+");
    const int key = binding & Mapper::KeyMask;
    if (key >= VK_F1 && key <= VK_F24)
        return text + QStringLiteral("F%1").arg(key - VK_F1 + 1);
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
    QVariantList measured;
    if (t.measuredCurve.valid())
        for (const auto rate : t.measuredCurve.rates) measured.push_back(rate);
    return {{QStringLiteral("horizontal"), t.horizontal}, {QStringLiteral("vertical"), t.vertical},
            {QStringLiteral("turnBoost"), t.turnBoost}, {QStringLiteral("inputDeadzone"), t.inputDeadzone},
            {"dpi",t.dpi},{"turnSpeed",t.turnSpeed},{"adsScale",t.adsScale},{"adsDelayMs",t.adsDelayMs},{"adsAction",t.adsAction},
            {"deadzoneShape",t.deadzoneShape},{"walkScale",t.walkScale},{"unlock",t.unlock},{"gyro",t.gyro},{"carry",t.carry},
            {QStringLiteral("curve"), t.curve}, {QStringLiteral("deadzone"), t.deadzone},
            {QStringLiteral("smoothing"), t.smoothingMs}, {QStringLiteral("invertY"), t.invertY},
            {QStringLiteral("mouseLook"), t.mouseLook},
            {"physicalAim", t.physicalAim}, {"aimConfigured", t.aimConfigured}, {"dpiConfirmed", t.dpiConfirmed},
            {"aimFullSpeed", t.aimFullSpeed}, {"aimHorizontal", t.aimHorizontal}, {"aimVertical", t.aimVertical},
            {"gameDeadzoneX", t.gameDeadzoneX}, {"gameDeadzoneY", t.gameDeadzoneY}, {"gameCurve", t.gameCurve},
            {"aimFps", t.aimFps}, {"mixPhysical", t.mixPhysical}, {"graduatedSteering", t.graduatedSteering},
            {"angleCarry", t.angleCarry}, {"consoleLowLatency", t.consoleLowLatency},
            {"measuredRates", measured}, {"measuredDeadzone", t.measuredCurve.deadzone}, {"measuredExponent", t.measuredCurve.exponent},
            {"measuredMax", t.measuredCurve.maxPixelsPerSecond}, {"measuredAcceleration", t.measuredCurve.acceleration},
            {"steeringInitial", t.steeringInitial}, {"steeringFullMs", t.steeringFullMs},
            {"steeringReturnMs", t.steeringReturnMs}, {"pedalRiseMs", t.pedalRiseMs},
            {"mouseSteering", t.mouseSteering}, {"mouseSteerGain", t.mouseSteerGain},
            {"autoDriving", t.autoDriving}, {"autoDriveMs", t.autoDriveMs}};
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
    t.physicalAim = map.value("physicalAim").toBool();
    t.aimConfigured = map.value("aimConfigured", t.physicalAim).toBool();
    t.dpiConfirmed = map.value("dpiConfirmed", true).toBool();
    t.aimFullSpeed = bounded("aimFullSpeed", 25., .25, 150.);
    t.aimHorizontal = bounded("aimHorizontal", 1., .1, 6.);
    t.aimVertical = bounded("aimVertical", 1., .1, 6.);
    t.gameDeadzoneX = bounded("gameDeadzoneX", .18, 0., .50);
    t.gameDeadzoneY = bounded("gameDeadzoneY", .18, 0., .50);
    t.gameCurve = bounded("gameCurve", 2., 1., 3.);
    const int fps = map.value("aimFps").toInt();
    t.aimFps = fps == 30 || fps == 60 ? fps : 0;
    t.angleCarry = map.value("angleCarry").toBool();
    t.consoleLowLatency = map.value("consoleLowLatency").toBool();
    auto& measured = t.measuredCurve;
    const auto rates = map.value("measuredRates").toList();
    if (rates.size() == qsizetype(measured.rates.size())) {
        measured.deadzone = bounded("measuredDeadzone", 0., 0., .4);
        measured.exponent = bounded("measuredExponent", 2., .8, 3.2);
        measured.maxPixelsPerSecond = bounded("measuredMax", 0., 0., 20000.);
        measured.acceleration = bounded("measuredAcceleration", 1., 1., 10.);
        for (qsizetype i = 0; i < rates.size(); ++i) {
            const auto rate = rates[i].toDouble();
            measured.rates[i] = std::isfinite(rate) ? std::clamp(rate, 0., 1.) : 0.;
        }
        if (!measured.valid()) measured = {};
    }
    t.mixPhysical = map.value("mixPhysical", true).toBool();
    t.graduatedSteering = map.value("graduatedSteering", false).toBool();
    t.steeringInitial = bounded("steeringInitial", .4, .2, .6);
    t.steeringFullMs = int(bounded("steeringFullMs", 240, 80, 400));
    t.steeringReturnMs = int(bounded("steeringReturnMs", 60, 30, 200));
    t.pedalRiseMs = int(bounded("pedalRiseMs", 0, 0, 300));
    t.mouseSteering = map.value("mouseSteering").toBool();
    t.mouseSteerGain = bounded("mouseSteerGain", 1., .1, 6.);
    t.autoDriving = map.value("autoDriving").toBool();
    t.autoDriveMs = int(bounded("autoDriveMs", 1200, 200, 3000));
    return t;
}

Mapper::Tuning newProfileTuning() {
    auto tuning = Mapper::defaultTuning();
    tuning.horizontal *= 800. / tuning.dpi;
    tuning.vertical *= 800. / tuning.dpi;
    tuning.dpi = 800;
    tuning.physicalAim = tuning.aimConfigured = true;
    tuning.dpiConfirmed = false;
    tuning.graduatedSteering = true;
    return tuning;
}
} // namespace

// The game's input is captured by a dedicated message-only window. Qt keeps
// keyboard messages on the editor path and never receives mapped game events.
struct StreamInputCapture::ConsoleInputWorker {
    ConsoleInputWorker(StreamInputCapture& owner, HWND target, std::shared_ptr<remoteplay::MappedController> mapped,
                       int unlock, int quick, uint64_t nativeTarget, bool preview)
        : owner(owner), target(target), mapped(std::move(mapped)), unlock(unlock), quick(quick),
          nativeTarget(nativeTarget), preview(preview) {}

    bool start() {
        std::promise<bool> ready;
        auto started = ready.get_future();
        thread = std::thread([this, ready = std::move(ready)]() mutable {
            constexpr wchar_t className[] = L"VeyraConsoleInput";
            const auto instance = GetModuleHandleW(nullptr);
            const WNDCLASSW wc{.lpfnWndProc = dispatch, .hInstance = instance, .lpszClassName = className};
            if (!RegisterClassW(&wc) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
                ready.set_value(false);
                return;
            }
            const auto hwnd = CreateWindowExW(0, className, nullptr, 0, 0, 0, 0, 0,
                                             HWND_MESSAGE, nullptr, instance, this);
            if (!hwnd) { ready.set_value(false); return; }
            threadId = GetCurrentThreadId();
            RAWINPUTDEVICE devices[] = {
                {0x01, 0x02, RIDEV_INPUTSINK | (preview ? 0u : RIDEV_NOLEGACY), hwnd},
                {0x01, 0x06, RIDEV_INPUTSINK | RIDEV_NOLEGACY, hwnd}
            };
            const bool registered = RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
            ready.set_value(registered);
            if (registered) {
                SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
                MSG message{};
                while (GetMessageW(&message, nullptr, 0, 0) > 0)
                    DispatchMessageW(&message);
            }
            for (auto& device : devices) { device.dwFlags = RIDEV_REMOVE; device.hwndTarget = nullptr; }
            RegisterRawInputDevices(devices, 2, sizeof(RAWINPUTDEVICE));
            DestroyWindow(hwnd);
        });
        return started.get();
    }

    ~ConsoleInputWorker() {
        if (!thread.joinable()) return;
        if (threadId) PostThreadMessageW(threadId, WM_QUIT, 0, 0);
        thread.join();
        veyra::log::info("console-input", std::format(
            "raw mouse reports={} queue maxMs={} staleDropped={} maxDeliveryGapUs={}",
            reports, maxQueueMs, staleReports, maxDeliveryGapUs));
    }

private:
    static LRESULT CALLBACK dispatch(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
        }
        if (message == WM_INPUT) {
            auto* self = reinterpret_cast<ConsoleInputWorker*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
            if (self) self->input(lParam);
        }
        return DefWindowProcW(hwnd, message, wParam, lParam);
    }

    void release() {
        active = false;
        mapped->stop();
        QMetaObject::invokeMethod(&owner, [&capture = owner, input = std::weak_ptr(mapped)] {
            if (const auto released = input.lock(); released && released == capture.mapped_) capture.releasePs5();
        }, Qt::QueuedConnection);
    }

    void keyboard(const RAWKEYBOARD& raw, int64_t stamp) {
        if (raw.MakeCode == 0xff || raw.VKey >= 0xff) return;
        const auto host = moonlight::hostKey(raw.VKey, raw.MakeCode, (raw.Flags & RI_KEY_E0) != 0);
        const int key = host.code & 255;
        const bool down = (raw.Flags & RI_KEY_BREAK) == 0;
        if (key <= 0 || pressed[key] == down) return;
        pressed[key] = down;
        int mods = 0;
        for (int k = VK_LSHIFT; k <= VK_RMENU; ++k)
            if (pressed[k]) mods |= Mapper::modifier(k);
        const int chord = Mapper::encode(key, mods);
        if (down && ((preview && key == VK_ESCAPE) || unlockChord(chord, unlock))) { release(); return; }
        if (mapped->aimKey(key, down)) return;
        if (!preview && (key == quickDown || (down && chord == quick))) {
            quickDown = down ? key : 0;
            if (down) QMetaObject::invokeMethod(&owner, &StreamInputCapture::quickReferenceRequested, Qt::QueuedConnection);
            return;
        }
        if (!preview && key >= VK_F7 && key <= VK_F10) {
            if (down) QMetaObject::invokeMethod(&owner, [&capture = owner, chord] {
                emit capture.applicationHotkeyRequested(bindingLabel(chord));
            }, Qt::QueuedConnection);
            return;
        }
#ifdef VEYRA_ENABLE_XBOX
        if (nativeTarget) {
            owner.engine_.xboxNativeKey(nativeTarget, {uint8_t(key), down});
            return;
        }
#endif
        mapped->press(key, down, stamp);
    }

    void mouse(const RAWMOUSE& raw, int64_t stamp, DWORD lag) {
        constexpr int keys[] = {Mapper::MouseLeft, Mapper::MouseMiddle, Mapper::MouseRight,
                                Mapper::MouseBack, Mapper::MouseForward};
        constexpr USHORT down[] = {RI_MOUSE_LEFT_BUTTON_DOWN, RI_MOUSE_MIDDLE_BUTTON_DOWN, RI_MOUSE_RIGHT_BUTTON_DOWN,
                                  RI_MOUSE_BUTTON_4_DOWN, RI_MOUSE_BUTTON_5_DOWN};
        constexpr USHORT up[] = {RI_MOUSE_LEFT_BUTTON_UP, RI_MOUSE_MIDDLE_BUTTON_UP, RI_MOUSE_RIGHT_BUTTON_UP,
                                RI_MOUSE_BUTTON_4_UP, RI_MOUSE_BUTTON_5_UP};
        const auto flags = raw.usButtonFlags;
#ifdef VEYRA_ENABLE_XBOX
        if (nativeTarget) {
            constexpr uint8_t masks[]{1, 4, 2, 8, 16};
            for (size_t i = 0; i < std::size(masks); ++i) {
                if (flags & down[i]) buttons |= masks[i];
                if (flags & up[i]) buttons &= uint8_t(~masks[i]);
            }
            const bool relative = (raw.usFlags & MOUSE_MOVE_ABSOLUTE) == 0;
            const int delta = int(static_cast<SHORT>(raw.usButtonData));
            const int wheelY = flags & RI_MOUSE_WHEEL ? delta : 0;
            const int wheelX = flags & RI_MOUSE_HWHEEL ? delta : 0;
            if (wheelY && owner.quickReferenceOpen()) {
                QMetaObject::invokeMethod(&owner, [&capture = owner, delta] {
                    emit capture.quickReferenceScrolled(delta);
                }, Qt::QueuedConnection);
            } else {
                owner.engine_.xboxNativeMouse(nativeTarget, {raw.lLastX, raw.lLastY, wheelX, wheelY, buttons, relative});
            }
            return;
        }
#endif
        for (size_t i = 0; i < std::size(keys); ++i) {
            if (flags & down[i]) mapped->press(keys[i], true, stamp);
            if (flags & up[i]) mapped->press(keys[i], false, stamp);
        }
        const auto delta = int(static_cast<SHORT>(raw.usButtonData));
        if (delta && (flags & (RI_MOUSE_WHEEL | RI_MOUSE_HWHEEL))) {
            const bool horizontal = (flags & RI_MOUSE_HWHEEL) != 0;
            if (!horizontal && owner.quickReferenceOpen())
                QMetaObject::invokeMethod(&owner, [&capture = owner, delta] { emit capture.quickReferenceScrolled(delta); }, Qt::QueuedConnection);
            else
                mapped->wheel(horizontal ? (delta > 0 ? Mapper::WheelRight : Mapper::WheelLeft)
                                         : (delta > 0 ? Mapper::WheelUp : Mapper::WheelDown),
                              std::clamp(std::abs(delta) / WHEEL_DELTA, 1, 16), stamp);
        }
        if ((raw.usFlags & MOUSE_MOVE_ABSOLUTE) || (!raw.lLastX && !raw.lLastY)) return;
        maxQueueMs = std::max(maxQueueMs, lag);
        if (lag > 40) { ++staleReports; return; }
        if (lastDeliveryUs && stamp - lastDeliveryUs <= 100000)
            maxDeliveryGapUs = std::max(maxDeliveryGapUs, stamp - lastDeliveryUs);
        lastDeliveryUs = stamp;
        ++reports;
        mapped->move(raw.lLastX, raw.lLastY, stamp);
    }

    void input(LPARAM data) {
        if (!active) return;
        if (GetAncestor(GetForegroundWindow(), GA_ROOT) != target) { release(); return; }
        RAWINPUT raw{};
        UINT size = sizeof(raw);
        if (GetRawInputData(reinterpret_cast<HRAWINPUT>(data), RID_INPUT, &raw, &size,
                            sizeof(RAWINPUTHEADER)) == UINT(-1)) return;
        const DWORD age = GetTickCount() - DWORD(GetMessageTime());
        const DWORD lag = age < 0x80000000u ? age : 0;
        const auto stamp = nowUs();
        if (raw.header.dwType == RIM_TYPEKEYBOARD) keyboard(raw.data.keyboard, stamp);
        else if (raw.header.dwType == RIM_TYPEMOUSE) mouse(raw.data.mouse, stamp, lag);
    }

    StreamInputCapture& owner;
    HWND target;
    std::shared_ptr<remoteplay::MappedController> mapped;
    int unlock, quick, quickDown = 0;
    uint64_t nativeTarget = 0;
    bool preview = false;
    uint8_t buttons = 0;
    bool active = true;
    std::array<bool, 256> pressed{};
    std::thread thread;
    DWORD threadId = 0, maxQueueMs = 0;
    uint64_t reports = 0, staleReports = 0;
    int64_t lastDeliveryUs = 0, maxDeliveryGapUs = 0;
};

StreamInputCapture::StreamInputCapture(engine::EngineController& engine, QString profilePath,
                                       std::function<uint64_t()> ps5Target, std::function<uint64_t()> xboxTarget)
    : engine_(engine), router_(*this), profilePath_(std::move(profilePath)),
      ps5Target_(std::move(ps5Target)), xboxTarget_(std::move(xboxTarget)) {
    mapper_.setTuning(newProfileTuning());
    load();
    ps5Profile_ = profile_;
    profiles_[profile_]=activeProfile();
    timer_.setTimerType(Qt::PreciseTimer);
    timer_.setInterval(33);
    connect(&timer_, &QTimer::timeout, this, &StreamInputCapture::tick);
    QCoreApplication::instance()->installNativeEventFilter(this);
    connect(qGuiApp, &QGuiApplication::applicationStateChanged, this, [this](Qt::ApplicationState state) {
        if (state == Qt::ApplicationActive) return;
        quickKeyDown_ = 0;
        end();
        cancelLearning();
    });
}

StreamInputCapture::~StreamInputCapture() {
    end();
    cancelLearning();
    if (auto* app = QCoreApplication::instance()) app->removeNativeEventFilter(this);
}

void StreamInputCapture::setWindow(HWND window, HWND videoViewport) {
    if (window_ != window) { quickKeyDown_ = 0; end(); cancelLearning(); }
    window_ = window;
    viewport_ = videoViewport;
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
    selectPlatform(false);
    return begin(Route::Ps5);
}
bool StreamInputCapture::captureXbox(bool on) {
    if (!on) { if (xboxCaptured()) end(); return true; }
    if (!xboxKeys_ || !xboxTarget_ || !xboxTarget_()) return false;
    selectPlatform(true);
    return begin(Route::Xbox);
}

bool StreamInputCapture::begin(Route route) {
    if (route_ == route) return true;
    const HWND window = route == Route::Preview ? previewWindow_ : window_;
    if (!window || GetForegroundWindow() != window) return false;
    end();
    cancelLearning();
    const uint64_t destination = route == Route::Ps5 && ps5Target_ ? ps5Target_() :
        route == Route::Xbox && xboxTarget_ ? xboxTarget_() : 0;
    const bool native = route == Route::Xbox && nativeXbox_ && !armedAimMode_;
    if (route == Route::Moonlight) {
        RAWINPUTDEVICE device{0x01, 0x02, 0, window};
        if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
            veyra::log::warn("stream-input", "RegisterRawInputDevices failed");
            return false;
        }
    } else {
        auto mapped = std::make_shared<remoteplay::MappedController>(mapper_, [window] {
            return GetAncestor(GetForegroundWindow(), GA_ROOT) == window;
        });
        auto worker = std::make_unique<ConsoleInputWorker>(*this, window, mapped, mapper_.tuning().unlock,
            quickKey_, native ? destination : 0, route == Route::Preview);
        if (!worker->start()) {
            veyra::log::warn("console-input", "Raw keyboard and mouse registration failed");
            return false;
        }
        mapped_ = std::move(mapped);
        consoleInput_ = std::move(worker);
    }
    router_.reset();
    mapper_.reset();
    route_ = route;
    target_ = destination;
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (route_ == Route::Ps5 && (!target_ || !engine_.remotePlayMappedController(target_, mapped_))) {
        end();
        return false;
    }
#endif
#ifdef VEYRA_ENABLE_XBOX
    if (route_ == Route::Xbox && (!target_ || !engine_.xboxMappedController(target_, mapped_, native))) {
        end();
        return false;
    }
#endif
    if (route_ == Route::Ps5 || route_ == Route::Xbox) {
        using Mode = remoteplay::AimTest::Mode;
        using Render = engine::EngineController::ConsoleRenderMode;
        if (armedAimMode_) {
            const auto mode = Mode(armedAimMode_);
            const bool compareFg = mode == Mode::Latency && engine_.snapshot().fgActive;
            aimTest_ = std::make_shared<remoteplay::AimTest>(mode, compareFg);
            armedAimMode_ = 0;
            measuredFg_ = false;
            aimStageAt_ = nowUs();
            mapped_->setAimTest(aimTest_);
            if (aimTest_->needsFrames()) engine_.setConsoleAimProbe(aimTest_);
            if (mode == Mode::Latency && !engine_.setConsoleRenderMode(Render::MeasureWithoutFg)) {
                end();
                setAimStatus(tr("画面尚未就绪。请先进入游戏并确认画面正常播放。"));
                return false;
            }
            emit aimStatusChanged();
        } else if (mapper_.tuning().consoleLowLatency) {
            lowLatencyActive_ = engine_.setConsoleRenderMode(Render::LowLatency);
            emit aimStatusChanged();
        }
    }
    if (route_ != Route::Moonlight) {
        restoreCursor_ = route_ != Route::Preview && GetCursorPos(&savedCursor_) != FALSE;
        suspendIme(window);
    }
    if (route_ != Route::Preview) {
        clipToWindow();
        SetCursor(nullptr);
    }
    if (route_ != Route::Moonlight) {
        inputDiagnostics_ = {};
        if (route_ == Route::Preview) {
            previewSender_ = std::jthread([input = mapped_](std::stop_token stop) {
                const auto timer = std::unique_ptr<void, decltype(&CloseHandle)>(
                    CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS),
                    &CloseHandle);
                auto due = std::chrono::steady_clock::now();
                while (!stop.stop_requested()) {
                    input->sample(nowUs());
                    const auto now = std::chrono::steady_clock::now();
                    do { due += std::chrono::milliseconds(4); } while (due <= now);
                    LARGE_INTEGER next;
                    next.QuadPart = -std::max<int64_t>(1, std::chrono::duration_cast<std::chrono::microseconds>(due - now).count()) * 10;
                    if (timer && SetWaitableTimer(timer.get(), &next, 0, nullptr, nullptr, FALSE))
                        WaitForSingleObject(timer.get(), 10);
                    else std::this_thread::sleep_until(due);
                }
            });
        }
        timer_.start();
        tick();
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
    if (old == Route::Preview) previewWindow_ = nullptr;
    timer_.stop();
    if (mapped_) mapped_->stop();
    if (aimTest_) {
        if (aimTest_->status().state == remoteplay::AimTest::State::Cancelled)
            setAimStatus(tr("校准或延迟测试已取消"));
        engine_.setConsoleAimProbe({});
        aimTest_.reset();
        emit aimStatusChanged();
    }
    if (lowLatencyActive_ || old == Route::Ps5 || old == Route::Xbox) {
        engine_.setConsoleRenderMode(engine::EngineController::ConsoleRenderMode::Normal);
        lowLatencyActive_ = false;
        emit aimStatusChanged();
    }
    consoleInput_.reset();
    if (previewSender_.joinable()) {
        previewSender_.request_stop();
        previewSender_.join();
    }
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
    SetCursor(LoadCursorW(nullptr, IDC_ARROW));
    router_.reset();
    mapper_.reset();
#ifdef VEYRA_ENABLE_MOONLIGHT
    if (old == Route::Moonlight) engine_.moonlightReleaseInput();
#endif
#ifdef VEYRA_ENABLE_XBOX
    if (old == Route::Xbox && target_) engine_.xboxMappedController(target_, {}, false);
#endif
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (old == Route::Ps5 && target_) {
        if (engine_.remotePlayMappedController(target_, {})) engine_.remotePlayController({});
    }
#endif
    mapped_.reset();
    target_ = 0;
    if (old == Route::Moonlight) emit capturedChanged(false);
    if (old != Route::Moonlight) {
        previewState_ = {};
        previewSpeed_ = 0.;
        emit padStateChanged();
    }
    emit stateChanged();
}

void StreamInputCapture::clipToWindow() {
    if (route_ == Route::Preview) return;
    const bool console = route_ == Route::Ps5 || route_ == Route::Xbox;
    const HWND window = console && IsWindowVisible(viewport_) ? viewport_ : window_;
    RECT client{};
    if (!window || !GetClientRect(window, &client)) return;
    POINT topLeft{client.left, client.top}, bottomRight{client.right, client.bottom};
    ClientToScreen(window, &topLeft);
    ClientToScreen(window, &bottomRight);
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
    if (eventType != "windows_generic_MSG") return false;
    if (route_ == Route::None && learning_ < 0 && !quickReferenceEnabled_ && !quickKeyDown_) return false;
    const MSG* msg = static_cast<const MSG*>(message);
    const HWND window = learning_ >= 0 ? learningWindow_ :
        route_ == Route::Preview ? previewWindow_ : window_;
    if (!msg->hwnd || !window || GetAncestor(msg->hwnd, GA_ROOT) != window) return false;
    WPARAM keyParam=msg->wParam;
    const bool keyMessage = msg->message == WM_KEYDOWN || msg->message == WM_KEYUP ||
        msg->message == WM_SYSKEYDOWN || msg->message == WM_SYSKEYUP;
    const auto consume = [result] { if (result) *result = 0; return true; };
    if (!captured() && keyMessage) {
        if(keyParam==VK_PROCESSKEY){
            UINT sc=(msg->lParam>>16)&0xff;
            if(msg->lParam&(1<<24))sc|=0xe000;
            keyParam=MapVirtualKeyW(sc,MAPVK_VSC_TO_VK_EX);
            if (!keyParam || keyParam == VK_PROCESSKEY)
                return (route_ != Route::None || learning_ >= 0) && consume();
        }
        const bool down = msg->message == WM_KEYDOWN || msg->message == WM_SYSKEYDOWN;
        const bool repeat = (msg->lParam & (1L << 30)) != 0;
        const int key = uint16_t(moonlight::hostKey(uint32_t(keyParam), uint32_t(msg->lParam >> 16) & 255,
                                                  (msg->lParam & (1L << 24)) != 0).code) & 255;
        const int chord = Mapper::encode(key, route_ == Route::None && learning_ < 0 ? windowModifiers() : mapper_.modifiers());
        if (quickKeyDown_ == key && key) {
            if (!down) quickKeyDown_ = 0;
            return consume();
        }
        if (learning_ < 0 && quickReferenceEnabled_ && quickKey_ && chord == quickKey_) {
            if (down && !repeat) {
                quickKeyDown_ = key;
                emit quickReferenceRequested();
            }
            return consume();
        }
        if (route_ != Route::None && learning_ < 0 && unlockChord(chord, mapper_.tuning().unlock)) {
            if (down && !repeat) end();
            return consume();
        }
        if (learning_ >= 0 && down) {
            if (repeat) return consume();
            if (key == VK_ESCAPE) {
                cancelLearning();
                return consume();
            }
            if (key == VK_BACK && !mapper_.modifiers() && learning_ < Mapper::ActionCount) {
                clearBinding(learning_, learningDriving_, learningSlot_);
                return consume();
            }
        }
    }
    if (route_ == Route::None && learning_ < 0) return false;
    if(learning_>=0 && (msg->message==WM_KILLFOCUS||(msg->message==WM_ACTIVATE&&LOWORD(msg->wParam)==WA_INACTIVE))){cancelLearning();return false;}
    if (learning_ >= 0) {
        if (msg->message == WM_LBUTTONDOWN) {
            const auto* controls = qobject_cast<QQuickItem*>(learningControls_.data());
            if (controls && controls->window()) {
                POINT click{GET_X_LPARAM(msg->lParam), GET_Y_LPARAM(msg->lParam)};
                MapWindowPoints(msg->hwnd, window, &click, 1);
                const auto scale = controls->window()->devicePixelRatio();
                if (clicksLearningControl(controls, QPointF(click.x / scale, click.y / scale))) return false;
            }
        }
        if (!keyMessage && msg->message != WM_MOUSEWHEEL && msg->message != WM_MOUSEHWHEEL &&
            msg->message != WM_LBUTTONDOWN && msg->message != WM_RBUTTONDOWN &&
            msg->message != WM_MBUTTONDOWN && msg->message != WM_XBUTTONDOWN) return false;
        return router_.message(msg->message, keyParam, msg->lParam, false) && consume();
    }
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
        if (route_ == Route::Preview) return false;
        SetCursor(nullptr);
        if (result) *result = TRUE;
        return true;
    case WM_SIZE: case WM_MOVE: clipToWindow(); return false;
    case WM_ACTIVATE:
        if (LOWORD(msg->wParam) == WA_INACTIVE) end();
        return false;
    case WM_KILLFOCUS: {
        const auto next = reinterpret_cast<HWND>(msg->wParam);
        const auto owner = route_ == Route::Preview ? previewWindow_ : window_;
        if (route_ == Route::Moonlight || !next || GetAncestor(next, GA_ROOT) != owner) end();
        return false;
    }
    default: break;
    }
    if (!captured()) return false;
    const bool consumed = router_.message(msg->message, keyParam, msg->lParam, true);
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
    if (learning_ < 0 || !delta) return;
    const int key = horizontal ? (delta > 0 ? Mapper::WheelRight : Mapper::WheelLeft) :
        (delta > 0 ? Mapper::WheelUp : Mapper::WheelDown);
    bind(key, mapper_.modifiers());
}

void StreamInputCapture::digital(int key, bool down) {
    if (learning_ < 0) return;
    const int mod = Mapper::modifier(key);
    const int before = mapper_.modifiers();
    mapper_.digital(key, down);
    if (mod && down) learningModifier_ = key;
    else if (mod && key == learningModifier_) bind(key, before);
    else if (down) bind(key, before);
}

void StreamInputCapture::tick() {
    if (!mapped_) return;
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (ps5Captured() && (!target_ || ps5Target_() != target_)) { end(); return; }
#endif
#ifdef VEYRA_ENABLE_XBOX
    if (xboxCaptured() && (!target_ || xboxTarget_() != target_)) { end(); return; }
#endif
    mapped_->setGameFps(engine_.mappedGameFps());
    if (aimTest_) { tickAimTest(); return; }
    const auto latest = mapped_->preview();
    if (driving() != latest.driving) {
        mapper_.selectDriving(latest.driving);
        emit drivingChanged();
    }
    const bool newStats = latest.diagnostics.revision != inputDiagnostics_.revision;
    if (newStats) {
        inputDiagnostics_ = latest.diagnostics;
        const auto& timing = inputDiagnostics_;
        veyra::log::info("console-input-timing", std::format(
            "keyCount={} keyToSendP50Ms={:.2f} keyToSendP95Ms={:.2f} keyToSendMaxMs={:.2f} "
            "mouseCount={} mouseToSendP50Ms={:.2f} mouseToSendP95Ms={:.2f} sendIntervalP95Ms={:.2f} "
            "mouseReportsPerSecond={:.1f} fullStickPct={:.1f}",
            timing.keys.events, timing.keys.p50, timing.keys.p95, timing.keys.maximum,
            timing.mouse.events, timing.mouse.p50, timing.mouse.p95, timing.sends.p95,
            timing.mouseReportsPerSecond, timing.fullStickFraction * 100.));
    }
    const double speed = std::round(latest.speedCounts);
    if (latest.pad != previewState_ || speed != previewSpeed_ || newStats) {
        previewState_ = latest.pad;
        previewSpeed_ = speed;
        emit padStateChanged();
    }
}

void StreamInputCapture::setAimStatus(QString value) {
    if (aimStatus_ == value) return;
    aimStatus_ = std::move(value);
    emit aimStatusChanged();
}
void StreamInputCapture::armAimTest(int mode) {
    if (mode < 0 || mode > int(remoteplay::AimTest::Mode::Latency)) return;
    releasePs5();
    armedAimMode_ = mode;
    setAimStatus(mode ? tr("站在原地，面向有纹理的静止场景。点击下方按钮返回游戏开始测试。") : QString{});
}
void StreamInputCapture::tickAimTest() {
    using Test = remoteplay::AimTest;
    using Render = engine::EngineController::ConsoleRenderMode;
    const auto status = aimTest_->status();
    const auto mode = aimTest_->mode();
    if (status.state == Test::State::Cancelled || status.state == Test::State::Unreliable) {
        const bool manual = !aimTest_->needsFrames();
        end();
        if (status.state == Test::State::Cancelled) setAimStatus(tr("校准或延迟测试已取消"));
        else if (manual) setAimStatus(tr("未确认死区。请在镜头开始移动时按空格保存。"));
        else {
            setAimStatus(tr("只检测到 %1 次有效镜头响应。请换到有纹理的静止场景再试。").arg(status.latencySamples[0]));
            veyra::log::info("console-aim", std::format("latency responses={} motionSamples={}", status.latencySamples[0], status.motionSamples));
        }
        return;
    }
    const auto baselineLatency = [&] {
        return tr("摇杆发送到画面提交 · 关闭补帧 %1 ms").arg(status.latencyMs[0], 0, 'f', 1);
    };
    if (status.state == Test::State::Complete) {
        if (mode == Test::Mode::Latency) {
            const bool compared = status.latencySamples[1] >= Test::kMinLatencySamples;
            setAimStatus(compared
                ? tr("摇杆发送到画面提交 · 关闭补帧 %1 ms · 开启补帧 %2 ms")
                    .arg(status.latencyMs[0], 0, 'f', 1).arg(status.latencyMs[1], 0, 'f', 1)
                : baselineLatency());
            veyra::log::info("console-aim", std::format("inputToPresent fgOffSamples={} fgOffMs={:.2f} fgOnSamples={} fgOnMs={:.2f}",
                status.latencySamples[0], status.latencyMs[0], status.latencySamples[1], status.latencyMs[1]));
        } else {
            auto tune = mapper_.tuning();
            tune.physicalAim = tune.aimConfigured = true;
            if (mode == Test::Mode::Automatic) {
                const auto result = aimTest_->calibration();
                const auto& curve = result.curve;
                veyra::log::info("console-aim", std::format(
                    "calibration failure={} motionSamples={} measuredSteps={} neutralSpeed={:.2f} fullSpeed={:.2f} firstMotion={:.3f} fitError={:.4f}",
                    int(result.failure), status.motionSamples, result.measuredSteps, result.backgroundSpeed,
                    result.fullSpeed, result.firstMotion, result.fitError));
                if (!curve.valid()) {
                    end();
                    if (result.failure == Test::CalibrationFailure::NoResponse)
                        setAimStatus(tr("没有检测到足够的镜头转动。确认手柄已连接、角色能转动视角，也可以手动测死区。"));
                    else if (result.failure == Test::CalibrationFailure::MovingScene)
                        setAimStatus(tr("静止时画面也在移动。请面向有纹理的静止场景，松开手柄后重试。"));
                    else setAimStatus(tr("镜头转动速度不稳定。可以换个场景再试，或手动测死区。"));
                    return;
                }
                tune.measuredCurve = curve;
                tune.gameDeadzoneX = curve.deadzone;
                tune.gameCurve = std::clamp(curve.exponent, 1., 3.);
                setAimStatus(tr("校准完成 · 横向死区 %1% · 满杆画面速度 %2 px/s")
                    .arg(curve.deadzone * 100., 0, 'f', 1).arg(curve.maxPixelsPerSecond, 0, 'f', 1));
                veyra::log::info("console-aim", std::format("calibration deadzone={:.3f} response={:.3f} speedPxPerSec={:.2f} acceleration={:.3f}",
                    curve.deadzone, curve.exponent, curve.maxPixelsPerSecond, curve.acceleration));
            } else {
                if (mode == Test::Mode::Vertical) tune.gameDeadzoneY = status.manualDeadzone;
                else {
                    tune.gameDeadzoneX = status.manualDeadzone;
                    tune.measuredCurve = {};
                }
                setAimStatus(tr("已保存游戏死区 %1%").arg(status.manualDeadzone * 100., 0, 'f', 0));
            }
            applyMouseTuning(tune);
        }
        end();
        return;
    }
    if (mode == Test::Mode::Latency && status.fg != measuredFg_) {
        measuredFg_ = status.fg;
        aimStageAt_ = nowUs();
        if (!engine_.setConsoleRenderMode(Render::MeasureWithFg)) {
            end();
            setAimStatus(baselineLatency());
            return;
        }
    }
    const bool ready = engine_.consoleRenderReady() && (!status.fg || engine_.snapshot().fgActive);
    aimTest_->ready(ready);
    if (!ready && nowUs() - aimStageAt_ > 15000000) {
        end();
        setAimStatus(status.latencySamples[0] >= Test::kMinLatencySamples ? baselineLatency() :
            tr("画面尚未就绪。请先进入游戏并确认画面正常播放。"));
        return;
    }
    if (!ready) {
        setAimStatus(tr("正在等待游戏画面 · Esc 取消"));
    } else if (aimTest_->needsFrames()) {
        setAimStatus(tr("正在测量画面响应 %1% · Esc 取消").arg(status.progress));
    } else {
        setAimStatus(tr("摇杆 %1% · 镜头开始转动时按空格保存 · ←／→ 暂停并微调 · Esc 取消").arg(status.percent));
    }
}

QVariantList StreamInputCapture::bindingLabels(bool driving, int slot) const {
    QVariantList list;
    for (const int binding : mapper_.bindings(driving, slot)) list.push_back(bindingLabel(binding));
    return list;
}
QVariantList StreamInputCapture::bindings() const { return bindingLabels(false, 0); }
QVariantList StreamInputCapture::drivingBindings() const { return bindingLabels(true, 0); }
QVariantList StreamInputCapture::alternateBindings() const { return bindingLabels(false, 1); }
QVariantList StreamInputCapture::drivingAlternateBindings() const { return bindingLabels(true, 1); }
QString StreamInputCapture::driveKeyLabel() const { return bindingLabel(mapper_.driveKey()); }
QString StreamInputCapture::quickKeyLabel() const { return bindingLabel(quickKey_); }
void StreamInputCapture::selectDriving(bool on) {
    if (driving() == on) return;
    if (mapped_) { mapped_->selectDriving(on, nowUs()); return; }
    mapper_.selectDriving(on);
    emit drivingChanged();
}
void StreamInputCapture::learnDriveKey(QObject* learningControls) { learn(Mapper::ActionCount, learningControls); }
void StreamInputCapture::learnQuickKey(QObject* learningControls) { learn(LearnQuickKey, learningControls); }
QVariantMap StreamInputCapture::tuning() const { return tuningMap(mapper_.tuning()); }
QVariantMap StreamInputCapture::padState() const {
    const int a=mapper_.tuning().adsAction;
    const bool ads=a==16?!driving()&&previewState_.l2!=0:a==17?previewState_.r2!=0:(previewState_.buttons&(1u<<a))!=0;
    const auto& timing = inputDiagnostics_;
    return {{"ads",ads},{"speed",previewSpeed_}, {"speedCms", previewSpeed_ * 2.54 / mapper_.tuning().dpi},
            {"timingReady", timing.revision != 0}, {"keyCount", qulonglong(timing.keys.events)},
            {"mouseCount", qulonglong(timing.mouse.events)}, {"keyP50", timing.keys.p50},
            {"keyP95", timing.keys.p95}, {"mouseP50", timing.mouse.p50}, {"mouseP95", timing.mouse.p95},
            {"sendP95", timing.sends.p95}, {"fullStickPct", timing.fullStickFraction * 100.},
            {"legacyFullSpeed", remoteplay::MouseStick::legacyFullSpeed(mapper_.tuning())},
            {QStringLiteral("buttons"), previewState_.buttons}, {QStringLiteral("leftX"), previewState_.leftX / 32767.0},
            {QStringLiteral("leftY"), previewState_.leftY / 32767.0}, {QStringLiteral("rightX"), previewState_.rightX / 32767.0},
            {QStringLiteral("rightY"), previewState_.rightY / 32767.0}, {QStringLiteral("l2"), previewState_.l2},
            {QStringLiteral("r2"), previewState_.r2}};
}
void StreamInputCapture::setKeyboardEnabled(bool enabled) {
    auto& current = xboxPlatform_ ? xboxKeys_ : keyboardEnabled_;
    if (enabled == current) return;
    releasePs5();
    current = enabled;
#ifdef VEYRA_ENABLE_REMOTEPLAY
    if (!xboxPlatform_ && enabled && ps5Target_ && ps5Target_()) engine_.remotePlayController({});
#endif
    save();
    emit mappingChanged();
}
void StreamInputCapture::learn(int action, QObject* learningControls, bool drivingLayout, int slot) {
    if (action < 0 || action > LearnQuickKey || slot < 0 || slot >= Mapper::SlotCount) return;
    end();
    cancelLearning();
    // An owned editor window has its own keyboard focus. Keep the player's
    // HWND for the quick reference and for capture after the editor closes.
    const auto* item = qobject_cast<QQuickItem*>(learningControls);
    auto* tool = item ? item->window() : nullptr;
    const HWND editor = tool && tool->isVisible() ? reinterpret_cast<HWND>(tool->winId()) : GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(editor, &pid);
    if (pid != GetCurrentProcessId()) return;
    learningWindow_ = editor;
    mapper_.reset();
    router_.reset();
    suspendIme(learningWindow_);
    learningControls_ = learningControls;
    learningDriving_ = drivingLayout;
    learningSlot_ = slot;
    learning_ = action;
    learningModifier_ = 0;
    feedback_.clear();
    emit mappingChanged();
}
void StreamInputCapture::learnPanelBinding(int action, QObject* learningControls, bool drivingLayout, int slot) {
    if (action < 0 || action >= Mapper::ActionCount) return;
    learn(action, learningControls, drivingLayout, slot);
    panelLearning_ = learning_ == action;
}
QVariantMap StreamInputCapture::bindingConflict() const {
    if (!conflict_.binding) return {};
    return {{QStringLiteral("key"), bindingLabel(conflict_.binding)},
            {QStringLiteral("source"), conflict_.source}};
}
void StreamInputCapture::resolveBindingConflict(bool move) {
    if (!conflict_.binding) return;
    const auto conflict = conflict_;
    conflict_ = {};
    if (move && mapper_.setBinding(conflict.action, conflict.binding, conflict.driving, conflict.slot)) {
        feedback_ = tr("已将 %1 从原按键移到此处。").arg(bindingLabel(conflict.binding));
        save();
    }
    emit mappingChanged();
}
void StreamInputCapture::bind(int key, int modifiers) {
    const int encoded = Mapper::encode(key, modifiers);
    if (unlockChord(encoded, mapper_.tuning().unlock) ||
        (learning_ != LearnQuickKey && encoded == quickKey_)) {
        feedback_ = tr("此键用于释放鼠标或查看键位，请换一个键。");
        emit mappingChanged();
        return;
    }
    if (learning_ == LearnQuickKey) {
        if (!quickKeyAvailable(encoded)) {
            feedback_ = tr("该快捷键已被占用，或不是可用的键盘组合。");
            emit mappingChanged();
            return;
        }
        quickKey_ = encoded;
        feedback_.clear();
        cancelLearning();
        save();
        return;
    }
    if (!Mapper::validBinding(encoded,mapper_.tuning().unlock!=0)) {
        feedback_ = tr("此键用于系统或 Veyra 快捷键，请换一个键。");
        emit mappingChanged();
        return;
    }
    const bool isDriveKey = learning_ == Mapper::ActionCount;
    const bool moved = !isDriveKey && mapper_.hasBinding(encoded, learningDriving_) &&
        mapper_.bindings(learningDriving_, learningSlot_)[learning_] != encoded;
    if (panelLearning_ && moved) {
        for (int slot = 0; slot < Mapper::SlotCount; ++slot) {
            const auto& keys = mapper_.bindings(learningDriving_, slot);
            const auto existing = std::find(keys.begin(), keys.end(), encoded);
            if (existing == keys.end()) continue;
            conflict_ = {encoded, int(existing - keys.begin()), learning_, learningSlot_, learningDriving_};
            restoreIme();
            learning_ = -1;
            panelLearning_ = false;
            learningControls_.clear();
            learningWindow_ = nullptr;
            feedback_.clear();
            mapper_.reset();
            router_.reset();
            emit mappingChanged();
            return;
        }
    }
    const bool assigned = isDriveKey ? mapper_.setDriveKey(encoded)
        : mapper_.setBinding(learning_, encoded, learningDriving_, learningSlot_);
    if (!assigned) {
        feedback_ = tr("此键已被另一套键位或布局切换占用。");
        emit mappingChanged();
        return;
    }
    feedback_ = !isDriveKey && moved ?
        tr("已将 %1 从原按键移到此处。").arg(bindingLabel(encoded)) : QString{};
    restoreIme();
    learning_ = -1;
    panelLearning_ = false;
    save();
    emit mappingChanged();
}
bool StreamInputCapture::quickKeyAvailable(int chord) const {
    const int key = chord & Mapper::KeyMask;
    return key > 0 && key < Mapper::MouseLeft && !Mapper::modifier(key) &&
           Mapper::validBinding(chord, mapper_.tuning().unlock != 0) &&
           chord != Mapper::encode(VK_F1, Mapper::Ctrl | Mapper::Alt) &&
           !unlockChord(chord, mapper_.tuning().unlock) &&
           chord != mapper_.driveKey() && !mapper_.hasBinding(chord, false) && !mapper_.hasBinding(chord, true);
}

bool StreamInputCapture::unlockAvailable(int setting) const {
    if (unlockChord(quickKey_, setting) || unlockChord(mapper_.driveKey(), setting)) return false;
    for (bool driving : {false, true})
        for (int slot = 0; slot < Mapper::SlotCount; ++slot)
            for (const int chord : mapper_.bindings(driving, slot))
                if (unlockChord(chord, setting)) return false;
    return true;
}

int StreamInputCapture::fallbackQuickKey() const {
    for (const int key : {VK_F2, VK_F3, VK_F4, VK_F5, VK_F6, VK_F11, VK_F12})
        if (quickKeyAvailable(key)) return key;
    return 0;
}

void StreamInputCapture::setActionNote(int action, bool driving, const QString& text) {
    if (action < 0 || action >= Mapper::ActionCount) return;
    auto& notes = notes_[driving];
    const auto note = shortNote(text);
    if (notes.value(action) == note) return;
    notes.resize(Mapper::ActionCount);
    notes[action] = note;
    save();
    emit mappingChanged();
}
void StreamInputCapture::clearBinding(int action, bool drivingLayout, int slot) {
    releasePs5();
    if (mapper_.setBinding(action, 0, drivingLayout, slot)) { save(); emit mappingChanged(); }
}
void StreamInputCapture::cancelLearning() {
    if (learning_ < 0 && !conflict_.binding) return;
    restoreIme();
    learning_ = -1;
    panelLearning_ = false;
    conflict_ = {};
    mapper_.reset();
    router_.reset();
    emit mappingChanged();
}
void StreamInputCapture::applyDefaults(bool drivingLayout) {
    releasePs5();
    mapper_.resetBindings(drivingLayout);
    for (int action = 0; action < Mapper::ActionCount; ++action) {
        const int chord = mapper_.bindings(drivingLayout)[action];
        if ((chord && chord == quickKey_) || unlockChord(chord, mapper_.tuning().unlock))
            mapper_.setBinding(action, 0, drivingLayout);
    }
    if (!drivingLayout) mapper_.setToggleMask(0);
    const auto toggle = mapper_.driveKey();
    if (!mapper_.setDriveKey(toggle)) mapper_.setDriveKey(0);
    feedback_.clear();
    save();
    emit mappingChanged();
}
void StreamInputCapture::resetMouseTuning() {
    auto defaults = newProfileTuning();
    defaults.mouseLook = mapper_.tuning().mouseLook;
    defaults.unlock = mapper_.tuning().unlock;
    applyMouseTuning(defaults);
}
void StreamInputCapture::selectAimAlgorithm(bool physical) {
    auto tune = mapper_.tuning();
    if (physical && !tune.aimConfigured) {
        tune.aimFullSpeed = std::clamp(remoteplay::MouseStick::legacyFullSpeed(tune), .25, 150.);
        tune.aimHorizontal = 1.;
        tune.aimVertical = std::clamp(tune.vertical / tune.horizontal, .1, 6.);
        tune.aimConfigured = true;
    }
    tune.physicalAim = physical;
    applyMouseTuning(tune);
}
void StreamInputCapture::setTuning(const QString& key, const QVariant& value) {
    auto map = tuning();
    if (!map.contains(key)) return;
    map[key] = value;
    if (key == "dpi") map["dpiConfirmed"] = true;
    if (key == "gameDeadzoneX" || key == "gameCurve") map["measuredRates"] = QVariantList{};
    applyMouseTuning(readTuning(map));
}
void StreamInputCapture::applyMouseTuning(const Mapper::Tuning& next) {
    if (next == mapper_.tuning()) return;
    if (next.unlock != mapper_.tuning().unlock && !unlockAvailable(next.unlock)) {
        feedback_ = tr("此快捷键已绑给手柄动作或其他功能，请先解绑。");
        emit mappingChanged();
        return;
    }
    feedback_.clear();
    mapper_.setTuning(next);
    if (mapped_) mapped_->setTuning(next);
    save();
    emit mappingChanged();
}
void StreamInputCapture::preview(bool on) {
    if (!on) { if (previewing()) end(); return; }
    const HWND window = GetForegroundWindow();
    DWORD pid = 0;
    GetWindowThreadProcessId(window, &pid);
    if (pid != GetCurrentProcessId()) return;
    if (previewing()) return;
    previewWindow_ = window;
    if (!begin(Route::Preview)) previewWindow_ = nullptr;
}
void StreamInputCapture::releasePs5() {
    if (ps5Captured() || xboxCaptured() || previewing()) end();
    if (armedAimMode_) {
        armedAimMode_ = 0;
        setAimStatus({});
    }
    cancelLearning();
}
void StreamInputCapture::load() {
    QFile file(profilePath_);
    if (!file.open(QIODevice::ReadOnly) || file.size() > 128 * 1024) return;
    const auto json = QJsonDocument::fromJson(file.readAll()).object();
    if (json.value(QStringLiteral("version")).toInt() != 1) return;
    profiles_ = json.value("profiles").toObject();
    const auto selected = json.value("profile").toString(profile_);
    keyboardEnabled_ = json.value("enabled").toBool();
    xboxKeys_ = json.value("xboxEnabled").toBool();
    nativeXbox_ = json.value("nativeXbox").toBool();
    xboxProfile_ = json.value("xboxProfile").toString();
    const auto gta = QStringLiteral("GTA V");
    if (profiles_.value(gta).toObject().value("gtaVProfile").toBool() ||
        (selected == gta && json.value("gtaVProfile").toBool())) {
        profiles_.remove(gta);
        if (selected == gta) {
            // The retired built-in preset is replaced by the new default.
            if (profiles_.contains(profile_)) readProfile(profiles_.value(profile_).toObject());
            return;
        }
    }
    profile_ = selected;
    readProfile(json);
}
void StreamInputCapture::suspendIme(HWND window) {
    if (imeWindow_ || !window) return;
    imeWindow_ = window;
    oldIme_ = ImmAssociateContext(window, nullptr);
    const auto current = ImmGetContext(window);
    veyra::log::info("ps5-input", std::format("ime suspended={} previousContext={}", current == nullptr, oldIme_ != nullptr));
    if (current) ImmReleaseContext(window, current);
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
    xboxOnlyProfile_ = json.value("xboxOnly").toBool();
    mapper_.setTuning(readTuning(json.value("tuning").toObject().toVariantMap()));
    mapper_.setDriveKey(0);
    mapper_.resetBindings();
    mapper_.resetBindings(true);
    mapper_.setBinding(Mapper::StickInwardBoth, 0);
    mapper_.setBinding(Mapper::StickInwardBoth, 0, true);
    const auto readBindings = [this](const QJsonArray& bindings, bool driving, int slot) {
        if (bindings.size() < 26 || bindings.size() > Mapper::HalfPedal + 1) return;
        for (int i = 0; i < Mapper::ActionCount; ++i) mapper_.setBinding(i, 0, driving, slot);
        for (int i = 0; i < bindings.size(); ++i)
            mapper_.setBinding(i, bindings[i].toInt(), driving, slot);
    };
    const auto walking = json.value("bindings").toArray();
    const auto driving = json.value("drivingBindings").toArray();
    readBindings(walking, false, 0);
    if (driving.size() < 26) mapper_.resetLegacyDrivingBindings();
    readBindings(driving, true, 0);
    readBindings(json.value("alternateBindings").toArray(), false, 1);
    readBindings(json.value("drivingAlternateBindings").toArray(), true, 1);
    const auto pedals = json.value("halfPedalBindings").toArray();
    for (int slot = 0; slot < std::min<qsizetype>(Mapper::SlotCount, pedals.size()); ++slot)
        mapper_.setBinding(Mapper::HalfPedal, pedals[slot].toInt(), true, slot);
    const auto& vehicle = mapper_.bindings(true);
    if (json.value("drivingLayoutVersion").toInt() < 2 && driving.size() >= 26 &&
        vehicle[Mapper::R1] == 'S' && vehicle[Mapper::R2] == 'W' && vehicle[Mapper::L2] == mapper_.bindings()[Mapper::L2]) {
        // Repair the old generated defaults, preserving unrelated driving keys.
        mapper_.setBinding(Mapper::L2, 'S', true);
        const int oldShoulder = mapper_.bindings()[Mapper::R1];
        mapper_.setBinding(Mapper::R1,
            std::find(vehicle.begin(), vehicle.end(), oldShoulder) == vehicle.end() ? oldShoulder : 0, true);
    }
    mapper_.setDriveKey(json.value("driveKey").toInt(Mapper::DefaultDriveKey));
    if (profile_ == QStringLiteral("默认")) {
        if (walking.size() <= Mapper::StickClickBoth && !mapper_.hasBinding('V', false))
            mapper_.setBinding(Mapper::StickClickBoth, 'V');
        if (driving.size() <= Mapper::StickClickBoth && !mapper_.hasBinding('V', true))
            mapper_.setBinding(Mapper::StickClickBoth, 'V', true);
    }
    mapper_.setToggleMask(uint64_t(json.value("toggles").toInteger()));
    const int quickKey = json.value("quickKey").toInt(DefaultQuickKey);
    quickKey_ = quickKeyAvailable(quickKey) ? quickKey : fallbackQuickKey();
    const auto inward = json.value("inwardStickBindings").toArray();
    for (bool drivingLayout : {false, true}) {
        const int layout = drivingLayout ? 1 : 0;
        const auto inwardKeys = layout < inward.size() ? inward.at(layout).toArray() : QJsonArray{};
        if (inwardKeys.isEmpty()) {
            constexpr int defaultInward = Mapper::Defaults[Mapper::StickInwardBoth];
            if (defaultInward != quickKey_ && !mapper_.hasBinding(defaultInward, drivingLayout))
                mapper_.setBinding(Mapper::StickInwardBoth, defaultInward, drivingLayout);
        } else {
            for (int slot = 0; slot < std::min<qsizetype>(Mapper::SlotCount, inwardKeys.size()); ++slot) {
                const int binding = inwardKeys[slot].toInt();
                if (binding && binding != quickKey_ && !unlockChord(binding, mapper_.tuning().unlock) &&
                    !mapper_.hasBinding(binding, drivingLayout))
                    mapper_.setBinding(Mapper::StickInwardBoth, binding, drivingLayout, slot);
            }
        }
        auto& notes = notes_[drivingLayout];
        notes.clear();
        const auto saved = json.value(drivingLayout ? "drivingActionNotes" : "actionNotes").toArray();
        for (int i = 0; i < std::min<qsizetype>(saved.size(), Mapper::ActionCount); ++i)
            notes.push_back(shortNote(saved[i].toString()));
    }
}
QJsonObject StreamInputCapture::activeProfile()const{
    const auto keys = [this](bool driving, int slot) {
        QJsonArray list;
        const auto& bindings = mapper_.bindings(driving, slot);
        for (int action = 0; action < Mapper::HalfPedal; ++action) list.append(bindings[action]);
        return list;
    };
    QJsonArray pedals;
    for (int slot = 0; slot < Mapper::SlotCount; ++slot) pedals.append(mapper_.bindings(true, slot)[Mapper::HalfPedal]);
    // Keep the legacy button arrays readable by older builds.
    QJsonArray inward;
    for (const bool driving : {false, true}) {
        QJsonArray inwardKeys;
        for (int slot = 0; slot < Mapper::SlotCount; ++slot)
            inwardKeys.append(mapper_.bindings(driving, slot)[Mapper::StickInwardBoth]);
        inward.append(inwardKeys);
    }
    auto profile = profiles_.value(profile_).toObject();
    profile.remove("gtaVProfile");
    auto options = profile.value("tuning").toObject();
    const auto current = tuning();
    for (auto it = current.cbegin(); it != current.cend(); ++it)
        options[it.key()] = QJsonValue::fromVariant(it.value());
    profile["bindings"] = keys(false, 0);
    profile["drivingBindings"] = keys(true, 0);
    profile["alternateBindings"] = keys(false, 1);
    profile["drivingAlternateBindings"] = keys(true, 1);
    profile["drivingLayoutVersion"] = 2;
    profile["driveKey"] = mapper_.driveKey();
    profile["quickKey"] = quickKey_;
    profile["actionNotes"] = QJsonArray::fromStringList(notes_[0]);
    profile["drivingActionNotes"] = QJsonArray::fromStringList(notes_[1]);
    profile["tuning"] = options;
    profile["toggles"] = qint64(mapper_.toggleMask());
    profile["xboxOnly"] = xboxOnlyProfile_;
    profile["halfPedalBindings"] = pedals;
    profile["inwardStickBindings"] = inward;
    return profile;
}
bool StreamInputCapture::toggleAction(int action)const{return action>=0&&action<Mapper::ActionCount&&(mapper_.toggleMask()&(uint64_t{1}<<action));}
void StreamInputCapture::setToggleAction(int action,bool on){
    if(action!=Mapper::L2&&action!=Mapper::L3&&action!=Mapper::Circle&&action!=Mapper::MenuStick)return;
    releasePs5();auto bit=uint64_t{1}<<action;
    mapper_.setToggleMask(on?mapper_.toggleMask()|bit:mapper_.toggleMask()&~bit);save();emit mappingChanged();
}
void StreamInputCapture::selectProfile(const QString& value,bool create){
    const auto name=value.trimmed();if(name.isEmpty()||name.size()>40||name==profile_)return;
    if(create?(profiles_.contains(name)||profiles_.size()>=16):!profiles().contains(name))return;
    releasePs5();profiles_[profile_]=activeProfile();profile_=name;
    if (!create) readProfile(profiles_.value(name).toObject());
    else {
        auto tuning = newProfileTuning();
        tuning.unlock = mapper_.tuning().unlock;
        mapper_.setTuning(tuning);
        xboxOnlyProfile_ = false;
    }
    selectDriving(false);
    save();emit mappingChanged();
}
bool StreamInputCapture::canRemoveProfile() const {
    if (profile_ == QStringLiteral("默认")) return false;
    for (auto item = profiles_.constBegin(); item != profiles_.constEnd(); ++item) {
        if (item.key() == profile_) continue;
        if (!item.value().toObject().value("xboxOnly").toBool() || (xboxPlatform_ && profile_ != ps5Profile_))
            return true;
    }
    return false;
}
void StreamInputCapture::renameProfile(const QString& value) {
    const auto name = value.trimmed();
    if (!canRemoveProfile() || name.isEmpty() || name.size() > 40 || name == profile_) return;
    if (profiles_.contains(name)) {
        feedback_ = tr("档案名称已存在。");
        emit mappingChanged();
        return;
    }
    releasePs5();
    const auto previousProfiles = profiles_;
    const auto previousPs5 = ps5Profile_;
    const auto previousXbox = xboxProfile_;
    const auto previousName = profile_;
    const auto active = activeProfile();
    profiles_.remove(previousName);
    profiles_[name] = active;
    profile_ = name;
    if (ps5Profile_ == previousName) ps5Profile_ = name;
    if (xboxProfile_ == previousName) xboxProfile_ = name;
    feedback_.clear();
    if (!save()) {
        profiles_ = previousProfiles;
        ps5Profile_ = previousPs5;
        xboxProfile_ = previousXbox;
        profile_ = previousName;
    }
    emit mappingChanged();
}
void StreamInputCapture::removeProfile() {
    if (!canRemoveProfile()) return;
    const auto removed = profile_;
    const auto replacement = [&](bool xbox) {
        QString firstMatching;
        for (auto item = profiles_.constBegin(); item != profiles_.constEnd(); ++item) {
            if (item.key() == removed || (!xbox && item.value().toObject().value("xboxOnly").toBool()))
                continue;
            if (item.key() == QStringLiteral("默认")) return item.key();
            if (firstMatching.isEmpty()) firstMatching = item.key();
        }
        return firstMatching;
    };
    const auto next = replacement(xboxPlatform_);
    if (next.isEmpty()) return;
    const auto nextPs5 = ps5Profile_ == removed ? replacement(false) : ps5Profile_;
    const auto nextXbox = xboxProfile_ == removed ? replacement(true) : xboxProfile_;
    if (nextPs5.isEmpty()) return;

    releasePs5();
    const auto previousProfiles = profiles_;
    const auto previousPs5 = ps5Profile_;
    const auto previousXbox = xboxProfile_;
    const auto previousActive = activeProfile();
    profiles_.remove(removed);
    ps5Profile_ = nextPs5;
    xboxProfile_ = nextXbox;
    profile_ = next;
    readProfile(profiles_.value(next).toObject());
    feedback_.clear();
    if (save()) {
        selectDriving(false);
    } else {
        profiles_ = previousProfiles;
        ps5Profile_ = previousPs5;
        xboxProfile_ = previousXbox;
        profile_ = removed;
        readProfile(previousActive);
    }
    emit mappingChanged();
}
QStringList StreamInputCapture::profiles() const {
    QStringList list;
    for (auto i = profiles_.begin(); i != profiles_.end(); ++i)
        if (xboxPlatform_ || !i.value().toObject().value("xboxOnly").toBool()) list.push_back(i.key());
    return list;
}
void StreamInputCapture::selectPlatform(bool xbox) {
    if (xbox == xboxPlatform_) return;
    releasePs5();
    profiles_[profile_] = activeProfile();
    (xboxPlatform_ ? xboxProfile_ : ps5Profile_) = profile_;
    xboxPlatform_ = xbox;
    const auto preferred = xbox && !xboxProfile_.isEmpty() ? xboxProfile_ : ps5Profile_;
    if (profiles().contains(preferred)) {
        profile_ = preferred;
        readProfile(profiles_.value(profile_).toObject());
    }
    emit mappingChanged();
}
void StreamInputCapture::setXboxOnlyProfile(bool only) {
    if (!xboxPlatform_ || profile_ == ps5Profile_ || xboxOnlyProfile_ == only) return;
    xboxOnlyProfile_ = only;
    save();
    emit mappingChanged();
}
void StreamInputCapture::setNativeXbox(bool enabled) {
    if (nativeXbox_ == enabled) return;
    releasePs5();
    nativeXbox_ = enabled;
    save();
    emit mappingChanged();
}
void StreamInputCapture::useTemplate(int index){
    if(index<0||index>2)return;
    auto t=mapper_.tuning();
    t.deadzone=index==1?.18:index==2?.05:.08;t.curve=index==1?.60:index==2?1.:.65;
    t.deadzoneShape=index==1?.5:0.;t.turnBoost=index==1?1.5:1.;
    if (t.physicalAim) {
        t.gameDeadzoneX = t.gameDeadzoneY = index == 1 ? .18 : index == 2 ? .05 : .08;
        t.gameCurve = index == 2 ? 1. : 2.;
    }
    applyMouseTuning(t);
}
bool StreamInputCapture::save() {
    profiles_[profile_] = activeProfile();
    (xboxPlatform_ ? xboxProfile_ : ps5Profile_) = profile_;
    auto json = profiles_.value(ps5Profile_).toObject();
    json["version"]=1;json["enabled"]=keyboardEnabled_;json["profile"]=ps5Profile_;json["profiles"]=profiles_;
    json["xboxEnabled"] = xboxKeys_;
    json["xboxProfile"] = xboxProfile_;
    json["nativeXbox"] = nativeXbox_;
    QDir().mkpath(QFileInfo(profilePath_).absolutePath());
    QSaveFile file(profilePath_);
    const auto data = QJsonDocument(json).toJson(QJsonDocument::Compact);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        feedback_ = tr("无法保存键鼠映射。");
        veyra::log::warn("stream-input", "saving PS5 keyboard mapping failed");
        return false;
    }
    return true;
}

} // namespace veyra::ui
