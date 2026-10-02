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
#include <QApplication>
#include <QProcess>
#include <QIcon>
#include <QImage>
#include <QPixmap>
#include "../veyra/resource.h"
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QKeyEvent>
#include <QTimer>
#include <atomic>
#include <QPointer>
#include <QVariantMap>
#include <QList>
#include <QRect>
#include <QtCore/private/qabstractanimation_p.h>
#include <QDebug>
#include <QAbstractNativeEventFilter>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

#include <windows.h>
#include <shellapi.h>
#include <shobjidl.h>
#include <dbghelp.h>
#include <d3d12.h>

#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>

#include "veyra/Log.h"
#include "veyra/RuntimePaths.h"
#include "veyra/engine/EngineController.h"
#include "veyra/engine/ExportJobManager.h"
#include "veyra/gfx/PresentationHooks.h"
#include "veyra/ui/QmlDataDirectory.h"
#include "veyra/ui/QmlPlayerBridge.h"
#include "veyra/ui/ThumbnailProvider.h"

using namespace veyra;

namespace {
HWND g_video = nullptr;
enum class VideoProbeMode { Normal, Hidden, NoRegion, CoversOnly };
VideoProbeMode g_videoProbe = VideoProbeMode::Normal;
bool g_videoHiddenByProbe = false;
// Set while a new source is opening: the window keeps the previous session's last
// frame until the new one presents, so its region is emptied instead. A slow PS5
// connect after a capture card left the card's last frame up for 20 s and the
// window looked frozen (field report 2026-10-01).
bool g_videoBlanked = false;

void blankVideo(const char* why) {
    if (!g_video || g_videoBlanked) return;
    g_videoBlanked = true;
    const int result = SetWindowRgn(g_video, CreateRectRgn(0, 0, 0, 0), TRUE);
    veyra::log::info("qml-window", std::format("blank video until the new source presents ({}) result={}", why, result));
}

const char* videoProbeName() {
    switch (g_videoProbe) {
    case VideoProbeMode::Hidden: return "hidden";
    case VideoProbeMode::NoRegion: return "no-region";
    case VideoProbeMode::CoversOnly: return "covers-only";
    default: return "normal";
    }
}

void logVideoWindowState(QQuickWindow* window, QQuickItem* host, const char* event) {
    if (!window || !host || !g_video) return;
    const HWND parent = GetParent(g_video);
    RECT parentClient{}, client{}, screen{};
    GetClientRect(parent, &parentClient);
    GetClientRect(g_video, &client);
    GetWindowRect(g_video, &screen);
    HRGN region = CreateRectRgn(0, 0, 0, 0);
    const int regionType = GetWindowRgn(g_video, region);
    RECT regionBox{};
    const int regionBoxType = GetRgnBox(region, &regionBox);
    DeleteObject(region);
    const QPointF scene = host->mapToScene(QPointF(0, 0));
    veyra::log::info("qml-window", std::format(
        "{} probe={} root={}x{} dpr={:.3f} host=({:.1f},{:.1f}) {:.1f}x{:.1f} "
        "parent={} parentClient={}x{} client={}x{} screen=({},{}) {}x{} "
        "style=0x{:X} visible={} regionType={} regionBoxType={} regionBox=({},{}) {}x{}",
        event, videoProbeName(), window->width(), window->height(), window->devicePixelRatio(),
        scene.x(), scene.y(), host->width(), host->height(), parent != nullptr,
        parentClient.right, parentClient.bottom, client.right, client.bottom,
        screen.left, screen.top, screen.right - screen.left, screen.bottom - screen.top,
        static_cast<unsigned long long>(GetWindowLongPtrW(g_video, GWL_STYLE)),
        IsWindowVisible(g_video) != 0, regionType, regionBoxType,
        regionBox.left, regionBox.top, regionBox.right - regionBox.left,
        regionBox.bottom - regionBox.top));
}

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
bool syncVideoGeometry(QQuickWindow* window, QQuickItem* host) {
    if (!window || !host || !g_video || GetParent(g_video) != reinterpret_cast<HWND>(window->winId()))
        return false;
    const QPointF topLeft = host->mapToScene(QPointF(0, 0));
    const qreal dpr = window->devicePixelRatio();
    if (host->width() < 1 || host->height() < 1 ||
        topLeft.x() + host->width() <= 0 || topLeft.y() + host->height() <= 0 ||
        topLeft.x() >= window->width() || topLeft.y() >= window->height()) {
        if (IsWindowVisible(g_video)) ShowWindow(g_video, SW_HIDE);
        return false;
    }
    // A fractional size is a resize artifact, not intent: floor it so the
    // swapchain never alternates between two sizes on alternating frames.
    const int x = int(std::floor(topLeft.x() * dpr));
    const int y = int(std::floor(topLeft.y() * dpr));
    const int w = std::max(1, int(std::floor(host->width() * dpr)));
    const int h = std::max(1, int(std::floor(host->height() * dpr)));
    // Called every rendered frame: only a changed rect reaches the window manager,
    // so a moving popover never makes the presenter see a spurious resize.
    static RECT last{-1, -1, -1, -1};
    if (IsWindowVisible(g_video) && last.left == x && last.top == y && last.right == w && last.bottom == h) return true;
    last = RECT{x, y, w, h};
    SetLastError(0);
    const BOOL placed = SetWindowPos(g_video, HWND_TOP, x, y, w, h,
                                     SWP_NOACTIVATE | (g_videoHiddenByProbe ? 0 : SWP_SHOWWINDOW));
    const DWORD error = placed ? 0 : GetLastError();
    veyra::log::info("qml-window", std::format(
        "SetWindowPos rect=({},{}) {}x{} result={} error={}", x, y, w, h, placed != 0, error));
    logVideoWindowState(window, host, "geometry");
    return placed != 0;
}

struct Cover { QRectF rect; qreal radius = 0; };

// cineIn (pages.css @keyframes cineIn, plan M21): the stage is clipped with
// inset(12% 0 12% 0) and expands to inset(0). The picture is a native window, so
// the equivalent is its REGION: bands are cut off the top and bottom while the
// animation runs. The window and the swapchain keep their size, so this costs no
// ResizeBuffers - only the visible band changes. MinimalPage.inset carries the
// fraction (0.12 -> 0).
//
// The walk starts at the window's content item, NOT at the video host: the host is a
// sibling of the pages, so searching under it finds nothing. (The cover walk has the
// same shape and the same reason; both are rooted at the scene.)
qreal videoInsetFraction(QQuickItem* item) {
    if (!item) return 0.0;
    if (item->objectName() == QLatin1String("videoInset"))
        return item->property("frac").toReal();
    for (QQuickItem* child : item->childItems()) {
        const qreal f = videoInsetFraction(child);
        if (f > 0.0) return f;
    }
    return 0.0;
}

qreal sceneInsetFraction(QQuickWindow* window) {
    if (!window) return 0.0;
    QQuickItem* content = window->contentItem();
    QQuickItem* root = content && content->parentItem() ? content->parentItem() : content;
    return videoInsetFraction(root);
}

void collectCovers(QQuickItem* item, QList<Cover>& out) {
    if (!item->isVisible() || item->opacity() <= 0.0) return;
    // A cover is named "videoCover" or carries videoCover: true; the property lets a
    // dialog keep its own objectName for tests (an overridden name used to drop
    // the hole, leaving five dialogs behind the picture).
    // Hover tips are Controls ToolTips: their popup item sits in the window overlay and
    // its QObject parent is the ToolTip. Every tip is a cover, so a tip over the
    // picture is no longer hidden behind it (field report 2026-10-02).
    // Only the popup's own item: its background and text are QObject children of the
    // ToolTip too, and would only add the same hole again.
    const bool toolTip = item->inherits("QQuickPopupItem") && item->parent() && item->parent()->inherits("QQuickToolTip");
    if (toolTip || item->objectName() == QLatin1String("videoCover") || item->property("videoCover").toBool()) {
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
// moves or the cineIn inset changes; with neither the window gets its rectangle back.
void syncVideoCovers(QQuickWindow* window, QQuickItem* host, qreal inset) {
    if (!window || !host || !g_video) return;
    if (g_videoProbe == VideoProbeMode::CoversOnly) inset = 0;
    QList<Cover> covers;
    collectCovers(window->contentItem()->parentItem() ? window->contentItem()->parentItem()
                                                      : window->contentItem(), covers);
    const qreal dpr = window->devicePixelRatio();
    const QPointF origin = host->mapToScene(QPointF(0, 0));
    const int w = std::max(1, int(std::floor(host->width() * dpr)));
    const int h = std::max(1, int(std::floor(host->height() * dpr)));
    static QList<QRect> last;
    // blankVideo() set the region behind this cache's back.
    if (g_videoBlanked) { last.clear(); g_videoBlanked = false; }
    QList<QRect> holes;       // x, y, w, h; the radius rides along as a fifth rect
    for (const Cover& c : covers) {
        const QRectF local = c.rect.translated(-origin);
        holes << QRect(int(std::floor(local.left() * dpr)), int(std::floor(local.top() * dpr)),
                       int(std::ceil(local.width() * dpr)) + 1, int(std::ceil(local.height() * dpr)) + 1);
        holes << QRect(0, 0, int(std::round(c.radius * dpr * 2)), 0);
    }
    // cineIn rides along as a marker entry so one cache covers both, and the last
    // entry keeps its steadiness on a scene that renders without moving either.
    const int insetBand = int(std::round(inset * h));
    {
        QList<QRect> key = holes;
        key << QRect(0, 0, insetBand, 0) << QRect(0, 0, w, h);
        if (key == last) return;
        last = key;
    }
    if (g_videoProbe == VideoProbeMode::NoRegion) {
        SetLastError(0);
        const int result = SetWindowRgn(g_video, nullptr, TRUE);
        veyra::log::info("qml-window", std::format(
            "SetWindowRgn no-region result={} error={}", result, result ? 0 : GetLastError()));
        logVideoWindowState(window, host, "region");
        return;
    }
    // Covers win: a popover over the picture shows the QML beneath it, and the
    // clip inset only ever runs without one. A cover opening mid-animation simply
    // takes the whole rectangle back for as long as it is up.
    if (holes.isEmpty() && insetBand > 0) {
        // The design's inset(12% 0 12% 0 round 8px): both bands off the top and
        // bottom, corners eased with the band so the last frame has no snap.
        const int radius = std::max(2, int(std::round(8 * dpr * (inset / 0.12))));
        HRGN band = CreateRoundRectRgn(0, insetBand, w + 1, h - insetBand + 1, radius, radius);
        SetLastError(0);
        const int result = SetWindowRgn(g_video, band, TRUE);
        const DWORD error = result ? 0 : GetLastError();
        if (!result) DeleteObject(band);
        veyra::log::info("qml-window", std::format(
            "SetWindowRgn inset={}x{} band={} result={} error={}",
            w, h, insetBand, result, error));
        logVideoWindowState(window, host, "region");
        // The picture is a native window PrintWindow cannot show, so the cineIn
        // animation is evidenced here rather than in a screenshot.
        veyra::log::info("qml", std::format("cineIn inset band={}px of {} (frac={:.3f}) radius={}",
                                            insetBand, h, inset, radius));
        return;
    }
    if (holes.isEmpty()) {
        SetLastError(0);
        const int result = SetWindowRgn(g_video, nullptr, TRUE);
        veyra::log::info("qml-window", std::format(
            "SetWindowRgn full={}x{} result={} error={}", w, h, result, result ? 0 : GetLastError()));
        logVideoWindowState(window, host, "region");
        return;
    }
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
    SetLastError(0);
    const int result = SetWindowRgn(g_video, region, TRUE); // the system owns the region on success
    const DWORD error = result ? 0 : GetLastError();
    if (!result) DeleteObject(region);
    veyra::log::info("qml-window", std::format(
        "SetWindowRgn covers={} size={}x{} result={} error={}",
        covers.size(), w, h, result, error));
    logVideoWindowState(window, host, "region");
}
} // namespace

namespace {
// Last-chance handler, as in the Win32 shell (apps/veyra/main.cpp): provider and
// driver faults are structured exceptions no C++ catch sees. The 2.0 beta had no
// handler, so a field crash left neither a reason nor the last buffered log lines.
std::wstring g_crashDumpDirectory;
LONG WINAPI crashFilter(EXCEPTION_POINTERS* info) {
    static LONG entered = 0;
    if (InterlockedIncrement(&entered) > 1) return EXCEPTION_CONTINUE_SEARCH;
    const auto code = info && info->ExceptionRecord ? info->ExceptionRecord->ExceptionCode : 0u;
    const auto address = info && info->ExceptionRecord
        ? reinterpret_cast<uintptr_t>(info->ExceptionRecord->ExceptionAddress) : uintptr_t(0);
    HMODULE module = nullptr;
    wchar_t moduleName[MAX_PATH]{};
    if (address && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                      reinterpret_cast<LPCWSTR>(address), &module) && module)
        GetModuleFileNameW(module, moduleName, MAX_PATH);
    veyra::log::error("crash", std::format("unhandled exception code=0x{:08X} address=0x{:X} module={} offset=0x{:X} thread={}",
        code, address, std::filesystem::path(moduleName).filename().string(),
        module ? address - reinterpret_cast<uintptr_t>(module) : uintptr_t(0), GetCurrentThreadId()));
    SYSTEMTIME st{};
    GetLocalTime(&st);
    wchar_t name[96]{};
    swprintf_s(name, L"veyra-crash-%04u%02u%02u-%02u%02u%02u-%lu.dmp", unsigned(st.wYear), unsigned(st.wMonth),
               unsigned(st.wDay), unsigned(st.wHour), unsigned(st.wMinute), unsigned(st.wSecond), GetCurrentProcessId());
    const std::wstring path = (std::filesystem::path(g_crashDumpDirectory) / name).wstring();
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), info, FALSE};
        const BOOL ok = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file,
                                          MiniDumpWithIndirectlyReferencedMemory, info ? &mei : nullptr, nullptr, nullptr);
        CloseHandle(file);
        veyra::log::error("crash", std::format("minidump written={} file={}", ok != FALSE,
                                               std::filesystem::path(name).string()));
    } else {
        veyra::log::error("crash", std::format("minidump open failed error={}", GetLastError()));
    }
    // What crashed and which known-risky injected components were loaded, for the next
    // start to explain (QmlPlayerBridge reads and deletes it).
    const std::wstring note = L"crash " + std::filesystem::path(moduleName).filename().wstring() + L"\n" +
                              veyra::gfx::riskyInjections() + L"\n";
    HANDLE marker = CreateFileW((std::filesystem::path(g_crashDumpDirectory) / L"veyra-last-failure.txt").c_str(),
                                GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (marker != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(marker, note.data(), DWORD(note.size() * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(marker);
    }
    veyra::Logger::instance().flush();
    return EXCEPTION_CONTINUE_SEARCH;
}
} // namespace

static int runApplication(int argc, char** argv, QString& restartProgram, QStringList& restartArgs) {
    // ExportJobManager relaunches the owning executable with an inherited mapping.
    // Dispatch before Qt or the video HWND exists, as the Win32 shell does.
    if (argc > 1 && std::strcmp(argv[1], "--export-worker") == 0) {
        if (argc != 3) return 2;
        char* end = nullptr;
        const auto value = std::strtoull(argv[2], &end, 10);
        if (!value || !end || *end != '\0') return 2;
        const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const int result = engine::runExportWorker(reinterpret_cast<HANDLE>(static_cast<uintptr_t>(value)));
        if (SUCCEEDED(com)) CoUninitialize();
        return result;
    }
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
    g_crashDumpDirectory = logPath.parent_path().wstring();
    SetUnhandledExceptionFilter(crashFilter);

    // Interface scale (设置 → 界面缩放) has to be fixed before Qt starts, so the
    // preference file is read here, from the same data directory the bridge uses.
    // "Auto" leaves Qt following the Windows display scale.
    int uiScale = 0;
    bool obsGameCapture = false;
    {
        int count = 0;
        LPWSTR* args = CommandLineToArgvW(GetCommandLineW(), &count);
        std::filesystem::path dir = veyra::runtime::localDataDirectory() / L"user-data-2.0.0";
        bool obsGameCaptureArg = false;
        for (int i = 1; args && i < count; ++i) {
            if (std::wstring(args[i]) == L"--data-dir" && i + 1 < count) dir = args[++i];
            else if (std::wstring(args[i]) == L"--obs-game-capture") obsGameCaptureArg = true;
        }
        if (args) LocalFree(args);
        QFile prefs(QString::fromStdWString((dir / L"qml-preferences.v1.json").wstring()));
        if (prefs.open(QIODevice::ReadOnly)) {
            const auto saved = QJsonDocument::fromJson(prefs.readAll()).object();
            uiScale = saved.value(QStringLiteral("uiScale")).toInt();
            obsGameCapture = saved.value(QStringLiteral("obsGameCapture")).toBool();
        }
        obsGameCapture = obsGameCapture || obsGameCaptureArg;
        if (uiScale == 100 || uiScale == 125 || uiScale == 150) {
            qputenv("QT_ENABLE_HIGHDPI_SCALING", "0");
            qputenv("QT_SCALE_FACTOR", QByteArray::number(uiScale / 100.0));
            veyra::log::info("app", std::format("interface scale fixed at {}%", uiScale));
        } else {
            uiScale = 0;
        }
    }
    // OBS 32.1.2 shares capture state across DXGI swapchains. Keeping Qt off
    // DXGI prevents its resize/destruction from freeing the video's capture
    // resources on another thread. This opt-in affects UI drawing only;
    // the native D3D12 video and enhancement pipeline are unchanged.
    if (obsGameCapture) {
        QQuickWindow::setSceneGraphBackend(QStringLiteral("software"));
        veyra::log::info("app", "OBS game capture compatibility: software UI; native D3D12 video unchanged");
    }
    QApplication app(argc, argv);
    // Qt does not automatically use the executable's Win32 icon for its windows.
    // Load the same embedded multi-size resource for the taskbar and Alt+Tab.
    QIcon appIcon;
    for (const int size : {16, 20, 24, 32, 40, 48, 64, 96, 128, 256}) {
        const auto handle = static_cast<HICON>(LoadImageW(GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(IDI_VEYRA), IMAGE_ICON, size, size, 0));
        if (!handle) {
            veyra::log::error("app-icon", std::format("LoadImageW size={} error={}", size, GetLastError()));
            continue;
        }
        const auto image = QImage::fromHICON(handle);
        DestroyIcon(handle);
        if (!image.isNull()) appIcon.addPixmap(QPixmap::fromImage(image));
    }
    app.setWindowIcon(appIcon);
    veyra::log::info("app-icon", std::format("embedded window icon sizes={}", appIcon.availableSizes().size()));
    // The interface draws with Direct3D 12, like the video. Qt's default is Direct3D 11,
    // and a D3D11 device in the process changed how overlays treat it: RivaTuner (MSI
    // Afterburner) then crashed in d3d11.dll inside its own hooks while drawing its OSD on
    // the XeSS swapchain through D3D11On12 (field dumps 2026-10-01). 1.4.4 drew its UI with
    // GDI and had no D3D11 device; the overlays coexisted. VEYRA_UI_RHI=d3d11 restores
    // Qt's default; without a usable D3D12 device Qt keeps it too.
    if (!obsGameCapture) {
        wchar_t forced[16]{};
        GetEnvironmentVariableW(L"VEYRA_UI_RHI", forced, 16);
        const bool wantD3d11 = _wcsicmp(forced, L"d3d11") == 0;
        const bool d3d12Usable = SUCCEEDED(D3D12CreateDevice(nullptr, D3D_FEATURE_LEVEL_11_0, __uuidof(ID3D12Device), nullptr));
        if (!wantD3d11 && d3d12Usable) QQuickWindow::setGraphicsApi(QSGRendererInterface::Direct3D12);
        veyra::log::info("app", std::format("interface renderer={} (d3d12 usable={} forced={})",
            !wantD3d11 && d3d12Usable ? "d3d12" : "d3d11", d3d12Usable, QString::fromWCharArray(forced).toStdString()));
    }
    app.setProperty("veyraLogFile", QString::fromStdWString(logPath.wstring()));
    app.setProperty("veyraUiScale", uiScale);
    app.setProperty("veyraObsGameCapture", obsGameCapture);
    // Frameless windows have no menu, yet DefWindowProc turns a bare Alt tap into
    // SC_KEYMENU and enters modal menu tracking: the next mouse click is eaten and
    // input stalls for about two seconds. Qt has already handled the key itself
    // (Alt+Enter etc. are unaffected), so the system menu loop is refused.
    struct NoKeyMenuFilter final : QAbstractNativeEventFilter {
        bool nativeEventFilter(const QByteArray& type, void* message, qintptr* result) override {
            if (type != "windows_generic_MSG") return false;
            const auto* msg = static_cast<const MSG*>(message);
            if (msg->message != WM_SYSCOMMAND || (msg->wParam & 0xFFF0) != SC_KEYMENU) return false;
            veyra::log::info("ui-window", std::format("system key menu refused (lParam=0x{:X})", unsigned(msg->lParam)));
            if (result) *result = 0;
            return true;
        }
    };
    static NoKeyMenuFilter noKeyMenu;
    app.installNativeEventFilter(&noKeyMenu);
    app.setApplicationName(QStringLiteral("Veyra"));
    app.setOrganizationName(QStringLiteral("Veyra"));
    veyra::log::info("app", "Veyra QML session started");

    engine::EngineController controller;

    // The window exists before QML loads so the engine can be handed a valid
    // HWND as soon as the user opens something.
    g_video = createVideoWindow();

    // Keep 2.0.0 settings separate from 1.4.4 so the old executable remains a
    // working rollback. An explicit data directory is for isolated runs and is
    // never populated from the user's old settings.
    std::filesystem::path dataDirectory = veyra::runtime::localDataDirectory() / L"user-data-2.0.0";
    std::filesystem::path legacyDirectory = veyra::runtime::localDataDirectory();
    bool explicitDataDirectory = false;
    {
        const QStringList early = QCoreApplication::arguments();
        const auto at = early.indexOf(QStringLiteral("--data-dir"));
        if (at > 0 && at + 1 < early.size()) {
            dataDirectory = std::filesystem::path(early.at(at + 1).toStdWString());
            explicitDataDirectory = true;
            veyra::log::info("app", "test data directory " + early.at(at + 1).toStdString());
        }
        const auto importAt = early.indexOf(QStringLiteral("--import-1.4.4-data"));
        if (importAt > 0 && importAt + 1 < early.size())
            legacyDirectory = std::filesystem::path(early.at(importAt + 1).toStdWString());
    }
    if (!explicitDataDirectory) {
        const auto migration = ui::prepareQmlDataDirectory(legacyDirectory, dataDirectory);
        if (!migration.ok) {
            veyra::log::error("app", "2.0.0 data migration failed: " + migration.error);
            return 2;
        }
        veyra::log::info("app", std::format("2.0.0 data directory ready; copied {} legacy files",
                                             migration.copied));
    }
    ui::QmlPlayerBridge bridge(controller, dataDirectory);
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
    auto* thumbnails = new ui::ThumbnailProvider;
    engine.addImageProvider(QStringLiteral("veyra-thumb"), thumbnails);
    QObject::connect(&bridge, &ui::QmlPlayerBridge::thumbnailSourceChanged, &app,
                     [thumbnails](const QString& path) { thumbnails->setSource(path); });
    engine.addImportPath(QCoreApplication::applicationDirPath() + "/qml");
    engine.rootContext()->setContextProperty(QStringLiteral("veyra"), &bridge);
    bridge.setQmlEngine(&engine);   // Qt.uiLanguage drives the per-language font (Theme.qml)

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
    //   --data-dir <path>       presets and session history from that folder (read before the bridge exists)
    //   --import-1.4.4-data <path>  read-only legacy settings source for first 2.0.0 launch
    //   --motion-probe <name>   dock | page | switch | seg | menu: start that motion, log its value per frame
    //   --exit-after <ms>       quit the app by itself after N ms, so a test run ends
    //                           through the engine's own teardown instead of a kill
    //   --video-probe <normal|hidden|no-region|covers-only>  native-window diagnosis
    const QStringList args = QCoreApplication::arguments();
    QString openPath;
    QVariantMap testOptions;
    QSize testSize;
    // --exit-after: see the switch table. 0 means "run until closed".
    int exitAfterMs = 0;
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
        } else if (a == QLatin1String("--data-dir") && hasValue) {
            ++i;  // handled before the bridge was created
        } else if (a == QLatin1String("--import-1.4.4-data") && hasValue) {
            ++i;  // handled before the bridge was created
        } else if (a == QLatin1String("--dock-pinned")) {
            testOptions.insert(QStringLiteral("dockPinned"), true);
        } else if (a == QLatin1String("--size") && hasValue) {
            const QStringList wh = args.at(++i).split(QLatin1Char('x'));
            if (wh.size() == 2) testSize = QSize(wh.at(0).toInt(), wh.at(1).toInt());
        } else if (a == QLatin1String("--motion-probe") && hasValue) {
            testOptions.insert(QStringLiteral("motionProbe"), args.at(++i));
        } else if (a == QLatin1String("--exit-after") && hasValue) {
            exitAfterMs = args.at(++i).toInt();
        } else if (a == QLatin1String("--video-probe") && hasValue) {
            const QString mode = args.at(++i);
            if (mode == QLatin1String("hidden")) g_videoProbe = VideoProbeMode::Hidden;
            else if (mode == QLatin1String("no-region")) g_videoProbe = VideoProbeMode::NoRegion;
            else if (mode == QLatin1String("covers-only")) g_videoProbe = VideoProbeMode::CoversOnly;
            else if (mode != QLatin1String("normal")) {
                veyra::log::error("qml-window", "invalid video probe mode " + mode.toStdString());
                return 2;
            }
            g_videoHiddenByProbe = g_videoProbe == VideoProbeMode::Hidden;
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
    veyra::log::info("qml-window", std::format("probe mode={}", videoProbeName()));
    engine.rootContext()->setContextProperty(QStringLiteral("vyTest"), testOptions);
    if (!openPath.isEmpty()) {
        QTimer::singleShot(600, &bridge, [&bridge, openPath] { bridge.openPath(openPath); });
    } else {
        QTimer::singleShot(600, &bridge, [&bridge] { bridge.autoResumeLastSource(); });
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

    // Fullscreen must own the whole monitor. Without telling the shell, the taskbar
    // stayed above the fullscreen picture on some systems (field report with the
    // control pill showing behind it): the shell decides "fullscreen application"
    // from its own heuristics, and the owned control window defeats them.
    // ITaskbarList2::MarkFullscreenWindow states it explicitly.
    QObject::connect(window, &QWindow::visibilityChanged, &app, [window](QWindow::Visibility visibility) {
        const bool full = visibility == QWindow::FullScreen;
        ITaskbarList2* taskbar = nullptr;
        const HRESULT created = CoCreateInstance(CLSID_TaskbarList, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&taskbar));
        HRESULT marked = created;
        if (SUCCEEDED(created) && taskbar) {
            if (SUCCEEDED(taskbar->HrInit()))
                marked = taskbar->MarkFullscreenWindow(reinterpret_cast<HWND>(window->winId()), full ? TRUE : FALSE);
            taskbar->Release();
        }
        veyra::log::info("qml-window", std::format("taskbar fullscreen mark={} hr=0x{:X}", full, unsigned(marked)));
    });

    // Clicking the taskbar button minimises a window only when it has a minimise box;
    // Qt's frameless window is a bare WS_POPUP, so the click did nothing (field report).
    // The two bits add no frame or caption. Qt may rebuild the style on a fullscreen
    // switch, so it is put back on every visibility change.
    const auto allowTaskbarMinimise = [window] {
        const HWND hwnd = reinterpret_cast<HWND>(window->winId());
        const LONG_PTR style = GetWindowLongPtrW(hwnd, GWL_STYLE);
        const LONG_PTR wanted = style | WS_MINIMIZEBOX | WS_SYSMENU;
        if (wanted == style) return;
        SetWindowLongPtrW(hwnd, GWL_STYLE, wanted);
        veyra::log::info("qml-window", std::format("taskbar minimise enabled style=0x{:X}", static_cast<unsigned long long>(wanted)));
    };
    allowTaskbarMinimise();
    QObject::connect(window, &QWindow::visibilityChanged, &app, [allowTaskbarMinimise](QWindow::Visibility) { allowTaskbarMinimise(); });

    // Closing the window must run the engine's own teardown, not just end the loop.
    // A test harness that hard-kills the process (Stop-Process -Force) loses every
    // log line still in the file sink's 64 KB buffer - including the ones a test is
    // looking for, because the flush condition (250 ms since the last write) is
    // never reached when the process dies first. Qt's default for a window with no
    // explicit handler is to close it and exit on the last window, which does run
    // teardown; this makes it explicit so a harness can also ask for a clean exit
    // with --exit-after and get the same, complete log either way.
    QObject::connect(window, &QQuickWindow::closing, &app, [](QQuickCloseEvent*) {
        veyra::log::info("qml", "closing: quitting the application loop");
        // Flush before quitting: the file sink is a 64 KB _IOFBF buffer, so a run
        // that ends without this loses its last lines (the exact ones a headless
        // check is looking for). closeFile() at teardown would also flush, but a
        // quit path is not guaranteed to reach it.
        veyra::Logger::instance().flush();
        QCoreApplication::quit();
    }, Qt::DirectConnection);
    if (exitAfterMs > 0) {
        // Self-terminating test run: the same teardown path as a user closing the
        // window, so the log a harness reads is complete either way.
        QTimer::singleShot(exitAfterMs, &app, [window] {
            veyra::log::info("qml", "exit-after elapsed: closing the window");
            window->close();
        });
    }

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
    auto follow = [window, &bridge] {
        if (!g_video) return;
        // Only the pre-open hook attaches the popup and converts it to WS_CHILD.
        // GetParent() is null for an unattached WS_POPUP, so reparenting here on
        // every animation frame can starve input and timers before any source opens.
        if (!bridge.hasSource()) {
            // The window stays up for the presenter, but empty: the QML stage and its
            // "正在连接" pill show instead of the last source's frozen frame.
            if (bridge.openingSource()) { blankVideo("opening"); return; }
            if (IsWindowVisible(g_video)) {
                veyra::log::info("qml-window", "follow: hide video because source snapshot is inactive");
                ShowWindow(g_video, SW_HIDE);
            }
            return;
        }
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
        if (syncVideoGeometry(window, host))
            syncVideoCovers(window, host, sceneInsetFraction(window));
    };
    // Hold-to-compare (V by default) is the bridge's application event filter:
    // the key is rebindable (设置 → 快捷键) and can be switched off (显示 →
    // 按住 V 查看原画). A second hard-wired V filter here used to override both.
    QObject::connect(window, &QQuickWindow::afterAnimating, &app, follow);
    QObject::connect(&bridge, &ui::QmlPlayerBridge::snapshotChanged, &app, follow);
    // The first frame may come before the page layout settles; one pass after the
    // event loop starts covers a scene that then never animates.
    QTimer::singleShot(0, &app, follow);
    // How often the Qt windows present. OBS game capture locks onto one swapchain and only
    // moves to another after 16 presents in a row from it, so a UI that keeps presenting
    // during playback keeps OBS on the UI instead of the video (field report 2026-10-01).
    {
        static std::atomic<int> mainFrames{0}, barFrames{0};
        QObject::connect(window, &QQuickWindow::frameSwapped, &app, [] { ++mainFrames; }, Qt::DirectConnection);
        if (auto* bar = window->findChild<QQuickWindow*>(QStringLiteral("fullscreenBar")))
            QObject::connect(bar, &QQuickWindow::frameSwapped, &app, [] { ++barFrames; }, Qt::DirectConnection);
        auto* frameLog = new QTimer(&app);
        QObject::connect(frameLog, &QTimer::timeout, &app, [] {
            const int m = mainFrames.exchange(0), b = barFrames.exchange(0);
            if (m || b) veyra::log::info("qml-frames", std::format("ui presents per 10 s: main={} controlBar={}", m, b));
        });
        frameLog->start(10000);
    }
    // Test only: VEYRA_TEST_DPI_FLIP=<ms> sends the window the WM_DPICHANGED a move to a
    // 150 % screen would, then back to 100 % three seconds later, so the screen-change
    // crash (field report 2026-10-01, two monitors) can be reproduced on one monitor.
    if (const auto flip = qEnvironmentVariableIntValue("VEYRA_TEST_DPI_FLIP"); flip > 0) {
        const auto send = [window](UINT dpi) {
            const HWND hwnd = reinterpret_cast<HWND>(window->winId());
            RECT r{};
            GetWindowRect(hwnd, &r);
            veyra::log::info("dpi-test", std::format("WM_DPICHANGED dpi={}", dpi));
            SendMessageW(hwnd, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), reinterpret_cast<LPARAM>(&r));
        };
        QTimer::singleShot(flip, &app, [send] { send(144); });
        QTimer::singleShot(flip + 3000, &app, [send] { send(96); });
    }

    // Before every open, place the native window and force the pending layout to
    // be applied so the client size the presenter reads is the real one.
    bridge.setPreOpenHook([window, &bridge] {
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
        const bool validHost = host && syncVideoGeometry(window, host);
        if (!validHost) {
            veyra::log::error("qml-window", "pre-open: video host has no valid client rectangle");
            return;
        }
        const BOOL wasVisible = IsWindowVisible(g_video);
        ShowWindow(g_video, SW_SHOWNA);
        veyra::log::info("qml-window", std::format(
            "pre-open: show video wasVisible={} nowVisible={} sourceSnapshot={}",
            wasVisible != 0, IsWindowVisible(g_video) != 0, bridge.hasSource()));
        if (g_videoHiddenByProbe) ShowWindow(g_video, SW_HIDE);
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
    if (rc == 42) {
        restartProgram = QCoreApplication::applicationFilePath();
        // Preserve the selected profile, not transient capture/open/test flags.
        const auto args = QCoreApplication::arguments();
        for (int i = 1; i + 1 < args.size(); ++i) {
            if (args[i] == QStringLiteral("--data-dir")) {
                restartArgs << args[i] << args[i + 1];
                ++i;
            }
        }
        restartArgs << QStringLiteral("--page") << QStringLiteral("set");
    }
    if (g_video) { DestroyWindow(g_video); g_video = nullptr; }
    return rc;
}

int main(int argc, char** argv) {
    QString program;
    QStringList arguments;
    const int result = runApplication(argc, argv, program, arguments);
    if (result != 42) return result;
    // Player, QML engine and QApplication have all been destroyed.
    if (QProcess::startDetached(program, arguments)) return 0;
    MessageBoxW(nullptr, L"设置已保存，但自动重启失败。请重新打开软件。\nSettings saved. Automatic restart failed; please reopen Veyra.",
                L"Veyra", MB_OK | MB_ICONERROR);
    return 1;
}
