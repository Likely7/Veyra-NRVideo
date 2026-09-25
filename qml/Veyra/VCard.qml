import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    default property alias body: inner.data
    radius: Theme.rCard
    color: Theme.card
    border.width: 1
    border.color: Theme.stroke
    implicitHeight: inner.implicitHeight + 28
    ColumnLayout {
        id: inner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 6
    }
}
