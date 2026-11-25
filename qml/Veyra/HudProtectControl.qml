import QtQuick
import QtQuick.Layouts

VSubGroup {
    id: root
    property var bridge: veyra
    property string namePrefix: objectName
    Layout.fillWidth: true
    label: qsTr("HUD 保护")
    expanded: false
    VRow {
        label: qsTr("剔除所有效果")
        hintBelow: true
        hint: qsTr("识别到的 HUD 显示原画，保留文字和准星的清晰度。只开画质增强时也可使用。")
        VSwitch {
            objectName: root.namePrefix + "-hud-all-effects"
            checked: root.bridge.hudAllEffects
            onToggled: checked => root.bridge.hudAllEffects = checked
        }
    }
    VRow {
        label: qsTr("HUD 自动锁定")
        visible: root.bridge.hudAllEffects || root.bridge.fgBackendName !== "xess"
        hint: qsTr("自动识别画面中持续静止的文字和边缘。")
        VSwitch {
            objectName: root.namePrefix + "-hud-lock-switch"
            checked: root.bridge.hudLock
            onToggled: checked => root.bridge.hudLock = checked
        }
    }
    VRow {
        label: qsTr("准心/HUD 保护")
        hint: qsTr("可自动识别中心准星，也可在指定区域内识别。")
        VSelect {
            objectName: root.objectName + "-mode"
            title: qsTr("准心/HUD 保护")
            options: [
                {id:"0", label:qsTr("关闭")},
                {id:"1", label:qsTr("自动 · 画面中心")},
                {id:"2", label:qsTr("框内 · 手绘区域")}
            ]
            value: (options[root.bridge.hudProtect] || options[0]).label
            onPicked: id => root.bridge.hudProtect = Number(id)
        }
    }
    Loader {
        Layout.fillWidth: true
        active: root.bridge.hudProtect === 2
        visible: active
        sourceComponent: MaskEditor {
            bridge: root.bridge
            hud: true
            namePrefix: root.objectName
        }
    }
    VRow {
        label: qsTr("游戏字幕防拖影")
        hint: qsTr("识别画面下方的内嵌字幕，补帧前清除字形，生成后贴回。")
        VSwitch {
            objectName: root.namePrefix + "-subtitle-hold"
            checked: root.bridge.subtitleHold === true
            onToggled: on => root.bridge.subtitleHold = on
        }
    }
    DynamicHudRow { backend: root.bridge; namePrefix: root.namePrefix }
}
