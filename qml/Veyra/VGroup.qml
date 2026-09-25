import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// .dgroup — a card holding a stack of label/control rows.
//
// A ColumnLayout, not a Column. VRow has no width of its own: it asks for one through
// Layout.fillWidth, which only a layout honours. A Column ignores Layout.* and skips
// zero-width children entirely, so every row stayed at y=0 with a 0 implicitHeight and
// the rows drew on top of each other (reproduced with qml.exe, 2026-09-26).
//
// The group fills its column by default, as VRow does. Without it a group placed in a
// ColumnLayout got width 0, its inner layout -28, and the rows were not positioned.
Rectangle {
    id: group
    default property alias body: inner2.data
    Layout.fillWidth: true
    radius: 12
    color: Theme.card2
    border.width: 1
    border.color: Theme.stroke
    implicitHeight: inner2.height + 6

    ColumnLayout {
        id: inner2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.top: parent.top
        anchors.topMargin: 3
        spacing: 0
    }
}
