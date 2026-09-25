import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// .dgroup — a card holding a stack of label/control rows.
//
// A ColumnLayout, matching the accordion body that lays its rows out correctly. A
// plain Column here reported implicitHeight 0 and left every child at y=0 - it never
// positioned them - which is why the rows drew on top of each other. The rows carry
// Layout.preferredHeight and Layout.fillWidth for it.
Rectangle {
    id: group
    default property alias body: inner2.data
    radius: 12
    color: Theme.card2
    border.width: 1
    border.color: Theme.stroke
    implicitHeight: inner2.height + 6

    Column {
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
