// Clean, fast startup fade for the dusk palette.
import QtQuick

Item {
    id: intro
    property rect target: Qt.rect(width / 2 - 115, height * 0.2, 230, 192)
    property string skipText: ""
    property real ms: 0
    property bool running: false
    property bool dismissing: false
    property var musicClock: null
    property bool followMusic: false
    property int frames: 0
    property real worstFrameMs: 0

    signal frame()
    signal revealHome()
    signal finished()

    function start() {
        if (running || dismissing) return
        running = true
        forceActiveFocus()
        revealHome()
        fadeAnim.restart()
    }

    function skip() {
        if (dismissing) return
        dismiss()
    }

    function dismiss() {
        if (dismissing) return
        dismissing = true
        revealHome()
        fadeAnim.restart()
    }

    focus: true
    Keys.onPressed: event => {
        intro.skip()
        event.accepted = true
    }

    MouseArea {
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.AllButtons
        property point pressAt
        property bool moved: false
        onPressed: mouse => {
            pressAt = Qt.point(mouse.x, mouse.y)
            moved = false
        }
        onPositionChanged: mouse => {
            if (!pressed || moved || Math.hypot(mouse.x - pressAt.x, mouse.y - pressAt.y) < 6) return
            moved = true
            const w = intro.Window.window
            if (w && w.visibility === Window.Windowed) w.startSystemMove()
        }
        onClicked: if (!moved) intro.skip()
        onWheel: wheel => wheel.accepted = true
    }

    Rectangle {
        id: backdrop
        anchors.fill: parent
        radius: Theme.rWindow
        color: Theme.bgOuter
    }

    NumberAnimation {
        id: fadeAnim
        target: intro
        property: "opacity"
        from: intro.opacity
        to: 0
        duration: Theme.d(250)
        easing.bezierCurve: Theme.easeOut
        onFinished: {
            intro.ms = 250
            intro.running = false
            intro.finished()
        }
    }
}
