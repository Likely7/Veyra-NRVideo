import QtQuick

VRow {
    label: qsTr("准心/HUD 保护")
    hint: qsTr("先从补帧输入去掉准心，再贴回增强后的准心。动态准心或第三人称游戏可在剔除面板画紧一点的框。")
    VSelect {
        title: qsTr("准心/HUD 保护")
        options: [
            {id:"0", label:qsTr("关闭")},
            {id:"1", label:qsTr("自动 · 画面中心")},
            {id:"2", label:qsTr("框内 · 使用剔除区域")}
        ]
        value: (options[veyra.hudProtect] || options[0]).label
        onPicked: id => veyra.hudProtect = Number(id)
    }
}
