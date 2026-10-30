import QtQuick
import QtQuick.Layouts
ColumnLayout {
    id: root
    property string namePrefix: "list"
    readonly property bool available: veyra.nvidiaLowLatencyAvailable === true
    Layout.fillWidth: true
    spacing: 4
    VRow {
        label: qsTr("英伟达低延迟")
        hintBelow: true
        hint: root.available ? qsTr("减少处理排队；增强档提高 GPU 频率，可能增加功耗。") : qsTr("需要 NVIDIA 显卡")
        VSeg {
            objectName: root.namePrefix+"-nvidia-low-latency"
            enabled: root.available
            opacity: enabled ? 1 : 0.4
            options: [{id:"0",label:qsTr("关")},{id:"1",label:qsTr("开")},{id:"2",label:qsTr("开＋增强")}]
            current: String(veyra.nvidiaLowLatency ?? 0)
            onPicked: id => veyra.nvidiaLowLatency=Number(id)
        }
    }
    Text {
        Layout.fillWidth: true
        text: veyra.running ? veyra.presentationStatus : qsTr("打开视频后生效")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
}
