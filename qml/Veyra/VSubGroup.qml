// .sub — the nested collapsible panel inside an accordion body.
//
// From pages.css .editor .sub: a slim rounded strip whose summary row shows the
// group name, an item count on the right, and a chevron that flips when open.
// The design uses one for each advanced group (模型参数 6 项, 增强变化量 5 项,
// 实验 2 项).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: sub
    property string label: ""
    property int count: 0
    property bool expanded: false
    default property alias content: inner.data

    implicitHeight: header.implicitHeight + (expanded ? inner.implicitHeight + 8 : 0)
    radius: 9
    color: Qt.rgba(1, 1, 1, 0.03)
    clip: true
    Behavior on implicitHeight { NumberAnimation { duration: 450; easing.bezierCurve: Theme.springSoft } }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 0

        Item {
            id: header
            Layout.fillWidth: true
            implicitHeight: 30
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 10
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: sub.label
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: 12
                }
                Text {
                    visible: sub.count > 0
                    text: sub.count + " 项"
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: 11
                }
                Text {
                    text: "⌄"
                    color: Theme.t3
                    font.pixelSize: 12
                    rotation: sub.expanded ? 180 : 0
                    Behavior on rotation { NumberAnimation { duration: 300; easing.bezierCurve: Theme.spring } }
                }
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: sub.expanded = !sub.expanded }
        }

        ColumnLayout {
            id: inner
            Layout.fillWidth: true
            Layout.leftMargin: 10
            Layout.rightMargin: 10
            Layout.bottomMargin: 8
            spacing: 2
            visible: sub.expanded
        }
    }
}
