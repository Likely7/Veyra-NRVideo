// Qt + D3D12 host probe (S3.0).
//
// The migration's one real technical risk: the video is presented by a native
// child HWND that Veyra creates a D3D12 swapchain for. Qt must host that window
// without taking over presentation, or the low-latency path is lost. This probe
// answers three questions the plan depends on, and nothing else:
//
//   1. Does the video HWND render and present on top of the QML scene?
//   2. Does the overlay QML (controls) stay visible and interactive?
//   3. Does present still work while the QML scene animates (the latency risk)?
//
// It reports frame counts and present timing so the answers are numbers, not
// impressions.
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QTimer>
#include <QElapsedTimer>
#include <QDebug>
#include <QWindow>
#include <QFile>
#include <QDir>
#include <QLoggingCategory>

#include <windows.h>
#include <chrono>
#include <cstdio>
#include <atomic>
#include <thread>

namespace {
// Progress marker: if the probe hangs, the watchdog prints where, which is the
// answer we actually need. A silent hang would tell us nothing.
std::atomic<const char*> g_stage{"start"};
void mark(const char* what) {
    g_stage.store(what, std::memory_order_relaxed);
    std::printf("qt-probe: stage=%s\n", what);
    std::fflush(stdout);
}
// Hard stop. Qt plus a native swapchain can block in a present or a message
// pump; a diagnostic must still return a verdict.
void armWatchdog(int seconds) {
    std::thread([seconds] {
        std::this_thread::sleep_for(std::chrono::seconds(seconds));
        std::printf("qt-probe WATCHDOG after %ds at stage=%s\n", seconds,
                    g_stage.load(std::memory_order_relaxed));
        std::fflush(stdout);
        std::_Exit(9);
    }).detach();
}
} // namespace

#include "veyra/Log.h"
#include "veyra/gfx/CommandSlotRing.h"
#include "veyra/gfx/D3D12DeviceContext.h"
#include "veyra/gfx/PresentSink.h"

using namespace veyra;

namespace {
// A native child window: exactly what the product presents into today.
HWND g_video = nullptr;
LRESULT CALLBACK videoProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_ERASEBKGND) return 1;
    return DefWindowProcW(h, m, w, l);
}
HWND createVideoWindow(HWND parent) {
    static bool registered = false;
    const wchar_t* cls = L"VeyraQtProbeVideo";
    if (!registered) {
        WNDCLASSEXW wc{sizeof(wc)};
        wc.lpfnWndProc = videoProc; wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = cls; wc.style = CS_HREDRAW | CS_VREDRAW;
        RegisterClassExW(&wc); registered = true;
    }
    return CreateWindowExW(0, cls, L"", WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS,
                           0, 0, 100, 100, parent, nullptr, GetModuleHandleW(nullptr), nullptr);
}
struct ProbeStats {
    int prepared = 0, presented = 0, failed = 0;
    double prepareMs = 0, presentMs = 0;
    int qmlTicks = 0;
};
} // namespace

int main(int argc, char** argv) {
    setvbuf(stdout, nullptr, _IONBF, 0);
    armWatchdog(20);
    mark("qapp");
    // The probe is a real Qt app; it must also be a console app so the numbers
    // land in a log for the execution record.
    QGuiApplication app(argc, argv);
    // Qt's own diagnostics are the only way to know why a QML load failed; send
    // them somewhere we can read instead of losing them to a hidden console.
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext&, const QString& m) {
        std::printf("qt-msg: %s\n", m.toUtf8().constData());
        std::fflush(stdout);
    });
    std::printf("qt-probe: qtVersion=%s qmlImportPath=%s\n",
                qVersion(), qgetenv("QML2_IMPORT_PATH").constData());
    std::fflush(stdout);
    QQmlApplicationEngine engine;
    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");

    const char* qml = R"QML(
import QtQuick
import QtQuick.Controls
Window {
    id: root
    width: 960; height: 640
    visible: true
    color: "#0b0b0d"
    // A QML item that animates continuously: this is the interference source we
    // need present to survive. If presenting stalls while this runs, the
    // migration would cost latency.
    Rectangle {
        id: spinner
        width: 120; height: 120; radius: 12
        color: "#7aa2f7"
        anchors.right: parent.right; anchors.top: parent.top
        anchors.margins: 24
        NumberAnimation on rotation { from: 0; to: 360; duration: 1200; loops: Animation.Infinite }
    }
    Text {
        anchors.left: parent.left; anchors.bottom: parent.bottom
        anchors.margins: 16
        color: "white"; font.pixelSize: 20
        text: "QML overlay alive: " + spinner.rotation.toFixed(0)
    }
    // The slot the native video window is placed into.
    Item { id: videoHost; objectName: "videoHost"; anchors.fill: parent; anchors.margins: 0 }
}
)QML";
    engine.loadData(QByteArray(qml));
    if (engine.rootObjects().isEmpty()) { std::fprintf(stderr, "QML failed to load\n"); return 1; }
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) { std::fprintf(stderr, "root is not a QQuickWindow\n"); return 1; }

    // Native parent handle of the QML window: the video HWND becomes its child,
    // which is how the bridge works without Qt touching presentation.
    mark("winId");
    HWND parent = reinterpret_cast<HWND>(window->winId());
    g_video = createVideoWindow(parent);
    if (!g_video) { std::fprintf(stderr, "video child window failed\n"); return 1; }
    std::printf("qt-probe: qml window hwnd=%p video child hwnd=%p\n", (void*)parent, (void*)g_video);

    gfx::D3D12DeviceContext ctx;
    gfx::DeviceContextDesc ddesc;
    Status st = Status::Ok;
    if (!ctx.initialize(ddesc, st)) { std::fprintf(stderr, "device init failed\n"); return 1; }
    gfx::CommandSlotRing ring;
    if (!ring.initialize(ctx.device(), ctx.directQueue(), ctx.fence(), ctx.fenceEvent(), 4, st)) return 1;

    RECT rc{}; GetClientRect(g_video, &rc);
    gfx::PresentSink present;
    gfx::PresentSink::Desc pd;
    pd.targetWindow = g_video;
    pd.width = uint32_t(std::max<LONG>(rc.right, 640));
    pd.height = uint32_t(std::max<LONG>(rc.bottom, 360));
    pd.vsync = false;              // the product's low-latency setting
    pd.title = L"VeyraQtProbe";
    Status pst = Status::Ok;
    mark("present-init");
    if (!present.initialize(ctx.device(), ctx.directQueue(), pd, pst)) {
        std::fprintf(stderr, "present sink init failed\n");
        return 1;
    }

    auto stats = std::make_shared<ProbeStats>();
    QElapsedTimer wall; wall.start();
    auto* timer = new QTimer(&app);
    // Present at a fixed rate while QML animates, and measure how long each
    // present takes. The point is the trend, not the absolute figure.
    QObject::connect(timer, &QTimer::timeout, [&, stats] {
        if (wall.elapsed() > 4000) {
            std::printf("qt-probe RESULT prepared=%d presented=%d failed=%d "
                        "avgPrepare=%.3fms avgPresent=%.3fms qmlFrames=%d\n",
                        stats->prepared, stats->presented, stats->failed,
                        stats->prepared ? stats->prepareMs / stats->prepared : 0.0,
                        stats->presented ? stats->presentMs / stats->presented : 0.0,
                        stats->qmlTicks);
            QCoreApplication::quit();
            return;
        }
        // The sink only presents; drawing is the graph's job and is out of
        // scope here. Present timing is the number that matters: it must not
        // grow while the QML scene animates.
        bool closed = false;
        present.processMessages(closed);
        if (closed) { QCoreApplication::quit(); return; }
        const auto t1 = std::chrono::steady_clock::now();
        Status st2 = Status::Ok;
        if (present.present(st2)) {
            const auto t2 = std::chrono::steady_clock::now();
            stats->presentMs += std::chrono::duration<double, std::milli>(t2 - t1).count();
            ++stats->presented;
        } else ++stats->failed;
        ++stats->prepared;
    });
    // Count QML animation frames to prove the scene kept running while the
    // native window presented.
    QObject::connect(window, &QQuickWindow::frameSwapped, [stats] { ++stats->qmlTicks; });
    mark("exec");
    timer->start(8);   // ~120 Hz ceiling: faster than any panel we target

    const int rc2 = app.exec();
    present.shutdown();
    DestroyWindow(g_video);
    return rc2;
}
