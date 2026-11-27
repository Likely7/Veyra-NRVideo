import QtQuick
import QtQuick.Layouts

RowLayout {
    id: tabs
    required property var steps
    required property string current
    signal picked(string step)
    spacing: 4

    Repeater {
        model: tabs.steps
        delegate: ControllerPanelButton {
            id: tab
            required property var modelData
            required property int index
            objectName: "controller-panel-step-" + modelData.id
            Layout.fillWidth: true
            Layout.preferredWidth: 1
            Layout.minimumWidth: 0
            Layout.preferredHeight: Theme.rowMinHeight
            enabled: modelData.enabled
            primary: tabs.current === modelData.id
            visible: true
            text: qsTr("%1 %2").arg(modelData.done ? "✓" : String(index + 1)).arg(modelData.label)
            Accessible.name: modelData.label
            Accessible.description: !enabled ? (modelData.hint || "") : ""
            tip: !enabled ? (modelData.hint || "") : modelData.label
            fontSize: Theme.fsSmall
            onClicked: tabs.picked(modelData.id)
        }
    }
}
