// One anti-aliasing instance's parameters (#53), shared by the list page's
// 抗锯齿 card and the node page's anti-aliasing node, so both edit the same
// values through the same bridge calls (as NrLayerEditor does for NR).
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    // veyra.antiAliasParams (list card) or veyra.antiAliasParamsAt(index) (node).
    required property var params
    property string namePrefix: "aa"
    signal edited(string key, double amount)
    spacing: 6
    readonly property int method: params.method ?? 1
    readonly property int quality: params.quality ?? 2
    readonly property int temporal: params.temporal ?? 0
    readonly property var qualityChoices: method === 0
        ? [{id: "0", label: qsTr("中")}, {id: "1", label: qsTr("高")}, {id: "2", label: qsTr("极高")}]
        : [{id: "0", label: qsTr("低")}, {id: "1", label: qsTr("中")}, {id: "2", label: qsTr("高")}, {id: "3", label: qsTr("极高")}]

    Text {
        Layout.fillWidth: true
        text: qsTr("在源分辨率去除锯齿边缘，适合游戏画面、文字和界面线条；放在超分之前，锯齿不会被一起放大。")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.WordWrap
    }
    VRow {
        label: qsTr("方式")
        hint: editor.method === 0 ? qsTr("FXAA：最快，整体略柔")
            : editor.method === 1 ? qsTr("SMAA：边缘判断更准，细节保留更好")
            : qsTr("MLAA：形态学抗锯齿，阈值与强度可调")
        VSeg {
            objectName: editor.namePrefix + "-method"
            options: [{id: "0", label: "FXAA"}, {id: "1", label: "SMAA"}, {id: "2", label: "MLAA"}]
            current: String(editor.method)
            onPicked: id => editor.edited("method", Number(id))
        }
    }
    VRow {
        label: qsTr("质量")
        visible: editor.method !== 2
        VSeg {
            objectName: editor.namePrefix + "-quality"
            options: editor.qualityChoices
            current: String(editor.quality)
            onPicked: id => editor.edited("quality", Number(id))
        }
    }
    VRow {
        label: qsTr("边缘阈值")
        hint: qsTr("亮度差超过它才算边缘；越低，处理的边缘越多")
        visible: editor.method === 2
        value: Number(editor.params.mlaaThreshold ?? 0.08).toFixed(2)
        VSlider {
            objectName: editor.namePrefix + "-mlaa-threshold"
            implicitWidth: 110
            valueFromModel: true
            resettable: true; defaultValue: 0.08
            from: 0.02; to: 0.30
            value: editor.params.mlaaThreshold ?? 0.08
            onMoved: value => editor.edited("mlaaThreshold", Math.round(value * 100) / 100)
        }
    }
    VRow {
        label: qsTr("强度")
        visible: editor.method === 2
        value: Number(editor.params.mlaaStrength ?? 1).toFixed(2)
        VSlider {
            objectName: editor.namePrefix + "-mlaa-strength"
            implicitWidth: 110
            valueFromModel: true
            resettable: true; defaultValue: 1
            from: 0; to: 1
            value: editor.params.mlaaStrength ?? 1
            onMoved: value => editor.edited("mlaaStrength", Math.round(value * 20) / 20)
        }
    }
    VRow {
        label: qsTr("时域累积")
        hint: qsTr("实验：采集画面无法抖动，这是 SMAA T2x / 4x 的不抖动近似；静止画面更稳，运动处可能拖影")
        visible: editor.method === 1
        VSeg {
            objectName: editor.namePrefix + "-temporal"
            options: [{id: "0", label: qsTr("关闭")}, {id: "1", label: "T2x"}, {id: "2", label: "4x"}]
            current: String(editor.temporal)
            onPicked: id => editor.edited("temporal", Number(id))
        }
    }
    VRow {
        label: qsTr("历史权重")
        hint: qsTr("越高越平滑，运动拖影也越多")
        visible: editor.method === 1 && editor.temporal > 0
        value: Number(editor.params.historyWeight ?? 0.5).toFixed(2)
        VSlider {
            objectName: editor.namePrefix + "-history-weight"
            implicitWidth: 110
            valueFromModel: true
            resettable: true; defaultValue: editor.temporal === 2 ? 0.75 : 0.5
            from: 0; to: 0.95
            value: editor.params.historyWeight ?? 0.5
            onMoved: value => editor.edited("historyWeight", Math.round(value * 20) / 20)
        }
    }
    VRow {
        label: qsTr("历史拒绝")
        hint: qsTr("画面变化超过时丢弃历史；越高越不容易拖影")
        visible: editor.method === 1 && editor.temporal > 0
        value: Number(editor.params.historyRejection ?? 8).toFixed(1)
        VSlider {
            objectName: editor.namePrefix + "-history-rejection"
            implicitWidth: 110
            valueFromModel: true
            resettable: true; defaultValue: 8
            from: 0; to: 32
            value: editor.params.historyRejection ?? 8
            onMoved: value => editor.edited("historyRejection", Math.round(value * 2) / 2)
        }
    }
}
