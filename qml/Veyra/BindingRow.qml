// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: row
    property int action: -1
    property bool xbox: false
    property bool compact: false
    readonly property bool ps5LearningControl: !compact
    property bool selected: false
    property bool pressed: false
    property bool toggleBinding: false
    property string label: ""
    property string note: ""
    property string primary: "—"
    property string alternate: "—"
    property real uiScale: 1
    signal picked(int slot)

    readonly property string accessibleLabel: qsTr("%1，主键 %2，备用键 %3").arg(label).arg(primary).arg(alternate)
    readonly property string caption: note.length ? qsTr("%1 · %2").arg(label).arg(note) : label
    readonly property bool hasAlternate: alternate !== "" && alternate !== "—"

    implicitWidth: content.implicitWidth + 2 * content.anchors.margins
    implicitHeight: Math.max(compact ? 27 : Theme.rowMinHeight, content.implicitHeight + 2 * content.anchors.margins)
    radius: Theme.rCtl
    color: selected ? Theme.accentSoft : "transparent"
    border.width: selected ? 1 : 0
    border.color: Theme.accent

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.margins: row.compact ? 3 : 5
        spacing: 5
        ConsoleGlyph {
            action: row.action
            platform: row.xbox ? "xbox" : "ps5"
            size: Math.round(18 * row.uiScale)
            visualState: row.pressed ? "pressed" : row.selected ? "selected" : "normal"
        }
        Text {
            id: actionLabel
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: row.caption
            wrapMode: Text.Wrap
            textFormat: Text.PlainText
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Math.round(Theme.fsSmall * row.uiScale)
            TapHandler { enabled: !row.compact; onTapped: row.picked(-1) }
        }
        KeyCap {
            objectName: "controller-panel-list-primary-" + row.action
            Layout.maximumWidth: Math.max(24, row.width * (row.compact && !row.hasAlternate ? 0.57 : 0.30))
            text: row.primary
            size: row.compact ? "s" : "m"
            visualState: row.primary.length === 0 || row.primary === "—" ? "empty" : "normal"
            interactive: !row.compact
            accessibleText: row.accessibleLabel
            onClicked: row.picked(0)
        }
        KeyCap {
            objectName: "controller-panel-list-alternate-" + row.action
            visible: !row.compact || row.hasAlternate
            Layout.maximumWidth: Math.max(24, row.width * 0.30)
            text: row.alternate
            size: "s"
            visualState: row.hasAlternate ? "secondary" : "empty"
            interactive: !row.compact
            accessibleText: row.accessibleLabel
            onClicked: row.picked(1)
        }
        Text {
            visible: row.toggleBinding
            text: qsTr("切换")
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsEyebrow
        }
    }
}
