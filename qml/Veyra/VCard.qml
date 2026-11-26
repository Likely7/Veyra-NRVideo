import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// .card — a plain content card.
//
// A Column, not a ColumnLayout, for the same reason as VGroup: its children are
// Items that set their own height, and a layout would try to negotiate sizes they do
// not declare.
Rectangle {
    default property alias body: inner.data
    radius: Theme.rCard
    color: Theme.card
    border.width: 1
    border.color: Theme.stroke
    clip: true
    implicitHeight: inner.height + Theme.s16 * 2

    ColumnLayout {
        id: inner
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: Theme.s16
        anchors.rightMargin: Theme.s16
        anchors.top: parent.top
        anchors.topMargin: Theme.s16
        spacing: Theme.s8
    }
}
