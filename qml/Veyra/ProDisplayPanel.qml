// Professional-view displayCol controls; settings remain in the shared bridge.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

ColumnLayout {
    id: displayCol
    readonly property bool hdrMeterObserved: visible && hdrCurveAccordion.open && hdrReadAccordion.open && veyra.hdrCurve
    Layout.fillWidth: true
    spacing: 6
    VGroup {
        ProPresentationRows { }
        VRow {
            label: qsTr("原画 / 增强对比")
            hint: (veyra.compareMode === 2 ? qsTr("在画面上按住左键拖动分割线：左边原画、右边增强。") : "") + (veyra.compareMode !== 0 ? qsTr("对比时补帧暂停，关闭对比后恢复") : "")
            VSeg {
                objectName: "display-compare"
                options: [{ id: "0", label: qsTr("关闭", "off") }, { id: "2", label: qsTr("分屏") }, { id: "1", label: qsTr("只看原画") }]
                current: String(veyra.compareMode)
                onPicked: id => veyra.compareMode = Number(id)
            }
        }
        VRow {
            label: qsTr("对照底图")
            hint: qsTr("低延迟 NR 先行时，“增强前底图”是原图缩放，不是独立超分对照")
            visible: veyra.compareMode !== 0
            VSeg {
                objectName: "display-compare-base"
                options: [{ id: "0", label: qsTr("输入原画") }, { id: "1", label: qsTr("增强前底图") }]
                current: veyra.compareBase ? "1" : "0"
                onPicked: id => veyra.compareBase = id === "1"
            }
        }
        VRow {
            label: qsTr("按住 V 查看原画")
            hint: qsTr("按键可在 设置 → 快捷键 里改")
            VSwitch {
                objectName: "display-hold-compare"
                checked: veyra.preferences.holdCompare !== false
                onToggled: checked => veyra.setPreference("holdCompare", checked)
            }
        }
        VRow {
            label: qsTr("画面比例")
            hint: veyra.aspectMode === 1 ? qsTr("原始 = 一个输出像素对一个屏幕像素") : veyra.aspectMode === 2 ? qsTr("填满窗口，超出部分裁掉") : ""
            VSelect {
                objectName: "display-aspect"
                options: [{ id: "0", label: qsTr("适应") }, { id: "1", label: qsTr("原始像素") }, { id: "2", label: qsTr("填充裁切") }, { id: "3", label: qsTr("拉伸铺满") }, { id: "4", label: "16:9" }, { id: "5", label: "4:3" }, { id: "6", label: "21:9" }]
                value: options[veyra.aspectMode].label
                onPicked: id => veyra.aspectMode = Number(id)
            }
        }
        VRow {
            label: qsTr("强制 SDR 预览")
            hint: qsTr("HDR 片源也按 SDR 显示；与采集卡的“转为 SDR 显示”是同一设置")
            VSwitch {
                objectName: "display-force-sdr"
                checked: veyra.captureForceSdr
                onToggled: checked => veyra.captureForceSdr = checked
            }
        }
        VRow {
            label: qsTr("HDR 输出格式")
            hint: qsTr("补帧路径固定 HDR10；需要 Windows HDR 开启")
            VSeg { objectName: "display-hdr-output"; options: [{id:"0",label:"HDR10"},{id:"1",label:qsTr("scRGB 浮点")}]; current: String(veyra.hdrOutputMode); onPicked: id => veyra.hdrOutputMode = Number(id) }
        }

    // HDR 亮度曲线: a static curve. Unlike a plain exposure the
    // highlights are protected by construction.
    }
    VGroup {
        VRow {
            label: qsTr("提交帧率")
            value: veyra.submitFpsKnown ? veyra.submitFps.toFixed(2) : qsTr("未测量")
        }
        VRow {
            label: qsTr("1% low")
            hint: qsTr("最近 10 秒里最慢的 1% 提交间隔折算的帧率，和 RTSS 等叠加工具的算法相同")
            value: veyra.submitLowFpsKnown ? veyra.submitLowFps.toFixed(2) : "—"
        }
        VRow {
            label: qsTr("晚点 P95")
            value: veyra.lateP95Ms.toFixed(2) + " ms"
        }
        VRow {
            label: qsTr("调度等待 P95")
            value: veyra.scheduleP95Ms.toFixed(2) + " ms"
        }
    }
}
