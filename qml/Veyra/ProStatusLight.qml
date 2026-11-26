// Professional-view reusable presentation component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: light
    spacing: Theme.s8
    readonly property string action: veyra.runStatusAction
    readonly property string summary: veyra.runStatus + (action.length > 0 ? " · " + action : "")
    readonly property string description: summary + (veyra.runStatusDetail.length > 0 ? " · " + veyra.runStatusDetail : "")
    readonly property color tone: veyra.runStatusLevel === "ok" ? Theme.ok
                                : veyra.runStatusLevel === "warn" ? Theme.warn
                                : veyra.runStatusLevel === "err" ? Theme.err : Theme.t3
    Item {
        Layout.fillWidth: true
        Layout.minimumWidth: 32
        Layout.preferredHeight: 4
        Rectangle {
            id: strip
            anchors.fill: parent
            radius: 2
            color: light.tone
            opacity: veyra.runStatusLevel === "idle" ? 0.35 : 0.9
            Behavior on color { enabled: strip.visible; ColorAnimation { duration: Theme.d(300) } }
        }
    }
    ColumnLayout {
        spacing: 2
        Layout.maximumWidth: Math.max(0, light.width - 40)
        Layout.preferredWidth: Math.min(implicitWidth, light.width * 0.85)
        Text {
            objectName: "run-status"
            Layout.fillWidth: true
            text: light.action.length > 0 ? light.summary : light.description
            color: light.tone
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsEyebrow
            font.weight: Font.Medium; font.variableAxes: Theme.axesMedium
            horizontalAlignment: Text.AlignRight
            wrapMode: Text.Wrap
            maximumLineCount: light.action.length > 0 ? 2147483647 : 2
            elide: light.action.length > 0 ? Text.ElideNone : Text.ElideRight
            Accessible.name: text
            Accessible.description: veyra.runStatusHint
        }
        Text {
            Layout.fillWidth: true
            visible: light.action.length > 0 && text.length > 0
            text: veyra.runStatusDetail
            color: light.tone
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsEyebrow
            horizontalAlignment: Text.AlignRight
            elide: Text.ElideRight
        }
        HoverHandler { id: statusHover }
        ToolTip {
            id: statusTip
            objectName: "run-status-tooltip"
            visible: statusHover.hovered && text.length > 0
            delay: 400
            width: Math.min(360, Math.max(0, light.Window.width - 24))
            text: light.description + (veyra.runStatusHint.length > 0 ? "\n\n" + veyra.runStatusHint : "")
            contentItem: Text {
                text: statusTip.text
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
                wrapMode: Text.Wrap
            }
        }
    }
}
