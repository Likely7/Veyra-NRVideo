import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: group
    default property alias body: inner2.data
    radius: 12
    color: Theme.card2
    border.width: 1
    border.color: Theme.stroke
    implicitHeight: inner2.childrenRect.height + 4
    Column {
        id: inner2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 14
    }
}
