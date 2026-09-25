import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: slider
    property real value: 0
    property real from: 0
    property real to: 1
    property bool center: false        // fill from the middle instead of the left
    property bool enabledControl: true
    signal moved(real value)

    implicitHeight: 20
    implicitWidth: 160
    readonly property real frac: (to > from) ? Math.max(0, Math.min(1, (value - from) / (to - from))) : 0

    Rectangle {
        id: track
        anchors.verticalCenter: parent.verticalCenter
        width: parent.width
        height: 4
        radius: 9
        color: Qt.rgba(1, 1, 1, 0.12)
    }
    Rectangle {
        anchors.verticalCenter: track.verticalCenter
        height: 4
        radius: 9
        color: Theme.accent
        visible: !slider.center
        width: track.width * slider.frac
        x: 0
    }
    Rectangle {
        id: knob
        width: Theme.sliderKnob
        height: Theme.sliderKnob
        radius: width / 2
        color: "#FFFFFF"
        anchors.verticalCenter: track.verticalCenter
        x: track.width * slider.frac - width / 2
        scale: drag.active || hover.hovered ? 1.25 : 1.0
        Behavior on scale { NumberAnimation { duration: 350; easing.bezierCurve: Theme.spring } }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor; enabled: slider.enabledControl }
    DragHandler {
        id: drag
        target: null
        enabled: slider.enabledControl
        onActiveTranslationChanged: {
            const x = Math.max(0, Math.min(track.width, knob.x + activeTranslation.x + knob.width / 2))
            slider.value = slider.from + (x / track.width) * (slider.to - slider.from)
        }
        onActiveChanged: if (!active) slider.moved(slider.value)
    }
    TapHandler {
        enabled: slider.enabledControl
        onTapped: point => {
            const x = Math.max(0, Math.min(track.width, point.position.x))
            slider.value = slider.from + (x / track.width) * (slider.to - slider.from)
            slider.moved(slider.value)
        }
    }
}
