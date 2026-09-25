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
    TapHandler { id: tap; onTapped: { sw.checked = !sw.checked; sw.toggled(sw.checked) } }
}
