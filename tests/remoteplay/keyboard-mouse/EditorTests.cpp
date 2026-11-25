#include "veyra/ui/Ps5KeyboardSettings.h"
#ifdef VEYRA_TEST_CONSOLE
#include "TestConsole.h"
using veyra::remoteplay::test::console;
#endif

#include "veyra/ui/UiLanguage.h"
#include <QAbstractEventDispatcher>
#include <QDir>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QSettings>
#include <QTemporaryDir>
#include <QWindow>
#include <QtQuickTest/quicktest.h>
#include <condition_variable>
#include <mutex>

using veyra::remoteplay::ControllerState;
using veyra::remoteplay::KeyboardMouse;
using veyra::ui::Ps5KeyboardSettings;

class Setup : public QObject {
    Q_OBJECT
    QTemporaryDir directory_;
    std::mutex mutex_;
    std::condition_variable submitted_;
    ControllerState last_;
    int submissions_ = 0;
    bool allowGrab_ = true;
    std::shared_ptr<KeyboardMouse> session_;
    std::unique_ptr<Ps5KeyboardSettings> settings_;
#ifdef VEYRA_TEST_CONSOLE
    std::unique_ptr<veyra::source::RemotePlaySessionSource> route_;
    template<class Match>
    bool sentSince(int first, Match match) {
        std::lock_guard lock(console.mutex);
        if (console.sent.empty()) return false;
        const auto start = first < 0 ? console.sent.size() - 1 : static_cast<size_t>(first);
        return start < console.sent.size() && std::all_of(console.sent.begin() + start, console.sent.end(), match);
    }
#endif

    QString preset() const {
        return QCoreApplication::applicationDirPath() + "/profiles/gta-vi-ps5-0.6.87-calibrated.v2.json";
    }

    // Deliver a message to this process's installed native filters only.
    bool dispatch(UINT message, WPARAM key = 0, LPARAM flags = 0) {
        auto* window = QGuiApplication::focusWindow();
        if (!window) return false;
        MSG event{};
        event.hwnd = reinterpret_cast<HWND>(window->winId());
        event.message = message;
        event.wParam = key;
        event.lParam = flags;
        qintptr result = 0;
        return QAbstractEventDispatcher::instance()->filterNativeEvent("windows_generic_MSG", &event, &result);
    }

public slots:
    void applicationAvailable() {
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, directory_.path());
        // Qt's offscreen plugin does not load the Windows UI font automatically.
        const auto fonts = QDir(qEnvironmentVariable("WINDIR")).filePath("Fonts/");
        QFontDatabase::addApplicationFont(fonts + "segoeui.ttf");
        QFontDatabase::addApplicationFont(fonts + "segoeuib.ttf");
        QGuiApplication::setFont(QFont("Segoe UI"));
        QFont::insertSubstitution("Noto Sans SC", "Segoe UI");
    }
    void qmlEngineAvailable(QQmlEngine* engine) {
        // Exercise the real filter and translator. The test window does not register
        // devices or clip the desktop cursor.
        settings_ = std::make_unique<Ps5KeyboardSettings>(directory_.path(), preset(),
            [this] { return session_; }, [this](HWND, bool on) { return !on || allowGrab_; });
        engine->addImportPath(QStringLiteral(VEYRA_TEST_QML));
        veyra::ui::i18n::apply("en");
        engine->setUiLanguage("en");
        engine->rootContext()->setContextProperty("testSettings", settings_.get());
        engine->rootContext()->setContextProperty("fixture", this);
    }
    void connectConsole() {
        // Posted key messages use the GUI thread's modifier state. Each simulated
        // input session starts with all keys up, independent of the desktop user.
        BYTE releasedKeys[256]{};
        if (!SetKeyboardState(releasedKeys))
            qFatal("Could not initialize the test thread's keyboard state");
        session_ = std::make_shared<KeyboardMouse>([this](ControllerState state) {
            std::lock_guard lock(mutex_);
            last_ = std::move(state);
            ++submissions_;
            submitted_.notify_all();
        });
    }
    void allowMouseGrab(bool allow) { allowGrab_ = allow; }
    void disconnectConsole() {
        session_.reset();
#ifdef VEYRA_TEST_CONSOLE
        route_.reset();
#endif
    }
    bool hasConsoleTransport() const {
#ifdef VEYRA_TEST_CONSOLE
        return true;
#else
        return false;
#endif
    }
#ifdef VEYRA_TEST_CONSOLE
    bool connectVideoRoute(bool captureCard, bool viewOnly) {
        disconnectConsole();
        route_ = std::make_unique<veyra::source::RemotePlaySessionSource>();
        if (!console.connect(*route_, captureCard, viewOnly)) return false;
        session_ = route_->keyboardMouse();
        return true;
    }
    int sentCount() { return static_cast<int>(console.mark()); }
    bool heldTriangleSince(int first) {
        return sentSince(first, [](const auto& s) { return s.buttons == ControllerState::Triangle; });
    }
    bool heldThrottleSince(int first) {
        return sentSince(first, [](const auto& s) { return s.r2 == 255 && !s.buttons; });
    }
    bool sentTriangle() { return heldTriangleSince(-1); }
    bool sentThrottle() { return heldThrottleSince(-1); }
    bool sentNeutral() { return sentSince(-1, [](const auto& s) { return s == ControllerState{}; }); }
#endif
    bool reloadCopy(QString selected, int dpi) {
        Ps5KeyboardSettings reopened(directory_.path(), preset(), [] { return nullptr; });
        return reopened.selected() == selected && reopened.dpi() == dpi && reopened.profile() == settings_->profile();
    }
    QUrl exportUrl() { return QUrl::fromLocalFile(directory_.filePath("roundtrip.json")); }
    bool reloadShortcuts(const QString& overlay, const QString& control) {
        Ps5KeyboardSettings reopened(directory_.path(), preset(), [] { return nullptr; });
        return reopened.overlayKey() == overlay && reopened.controlKey() == control
            && reopened.showShortcutHints() == settings_->showShortcutHints();
    }

    QString evidencePath(const QString& name) {
        const auto directory = qEnvironmentVariable("VEYRA_TEST_EVIDENCE_DIR");
        return directory.isEmpty() ? QString{} : QDir(directory).filePath(name);
    }
    bool nativeKey(int key, int scan, bool down) {
        return dispatch(down ? WM_KEYDOWN : WM_KEYUP, key, static_cast<LPARAM>(scan) << 16);
    }
    bool nativeSystemKey(int key, int scan, bool down, bool alt) {
        const LPARAM flags = 1 | (static_cast<LPARAM>(scan) << 16) | (alt ? (LPARAM{1} << 29) : 0);
        return dispatch(down ? WM_SYSKEYDOWN : WM_SYSKEYUP, key, down ? flags : flags | (LPARAM{3} << 30));
    }
    void practiceKey(int key, quint32 scan, int modifiers) {
        if (auto* window = QGuiApplication::focusWindow()) {
            for (auto type : {QEvent::KeyPress, QEvent::KeyRelease}) {
                QKeyEvent event(type, key, Qt::KeyboardModifiers(modifiers), scan, 0, 0);
                QCoreApplication::sendEvent(window, &event);
            }
        }
    }
    QStringList sharedDeviceObservations(QWindow* practice, QWindow* receiver) {
        // Windows supplies the raw packet and its friendly device name. Stand
        // in for those two queries so the test needs no physical input.
        veyra::ui::Ps5InputDevices monitor(
            [](HRAWINPUT input) { return *reinterpret_cast<const RAWINPUTHEADER*>(input); },
            [](HANDLE) { return QStringLiteral("Practice keyboard"); });
        RAWINPUTHEADER header{RIM_TYPEKEYBOARD, sizeof(RAWINPUTHEADER), reinterpret_cast<HANDLE>(1), 0};
        const auto report = [&](WPARAM source) {
            MSG message{};
            message.hwnd = reinterpret_cast<HWND>(receiver->winId());
            message.message = WM_INPUT;
            message.wParam = source;
            message.lParam = reinterpret_cast<LPARAM>(&header);
            QAbstractEventDispatcher::instance()->filterNativeEvent("windows_generic_MSG", &message, nullptr);
            return monitor.keyboard();
        };
        monitor.start(receiver); // A different window has focus.
        QStringList observations{report(RIM_INPUT)};
        monitor.start(practice);
        observations << report(RIM_INPUTSINK) << report(RIM_INPUT);
        return observations;
    }
    bool nativeRepeat(int key, int scan) {
        return dispatch(WM_KEYDOWN, key, (static_cast<LPARAM>(scan) << 16) | (1L << 30));
    }
    bool realWindowKeys() { return QGuiApplication::platformName() == "windows"; }
    bool postPlayerKey(int key, int scan) {
        auto* window = QGuiApplication::focusWindow();
        if (!window || !realWindowKeys()) return false;
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        const LPARAM flags = 1 | (static_cast<LPARAM>(scan) << 16);
        return PostMessageW(hwnd, WM_KEYDOWN, key, flags) &&
               PostMessageW(hwnd, WM_KEYUP, key, flags | (LPARAM{3} << 30));
    }
    bool crossDown() { std::lock_guard lock(mutex_); return (last_.buttons & ControllerState::Cross) != 0; }
    void physicalCross() {
        ControllerState state;
        state.buttons = ControllerState::Cross;
#ifdef VEYRA_TEST_CONSOLE
        if (route_) {
            route_->controller(state);
            return;
        }
#endif
        session_->physical(state);
    }
    int submissions() { std::lock_guard lock(mutex_); return submissions_; }
    bool triangleDown() { std::lock_guard lock(mutex_); return (last_.buttons & ControllerState::Triangle) != 0; }
    bool neutral() { std::lock_guard lock(mutex_); return last_ == ControllerState{}; }
    bool waitForThrottle() {
        // Wait for the real Mapper worker while the UI thread cannot poll the situation.
        std::unique_lock lock(mutex_);
        return submitted_.wait_for(lock,std::chrono::seconds(1),[this] { return last_.r2 == 255; });
    }
    void releaseBy(const QString& reason) {
        if (reason == "focus") {
            dispatch(WM_KILLFOCUS);
        } else if (reason == "alt-tab") {
            dispatch(WM_SYSKEYDOWN, VK_TAB, (1L << 29) | (0x0f << 16));
        } else {
            nativeKey(VK_CONTROL, 0x1d, true);
            nativeKey(VK_MENU, 0x38, true);
            nativeKey(VK_SHIFT, 0x2a, true);
            nativeKey('Z', 0x2c, true);
        }
    }
};

QUICK_TEST_MAIN_WITH_SETUP(ps5_keyboard, Setup)
#include "EditorTests.moc"
