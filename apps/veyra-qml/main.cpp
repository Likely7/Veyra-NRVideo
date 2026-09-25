// Veyra QML front end.
//
// The window is Qt; the video is not. The engine presents into a native child
// HWND that Qt hosts but never composites (measured in S3: 499/499 presents at
// 0.205 ms while the QML scene animated at its full 100 Hz panel rate). The QML
// scene draws the interface around and above that window.
//
// The video child window is created here, before the QML engine loads, so the
// first frame has somewhere to go. Its geometry is driven by the QML item named
// "videoHost": when that item moves or resizes, this code moves the native
// window to match. That keeps the layout in QML (where it belongs) and the
// pixels in D3D12 (where the latency budget lives).
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTimer>
#include <QVariantMap>
#include <QtCore/private/qabstractanimation_p.h>
#include <QDebug>

#include <windows.h>

#include <format>
#include <memory>

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/engine/EngineController.h"
#include "veyra/ui/QmlPlayerBridge.h"

using namespace veyra;

namespace {
HWND g_video = nullptr;

LRESULT CALLBACK videoProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    // The engine owns this window's pixels. Qt must not paint it, or the two
    // swapchains fight and we lose the whole point of the native path.
    if (m == WM_ERASEBKGND) return 1;
    if (m == WM_PAINT) { ValidateRect(h, nullptr); return 0; }
    return DefWindowProcW(h, m, w, l);
}

HWND createVideoWindow() {
    const wchar_t* cls = L"VeyraQmlVideoHost";
    WNDCLASSEXW wc{sizeof(wc)};
    wc.lpfnWndProc = videoProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = cls;
    wc.style = CS_HREDRAW | CS_VREDRAW;
    RegisterClassExW(&wc);
    // Created as an unrealised popup, then reparented into the QML window before
    // anything reads its size. Creating it as WS_CHILD with no parent does not
    // work: Windows refuses with ERROR_CANNOT_MAKE (1406), which is what left the
    // video window invalid and the swapchain at 1x1. WS_POPUP with no WS_VISIBLE
    // creates cleanly; SetParent plus the child style below turns it into a child.
    HWND created = CreateWindowExW(WS_EX_NOPARENTNOTIFY | WS_EX_TOOLWINDOW, cls, L"",
                                   WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                                   0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (created == nullptr) {
        veyra::log::error("qml", std::format("CreateWindowExW failed err={}", GetLastError()));
    }
    return created;
}

// Places the native video window at the QML item's position. Qt reports the
// item's geometry in scene coordinates, which for a child of the window's
// content item are already window-relative.
void syncVideoGeometry(QQuickWindow* window, QQuickItem* host) {
    if (!window || !host || !g_video) return;
    const QPointF topLeft = host->mapToScene(QPointF(0, 0));
    const qreal dpr = window->devicePixelRatio();
    // A fractional size is a resize artifact, not intent: floor it so the
    // swapchain never alternates between two sizes on alternating frames.
    const int x = int(std::floor(topLeft.x() * dpr));
    const int y = int(std::floor(topLeft.y() * dpr));
    const int w = std::max(1, int(std::floor(host->width() * dpr)));
    const int h = std::max(1, int(std::floor(host->height() * dpr)));
    SetWindowPos(g_video, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
} // namespace

int main(int argc, char** argv) {
    // A log file has to be opened explicitly; without this the QML app produced
    // no diagnostics at all, which made every failure look like a silent exit.
    // Same path and override as the Win32 build, so support instructions do not
    // change between the two front ends.
    wchar_t logOverride[32768]{};
    const auto overrideLength = GetEnvironmentVariableW(L"VEYRA_LOG_FILE", logOverride, 32768);
    auto logPath = overrideLength > 0 && overrideLength < 32768
                       ? std::filesystem::path(logOverride)
                       : veyra::runtime::logsDirectory() / L"veyra-qml.log";
    if (!veyra::Logger::instance().openFile(logPath.wstring(), true)) {
        logPath = logPath.parent_path() / (L"veyra-qml-" + std::to_wstring(GetCurrentProcessId()) + L".log");
        (void)veyra::Logger::instance().openFile(logPath.wstring(), true);
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Veyra"));
    app.setOrganizationName(QStringLiteral("Veyra"));
    veyra::log::info("app", "Veyra QML session started");

    engine::EngineController controller;

    // The window exists before QML loads so the engine can be handed a valid
    // HWND as soon as the user opens something.
    g_video = createVideoWindow();

    // The data directory mirrors what the Win32 build used, so presets and the
    // session file carry over instead of starting empty.
    ui::QmlPlayerBridge bridge(controller);
    bridge.attachVideoWindow(reinterpret_cast<qulonglong>(g_video));

    // Qt reports QML failures through its own message handler. Routing them to
    // the log is the difference between "it did not load" and why.
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& message) {
        const std::string utf8 = message.toUtf8().toStdString();
        if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
            veyra::log::error("qml", utf8);
        } else {
            veyra::log::info("qml", utf8);
        }
    });

    QQmlApplicationEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    engine.rootContext()->setContextProperty(QStringLiteral("veyra"), &bridge);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
    // Options are parsed rather than positional: `--page <id>` selects the screen
    // a test wants to capture, and any other non-flag argument is a file to open.
    //
    // The rest are test switches (G0.3). They put the app into the exact state a
    // design frame shows, so a screenshot of each can be compared with its design
    // reference without anyone clicking. They go to QML as `vyTest`, never to the
    // bridge's saved settings: a test run must not rewrite the user's preferences.
    //   --tab <quality|fg|color|audio|display>   professional-page tab
    //   --dialog <capture|ps5|screen|subtitle|audio|save|manage|tonode>
    //   --aspect <ratio>        cinema aspect, overriding the source's own
    //   --dock-pinned           dock open and held open
    //   --size <W>x<H>          window size in device-independent pixels
    //   --reduced-motion        every animation at zero duration
    //   --slow-animations <N>   every animation N times slower (motion sampling)
    //   --motion-probe <name>   dock | page | switch: start that motion, log its value per frame
    const QStringList args = QCoreApplication::arguments();
    QString openPath;
    QVariantMap testOptions;
    QSize testSize;
    for (int i = 1; i < args.size(); ++i) {
        const QString& a = args.at(i);
        const bool hasValue = i + 1 < args.size();
        if (a == QLatin1String("--page") && hasValue) {
            bridge.setInitialPage(args.at(++i));
        } else if (a == QLatin1String("--tab") && hasValue) {
            testOptions.insert(QStringLiteral("tab"), args.at(++i));
        } else if (a == QLatin1String("--dialog") && hasValue) {
            testOptions.insert(QStringLiteral("dialog"), args.at(++i));
        } else if (a == QLatin1String("--aspect") && hasValue) {
            testOptions.insert(QStringLiteral("aspect"), args.at(++i).toDouble());
        } else if (a == QLatin1String("--dock-pinned")) {
            testOptions.insert(QStringLiteral("dockPinned"), true);
        } else if (a == QLatin1String("--size") && hasValue) {
            const QStringList wh = args.at(++i).split(QLatin1Char('x'));
            if (wh.size() == 2) testSize = QSize(wh.at(0).toInt(), wh.at(1).toInt());
        } else if (a == QLatin1String("--motion-probe") && hasValue) {
            testOptions.insert(QStringLiteral("motionProbe"), args.at(++i));
        } else if (a == QLatin1String("--reduced-motion")) {
            testOptions.insert(QStringLiteral("reducedMotion"), true);
        } else if (a == QLatin1String("--slow-animations") && hasValue) {
            // Qt's own slow mode, the same one its debugging tools use: every
            // animation driven by the unified timer runs N times longer, so a
            // timed screenshot lands on a known point of the curve.
            const qreal factor = args.at(++i).toDouble();
            if (factor > 1.0) {
                QUnifiedTimer::instance()->setSlowModeEnabled(true);
                QUnifiedTimer::instance()->setSlowdownFactor(factor);
                testOptions.insert(QStringLiteral("slowAnimations"), factor);
            }
        } else if (!a.startsWith(QLatin1Char('-'))) {
            openPath = a;
        }
    }
    if (!testOptions.isEmpty() || testSize.isValid())
        veyra::log::info("qml", std::format("test switches: {} size={}x{}",
                                            QStringList(testOptions.keys()).join(',').toStdString(),
                                            testSize.width(), testSize.height()));
    engine.rootContext()->setContextProperty(QStringLiteral("vyTest"), testOptions);
    if (!openPath.isEmpty()) {
        QTimer::singleShot(600, &bridge, [&bridge, openPath] { bridge.openPath(openPath); });
    }

    engine.loadFromModule("Veyra", "Main");

    if (engine.rootObjects().isEmpty()) {
        veyra::log::error("qml", "no root object: the QML module failed to load");
        return 1;
    }

    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) {
        veyra::log::error("qml", "root object is not a window");
        return 1;
    }
    if (testSize.isValid()) window->resize(testSize);

    // Reparent the native window under the QML window and keep it in step. The
    // host item is looked up by objectName so the QML side owns the layout and
    // this side only follows it.
    // The host item's geometry is followed on a timer rather than through change
    // signals. Signals only fire when a value changes, and the first layout pass
    // can leave the host at its default (0x0) with no change to observe, which
    // is how the engine ended up building a swapchain from a 1x1 window.
    auto* follow = new QTimer(&app);
    QObject::connect(follow, &QTimer::timeout, &app, [window] {
        if (!g_video) return;
        // Report the search itself until it succeeds: "the log is silent" and
        // "the lookup failed" look identical otherwise, and that ambiguity cost
        // a round of guessing already.
        static int ticks = 0;
        ++ticks;
        if (ticks < 3 || ticks % 120 == 0) {
            auto* found = window->findChild<QQuickItem*>(QStringLiteral("videoHost"));
            veyra::log::info("qml", std::format("video host lookup: found={} contentChildren={}",
                                                found != nullptr,
                                                window->contentItem() ? window->contentItem()->childItems().size() : -1));
        }
        // Safety net: the pre-open hook parents the window before the engine reads
        // its size, but a resize before any open must also keep it in place.
        if (GetParent(g_video) == nullptr)
            SetParent(g_video, reinterpret_cast<HWND>(window->winId()));
        if (auto* host = window->findChild<QQuickItem*>(QStringLiteral("videoHost"))) {
            // Report the resolved geometry once at each distinct size, so the log
            // says what the video window was actually given.
            static int lastW = -1, lastH = -1;
            const int w = int(host->width()), h = int(host->height());
            if (w != lastW || h != lastH) {
                lastW = w; lastH = h;
                veyra::log::info("qml", std::format("video host geometry {}x{} at {},{}",
                                                    w, h, int(host->x()), int(host->y())));
            }
            syncVideoGeometry(window, host);
        }
    });
    follow->start(16);

    // Before every open, place the native window and force the pending layout to
    // be applied so the client size the presenter reads is the real one.
    bridge.setPreOpenHook([window] {
        auto* host = window->findChild<QQuickItem*>(QStringLiteral("videoHost"));
        veyra::log::info("qml", std::format("pre-open: host={} size={}x{} contentChildren={}",
                                            host != nullptr,
                                            host ? int(host->width()) : -1,
                                            host ? int(host->height()) : -1,
                                            window->contentItem() ? window->contentItem()->childItems().size() : -1));
        // Parent first: a WS_CHILD window without a parent is not realized, so its
        // rect stays 0x0 and any SetWindowPos is discarded. Reparenting here, not
        // on a later timer tick, is what lets the geometry below take effect.
        if (GetParent(g_video) == nullptr) {
            const HWND parent = reinterpret_cast<HWND>(window->winId());
            SetParent(g_video, parent);
            // SetParent alone leaves the popup style in place; a child style is what
            // makes the window clip to its parent and behave as part of the layout.
            const LONG_PTR style = GetWindowLongPtrW(g_video, GWL_STYLE);
            SetWindowLongPtrW(g_video, GWL_STYLE,
                              (style & ~(WS_POPUP)) | WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN);
        }
        if (host) syncVideoGeometry(window, host);
        ShowWindow(g_video, SW_SHOWNA);
        UpdateWindow(g_video);
        // Report the native window's own client rect, not the QML item's: the
        // presenter builds its swapchain from this, and the two sizes can differ.
        // The size the engine will build its swapchain from. Reporting it is what
        // makes a regression here visible instead of a 1x1 surface with no clue.
        RECT client{};
        GetClientRect(g_video, &client);
        veyra::log::info("qml", std::format("video window {}x{} parent={}",
                                            client.right, client.bottom,
                                            GetParent(g_video) != nullptr));
    });

    // A file on the command line opens immediately. This exists so playback can
    // be verified end to end by a test rather than by a person clicking around;
    // it is not a user-facing feature.
    const int rc = app.exec();
    if (g_video) { DestroyWindow(g_video); g_video = nullptr; }
    return rc;
}
