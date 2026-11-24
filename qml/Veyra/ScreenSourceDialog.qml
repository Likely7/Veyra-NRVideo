// screen dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: screenDialog
    objectName: "screen-dialog"
    key: "screen-viewing"
    glyph: "monitor"
    title: qsTr("屏幕捕获")
    sub: qsTr("把一个窗口或整块显示器作为片源")
    dialogWidth: 700
    actions: [
        { label: qsTr("取消") },
        { label: qsTr("开始捕获"), primary: true }
    ]
    onActionTriggered: label => {
        if (label !== qsTr("开始捕获")) { dialogHost.close(); return }
        if (veyra.startScreenCapture()) { dialogHost.close(); dialogHost.startScreen() }
    }
    Connections {
        target: dialogHost
        function onDialogChanged() { if (dialogHost.dialog === screenDialog.key) veyra.refreshCaptureTargets() }
    }
    readonly property var o: veyra.screenOptions
    // The chosen window is on another Windows virtual desktop (Win+Ctrl+D).
    readonly property bool otherDesktopTarget: {
        const targets = veyra.screenTargets, id = veyra.screenTargetId
        for (let i = 0; i < targets.length; ++i)
            if (targets[i].id === id) return targets[i].otherDesktop === true
        return false
    }
    // The chosen target's display is connected to another GPU than the one Veyra
    // processes on, so Windows copies every frame between the two (issue #107).
    readonly property var otherGpuTarget: {
        const targets = veyra.screenTargets, id = veyra.screenTargetId
        for (let i = 0; i < targets.length; ++i)
            if (targets[i].id === id) return targets[i].otherGpu === true ? targets[i] : null
        return null
    }

    DialogSection { text: qsTr("目标") }
    DialogGroup {
        VRow {
            label: qsTr("类型")
            VSeg {
                objectName: "screen-kind"
                options: [{ id: "0", label: qsTr("窗口") }, { id: "1", label: qsTr("显示器") }]
                current: String(screenDialog.o.kind)
                onPicked: id => veyra.setScreenOption("kind", Number(id))
            }
        }
        VRow {
            label: qsTr("捕获目标")
            hint: {
                if (veyra.screenTargets.length === 0) return qsTr("未找到可捕获的窗口或显示器")
                const lines = []
                if (screenDialog.otherDesktopTarget) lines.push(qsTr("这个窗口在其他虚拟桌面上，不用切换桌面即可捕获；部分程序在所在桌面不显示时会暂停绘制"))
                const gpu = screenDialog.otherGpuTarget
                if (gpu) lines.push(qsTr("这个目标所在的显示器接在 %1 上，Veyra 在 %2 上处理：每帧画面都要由 Windows 在两块显卡之间复制，延迟更高。双显卡分工时，建议把显示器接到处理用的显卡上（笔记本可开独显直连）").arg(gpu.displayGpu).arg(gpu.processingGpu))
                return lines.join("\n")
            }
            RowLayout {
                spacing: 6
                VSelect {
                    objectName: "screen-target"
                    implicitWidth: 300
                    value: veyra.screenTargetLabel
                    options: veyra.screenTargets
                    onPicked: id => veyra.screenTargetId = id
                }
                VButton { text: qsTr("刷新"); ghost: true; onClicked: veyra.refreshCaptureTargets() }
            }
        }
    }
    DialogSection { text: qsTr("采集") }
    DialogGroup {
        VRow {
            label: qsTr("采集方式")
            hint: screenDialog.o.kind === 1 ? qsTr("DXGI 为显示器兼容方式，无鼠标指针；捕获显示器时不显示黄色边框，Veyra 自己也不会出现在截图和录屏里") : qsTr("窗口只能用 Windows Graphics Capture")
            VSeg {
                enabled: screenDialog.o.kind === 1
                opacity: enabled ? 1 : 0.5
                options: [{ id: "0", label: "WGC" }, { id: "1", label: "DXGI" }]
                current: String(screenDialog.o.method)
                onPicked: id => veyra.setScreenOption("method", Number(id))
            }
        }
        VRow {
            label: qsTr("帧率上限")
            hint: qsTr("如果要开启 DLSS5（NR），建议帧率设置 60，减少 DLSS5 需要处理的画面量，减轻负担")
            hintColor: Theme.err
            hintBelow: true
            VSeg {
                options: [{ id: "0", label: qsTr("跟随") }, { id: "1", label: "30" }, { id: "2", label: "60" }, { id: "3", label: "120" }, { id: "4", label: "144" }, { id: "5", label: "240" }]
                current: String(screenDialog.o.fps)
                onPicked: id => veyra.setScreenOption("fps", Number(id))
            }
        }
        VRow {
            label: qsTr("显示鼠标指针")
            VSwitch {
                enabled: screenDialog.o.method === 0
                opacity: enabled ? 1 : 0.5
                checked: screenDialog.o.cursor === true
                onToggled: checked => veyra.setScreenOption("cursor", checked)
            }
        }
        VRow {
            label: qsTr("裁剪（像素）")
            hint: qsTr("左 / 上 / 右 / 下，按源像素")
            RowLayout {
                spacing: 4
                Repeater {
                    model: ["left", "top", "right", "bottom"]
                    delegate: DialogTextField {
                        required property string modelData
                        implicitWidth: 58
                        text: String(screenDialog.o[modelData])
                        onEdited: text => veyra.setScreenOption(modelData, Number(text))
                    }
                }
            }
        }
        VRow {
            label: qsTr("可视化裁剪")
            VButton {
                objectName: "screen-crop-preview"
                text: qsTr("预览并拖拽")
                enabled: veyra.screenTargetId.length > 0
                onClicked: {
                    if (!veyra.startScreenCapture(true)) return
                    dialogHost.close()
                    dialogHost.startScreenCrop()
                }
            }
        }
        VRow {
            label: qsTr("画面")
            VSeg {
                options: [{ id: "fit", label: qsTr("适应窗口") }, { id: "fill", label: qsTr("填满窗口") }]
                current: screenDialog.o.fill ? "fill" : "fit"
                onPicked: id => veyra.setScreenOption("fill", id === "fill")
            }
        }
    }
}
