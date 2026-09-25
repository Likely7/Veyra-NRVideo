import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    default property alias body: inner.data
    radius: Theme.rCard
    color: Theme.card
    border.width: 1
    border.color: Theme.stroke
    implicitHeight: inner.childrenRect.height + 28
    Item {
        id: inner
        anchors.fill: parent
        anchors.margins: 14
    }
}
