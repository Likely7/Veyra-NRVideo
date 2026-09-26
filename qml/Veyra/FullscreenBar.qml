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

    transientParent: owner
    flags: Qt.Tool | Qt.FramelessWindowHint | Qt.WindowDoesNotAcceptFocus
    color: "transparent"

    // Room for the pill plus the preset menu above it (VMenu is at most 420 tall).
    readonly property int menuRoom: 440
    readonly property int barWidth: Math.min(860, owner.width - 48)
    width: barWidth
    height: 92 + menuRoom
    // Centred, 24px above the screen's bottom edge (the design's 24px side margin).
    x: owner.x + Math.round((owner.width - width) / 2)
    y: owner.y + owner.height - height - 24

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

    CineBar {
        id: bar
        x: 0
        y: win.menuRoom
        width: win.width
        height: 92
        fullscreen: true
        opacity: win.fade
        // barIn-like rise while showing: translate 30px -> 0 with the fade.
        transform: Translate { y: (1 - win.fade) * 30 }
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
