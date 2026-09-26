// 极简模式, rebuilt from the design (pages-a.js PAGES.min + pages.css .min/.cine-bar).
//
// The design's whole point here: the window snaps to the film's aspect ratio (no
// letterbox bars), and a large rounded control pill straddles the bottom edge of
// the picture - half on the picture, half below it. The pill is a 3-column grid
// (230px | 1fr | 230px) at min(860px, 100% - 48px), 92px tall, radius 30.
//
// The window resize itself is Main.qml's job (it owns the window); this page only
// reports the film aspect and draws the bar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

VPage {
    id: root
    signal requestPage(string page)

    // Where the picture ends and the pill straddles. Main.qml sets the window
    // height to pictureHeight + 46, so the bar's top edge is pictureHeight - 46.
    // The picture is the window minus the control bar's full height: the bar
    // sits below the picture rather than straddling it, because a native video
    // window always draws above the QML scene.
    readonly property real pictureHeight: parent ? parent.height - 92 : 0

    // --- the picture ------------------------------------------------------
    // .min .stage: full width, the picture height, radius 8 (the window radius),
    // pure black behind. The native video window is placed here by main.cpp; the
    // radius applies to the black plate, never to the video, because rounding the
    // picture crops it.
    Rectangle {
        id: stage
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.pictureHeight
        radius: Theme.rWindow
        color: Theme.videoBlack
        clip: true

        Text {
            anchors.centerIn: parent
            visible: !veyra.hasSource
            text: "选择一个片源开始"
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsH3
        }
    }

    // --- the control pill (CineBar.qml) ------------------------------------
    // .cine-bar: min(860px, 100% - 48px), below the picture (see pictureHeight).
    CineBar {
        id: cineBar
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(860, root.width - 48)
        height: 92
        y: root.pictureHeight
        visible: veyra.hasSource
        onRequestPage: p => root.requestPage(p)
        onRequestFullscreen: root.requestFullscreen()

        // barIn: opacity 0, translate 30px, scale .94 -> settled
        opacity: 0
        SequentialAnimation on opacity {
            running: cineBar.visible
            PauseAnimation { duration: Theme.d(120) }
            NumberAnimation { to: 1.0; duration: Theme.d(800); easing.bezierCurve: Theme.spring }
        }
    }


    // Report the film aspect upward so the window can snap to it.
    onPictureHeightChanged: if (veyra.sourceAspect > 0.2) root.requestAspect(veyra.sourceAspect)
    signal requestAspect(real aspect)
    signal requestFullscreen()
}
