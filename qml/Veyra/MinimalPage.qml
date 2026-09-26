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

    // cineIn (pages.css @keyframes cineIn, plan M21 "上下各 12% 裁切展开 | 子窗口区域"):
    // the stage opens from inset(12% 0 12% 0 round 8px) to inset(0). The picture is a
    // native window above this scene, so a QML clip would not touch it - main.cpp
    // reads this item and insets the video window's REGION instead (no resize, so no
    // swapchain churn). It carries the value only: invisible, 0x0, never drawn. The
    // C++ walk does not filter on visibility (unlike the cover walk, which must), so
    // an invisible carrier still reaches it.
    Item {
        id: cineIn
        objectName: "videoInset"
        property real frac: 0
        visible: false
        width: 0
        height: 0
    }
    // The animation itself: 12% -> 0 over .8s --spring-soft, started when the page
    // shows through an animated switch (VPage.enter). A cold start (first open) has
    // nothing to animate from, so it goes straight to 0.
    NumberAnimation {
        id: cineInAnim
        target: cineIn
        property: "frac"
        to: 0
        duration: Theme.d(800)
        easing.bezierCurve: Theme.springSoft
    }
    onEnter: { cineIn.frac = 0.12; cineInAnim.restart() }

    // The independent pill extends below this window; no opaque spacer is needed.
    readonly property real pictureHeight: parent ? parent.height : 0

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

    // Report the film aspect upward so the window can snap to it.
    onPictureHeightChanged: if (veyra.sourceAspect > 0.2) root.requestAspect(veyra.sourceAspect)
    signal requestAspect(real aspect)
    signal requestFullscreen()
}
