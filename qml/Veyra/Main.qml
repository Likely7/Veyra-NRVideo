// The application shell, rebuilt from the approved design.
//
// Structure comes from the prototype (index.html + app.css + board.js):
//   * a frameless window with the small 8px radius;
//   * a hidden top dock that drops in when the pointer reaches the top edge;
//   * one page visible at a time: home / min / pro / node / exp / set;
//   * in cinema mode the WINDOW ITSELF snaps to the film's aspect ratio so the
//     picture has no letterbox bars. The prototype's own technical note says the
//     window must resize to the film when a file opens (capture cards and games
//     are 16:9), and its fitAspect() adds BAR_BELOW = 46 for the control pill.
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
    minimumWidth: 720
    minimumHeight: 260
    visible: true
    color: "transparent"
    flags: Qt.Window | Qt.FramelessWindowHint
    title: "Veyra " + veyra.version

    // Page ids match the prototype's PAGES keys so a design screenshot and an
    // app screenshot can be compared under the same name.
    property string page: "home"
    // Test switches from the command line (main.cpp, G0.3); empty in normal use.
    readonly property var test: typeof vyTest !== "undefined" ? vyTest : ({})
    readonly property real testAspect: test.aspect !== undefined ? test.aspect : 0
    readonly property bool cinema: page === "min"
    Binding { target: Theme; property: "reduced"; value: veyra.reducedMotion || root.test.reducedMotion === true }

    // Cinema geometry, straight from the prototype: the picture is width/aspect
    // and the window is that plus the lower half of the control pill.
    property real filmAspect: 2.39
    // The design straddles the bar on the picture edge with barBelow = 46. The bar
    // needs its own window for that, which does not exist yet, so the picture
    // area reserves the whole bar height instead.
    readonly property int barBelow: 92
    readonly property real pictureHeight: Math.round(width / filmAspect)

    function fitToFilm(aspect) {
        if (aspect > 0.2 && aspect < 5.0) filmAspect = aspect
        if (cinema) height = Math.round(pictureHeight + barBelow)
    }
    // .vy.cine transition: height .7s var(--spring-soft)
    Behavior on height {
        enabled: root.cinema
        NumberAnimation { duration: Theme.d(700); easing.bezierCurve: Theme.springSoft }
    }
    onWidthChanged: {
        if (cinema) height = Math.round(pictureHeight + barBelow)
        videoHost.syncRect()
    }
    // The window height animates towards the film's aspect, so the picture area has
    // to follow every step of that animation. Without this the host kept its
    // previous height and the picture sat inside black bars - exactly what cinema
    // mode exists to avoid.
    onHeightChanged: videoHost.syncRect()
    onPageChanged: {
        // Tell the bridge where the user is, so a command like "open a file" can
        // behave differently from home than from the professional page.
        if (veyra.currentPage !== page) veyra.currentPage = page
        if (cinema) height = Math.round(pictureHeight + barBelow)
        else if (height < 600) height = 800
        // The video rect follows the page, and it must be settled before the
        // engine opens anything: it samples the window's client size once.
        videoHost.syncRect()
    }

    // Background: pure black in cinema mode (the picture is the window), the
    // three-stop gradient otherwise - the prototype's .vy / .vy.cine split.
    VBackdrop {
        id: backdrop
        anchors.fill: parent
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
            // .nodeview padding 14; .nv-top is a fixed 330px, minus its 40px bar.
            return { x: 14, y: 14, width: Math.max(1, width - 28), height: 330 - 40 }
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
    StackLayout {
        id: pages
        anchors.fill: parent
        currentIndex: {
            switch (root.page) {
            case "min": return 1
            case "pro": return 2
            case "node": return 3
            case "exp": return 4
            case "set": return 5
            default: return 0
            }
        }

        HomePage { onRequestPage: p => root.page = p }
        MinimalPage {
            onRequestPage: p => root.page = p
            onRequestAspect: aspect => { if (root.testAspect <= 0) root.fitToFilm(aspect) }
        }
        ProPage {
            id: proPage
            onRequestPage: p => root.page = p
            onRequestDialog: key => dialogs.open(key)
        }
        NodePage { onRequestPage: p => root.page = p }
        ExportPage { onRequestPage: p => root.page = p }
        SettingsPage { onRequestPage: p => root.page = p }
    }

    // The five dialogs sit above the pages and below the dock's own tooltips.
    DialogHost {
        id: dialogs
        anchors.fill: parent
        onStartCapture: {
            if (veyra.captureDeviceId.length > 0) root.page = "min"
            else toast.show("先选择一个采集设备", true)
        }
        onStartPs5: root.page = "min"
        onStartScreen: {
            if (veyra.screenTargetId.length > 0) root.page = "min"
            else toast.show("先选择一个捕获目标", true)
        }
    }

    // The dock floats above every page and retracts on its own.
    TopDock {
        id: dock
        anchors.horizontalCenter: parent.horizontalCenter
        currentPage: root.page
        pinned: root.test.dockPinned === true
        opened: pinned
        onRequestPage: p => root.page = p
    }

    // Toasts: a refused edit, a finished export, a failed open.
    Rectangle {
        id: toast
        property string message: ""
        property bool isError: false
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 24
        width: Math.min(toastText.implicitWidth + 28, root.width - 48)
        height: toastText.implicitHeight + 18
        radius: 9
        color: toast.isError ? "#2A1414" : "#132218"
        border.width: 1
        border.color: toast.isError ? Qt.rgba(1, 0.365, 0.365, 0.4) : Qt.rgba(0.239, 0.863, 0.518, 0.35)
        opacity: 0
        visible: opacity > 0.01
        Behavior on opacity { NumberAnimation { duration: Theme.d(Theme.durNormal) } }
        function show(m, bad) { toast.message = m; toast.isError = bad; toast.opacity = 1; toastTimer.restart() }
        Timer { id: toastTimer; interval: 4200; onTriggered: toast.opacity = 0 }
        Text {
            id: toastText
            anchors.centerIn: parent
            width: parent.width - 20
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
            if (p === "capture" || p === "ps5" || p === "screen"
                || p === "subtitle" || p === "audio" || p === "save" || p === "manage") {
                dialogs.open(p)
                return
            }
            root.page = (p === "export" ? "exp" : p)
        }
    }

    // The film's aspect drives the window in cinema mode. Reported only: the
    // engine's own values, never an invented ratio.
    Connections {
        target: veyra
        function onSnapshotChanged() {
            if (root.testAspect > 0) return
            if (veyra.sourceAspect > 0.2 && Math.abs(veyra.sourceAspect - root.filmAspect) > 0.02)
                root.fitToFilm(veyra.sourceAspect)
        }
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
            else if (name === "page") root.page = "pro"
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
            else if (name === "page") v = proPage.opacity + "," + proPage.y
            else if (name === "menu") v = proPage.testMenuScale.toFixed(4)
            else if (name === "dialog") v = dialogs.motionScale.toFixed(4)
            else if (name === "seg") v = ((probeSeg.item.indicatorX - x0) / (x1 - x0)).toFixed(4)
            else if (probeSwitch.item) v = probeSwitch.item.children[0].x - 3
            else return
            console.log("motion-probe," + name + "," + ms + "," + v)
            if (ms > 1000) running = false
        }
    }

    Component.onCompleted: {
        // Open on the configured page: the user's saved preference, or the
        // override a test passes on the command line.
        if (veyra.initialPage.length > 0) root.page = veyra.initialPage
        if (root.test.tab !== undefined) proPage.tab = root.test.tab
        if (root.testAspect > 0) root.fitToFilm(root.testAspect)
        if (root.test.dialog !== undefined) dialogs.open(root.test.dialog)
        videoHost.syncRect()
        if (root.test.menu !== undefined) testMenuTimer.start()
    }
    // The menu anchors on laid-out buttons, so it opens once the page has settled.
    Timer { id: testMenuTimer; interval: 600; onTriggered: proPage.openTestMenu(root.test.menu) }
}
