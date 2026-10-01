// The application shell, rebuilt from the approved design.
//
// Structure comes from the prototype (index.html + app.css + board.js):
//   * a frameless window with the small 8px radius;
//   * a hidden top dock that drops in when the pointer reaches the top edge;
//   * one page visible at a time: home / min / pro / node / exp / set;
//   * in cinema mode the WINDOW ITSELF snaps to the film's aspect ratio so the
//     picture has no letterbox bars. The prototype's own technical note says the
//     window must resize to the film when a file opens (capture cards and games
//     are 16:9). The separate control window straddles its lower edge.
//
// The video is not in this scene graph. It stays the native D3D12 child window
// that apps/veyra-qml/main.cpp places over the item named "videoHost"; Qt hosts
// it and never composites it, which is what preserved present cost in S3.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: root
    width: 1280
    height: 800
    // 极简 follows the film, and a portrait film needs a narrow window (the dock is
    // about 330px wide).
    minimumWidth: cinema ? 360 : 720
    minimumHeight: 260
    visible: true
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "Veyra " + veyra.version

    // Page ids match the prototype's PAGES keys so a design screenshot and an
    // app screenshot can be compared under the same name.
    property string page: "home"
    // The node page splitter: height of its video card (picture + 40px bar).
    property real nodeVideoHeight: 330
    // Every navigation goes through here: "专业" means the node page while the
    // node configuration is active, so the dock never drops into the list UI.
    function goPageTarget(p) { return p === "pro" && veyra.nodeMode === 1 ? "node" : p }
    function goPage(p) { page = goPageTarget(p) }
    // Test switches from the command line (main.cpp, G0.3); empty in normal use.
    readonly property var test: typeof vyTest !== "undefined" ? vyTest : ({})
    readonly property real testAspect: test.aspect !== undefined ? test.aspect : 0
    readonly property bool cinema: page === "min"
    // Maximized behaves like fullscreen for geometry: the screen owns the size,
    // so the cinema aspect rule must not fight the window manager.
    readonly property bool maximized: visibility === Window.Maximized
    readonly property bool screenSized: fullTarget || maximized
    function toggleMaximized() {
        if (fullscreen) return
        visibility = maximized ? Window.Windowed : Window.Maximized
        veyra.logUi("ui-window", "maximized=" + (visibility === Window.Maximized))
    }
    Binding { target: Theme; property: "reduced"; value: veyra.reducedMotion || root.test.reducedMotion === true }
    Binding { target: Theme; property: "accentName"; value: veyra.preferences.accent || "orange" }
    Binding { target: Theme; property: "backdropLevel"; value: veyra.preferences.backdrop !== undefined ? veyra.preferences.backdrop : 1 }

    // Cinema geometry: the main window ends at the picture. The independent pill
    // can extend below it without leaving an opaque strip in this window.
    // 16:9 until a source reports its own shape: a PS5 / capture session has no
    // aspect while it connects, and 2.39 then left a thin black strip.
    property real filmAspect: 16 / 9
    readonly property real reportedAspect: veyra.previewAspect
    onReportedAspectChanged: if (reportedAspect < 0.2) fitToFilm(16 / 9)
    property string lastPage: "home"
    property real heightBeforeCinema: 0
    // The tallest picture that still fits on the screen with the pill (46px) under
    // it. A portrait video or a captured portrait window used to make the window
    // taller than the screen (field report 2026-10-01).
    readonly property real maxPictureHeight: {
        const s = root.screen
        return s ? Math.max(200, Math.min(s.height, s.desktopAvailableHeight) - 46 - 16) : 100000
    }
    readonly property real pictureHeight: Math.min(Math.round(width / filmAspect), maxPictureHeight)
    // The width a tall film took away, given back when a wider film fits again.
    property real widthBeforeNarrow: 0

    function fitToFilm(aspect) {
        if (aspect > 0.2 && aspect < 5.0) filmAspect = aspect
        // The page, not `cinema`: onPageChanged calls this before that binding updates.
        if (page !== "min" || screenSized) return
        // Narrow the window for a film too tall for the screen, keeping its centre.
        let w = widthBeforeNarrow > 0 ? widthBeforeNarrow : width
        if (Math.round(w / filmAspect) > maxPictureHeight)
            w = Math.max(minimumWidth, Math.round(maxPictureHeight * filmAspect))
        if (Math.abs(w - width) >= 1) {
            if (widthBeforeNarrow <= 0) widthBeforeNarrow = width
            x += Math.round((width - w) / 2)
            width = w
        }
        if (widthBeforeNarrow > 0 && w >= widthBeforeNarrow) widthBeforeNarrow = 0
        height = Math.round(pictureHeight)
        keepPillOnScreen()
    }
    // The pill hangs 46px below the picture: lift the window if that leaves the screen.
    function keepPillOnScreen() {
        const s = root.screen
        if (!s) return
        const bottom = s.virtualY + Math.min(s.height, s.desktopAvailableHeight)
        if (y + pictureHeight + 46 + 8 > bottom) y = Math.max(s.virtualY, bottom - pictureHeight - 46 - 8)
    }
    // .vy.cine transition: height .7s var(--spring-soft)
    Behavior on height {
        enabled: root.cinema && !root.screenSized
        NumberAnimation { duration: Theme.d(700); easing.bezierCurve: Theme.springSoft }
    }
    onWidthChanged: {
        if (cinema && !screenSized) height = Math.round(pictureHeight)
        videoHost.syncRect()
    }
    // The window height animates towards the film's aspect, so the picture area has
    // to follow every step of that animation. Without this the host kept its
    // previous height and the picture sat inside black bars - exactly what cinema
    // mode exists to avoid.
    onHeightChanged: videoHost.syncRect()
    onMaximizedChanged: {
        if (!maximized && cinema && !fullTarget) height = Math.round(pictureHeight)
        videoHost.syncRect()
    }
    onPageChanged: {
        if (page === "node" && veyra.nodeMode !== 1) {
            veyra.nodeMode = 1
            if (veyra.nodeMode !== 1) { page = "pro"; return }
        }
        // Tell the bridge where the user is, so a command like "open a file" can
        // behave differently from home than from the professional page.
        if (veyra.currentPage !== page) veyra.currentPage = page
        // Test the page itself: the cinema binding may not have caught up with
        // this change yet, and a stale value shrank the page after min instead.
        // Leaving 极简 gives the window back the height it had before (core.js
        // clears the cinema height on any other page).
        const wasCinema = lastPage === "min"
        lastPage = page
        if (!screenSized && wasCinema && page !== "min" && widthBeforeNarrow > 0) {
            // A tall film narrowed the window; the other pages get their width back.
            x -= Math.round((widthBeforeNarrow - width) / 2)
            width = widthBeforeNarrow
            widthBeforeNarrow = 0
        }
        if (!screenSized && page !== "min" && width < 720) { x -= Math.round((720 - width) / 2); width = 720 }
        if (screenSized) { /* the screen is the window */ }
        else if (page === "min") { if (!wasCinema) heightBeforeCinema = height; fitToFilm(filmAspect) }
        else if (wasCinema && heightBeforeCinema >= 400) height = heightBeforeCinema
        else if (height < 600) height = 800
        // core.js app.go: the old page sinks (.22s) and the new one shows 150 ms
        // later with its [data-in] items rising. Reduced motion shows it at once.
        const prev = shownPage
        if (prev === page) return
        leavingPage = Theme.reduced || prev === "" ? "" : prev
        // Leaving a page without a picture (home, settings): an open may follow in
        // the same call (openPath navigates, then opens) and the engine samples the
        // host size once, so the rect is placed now, not 150 ms later. There is no
        // picture on the old page to fade against.
        if (prev === "home" || prev === "set") videoHost.syncRect()
        if (leavingPage !== "") { leaveAnim.restart(); showTimer.restart() }
        else showPage(false)
    }
    // The page on screen (entering or settled) and the one sinking out.
    property string shownPage: "home"
    property string leavingPage: ""
    function showPage(animated) {
        shownPage = page
        if (animated)
            for (const p of pages.children) if (p.pageId === page) p.enter()
        // The video rect follows the page, and it must be settled before the
        // engine opens anything: it samples the window's client size once. It moves
        // here, once, not at the click: the native window cannot fade with the pages.
        videoHost.syncRect()
    }
    Timer { id: showTimer; interval: 150; onTriggered: root.showPage(true) }
    // @keyframes sink { to { opacity: 0; transform: scale(.985) } }, .22s --out.
    property real leaveT: 0
    NumberAnimation {
        id: leaveAnim
        target: root; property: "leaveT"; from: 0; to: 1
        duration: 220; easing.bezierCurve: Theme.easeOut
        onFinished: root.leavingPage = ""
    }

    // Background: pure black in cinema mode (the picture is the window), the
    // three-stop gradient otherwise - the prototype's .vy / .vy.cine split.
    VBackdrop {
        id: backdrop
        anchors.fill: parent
        visible: !root.cinema
        strokeColor: root.cinema ? "transparent" : Theme.stroke
    }

    // The one host item for the native video window; main.cpp finds it by name.
    // Exactly one must exist in the tree: an earlier version had a copy in each
    // page and the engine bound its swapchain to a hidden, zero-sized one.
    Item {
        id: videoHost
        objectName: "videoHost"
        visible: false          // geometry proxy; the native window draws pixels
        function syncRect() {
            const r = root.videoRect()
            x = r.x; y = r.y; width = r.width; height = r.height
        }
    }

    // Where the picture sits, per page, computed from the design's own layout
    // numbers rather than read from a laid-out child. A child reports 0x0 on the
    // frame the user opens a file, and that once produced a 1x1 swapchain.
    function videoRect() {
        // Fullscreen is a video-only viewport (AppShell): the picture is the screen
        // and the controls float above it in their own window (FullscreenBar).
        if ((fullscreen || (maximized && page === "min")) && page !== "home" && page !== "set")
            return { x: 0, y: 0, width: width, height: height }
        switch (page) {
        case "min":
            // .min .stage: full width, height = the picture height.
            return { x: 0, y: 0, width: width, height: Math.round(pictureHeight) }
        case "pro":
            // .pro grid: 1fr 376px, gap 10, padding 14. The video wrap is column
            // 1 row 2; .meters below it is a fixed 172px; .pro-head is 32; and the
            // card's own .vbar is 36px at its foot, which the picture must not cover.
            return { x: 14,
                     y: 14 + 32 + 10,
                     width: Math.max(1, width - 376 - 14 * 2 - 10 - 14),
                     height: Math.max(1, height - 14 * 2 - 32 - 10 - 172 - 10 - 36) }
        case "node":
            // .nodeview padding 14; .nv-top (330px by default, the splitter under
            // it resizes it) minus its 40px bar.
            return { x: 14, y: 14, width: Math.max(1, width - 28), height: root.nodeVideoHeight - 40 }
        case "exp":
            // .exp grid: 250px 1fr 330px; the video is column 2 row 2.
            return { x: 14 + 250 + 10,
                     y: 14 + 32 + 10,
                     width: Math.max(1, width - 250 - 330 - 14 * 2 - 20),
                     height: Math.max(1, height - 14 * 2 - 32 - 10 - 120 - 10) }
        default:
            return { x: 0, y: 0, width: 0, height: 0 }
        }
    }

    // Pages live for the whole session so switching never loses a half-edited
    // chain; only the visible page owns the video window.
    Item {
        id: pages
        anchors.fill: parent
        // In fullscreen the picture covers the page, and the native video window
        // passes the pointer through: a hidden page control must not take a click
        // meant for the picture.
        visible: !root.fullscreen || root.page === "home" || root.page === "set" || root.quickPanelShown
        // Each page is shown while current or while sinking out; the sinking one
        // takes no input (.page.leave { pointer-events: none }).
        HomePage { pageId: "home"; onRequestPage: p => root.goPage(p) }
        MinimalPage {
            id: minPage
            pageId: "min"
            onRequestPage: p => root.goPage(p)
            onRequestAspect: aspect => { if (root.testAspect <= 0) root.fitToFilm(aspect) }
            onRequestFullscreen: root.toggleFullscreen()
        }
        ProPage {
            id: proPage
            pageId: "pro"
            overlay: root.quickPanelShown
            onRequestFullscreen: root.toggleFullscreen()
            onRequestPage: p => root.goPage(p)
            onRequestDialog: key => dialogs.open(key)
        }
        NodePage {
            pageId: "node"
            videoHeight: root.nodeVideoHeight
            onVideoHeightEdited: h => { root.nodeVideoHeight = h; videoHost.syncRect() }
            onRequestFullscreen: root.toggleFullscreen()
            onRequestPage: p => root.goPage(p)
            onRequestDialog: key => dialogs.open(key)
            onRequestInspector: (index, type) => {
                proPage.selectedEffect = index
                veyra.selectedNrLayer = index
                veyra.selectedColourLayer = index
                proPage.tab = type === "color" ? "color" : "quality"
                root.page = "pro"
            }
        }
        ExportPage { pageId: "exp"; onRequestPage: p => root.goPage(p) }
        SettingsPage { pageId: "set"; onRequestPage: p => root.goPage(p) }
    }

    // The five dialogs sit above the pages and below the dock's own tooltips.
    DialogHost {
        id: dialogs
        anchors.fill: parent
        // The dialogs open the source through the bridge; show the picture.
        onStartCapture: if (root.page === "home") root.goPage("min")
        onStartPs5: if (root.page === "home") root.goPage("min")
        onStartMoonlight: if (root.page === "home") root.goPage("min")
        onStartXbox: if (root.page === "home") root.goPage("min")
        onStartScreen: if (root.page === "home") root.goPage("min")
    }

    // The dock floats above every page and retracts on its own.
    TopDock {
        id: dock
        anchors.horizontalCenter: parent.horizontalCenter
        // The node page is the professional mode's node view.
        currentPage: root.page === "node" ? "pro" : root.page
        maximized: root.maximized
        pinned: root.test.dockPinned === true || veyra.preferences.dockPinned === true
        locked: root.fullLocked
        forcedTip: root.test.tip !== undefined ? root.test.tip : ""
        opened: pinned
        // In fullscreen only the picture is on screen, so another page (专业 above all)
        // would change nothing visible: leave fullscreen first, as the pill's menu does.
        onRequestPage: p => {
            if (root.fullscreen && root.goPageTarget(p) !== root.page) root.toggleFullscreen()
            root.goPage(p)
        }
        onRequestMinimize: root.showMinimized()
        onRequestMaximize: root.toggleMaximized()
        onRequestClose: root.close()
        onRequestMove: if (!root.fullscreen) root.startSystemMove()
    }

    // Frameless window: the system resizes from 6px borders and 12px corners.
    Repeater {
        model: [
            { e: Qt.LeftEdge, c: Qt.SizeHorCursor }, { e: Qt.RightEdge, c: Qt.SizeHorCursor },
            { e: Qt.TopEdge, c: Qt.SizeVerCursor }, { e: Qt.BottomEdge, c: Qt.SizeVerCursor },
            { e: Qt.LeftEdge | Qt.TopEdge, c: Qt.SizeFDiagCursor }, { e: Qt.RightEdge | Qt.BottomEdge, c: Qt.SizeFDiagCursor },
            { e: Qt.RightEdge | Qt.TopEdge, c: Qt.SizeBDiagCursor }, { e: Qt.LeftEdge | Qt.BottomEdge, c: Qt.SizeBDiagCursor }
        ]
        delegate: MouseArea {
            required property var modelData
            readonly property bool l: (modelData.e & Qt.LeftEdge) !== 0
            readonly property bool r: (modelData.e & Qt.RightEdge) !== 0
            readonly property bool t: (modelData.e & Qt.TopEdge) !== 0
            readonly property bool b: (modelData.e & Qt.BottomEdge) !== 0
            readonly property bool corner: (l || r) && (t || b)
            objectName: "window-resize-" + modelData.e
            // Above the dialog layer (z 100): an open dialog must not stop the
            // window from being resized.
            z: 130
            enabled: !root.fullscreen && !root.maximized
            visible: enabled
            width: corner ? 12 : (l || r) ? 6 : root.width - 24
            height: corner ? 12 : (t || b) ? 6 : root.height - 24
            x: l ? 0 : r ? root.width - width : 12
            y: t ? 0 : b ? root.height - height : 12
            cursorShape: modelData.c
            onPressed: root.startSystemResize(modelData.e)
        }
    }

    // Drag and drop opens the file (AppShell WM_DROPFILES).
    DropArea {
        anchors.fill: parent
        onDropped: drop => {
            if (!drop.hasUrls || drop.urls.length === 0) return
            drop.accept(Qt.CopyAction)
            veyra.openUrl(drop.urls[0])
        }
    }

    // Keyboard, aligned with AppShell's message loop. Text fields keep their own
    // keys (Qt's shortcut override), so typing in a field never seeks. V (hold for
    // the original picture) needs its release as well; main.cpp handles it.
    readonly property bool fullscreen: visibility === Window.FullScreen
    // Set before the visibility changes: Windows resizes the window first, and the
    // cinema rule would otherwise snap that fullscreen size back to the film.
    property bool fullTarget: false
    property bool fullLocked: false
    // Fullscreen quick-adjust panel (Home): the list mode's inspector on the left
    // of the picture, with the performance orbs. List mode only.
    property bool quickPanel: false
    readonly property bool quickPanelShown: fullscreen && quickPanel && page === "pro" && !fullLocked
    onQuickPanelShownChanged: veyra.logUi("ui-fullscreen", "quick panel shown=" + quickPanelShown)
    onFullscreenChanged: {
        veyra.setPresentationFullscreen(fullscreen)
        fullTarget = fullscreen
        if (!fullscreen) {
            fullLocked = false
            quickPanel = false
            if (cinema && !maximized) height = Math.round(pictureHeight)
        }
        fullControls = true
        fullHide.restart()
        videoHost.syncRect()
    }
    function toggleFullscreen() {
        fullLocked = false
        fullTarget = !fullscreen
        visibility = fullTarget ? Window.FullScreen : Window.Windowed
        veyra.logUi("ui-fullscreen", "enabled=" + fullscreen)
    }

    // G2.5 fullscreen controls. AppShell: any pointer movement shows the transport,
    // and 1.6 s without movement hides it (and the cursor) unless the pointer rests
    // on the bar or a menu is open; Ctrl+L locks it away. The bar is its own
    // top-level window (FullscreenBar.qml), because the native video window draws
    // above everything in this one.
    property bool fullControls: true
    function pointerActivity() {
        if (!fullscreen || fullLocked || root.test.fullBar === "hidden") return
        if (root.test.fullDebug === true) veyra.logUi("ui-fullscreen-debug", "pointer activity")
        if (!fullControls) { fullControls = true; veyra.logUi("ui-fullscreen", "controls shown by pointer") }
        fullHide.restart()
    }
    Timer {
        id: fullHide
        interval: 1600
        onTriggered: {
            if (!root.fullscreen || !root.fullControls) return
            if (fullBar.hovered || fullBar.menuOpen || root.test.fullBar === "shown") { restart(); return }
            root.fullControls = false
            veyra.logUi("ui-fullscreen", "controls hidden; video and subtitles only")
        }
    }
    // Every pointer move over this window (a HoverHandler sees them all, whatever
    // item is under the pointer; the video window passes them through). The cursor
    // hides with the controls, as in AppShell.
    HoverHandler {
        id: pointerWatch
        cursorShape: root.fullscreen && (!root.fullControls || root.fullLocked) && !root.quickPanelShown ? Qt.BlankCursor : Qt.ArrowCursor
        // Qt re-sends hover while anything animates under a still pointer; only a
        // real change of position counts as movement.
        property point last: Qt.point(-1, -1)
        onPointChanged: {
            const p = point.scenePosition
            if (Math.abs(p.x - last.x) < 1 && Math.abs(p.y - last.y) < 1) return
            last = p
            root.pointerActivity()
            // Fullscreen: the top edge brings the dock down (极简 / 专业 switch in
            // place). Its own 12px hot zone sits under the native picture, where
            // hover is not always delivered, so the window-wide handler helps it.
            if (root.fullscreen && !root.fullLocked) {
                if (p.y <= 14) dock.open()
                else if (p.y > 90 && dock.opened && !dock.pinned) dock.opened = false
            }
        }
    }
    // AppShell VideoSurface: a double click on the picture toggles fullscreen
    // (1.4.4 did both directions), unless locked. Windowed, only a double click
    // inside the picture counts; with the quick panel up, not one on the panel.
    TapHandler {
        enabled: !root.fullLocked
        onDoubleTapped: (eventPoint, button) => {
            const p = eventPoint.position
            if (root.fullscreen) {
                if (root.quickPanelShown && proPage.overlayContains(p.x, p.y)) return
                root.toggleFullscreen()
                return
            }
            if (root.page !== "min" && root.page !== "pro" && root.page !== "node") return
            if (dialogs.dialog !== "" || !veyra.hasSource) return
            if (veyra.protectionDrawShape.length > 0 || veyra.compareMode === 2) return
            if (p.x < videoHost.x || p.y < videoHost.y || p.x > videoHost.x + videoHost.width
                || p.y > videoHost.y + videoHost.height) return
            root.toggleFullscreen()
        }
    }
    // A click on the picture of a PC stream takes the keyboard and mouse back after a release
    // (Ctrl+Alt+Shift+Z). While captured the clicks belong to the host and never get here.
    TapHandler {
        enabled: veyra.moonlight && veyra.moonlight.state.streaming === true && !veyra.moonlightCaptured && dialogs.dialog === ""
        onTapped: eventPoint => {
            const p = eventPoint.position
            if (root.page !== "min" && root.page !== "pro" && root.page !== "node") return
            if (p.x < videoHost.x || p.y < videoHost.y || p.x > videoHost.x + videoHost.width
                || p.y > videoHost.y + videoHost.height) return
            veyra.moonlightCapture(true)
        }
    }
    // Stream statistics on and off. While a PC stream has the keyboard, the capture filter sees the keys
    // first and toggles the same switch.
    Shortcut {
        sequence: "Ctrl+Alt+Shift+S"
        enabled: (veyra.moonlight && veyra.moonlight.state.streaming === true) || (veyra.xbox && veyra.xbox.state.streaming === true)
        onActivated: veyra.moonlightStatsVisible = !veyra.moonlightStatsVisible
    }
    StreamHud {
        id: streamHud
        x: 16
        y: root.fullscreen ? 16 : 52
        z: 90
    }
    FullscreenBar {
        id: fullBar
        owner: root
        // Windowed cinema: the pill straddles the picture edge (D1). It hides under
        // an open dialog, which lives in the main window below it.
        cinema: root.cinema && !root.fullscreen
        pillTop: (root.maximized ? root.height : root.pictureHeight) - 46
        shown: veyra.hasSource && (root.fullscreen ? root.fullControls && !root.fullLocked
                                                   : root.cinema && root.shownPage === "min"
                                                     && root.leavingPage === "" && dialogs.dialog === ""
                                                     && root.visibility !== Window.Minimized)
        onRequestPage: p => { if (root.fullscreen) root.toggleFullscreen(); root.goPage(p) }
        onRequestDialog: k => dialogs.open(k)
        onRequestFullscreen: root.toggleFullscreen()
        onRequestLock: root.toggleLock()
        onRequestMove: if (!root.fullscreen && !root.maximized) root.startSystemMove()
        onActivity: root.pointerActivity()
    }
    function toggleLock() {
        fullLocked = !fullLocked
        if (fullLocked) dock.opened = false
        else { fullControls = true; fullHide.restart() }
        toast.show(fullLocked ? "已锁定全屏 · Ctrl+L 解锁，Esc 退出全屏" : "已解锁全屏", false)
        veyra.logUi("ui-fullscreen", "locked=" + fullLocked + " shortcut=Ctrl+L")
    }
    // Subtitles keep clear of the control pill: fullscreen bar 24px margin + pill,
    // windowed cinema pill straddling the picture's bottom edge.
    Binding { target: veyra; property: "subtitleBottomInset"; value: fullBar.shown ? (root.fullscreen ? 84 : 56) : 0 }
    // Rebindable (设置 → 快捷键); the hold-to-compare key is handled by the bridge's
    // application event filter because it needs the key release too.
    Shortcut { sequence: veyra.shortcuts.playPause; onActivated: veyra.togglePlayPause() }
    Shortcut { sequence: veyra.shortcuts.fullscreen; onActivated: root.toggleFullscreen() }
    Shortcut { sequence: veyra.shortcuts.screenshot; onActivated: veyra.takeScreenshot() }
    Shortcut {
        sequence: veyra.shortcuts.toggleMode
        enabled: dialogs.dialog === ""
        onActivated: root.goPage(root.page === "min" ? "pro" : "min")
    }
    Shortcut { sequences: ["F11", "Alt+Return", "Alt+Enter"]; onActivated: root.toggleFullscreen() }
    Shortcut { sequence: "Esc"; enabled: root.fullscreen; onActivated: root.toggleFullscreen() }
    // Home: the fullscreen quick-adjust panel, list mode only.
    Shortcut {
        sequence: "Home"
        enabled: root.fullscreen && !root.fullLocked && dialogs.dialog === ""
        onActivated: {
            if (root.page !== "pro") { toast.show("快速调节只在专业模式的列表视图可用", false); return }
            root.quickPanel = !root.quickPanel
        }
    }
    Shortcut { sequence: "Left"; onActivated: veyra.seekBy(-10) }
    Shortcut { sequence: "Right"; onActivated: veyra.seekBy(10) }
    Shortcut { sequence: "Up"; onActivated: veyra.volume = Math.min(1, veyra.volume + 0.05) }
    Shortcut { sequence: "Down"; onActivated: veyra.volume = Math.max(0, veyra.volume - 0.05) }
    Shortcut { sequence: "Ctrl+O"; onActivated: veyra.openFileDialog() }
    Shortcut { sequence: "Ctrl+E"; onActivated: root.page = "exp" }
    Shortcut {
        sequence: veyra.shortcuts.lock
        enabled: root.fullscreen
        onActivated: root.toggleLock()
    }
    // Subtitle keys, as in 1.4.4: B on/off, Z/X -/+50 ms (Shift: 1 s), T/Y cycle
    // the primary/secondary track.
    Shortcut { sequence: "B"; onActivated: veyra.toggleSubtitles() }
    Shortcut { sequence: "Z"; onActivated: veyra.nudgeSubtitle(-50) }
    Shortcut { sequence: "Shift+Z"; onActivated: veyra.nudgeSubtitle(-1000) }
    Shortcut { sequence: "X"; onActivated: veyra.nudgeSubtitle(50) }
    Shortcut { sequence: "Shift+X"; onActivated: veyra.nudgeSubtitle(1000) }
    Shortcut { sequence: "T"; onActivated: veyra.cycleSubtitle(false) }
    Shortcut { sequence: "Y"; onActivated: veyra.cycleSubtitle(true) }

    // Toasts: a refused edit, a finished export, a failed open.
    // pages.css .canvas-toast: top 12px, padding 7px 12px, from translate -8px and
    // opacity 0; .on: opacity .2s, translate .45s --spring; hidden after 2600 ms.
    Rectangle {
        id: toast
        // Under the dock (z 40): an opened dock stays usable over a toast.
        z: 30
        property string message: ""
        property bool isError: false
        property bool on: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: parent.top
        anchors.topMargin: 12
        // Over the picture: cut out of the video window.
        objectName: "videoCover"
        property real coverRadius: 9
        width: Math.min(toastText.implicitWidth + 24 + 2, root.width - 48)
        height: toastText.implicitHeight + 14 + 2
        radius: 9
        color: toast.isError ? "#2A1414" : "#132218"
        border.width: 1
        border.color: toast.isError ? Qt.rgba(1, 0.365, 0.365, 0.4) : Qt.rgba(0.239, 0.863, 0.518, 0.35)
        opacity: on ? 1 : 0
        visible: opacity > 0.01
        transform: Translate { id: toastShift; y: toast.on ? 0 : -8
            Behavior on y { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } } }
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        function show(m, bad) { toast.message = m; toast.isError = bad; toast.on = true; toastTimer.restart() }
        Timer { id: toastTimer; interval: 2600; onTriggered: toast.on = false }
        Text {
            id: toastText
            anchors.centerIn: parent
            width: Math.min(implicitWidth, root.width - 72)
            text: toast.message
            color: toast.isError ? "#FFC9C9" : "#BDF3D2"
            font.family: Theme.fontUi
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignHCenter
        }
    }

    Connections {
        target: veyra
        function onNotice(text, isError) { toast.show(text, isError) }
        function onNavigate(p) {
            // A dialog key opens the dialog; anything else is a page.
            if (p === "capture" || p === "ps5" || p === "moonlight" || p === "xbox" || p === "screen"
                || p === "subtitle" || p === "audio" || p === "save" || p === "manage") {
                dialogs.open(p)
                return
            }
            root.goPage(p === "export" ? "exp" : p)
        }
    }

    // The film's aspect drives the window in cinema mode. Reported only: the
    // engine's own values, never an invented ratio.
    Connections {
        target: veyra
        function onSnapshotChanged() {
            if (root.testAspect > 0) return
            if (veyra.previewAspect > 0.2 && Math.abs(veyra.previewAspect - root.filmAspect) > 0.02)
                root.fitToFilm(veyra.previewAspect)
        }
        function onCompareChanged() { if(root.testAspect <= 0) root.fitToFilm(veyra.previewAspect) }
    }

    // Motion probe (G0.5, --motion-probe): starts one motion 800 ms after load and logs
    // "motion-probe,<name>,<ms>,<value>" every frame for a second, to line up against the
    // design's curve from shotpage.js ?probe=. Sampled per rendered frame, so the spacing
    // follows the display rate rather than a fixed 10 ms step.
    Loader {
        id: probeSwitch
        active: root.test.motionProbe === "switch"
        x: 40; y: 120; z: 100
        sourceComponent: VSwitch {}
    }
    Loader {
        id: probeSeg
        active: root.test.motionProbe === "seg"
        x: 40; y: 160; z: 100
        sourceComponent: VSeg {
            options: [{ id: "a", label: "自动" }, { id: "b", label: "有限 / Limited" }, { id: "c", label: "完整 / Full" }]
            current: "a"
        }
    }
    Timer {
        running: root.test.motionProbe !== undefined
        interval: 800
        onTriggered: {
            const name = root.test.motionProbe
            if (name === "dock") dock.opened = true
            else if (name === "page") { root.page = "pro"; probe.t0 = Date.now() + 150; probe.running = true; return }
            else if (name === "switch") probeSwitch.item.checked = true
            else if (name === "menu") proPage.openTestMenu("source")
            else if (name === "dialog") dialogs.open("capture")
            else if (name === "seg") { probe.x0 = probeSeg.item.indicatorX; probeSeg.item.current = "c"; probe.x1 = probeSeg.item.targetX }
            probe.t0 = Date.now()
            probe.running = true
        }
    }
    FrameAnimation {
        id: probe
        property real t0: 0
        property real x0: 0
        property real x1: 1
        running: false
        onTriggered: {
            const ms = Date.now() - t0
            const name = root.test.motionProbe
            let v
            if (name === "dock") v = dock.barY
            else if (name === "page") { if (ms < 0) return; v = proPage.probeRise.target.opacity.toFixed(4) + "," + proPage.probeRise.ty.toFixed(3) }
            else if (name === "menu") v = proPage.testMenuScale.toFixed(4)
            else if (name === "dialog") v = dialogs.motionScale.toFixed(4)
            else if (name === "seg") v = ((probeSeg.item.indicatorX - x0) / (x1 - x0)).toFixed(4)
            else if (probeSwitch.item) v = probeSwitch.item.children[0].x - 3
            else return
            console.log("motion-probe," + name + "," + ms + "," + v)
            if (ms > 1000) running = false
        }
    }

    // 设置 → 启动窗口大小: a fixed size or the last one, clamped to the screen
    // and centred. A --size test switch resizes after this, so tests keep theirs.
    function applyStartupSize() {
        const choice = veyra.preferences.windowSize || "1280x800"
        const text = choice === "last" ? (veyra.preferences.lastWindow || "1280x800") : choice
        const wh = text.split("x").map(Number)
        if (wh.length !== 2 || !(wh[0] > 0) || !(wh[1] > 0)) return
        const sw = root.screen ? root.screen.desktopAvailableWidth : wh[0]
        const sh = root.screen ? root.screen.desktopAvailableHeight : wh[1]
        root.width = Math.max(root.minimumWidth, Math.min(wh[0], sw - 40))
        root.height = Math.max(root.minimumHeight, Math.min(wh[1], sh - 40))
        if (root.screen) {
            root.x = root.screen.virtualX + Math.round((sw - root.width) / 2)
            root.y = root.screen.virtualY + Math.round((sh - root.height) / 2)
        }
    }
    onClosing: if (!root.fullscreen && !root.maximized && !root.cinema) veyra.rememberWindowSize(root.width, root.height)
    Component.onCompleted: {
        applyStartupSize()
        // Open on the configured page: the user's saved preference, or the
        // override a test passes on the command line.
        if (veyra.initialPage.length > 0)
            root.page = veyra.initialPage === "pro" && veyra.nodeMode === 1 ? "node" : veyra.initialPage
        if (root.test.tab !== undefined) proPage.tab = root.test.tab
        if (root.testAspect > 0) root.fitToFilm(root.testAspect)
        if (root.test.dialog !== undefined) dialogs.open(root.test.dialog)
        videoHost.syncRect()
        if (root.test.menu !== undefined) testMenuTimer.start()
        if (root.test.fullBar !== undefined) testFullTimer.start()
    }
    // The menu anchors on laid-out buttons, so it opens once the page has settled.
    // --full-bar shown|hidden: enter fullscreen with the control window held shown
    // or held hidden, for the present-timing comparison (G2.5).
    Timer { id: testFullTimer; interval: 1500; onTriggered: { root.toggleFullscreen(); if (root.test.fullBar === "hidden") root.fullControls = false } }
    Timer {
        id: testMenuTimer
        interval: 600
        onTriggered: root.test.menu === "min-source" ? minPage.openSourceMenuForTest() : proPage.openTestMenu(root.test.menu)
    }
}
