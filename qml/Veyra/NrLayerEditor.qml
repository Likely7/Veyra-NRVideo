// One NR layer's full parameter set, shared by the list page's NR card and the
// node page's NR node so both edit exactly the same values through the same
// bridge calls (user decision 2026-09-28: node parameters equal to the list).
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    required property var layerData
    property int layerCount: 1
    property bool nrFirst: false
    // The list page owns copy/delete and the list-wide NR->SR order; the node
    // page uses its node menu and wiring instead.
    property bool listControls: true
    signal edited(int nodeIndex, string key, double amount)
    signal duplicateRequested(int nodeIndex)
    signal removeRequested(int nodeIndex)
    signal orderEdited(bool enabled)
    readonly property var sizeChoices: [
        {id:"2",label:"480p"}, {id:"3",label:"720p"}, {id:"4",label:"900p"},
        {id:"0",label:qsTr("1080p · 默认")}, {id:"5",label:"1440p"}, {id:"1",label:qsTr("原生")}
    ]
    readonly property string sizeLabel: sizeChoices.find(o => Number(o.id) === layerData.sizePolicy)?.label ?? qsTr("未知")
    spacing: 6

    component NrValueRow: VRow {
        id: valueRow
        required property var spec
        required property double amount
        signal edited(double amount)
        label: spec.label
        value: amount.toFixed(2)
        VSlider {
            objectName: "nr-" + valueRow.spec.key
            valueFromModel: true
            resettable: true; defaultValue: valueRow.spec.key === "skin" ? -1 : 1
            implicitWidth: 110
            from: valueRow.spec.min; to: valueRow.spec.max
            value: valueRow.amount
            onMoved: value => valueRow.edited(value)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: editor.listControls
        VButton {
            objectName: "nr-copy"
            text: qsTr("复制此层"); enabled: editor.layerCount < 4
            onClicked: editor.duplicateRequested(editor.layerData.index)
        }
        VButton {
            objectName: "nr-reset"
            text: qsTr("恢复默认"); ghost: true
            onClicked: veyra.resetNrLayer(editor.layerData.index)
        }
        VButton {
            objectName: "nr-remove"
            text: qsTr("删除此层")
            onClicked: editor.removeRequested(editor.layerData.index)
        }
    }
    VRow {
        label: qsTr("NR 版本")
        hint: qsTr("全链共享 · 切换会重建 NR 管线")
        VSelect {
            objectName: "nr-runtime"
            value: editor.layerData.runtime === 2 ? "RTX 20–50 · SF-v2" : "RTX 50 · Lecram"
            options: [{id:"0",label:"RTX 50 · Lecram"},{id:"2",label:"RTX 20–50 · SF-v2"}]
            onPicked: id => editor.edited(editor.layerData.index, "runtime", Number(id))
        }
    }
    VRow {
        label: qsTr("NR 运动来源")
        hint: qsTr("全链共享；抗闪烁开启时仍需估算光流")
        VSeg {
            objectName: "nr-motion-source"
            options: [{id:"0",label:qsTr("零运动")},{id:"1",label:qsTr("光流")}]
            current: String(veyra.nrMotionSource)
            onPicked: id => veyra.nrMotionSource = Number(id)
        }
    }
    VRow {
        label: qsTr("内部处理分辨率")
        hint: qsTr("内部降采样 · 不改变输出尺寸")
        VSelect {
            objectName: "nr-resolution"
            value: editor.sizeLabel; options: editor.sizeChoices
            onPicked: id => editor.edited(editor.layerData.index, "sizePolicy", Number(id))
        }
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("预览保留比例且不放大小输入；图片/视频导出仍完整处理。")
        color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
    NrValueRow {
        spec: ({key:"intensity",label:qsTr("模型强度"),min:0,max:1})
        amount: editor.layerData.intensity
        onEdited: amount => editor.edited(editor.layerData.index, "intensity", amount)
    }
    VSubGroup {
        objectName: "nr-model-group"
        Layout.fillWidth: true
        label: qsTr("模型参数"); count: 6
        Repeater {
            model: [{key:"tone",label:qsTr("局部明暗"),min:0,max:1},
                    {key:"structure",label:qsTr("局部结构"),min:0,max:1}]
            delegate: NrValueRow {
                required property var modelData
                spec: modelData; amount: editor.layerData[modelData.key]
                onEdited: amount => editor.edited(editor.layerData.index, modelData.key, amount)
            }
        }
        VRow {
            label: qsTr("肤质 · 未证实")
            VSelect {
                objectName: "nr-skin-mode"
                value: editor.layerData.skin < 0 ? qsTr("未指定") : qsTr("自定义")
                options: [{id:"-1",label:qsTr("未指定")},{id:"0",label:qsTr("自定义")}]
                onPicked: id => editor.edited(editor.layerData.index,"skin",Number(id))
            }
        }
        NrValueRow {
            visible: editor.layerData.skin >= 0
            spec: ({key:"skin",label:qsTr("肤质数值"),min:0,max:2})
            amount: Math.max(0,editor.layerData.skin)
            onEdited: amount => editor.edited(editor.layerData.index,"skin",amount)
        }
        VRow {
            label: qsTr("风格 · 实验")
            VSeg {
                objectName: "nr-style"
                options: [{id:"0",label:"0"},{id:"1",label:"1"},{id:"2",label:"2"}]
                current: String(editor.layerData.style)
                onPicked: id => editor.edited(editor.layerData.index,"style",Number(id))
            }
        }
        VRow {
            label: qsTr("自动遮罩 · 实验")
            VSwitch {
                objectName: "nr-autoMask"
                checked: editor.layerData.autoMask
                onToggled: checked => editor.edited(editor.layerData.index,"autoMask",checked?1:0)
            }
        }
        VRow {
            label: qsTr("UI 修正 · 未证实")
            VSwitch {
                objectName: "nr-uiCorrection"
                checked: editor.layerData.uiCorrection
                onToggled: checked => editor.edited(editor.layerData.index,"uiCorrection",checked?1:0)
            }
        }
    }
    VSubGroup {
        objectName: "nr-residual-group"
        Layout.fillWidth: true
        label: qsTr("增强变化量"); count: 5
        Repeater {
            model: [{key:"total",label:qsTr("总变化强度"),min:0,max:2},
                    {key:"darken",label:qsTr("暗化变化"),min:0,max:2},
                    {key:"brighten",label:qsTr("亮化变化"),min:0,max:2},
                    {key:"color",label:qsTr("色彩变化"),min:0,max:2},
                    {key:"luminance",label:qsTr("明度变化"),min:0,max:2}]
            delegate: NrValueRow {
                required property var modelData
                spec: modelData; amount: editor.layerData[modelData.key]
                onEdited: amount => editor.edited(editor.layerData.index,modelData.key,amount)
            }
        }
    }
    VSubGroup {
        objectName: "nr-experimental-group"
        Layout.fillWidth: true
        label: qsTr("实验"); count: editor.layerData.temporal ? 3 : 2
        VRow {
            label: qsTr("时间域防闪烁")
            hint: qsTr("需要光流；关掉则不做时域累积")
            VSwitch {
                objectName: "nr-temporal"
                checked: editor.layerData.temporal
                onToggled: checked => editor.edited(editor.layerData.index,"temporal",checked?1:0)
            }
        }
        VRow {
            objectName: "nr-antiflicker-row"
            // Only meaningful when temporal accumulation is on; the tier picks
            // how the accumulation behaves, not whether it runs.
            visible: editor.layerData.temporal
            label: qsTr("抗闪烁档位")
            hint: qsTr("静态最稳、光流为默认、低频保留高频细节")
            VSelect {
                objectName: "nr-antiflicker"
                options: [
                    {id:"1",label:qsTr("静态累积")},
                    {id:"2",label:qsTr("光流累积 · 默认")},
                    {id:"3",label:qsTr("光流累积+")},
                    {id:"4",label:qsTr("低频时域重建")}
                ]
                value: options.find(o => Number(o.id) === editor.layerData.antiFlicker)?.label ?? qsTr("光流累积 · 默认")
                onPicked: id => editor.edited(editor.layerData.index,"antiFlicker",Number(id))
            }
        }
        VRow {
            visible: editor.listControls
            label: qsTr("NR → SR · 全层")
            hint: qsTr("低延迟顺序，仅预览、全层同步")
            VSwitch {
                checked: editor.nrFirst
                onToggled: checked => editor.orderEdited(checked)
            }
        }
    }
}
