import QtQuick
import QtQuick.Layouts

VRow {
    id: row
    required property var backend
    property string namePrefix: "list"
    property bool showHelp: true
    property int lastMode: 1
    readonly property var modes: [
        {id:"6",label:"60→30"},
        {id:"5",label:qsTr("60→30（兼容自动去重）")},
        {id:"1",label:qsTr("自动去除")}
    ]
    readonly property int currentMode: {
        const saved = Number(backend?.contentRate || 0)
        if (modes.some(option => Number(option.id) === saved)) return saved
        return saved >= 2 && saved <= 4 ? 1 : 0
    }
    onCurrentModeChanged: if(currentMode!==0)lastMode=currentMode
    Component.onCompleted: if(currentMode!==0)lastMode=currentMode
    label: qsTr("去除重复帧")
    hint: {
        if (!showHelp) return ""
        if (currentMode === 1 && backend?.captureHalfRateSuggested)
            return qsTr("检测到约两帧一组的画面。主机锁定 30 帧时，可试用 60→30 保持固定节奏。")
        return qsTr("60→30 每两帧取一帧，保持约 30 帧的节奏，游戏低于 30 帧时可能顿卡。兼容自动去重会跳过短时间重复的画面，菜单可能低于 30 帧。自动去除按画面变化识别帧率。")
    }
    hintBelow: true
    RowLayout {
        spacing: 8
        VSwitch {
            objectName: row.namePrefix+"-dedup-enabled"
            checked: row.currentMode!==0
            onToggled: checked=>row.backend.contentRate=checked?row.lastMode:0
        }
        VSelect {
            objectName: row.namePrefix+"-content-rate"
            implicitWidth: Math.min(220, Math.max(100, row.width - 48))
            title: qsTr("去除重复帧")
            enabled: row.currentMode!==0
            options: row.modes
            value: (row.modes.find(option=>Number(option.id)===(row.currentMode||row.lastMode))||row.modes[2]).label
            onPicked: id=>{
                const mode=Number(id)
                if(row.modes.some(option => option.id === id))row.backend.contentRate=mode
            }
        }
    }
}
