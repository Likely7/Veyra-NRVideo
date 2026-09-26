// G2.5: the fullscreen control bar, in its own top-level window.
//
// In fullscreen the picture is the whole screen, and the native D3D12 video window
// is drawn above everything in the main window's scene. A bar in the main scene
// would need a hole cut out of the picture; the design (and D1 in the goal plan)
// puts it in a separate small window instead. It is owned by the main window
// (always above it, never above other applications), takes no keyboard focus (the
// shortcuts stay with the main window), and is hidden outright when the controls
// hide, so a hidden bar composes nothing over the video.
//
// The window is taller than the pill so the preset menu can open upwards inside it;
// the pointer only hits the pill (or the open menu) because the rest is masked off.
import QtQuick
import QtQuick.Window

Window {
    id: win
    objectName: "fullscreenBar"
    required property Window owner
    property bool shown: false
    signal requestPage(string page)
    signal requestFullscreen()
    signal requestLock()
    // Pointer movement over the bar keeps the controls up (the main window cannot
    // see it here).
    signal activity()
    readonly property bool hovered: bar.hovered
    readonly property bool menuOpen: bar.menuOpen
    // G3.2 (D1): the same window carries the cinema pill in windowed 极简 mode, where
    // it straddles the picture's bottom edge: its top is pillTop (owner-relative),
    // half over the native video window and half below it. In fullscreen it sits
    // 24px above the screen edge.
    property bool cinema: false
    property real pillTop: 0

    transientParent: owner
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"

    // Room for the pill plus the preset menu above it (VMenu is at most 420 tall).
    readonly property int menuRoom: 440
    readonly property int barWidth: Math.min(860, owner.width - 48)
    width: barWidth
    height: 92 + menuRoom
    // Centred; fullscreen: 24px above the screen's bottom edge (the design's 24px
    // side margin); cinema: .cine-bar top = --pic-h - 46.
    x: owner.x + Math.round((owner.width - width) / 2)
    y: cinema ? owner.y + Math.round(pillTop) - menuRoom
              : owner.y + owner.height - height - 24

    // Hidden outright rather than transparent, so it composes nothing over the video
    // (the PresentMon comparison in the plan measures exactly this). The fade is
    // the design's control fade: opacity .2s; the window goes when it finishes.
    property real fade: shown ? 1 : 0
    Behavior on fade { NumberAnimation { duration: Theme.d(200) } }
    visible: shown || fade > 0.01
    onVisibleChanged: veyra.logUi("ui-fullscreen", "control window visible=" + visible)

    // Only the pill takes the pointer; with the menu open, the whole window does,
    // so a click beside the menu closes it instead of falling through to the picture.
    readonly property rect hitRect: menuOpen ? Qt.rect(0, 0, width, height)
                                             : Qt.rect(0, menuRoom, width, 92)
    onHitRectChanged: veyra.setWindowMask(win, hitRect)
    Component.onCompleted: veyra.setWindowMask(win, hitRect)

    // barIn (pages.css): from opacity 0, translate 30px, scale .94, .8s --spring
    // after .12s. Played by enter() when the cinema page shows.
    property real enterDy: 0
    property real enterScale: 1
    property real enterOpacity: 1
    function enter() { barIn.restart() }
    // The cinema pill enters with barIn each time it appears (page shown, dialog
    // closed); the fullscreen one keeps the plain fade and rise.
    onShownChanged: if (shown && cinema) enter()
    SequentialAnimation {
        id: barIn
        ScriptAction { script: { win.enterDy = 30; win.enterScale = 0.94; win.enterOpacity = 0 } }
        PauseAnimation { duration: Theme.d(120) }
        ParallelAnimation {
            NumberAnimation { target: win; property: "enterDy"; to: 0; duration: Theme.d(800); easing.bezierCurve: Theme.spring }
            NumberAnimation { target: win; property: "enterScale"; to: 1; duration: Theme.d(800); easing.bezierCurve: Theme.spring }
            NumberAnimation { target: win; property: "enterOpacity"; to: 1; duration: Theme.d(800); easing.bezierCurve: Theme.spring }
        }
    }

    CineBar {
        id: bar
        x: 0
        y: win.menuRoom
        width: win.width
        height: 92
        fullscreen: !win.cinema
        menuBottomLimit: win.menuRoom - 16
        opacity: win.fade * win.enterOpacity
        // barIn-like rise while showing: translate 30px -> 0 with the fade.
        transform: Translate { y: (win.cinema ? 0 : (1 - win.fade) * 30) + win.enterDy }
        scale: win.enterScale
        onRequestPage: p => win.requestPage(p)
        onRequestFullscreen: win.requestFullscreen()
        onRequestLock: win.requestLock()
        HoverHandler {
            // Real movement only: hover repeats while the bar animates.
            property point last: Qt.point(-1, -1)
            onPointChanged: {
                const p = point.scenePosition
                if (Math.abs(p.x - last.x) < 1 && Math.abs(p.y - last.y) < 1) return
                last = p
                win.activity()
            }
        }
    }
}
