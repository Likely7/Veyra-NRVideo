import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property var bridge: veyra
    Layout.fillWidth: true
    spacing: 8

    VRow {
        label: qsTr("准心/HUD 保护")
        hint: qsTr("从补帧输入移除识别到的准心，再贴回增强后的准心。")
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
}
