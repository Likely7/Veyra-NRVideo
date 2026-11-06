import QtQuick
import QtQuick.Layouts
ColumnLayout {
    id: root
    property string namePrefix: "list"
    readonly property bool available: veyra.nvidiaLowLatencyAvailable === true
    readonly property bool boostWarning: available && veyra.nvidiaLowLatency === 2
    Layout.fillWidth: true
    spacing: 4
    VRow {
        objectName: root.namePrefix+"-nvidia-low-latency-row"
        label: qsTr("英伟达低延迟")
        hintBelow: true
        // Field 2026-11-02 (RTX 5090, NR + VFG): Boost made the driver hold the
        // per-source Sleep 4-18 ms and 8-21% of generated frames missed their slot.
        hint: !root.available ? qsTr("需要 NVIDIA 显卡") : root.boostWarning ? qsTr("减少处理排队。高负载时建议选「开」而不是「开＋增强」：增强有概率导致掉帧，还会提高 GPU 频率、增加功耗。") : ""
        hintColor: root.boostWarning ? Theme.err : Theme.t3
        VSeg {
            objectName: root.namePrefix+"-nvidia-low-latency"
            enabled: root.available
            opacity: enabled ? 1 : 0.4
            options: [{id:"0",label:qsTr("关")},{id:"1",label:qsTr("开")},{id:"2",label:qsTr("开＋增强")}]
            // Another adapter shows 关: the NVIDIA choice is kept for the next
            // NVIDIA session but does nothing there.
            current: root.available ? String(veyra.nvidiaLowLatency ?? 0) : "0"
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
