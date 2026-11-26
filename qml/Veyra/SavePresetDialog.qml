// save dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: saveDialog
    objectName: "preset-save-dialog"
    key: "save"
    glyph: "plus"
    readonly property string kindLabel: veyra.nodeMode === 1 ? qsTr("节点") : qsTr("列表")
    readonly property string otherKindLabel: veyra.nodeMode === 1 ? qsTr("列表") : qsTr("节点")
    title: qsTr("另存为%1预设").arg(kindLabel)
    sub: qsTr("%1预设和%2预设分开保存，互不影响；应用时只覆盖勾选的部分").arg(kindLabel).arg(otherKindLabel)
    dialogWidth: 580
    // presets.js currentItems: what the chain holds right now, one row per
    // node, with the parameters that matter.
    function nodeSummary(n) {
        switch (n.type) {
        case "sr": return veyra.srTargetLabel + (veyra.srQualityLabel(veyra.videoSrQuality, true) ? " · " + veyra.srQualityLabel(veyra.videoSrQuality, true) : "")
        case "nr": { const l = veyra.nrLayers.find(x => x.index === n.index); return l ? qsTr("强度 ") + l.intensity.toFixed(2) : "NR" }
        case "video-hdr": return qsTr("峰值 ") + (veyra.videoHdrParams.peakNits || 1000) + " nits"
        case "protection": return veyra.protectionRegions.length + qsTr(" / 4 个区域")
        case "color": return qsTr("调色参数")
        case "frame-generation": return veyra.fgMultiplier + "X · " + (veyra.fgBackendChoices.find(x => x.id === veyra.fgBackendName) || {label: "FG"}).label
        }
        return ""
    }
    function nodeLabel(n) {
        if (n.type !== "nr") return n.label
        let k = 0
        for (const m of veyra.chain) { if (m.type === "nr") ++k; if (m.index === n.index) break }
        return qsTr("NR 层 ") + k
    }
    readonly property bool nameTaken: presetNameField.text.trim().length > 0 &&
        veyra.presets.some(p => p.name === presetNameField.text.trim())
    property var chosen: ({})
    property var parts: []
    property bool saveDefault: false
    function resetForOpen() {
        const current = veyra.presetSaveParts
        const selected = {}
        for (const part of current) selected[part.id] = part.meaningful === true
        parts = current
        chosen = selected
        presetNameField.text = ""
        saveNote.text = ""
        saveDefault = false
    }
    actions: [
        { label: qsTr("取消") },
        { label: qsTr("保存预设"), primary: true, icon: "check" }
    ]
    onActionTriggered: label => {
        if (label !== qsTr("保存预设")) { dialogHost.close(); return }
        const name = presetNameField.text.trim()
        if (name.length === 0) { saveNote.text = qsTr("预设需要一个名字"); return }
        let mask = 0
        const ids = ["chain", "color", "fg", "audio", "flow"]
        for (const part of parts) {
            const bit = ids.indexOf(part.id)
            if (bit >= 0 && chosen[part.id] === true) mask |= (1 << bit)
        }
        if (mask === 0) { saveNote.text = qsTr("至少选择一项内容"); return }
        if (veyra.savePresetAs(name, mask, veyra.nodeMode === 1)) {
            if (saveAsDefault.checked) {
                const saved = veyra.presets.find(p => p.name === name && p.nodeMode === (veyra.nodeMode === 1))
                if (saved) veyra.setDefaultPreset(saved.index)
            }
            dialogHost.close()
        }
        else saveNote.text = qsTr("保存失败：名称可能已存在")
    }

    // .namebox: the name first, with a live note under it.
    ColumnLayout {
        Layout.fillWidth: true
        Layout.topMargin: 4
        spacing: 6
        Text { text: qsTr("预设名称"); color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall }
        DialogTextField {
            id: presetNameField
            objectName: "preset-name"
            Layout.fillWidth: true
            placeholder: qsTr("例如：夜间游戏")
        }
        Text {
            objectName: "preset-name-hint"
            Layout.fillWidth: true
            text: saveDialog.nameTaken ? qsTr("已有同名预设，换一个名称才能保存")
                  : presetNameField.text.trim().length > 0 ? qsTr("可以保存")
                  : qsTr("名称会显示在极简模式和导出页的预设菜单里")
            color: saveDialog.nameTaken ? Theme.warn : (presetNameField.text.trim().length > 0 ? Theme.ok : Theme.t3)
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsEyebrow
        }
    }
    DialogSection { text: qsTr("将保存的内容 · 当前正在使用的%1设置").arg(saveDialog.kindLabel) }
    // .savelist: every node, a green dot when it runs; the list dims when
    // the effect chain itself is not part of the preset.
    ColumnLayout {
        objectName: "preset-save-list"
        Layout.fillWidth: true
        spacing: 4
        opacity: saveDialog.chosen["chain"] === true ? 1 : Theme.disabledOpacity
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        Repeater {
            model: saveDialog.shown ? veyra.chain : []
            delegate: Rectangle {
                required property var modelData
                Layout.fillWidth: true
                implicitHeight: 34
                radius: Theme.rCard
                color: Theme.hoverFill
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 10
                    Rectangle {
                        implicitWidth: 7; implicitHeight: 7; radius: 3.5
                        color: modelData.enabled ? Theme.ok : Theme.t3
                    }
                    Text {
                        Layout.preferredWidth: 110
                        text: saveDialog.nodeLabel(modelData)
                        color: modelData.enabled ? Theme.t1 : Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall
                        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: saveDialog.nodeSummary(modelData)
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: Theme.fsEyebrow
                        elide: Text.ElideRight
                    }
                    VTag { visible: !modelData.enabled; text: qsTr("已关闭") }
                }
            }
        }
    }
    DialogSection { text: qsTr("选项") }
    DialogGroup {
        Repeater {
            model: saveDialog.shown ? saveDialog.parts : []
            delegate: VRow {
                required property var modelData
                label: qsTr("包含%1").arg(modelData.label)
                hint: modelData.summary
                VSwitch {
                    objectName: "preset-part-" + modelData.id
                    // A part at its default is still offered, but starts off: a
                    // preset that stored nothing would be a surprise.
                    checked: saveDialog.chosen[modelData.id] === true
                    onToggled: checked => {
                        const c = Object.assign({}, saveDialog.chosen)
                        c[modelData.id] = checked
                        saveDialog.chosen = c
                    }
                }
            }
        }
        VRow {
            label: qsTr("保存后设为启动默认")
            hint: qsTr("下次启动软件时自动应用这个预设")
            VSwitch { id: saveAsDefault; objectName: "preset-save-default"; checked: saveDialog.saveDefault; onToggled: v => saveDialog.saveDefault = v }
        }
    }
    Text {
        id: saveNote
        objectName: "preset-save-error"
        Layout.fillWidth: true
        Layout.topMargin: 6
        text: ""
        color: Theme.warn
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
        wrapMode: Text.WordWrap
    }
}
