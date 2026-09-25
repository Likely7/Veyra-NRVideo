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
#include <QDebug>

#include <windows.h>

#include <memory>

#include "veyra/Log.h"
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
    // WS_CLIPSIBLINGS keeps Qt's own children from drawing over the video;
    // WS_EX_NOPARENTNOTIFY avoids a storm of notifications on every resize.
    return CreateWindowExW(WS_EX_NOPARENTNOTIFY, cls, L"", WS_CHILD | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
                           0, 0, 16, 16, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
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
    QGuiApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("Veyra"));
    app.setOrganizationName(QStringLiteral("Veyra"));

    engine::EngineController controller;

    // The window exists before QML loads so the engine can be handed a valid
    // HWND as soon as the user opens something.
    g_video = createVideoWindow();

    // The data directory mirrors what the Win32 build used, so presets and the
    // session file carry over instead of starting empty.
    ui::QmlPlayerBridge bridge(controller);
    bridge.attachVideoWindow(reinterpret_cast<qulonglong>(g_video));

    QQmlApplicationEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    engine.rootContext()->setContextProperty(QStringLiteral("veyra"), &bridge);

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app,
                     [] { QCoreApplication::exit(1); }, Qt::QueuedConnection);
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

    // Reparent the native window under the QML window and keep it in step. The
    // host item is looked up by objectName so the QML side owns the layout and
    // this side only follows it.
    QObject::connect(window, &QQuickWindow::sceneGraphInitialized, [window, &bridge] {
        if (g_video) SetParent(g_video, reinterpret_cast<HWND>(window->winId()));
        if (auto* host = window->findChild<QQuickItem*>(QStringLiteral("videoHost"))) {
            auto sync = [window, host] { syncVideoGeometry(window, host); };
            QObject::connect(host, &QQuickItem::xChanged, window, sync);
            QObject::connect(host, &QQuickItem::yChanged, window, sync);
            QObject::connect(host, &QQuickItem::widthChanged, window, sync);
            QObject::connect(host, &QQuickItem::heightChanged, window, sync);
            sync();
        } else {
            veyra::log::warn("qml", "no videoHost item: the video window has nowhere to sit");
        }
    });

    const int rc = app.exec();
    if (g_video) { DestroyWindow(g_video); g_video = nullptr; }
    return rc;
}
