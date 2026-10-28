// Three-step SR (user 2026-10-28): 先降到 -> 超分到 -> 再缩到, shared by the list
// page's SR card and the node page's SR node. The prescale and the output fit
// run on RTX Video SR only; with another backend those rows stay visible and
// disabled, with the reason.
import QtQuick
import QtQuick.Layouts
import "EffectSupport.js" as Support

ColumnLayout {
    id: editor
    property string namePrefix: "list"
    spacing: 6
    readonly property bool stageBackend: veyra.videoSrQuality >= 1 && veyra.videoSrQuality <= 4
    readonly property bool stageEnabled: stageBackend && Support.available(veyra, "sr")
    // SR target box height by the UI target IDs (1 2K, 2 4K, 3 8K, 4 5K, 5 6K, 6 7K).
    readonly property int targetHeight: ({1: 1440, 2: 2160, 3: 4320, 4: 2880, 5: 3456, 6: 4032})[veyra.srTargetIndex] || 2160
    // A manual prescale is stored as 100 + percent (50..90).
    readonly property bool manualPrescale: veyra.srPrescale >= 150 && veyra.srPrescale <= 190
    property int lastManualPercent: 75
    Connections {
        target: veyra
        function onSettingsChanged() { if (editor.manualPrescale) editor.lastManualPercent = veyra.srPrescale - 100 }
    }
    function sized(id, label, height) {
        const tooLarge = height >= targetHeight
        return {id: id, label: label, disabled: tooLarge, note: tooLarge ? qsTr("须小于超分目标") : ""}
    }
    readonly property var prescaleChoices: [
        {id: "0", label: qsTr("关闭", "off")}, sized("1", "720p", 720), sized("2", "900p", 900),
        sized("3", "1080p", 1080), sized("4", "1440p", 1440), {id: "manual", label: qsTr("手动")}
    ]
    readonly property var outputChoices: [
        {id: "0", label: qsTr("不缩")}, {id: "1", label: qsTr("片源尺寸")}, sized("2", "2K", 1440),
        sized("3", "4K", 2160), sized("4", "5K", 2880), sized("5", "6K", 3456), sized("6", "7K", 4032)
    ]

    VRow {
        label: qsTr("先降到")
        hint: editor.stageBackend
            ? qsTr("片源本身糊时，先缩小再超分，RTX 视频超分更容易重建细节；会损失片源中真实的高分辨率细节（如原生 4K 的 HUD、字幕）。")
            : qsTr("目前仅 RTX 视频超分支持")
        enabled: editor.stageEnabled
        opacity: enabled ? 1 : 0.4
        VSelect {
            objectName: editor.namePrefix + "-sr-prescale"
            options: editor.prescaleChoices
            value: editor.manualPrescale ? qsTr("手动") : (options.find(o => o.id === String(veyra.srPrescale))?.label || "")
            onPicked: id => veyra.srPrescale = id === "manual" ? 100 + editor.lastManualPercent : Number(id)
        }
    }
    VRow {
        objectName: editor.namePrefix + "-sr-prescale-percent-row"
        visible: editor.manualPrescale
        enabled: editor.stageEnabled
        opacity: enabled ? 1 : 0.4
        label: qsTr("先降比例")
        hint: qsTr("松开后应用 · 宽高同比例")
        value: Math.round(prescaleSlider.displayedValue) + "%"
        VSlider {
            id: prescaleSlider
            objectName: editor.namePrefix + "-sr-prescale-percent"
            valueFromModel: true
            resettable: true; defaultValue: 75
            implicitWidth: 110
            from: 50; to: 90; stepSize: 5
            live: false // SR sizes rebuild the graph: commit once on release.
            value: editor.manualPrescale ? veyra.srPrescale - 100 : editor.lastManualPercent
            onMoved: value => veyra.srPrescale = 100 + Math.round(value)
        }
    }
    VRow {
        label: qsTr("超分到")
        hint: editor.stageBackend
            ? qsTr("放大时重建细节；原尺寸时修复视频文件和串流的压缩伪影，无压缩的采集卡格式在原尺寸不运行；开启先降到后，片源已达目标也会执行。效果取决于片源。")
            : qsTr("目标须大于片源尺寸；已达到目标时不执行超分。")
        VSeg {
            objectName: editor.namePrefix + "-sr-target"
            options: [{ id: "1", label: "2K" }, { id: "2", label: "4K" }, { id: "4", label: "5K" }, { id: "5", label: "6K" }, { id: "6", label: "7K" }, { id: "3", label: "8K" }]
            current: String(veyra.srTargetIndex)
            onPicked: id => veyra.srTargetIndex = parseInt(id)
        }
    }
    VRow {
        label: qsTr("再缩到")
        hint: editor.stageBackend
            ? qsTr("超分到更大尺寸后按面积缩回，相当于超采样；只在超分放大后执行。8K 超分耗时显著增加。")
            : qsTr("目前仅 RTX 视频超分支持")
        enabled: editor.stageEnabled
        opacity: enabled ? 1 : 0.4
        VSelect {
            objectName: editor.namePrefix + "-sr-output"
            options: editor.outputChoices
            value: options.find(o => o.id === String(veyra.srOutput))?.label || ""
            onPicked: id => veyra.srOutput = Number(id)
        }
    }
    VRow {
        objectName: editor.namePrefix + "-sr-stage-actual"
        visible: veyra.srStageSummary.length > 0 || veyra.srStageNote.length > 0
        label: qsTr("实际尺寸")
        hint: veyra.srStageNote
        Text {
            text: veyra.srStageSummary
            color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 11
            wrapMode: Text.WordWrap
            Layout.maximumWidth: 240
        }
    }
}
