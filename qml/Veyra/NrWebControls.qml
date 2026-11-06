import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var layerData
    signal edited(string key, real amount)
    Layout.fillWidth: true
    spacing: 8
    VRow {
        label: qsTr("本层 NR")
        hint: qsTr("开关对比本层效果，保留所有参数。")
        VSwitch {
            objectName: "nr-web-layer-enabled"
            checked: editor.layerData.enabled ?? false
            onToggled: checked => veyra.setEffectEnabled(editor.layerData.index, checked)
        }
    }
    Text { text: qsTr("变化网"); color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: 14 }
    NrWebPad {
        objectName: "nr-web-changes"
        Layout.fillWidth: true
        identity: String(editor.layerData.index) + ":" + editor.layerData.runtime
        values: editor.layerData
        axes: [{key:"darken",label:qsTr("暗化变化"),hint:qsTr("增加 NR 带来的暗化，不是直接调低画面亮度。"),min:0,max:2,defaultValue:1},
               {key:"brighten",label:qsTr("亮化变化"),hint:qsTr("增加 NR 带来的提亮；0 取消这部分变化。"),min:0,max:2,defaultValue:1},
               {key:"color",label:qsTr("色彩变化"),hint:qsTr("增加 NR 带来的颜色变化；0 取消这部分变化。"),min:0,max:2,defaultValue:1},
               {key:"luminance",label:qsTr("明度变化"),hint:qsTr("增加 NR 带来的明度变化；0 取消这部分变化。"),min:0,max:2,defaultValue:1}]
        onEdited: (key, value) => editor.edited(key, value)
    }
    VRow {
        label: qsTr("总变化强度")
        hint: qsTr("整体倍率 · 同时影响四个方向的变化")
        value: editor.layerData.total.toFixed(2)
        VSlider {
            objectName: "nr-web-total"
            implicitWidth: 110; from: 0; to: 5
            valueFromModel: true; resettable: true; defaultValue: 1
            value: editor.layerData.total
            onMoved: value => editor.edited("total", value)
        }
    }
    Text { text: qsTr("保护网"); color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: 14 }
    VRow {
        label: qsTr("开启调控")
        VSwitch {
            objectName: "nr-web-correctionEnabled"
            checked: editor.layerData.correctionEnabled ?? false
            onToggled: checked => editor.edited("correctionEnabled", checked ? 1 : 0)
        }
    }
    VRow {
        visible: editor.layerData.correctionEnabled ?? false
        label: qsTr("调控方式")
        VSeg {
            objectName: "nr-web-correctionAuto"
            options: [{id:"1",label:qsTr("自动")},{id:"0",label:qsTr("手动")}]
            current: (editor.layerData.correctionAuto ?? true) ? "1" : "0"
            onPicked: id => editor.edited("correctionAuto", Number(id))
        }
    }
    NrWebPad {
        objectName: "nr-web-protections"
        Layout.fillWidth: true
        visible: editor.layerData.correctionEnabled ?? false
        editable: !(editor.layerData.correctionAuto ?? true)
        opacity: editable ? 1 : 0.65
        identity: String(editor.layerData.index) + ":" + editor.layerData.runtime
        values: (editor.layerData.correctionAuto ?? true) ? (editor.layerData.correctionEffective ?? ({})) : editor.layerData
        axes: [{key:"colorRetention",label:qsTr("原图色彩保留"),hint:qsTr("保留原图色相与饱和度；1 为完全保留色彩"),min:0,max:1,defaultValue:0},
               {key:"luminanceRetention",label:qsTr("亮度保持"),hint:qsTr("减少 NR 明暗改动；1 为保留原图亮度"),min:0,max:1,defaultValue:0},
               {key:"shadowProtection",label:qsTr("暗部保护"),hint:qsTr("减少暗部被进一步压黑，保留提亮细节"),min:0,max:1,defaultValue:0},
               {key:"highlightProtection",label:qsTr("高光保护"),hint:qsTr("只压缩向白色溢出的变化，保留高光回暗"),min:0,max:1,defaultValue:1},
               {key:"localCompression",label:qsTr("局部压缩"),hint:qsTr("同时软化过大的亮度和颜色变化"),min:0,max:1,defaultValue:0.75},
               {key:"temporalStability",label:qsTr("时域稳定"),hint:qsTr("播放时减闪；暂停单帧不体现时域效果"),min:0,max:1,defaultValue:0.8}]
        onEdited: (key, value) => editor.edited(key, value)
    }
    Text {
        Layout.fillWidth: true
        visible: (editor.layerData.correctionEnabled ?? false) && (editor.layerData.correctionAuto ?? true)
        text: qsTr("自动模式显示实际保护值。切到手动后可拖动；原有手动数值仍保留。")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.Wrap
    }
    Text { text: qsTr("其余完整参数"); color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: 14 }
}
