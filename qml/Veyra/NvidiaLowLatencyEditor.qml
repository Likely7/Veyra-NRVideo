import QtQuick
import QtQuick.Layouts
ColumnLayout {
    id: root
    property string namePrefix: "list"
    property var backend: veyra
    readonly property bool available: root.backend.nvidiaLowLatencyAvailable === true
    Layout.fillWidth: true
    spacing: 4
    VRow {
        objectName: root.namePrefix+"-nvidia-low-latency-row"
        label: qsTr("英伟达低延迟")
        hintBelow: true
        hint: !root.available ? qsTr("需要 NVIDIA 显卡") : ""
        VSeg {
            objectName: root.namePrefix+"-nvidia-low-latency"
            enabled: root.available
            opacity: enabled ? 1 : 0.4
            options: [{id:"0",label:qsTr("关")},{id:"1",label:qsTr("开")}]
            // Another adapter shows 关: the NVIDIA choice is kept for the next
            // NVIDIA session but does nothing there.
            current: root.available ? (root.backend.nvidiaLowLatency > 0 ? "1" : "0") : "0"
            onPicked: id => root.backend.nvidiaLowLatency=Number(id)
        }
    }
    Text {
        Layout.fillWidth: true
        text: root.backend.running ? root.backend.presentationStatus : qsTr("打开视频后生效")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
}
