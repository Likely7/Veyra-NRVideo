import QtQuick

VRow {
    id: row
    required property var backend
    required property string namePrefix
    label: qsTr("动态 HUD 保护")
    hint: qsTr("识别有固定边框的数字和状态条，补帧时保留真实画面中的内容 · 实验 · 默认关闭")
    visible: backend.fgBackendName !== "xess"
    VSwitch {
        objectName: row.namePrefix + "-dynamic-hud"
        checked: row.backend.dynamicHud
        onToggled: checked => row.backend.dynamicHud = checked
    }
}
