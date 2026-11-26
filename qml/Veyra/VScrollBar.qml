// The one scrollbar style: a thin rounded thumb, no track or arrows (the
// prototype's thin CSS scrollbar). The platform style drew a white Windows bar.
import QtQuick
import QtQuick.Controls.Basic as Basic

Basic.ScrollBar {
    id: bar
    // A wider gutter where the content sits flush against it (colour panel).
    property bool wide: false
    implicitWidth: wide ? 10 : 6
    implicitHeight: wide ? 10 : 6
    padding: wide ? 2 : 0
    minimumSize: 0.08
    contentItem: Rectangle {
        implicitWidth: 6
        implicitHeight: 6
        radius: Theme.rCtl
        visible: bar.size < 1
        color: bar.pressed ? Theme.t2 : bar.hovered ? Theme.t3 : Theme.strokeStrong
        Behavior on color { ColorAnimation { duration: Theme.d(Theme.animFast) } }
    }
    background: Item {}
}
