import QtQuick

VRow {
    id: row
    required property var backend
    required property string namePrefix
    label: qsTr("动态 HUD 保护")
    hint: qsTr("识别固定边框内变化的数字和状态条 · 实验 · 默认关闭")
    visible: backend.hudAllEffects || backend.fgBackendName !== "xess"
    VSwitch {
        objectName: row.namePrefix + "-dynamic-hud"
        checked: row.backend.dynamicHud
        onToggled: checked => row.backend.dynamicHud = checked
    }
}
