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
    property bool instantExpansion: false
    default property alias content: inner.data

    implicitHeight: header.implicitHeight + (expanded ? inner.implicitHeight + Theme.s8 : 0)
    radius: Theme.rCtl
    color: Theme.card
    border.width: header.activeFocus ? 2 : 1
    border.color: header.activeFocus ? Theme.focusRing : Theme.stroke
    clip: true
    Behavior on implicitHeight {
        enabled: !sub.instantExpansion
        NumberAnimation { duration: Theme.d(Theme.animNormal); easing.bezierCurve: Theme.springSoft }
    }

    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        spacing: 0

        Rectangle {
            id: header
            Layout.fillWidth: true
            implicitHeight: Math.max(32, groupLabel.implicitHeight + Theme.s12)
            radius: Theme.rCtl
            color: headerTap.pressed ? Theme.pressFill : headerHover.hovered ? Theme.hoverFill : "transparent"
            activeFocusOnTab: true
            Keys.onReturnPressed: event => { event.accepted = true; sub.expanded = !sub.expanded }
            Keys.onEnterPressed: event => { event.accepted = true; sub.expanded = !sub.expanded }
            Keys.onSpacePressed: event => { event.accepted = true; sub.expanded = !sub.expanded }
            Accessible.role: Accessible.Button
            Accessible.name: sub.label
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.s12
                anchors.rightMargin: Theme.s12
                spacing: Theme.s8
                Text {
                    Layout.fillWidth: true
                    id: groupLabel
                    Layout.minimumWidth: 0
                    wrapMode: Text.Wrap
                    text: sub.label
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsBody
                }
                Text {
                    visible: sub.count > 0
                    text: sub.count + qsTr(" 项")
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsEyebrow
                }
                VIcon {
                    name: "down"
                    color: Theme.t3
                    rotation: sub.expanded ? 180 : 0
                    Behavior on rotation { NumberAnimation { duration: Theme.d(Theme.animNormal); easing.bezierCurve: Theme.spring } }
                }
            }
            HoverHandler { id: headerHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { id: headerTap; gesturePolicy: TapHandler.WithinBounds; onTapped: sub.expanded = !sub.expanded }
        }

        ColumnLayout {
            id: inner
            Layout.fillWidth: true
            Layout.leftMargin: Theme.s12
            Layout.rightMargin: Theme.s12
            Layout.bottomMargin: Theme.s8
            spacing: Theme.s4
            visible: sub.expanded
        }
    }
}
