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
#include <QKeyEvent>
#include <QTimer>
#include <QPointer>
#include <QVariantMap>
#include <QList>
#include <QRect>
#include <QtCore/private/qabstractanimation_p.h>
#include <QDebug>

#include <windows.h>

#include <cmath>
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
    // The pointer belongs to the QML layer under the picture (the dock's hot zone
    // at the top edge, the cinema controls): pass hit testing through to it.
    if (m == WM_NCHITTEST) return HTTRANSPARENT;
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
    // Called every rendered frame: only a changed rect reaches the window manager,
    // so a moving popover never makes the presenter see a spurious resize.
    static RECT last{-1, -1, -1, -1};
    if (IsWindowVisible(g_video) && last.left == x && last.top == y && last.right == w && last.bottom == h) return;
    last = RECT{x, y, w, h};
    SetWindowPos(g_video, HWND_TOP, x, y, w, h, SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

struct Cover { QRectF rect; qreal radius = 0; };

void collectCovers(QQuickItem* item, QList<Cover>& out) {
    if (!item->isVisible() || item->opacity() <= 0.0) return;
    if (item->objectName() == QLatin1String("videoCover")) {
        // Scale is folded into the scene rect; the corner radius scales with it.
        const QRectF r = item->mapRectToScene(QRectF(0, 0, item->width(), item->height()));
        const qreal s = item->width() > 0 ? r.width() / item->width() : 1.0;
        out << Cover{r, item->property("coverRadius").toReal() * s};
    }
    for (QQuickItem* child : item->childItems()) collectCovers(child, out);
}

// The video is a native child window, so it is drawn above the whole QML scene
// (airspace): a menu or a dialog over the picture would sit behind it. Items named
// "videoCover" (popover panels, the dialog scrim) are cut out of the window's
// region instead, so the QML under them shows. Scene rects include transforms,
// so the hole follows a popover's scale-in. The region only changes when a cover
// moves; with none the window gets its full rectangle back.
void syncVideoCovers(QQuickWindow* window, QQuickItem* host) {
    if (!window || !host || !g_video) return;
    QList<Cover> covers;
    collectCovers(window->contentItem()->parentItem() ? window->contentItem()->parentItem()
                                                      : window->contentItem(), covers);
    const qreal dpr = window->devicePixelRatio();
    const QPointF origin = host->mapToScene(QPointF(0, 0));
    static QList<QRect> last;
    QList<QRect> holes;       // x, y, w, h; the radius rides along as a fifth rect
    for (const Cover& c : covers) {
        const QRectF local = c.rect.translated(-origin);
        holes << QRect(int(std::floor(local.left() * dpr)), int(std::floor(local.top() * dpr)),
                       int(std::ceil(local.width() * dpr)) + 1, int(std::ceil(local.height() * dpr)) + 1);
        holes << QRect(0, 0, int(std::round(c.radius * dpr * 2)), 0);
    }
    if (holes == last) return;
    last = holes;
    if (holes.isEmpty()) { SetWindowRgn(g_video, nullptr, TRUE); return; }
    const int w = std::max(1, int(std::floor(host->width() * dpr)));
    const int h = std::max(1, int(std::floor(host->height() * dpr)));
    HRGN region = CreateRectRgn(0, 0, w, h);
    for (qsizetype i = 0; i + 1 < holes.size(); i += 2) {
        const QRect& hole = holes[i];
        const int d = holes[i + 1].width();
        HRGN cut = d > 0 ? CreateRoundRectRgn(hole.left(), hole.top(), hole.left() + hole.width() + 1,
                                              hole.top() + hole.height() + 1, d, d)
                         : CreateRectRgn(hole.left(), hole.top(), hole.left() + hole.width(), hole.top() + hole.height());
        CombineRgn(region, region, cut, RGN_DIFF);
        DeleteObject(cut);
    }
    SetWindowRgn(g_video, region, TRUE);   // the system owns the region from here
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
    //   --menu <source|preset>  professional-page popover menu open
    //   --dock-pinned           dock open and held open
    //   --tip <page>            the dock button's tooltip for that page shown (review)
    //   --size <W>x<H>          window size in device-independent pixels
    //   --reduced-motion        every animation at zero duration
    //   --slow-animations <N>   every animation N times slower (motion sampling)
    //   --full-bar <shown|hidden>  fullscreen with the control window held shown / hidden
    //   --full-debug            log every pointer movement the fullscreen controls see
    //   --motion-probe <name>   dock | page | switch | seg | menu: start that motion, log its value per frame
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
        } else if (a == QLatin1String("--menu") && hasValue) {
            testOptions.insert(QStringLiteral("menu"), args.at(++i));
        } else if (a == QLatin1String("--aspect") && hasValue) {
            testOptions.insert(QStringLiteral("aspect"), args.at(++i).toDouble());
        } else if (a == QLatin1String("--tip") && hasValue) {
            testOptions.insert(QStringLiteral("tip"), args.at(++i));
        } else if (a == QLatin1String("--export-out") && hasValue) {
            // A review run's export target, so a real export starts without the dialog.
            bridge.setExportPath(args.at(++i));
        } else if (a == QLatin1String("--full-bar") && hasValue) {
            testOptions.insert(QStringLiteral("fullBar"), args.at(++i));
        } else if (a == QLatin1String("--full-debug")) {
            testOptions.insert(QStringLiteral("fullDebug"), true);
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
    // The host item is followed per rendered frame rather than on a timer or through
    // its own change signals. afterAnimating fires on the GUI thread before every
    // frame Qt produces, so anything that moves the host (layout, a resize, the
    // page switch, a cinema height animation, a popover scaling in) is picked up in
    // the same frame, and an idle scene renders no frames and costs nothing. Change
    // signals alone were not enough: the first layout pass can leave the host at its
    // default 0x0 with no change to observe, which is how the engine once built a
    // swapchain from a 1x1 window. The native calls only run when a value changed.
    auto follow = [window] {
        if (!g_video) return;
        // Safety net: the pre-open hook parents the window before the engine reads
        // its size, but a resize before any open must also keep it in place.
        if (GetParent(g_video) == nullptr)
            SetParent(g_video, reinterpret_cast<HWND>(window->winId()));
        static QPointer<QQuickItem> host;
        if (!host) {
            host = window->findChild<QQuickItem*>(QStringLiteral("videoHost"));
            // Report the search: "the log is silent" and "the lookup failed" look
            // identical otherwise.
            veyra::log::info("qml", std::format("video host lookup: found={} contentChildren={}",
                                                host != nullptr,
                                                window->contentItem() ? window->contentItem()->childItems().size() : -1));
            if (!host) return;
        }
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
        syncVideoCovers(window, host);
    };
    // V held shows the original picture on the professional page (AppShell: the
    // OriginalHold key, engine.comparison(1, false) until release). A Shortcut only
    // sees the press, so the window's key events are watched here. Auto-repeat is
    // ignored, and a focused text field keeps its V.
    struct HoldOriginalFilter : QObject {
        QQuickWindow* window; ui::QmlPlayerBridge* bridge; bool held = false;
        HoldOriginalFilter(QQuickWindow* w, ui::QmlPlayerBridge* b) : window(w), bridge(b) {}
        bool eventFilter(QObject*, QEvent* e) override {
            const auto t = e->type();
            if (t == QEvent::FocusOut && held) { held = false; bridge->holdOriginal(false); return false; }
            if (t != QEvent::KeyPress && t != QEvent::KeyRelease) return false;
            auto* k = static_cast<QKeyEvent*>(e);
            if (k->key() != Qt::Key_V || k->isAutoRepeat() || k->modifiers() != Qt::NoModifier) return false;
            const bool press = t == QEvent::KeyPress;
            if (press) {
                if (window->property("page").toString() != QLatin1String("pro")) return false;
                if (auto* f = window->activeFocusItem(); f && f->inherits("QQuickTextInput")) return false;
                if (auto* f = window->activeFocusItem(); f && f->inherits("QQuickTextEdit")) return false;
            }
            if (press == held) return false;
            held = press;
            bridge->holdOriginal(press);
            return true;
        }
    };
    window->installEventFilter(new HoldOriginalFilter(window, &bridge));
    QObject::connect(window, &QQuickWindow::afterAnimating, &app, follow);
    // The first frame may come before the page layout settles; one pass after the
    // event loop starts covers a scene that then never animates.
    QTimer::singleShot(0, &app, follow);

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
