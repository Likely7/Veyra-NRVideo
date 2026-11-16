import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: sw
    property bool checked: false
    signal toggled(bool checked)
    width: Theme.switchWidth
    height: Theme.switchHeight
    radius: 99
    color: checked ? Theme.accent : Qt.rgba(1, 1, 1, 0.14)
    Behavior on color { ColorAnimation { duration: Theme.d(250) } }
    // The dusk palette's sunset accent: the on track is the gradient, faded in
    // and out with the colour.
    VAccentFill {
        anchors.fill: parent
        radius: parent.radius
        visible: Theme.accentGradient && opacity > 0
        opacity: sw.checked ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.d(250) } }
    }

    Rectangle {
        id: knob
        width: tap.pressed ? 18 : Theme.switchKnob
        height: Theme.switchKnob
        radius: height / 2
        color: sw.checked ? Theme.accentInk : "#FFFFFF"
        anchors.verticalCenter: parent.verticalCenter
        x: sw.checked ? (parent.width - width - 3) : 3
        Behavior on x { NumberAnimation { duration: Theme.d(500); easing.bezierCurve: Theme.spring } }
        Behavior on width { NumberAnimation { duration: Theme.d(200) } }
    }
    HoverHandler { cursorShape: Qt.PointingHandCursor }
    // Controlled: owners bind checked to their model and write the new value back in
    // onToggled. Assigning checked here destroyed that binding, so a value changed
    // elsewhere (a preset, another page) no longer moved the switch (GitHub #29).
    TapHandler { id: tap; gesturePolicy: TapHandler.WithinBounds; onTapped: sw.toggled(!sw.checked) }
}
