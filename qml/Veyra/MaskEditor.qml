import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    property var bridge: veyra
    property bool hud: false
    property string namePrefix: "protection"
    readonly property var maskState: hud ? bridge.exclusionState : bridge.protectionState
    readonly property var regions: maskState.regions || []
    readonly property string drawShape: maskState.drawShape || ""
    Layout.fillWidth: true
    spacing: 8
    Text {
        Layout.fillWidth: true
        text: editor.drawShape.length>0
              ? qsTr("在画面上拖出区域，Esc 取消")
              : editor.hud
                ? qsTr("框住准心或 HUD，最多 4 个区域。背景继续补帧，画质增强保留。")
                : qsTr("区域内排除 NR 处理。最多 4 个区域。")
        wrapMode: Text.WordWrap
        color: editor.drawShape.length>0?Theme.accent:Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 11
    }
    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        VButton {
            objectName: editor.namePrefix+"-draw-rect"
            text: qsTr("画矩形")
            primary: editor.drawShape==="rect"
            onClicked: editor.bridge.beginMaskDraw(editor.hud,editor.drawShape==="rect"?"":"rect")
        }
        VButton {
            objectName: editor.namePrefix+"-draw-ellipse"
            text: qsTr("画圆形")
            primary: editor.drawShape==="ellipse"
            onClicked: editor.bridge.beginMaskDraw(editor.hud,editor.drawShape==="ellipse"?"":"ellipse")
        }
        Item { Layout.fillWidth: true }
        VButton {
            text: qsTr("清空")
            ghost: true
            visible: editor.regions.length>0
            onClicked: editor.bridge.clearMaskRegions(editor.hud)
        }
    }
    VRow {
        visible: editor.hud
        label: qsTr("显示区域红框")
        hint: qsTr("选中区域后可拖动位置；红框不会写入录制或导出")
        VSwitch {
            objectName: editor.namePrefix+"-show-regions"
            checked: editor.bridge.exclusionOverlayVisible
            onToggled: checked => editor.bridge.exclusionOverlayVisible = checked
        }
    }
    Repeater {
        model: editor.regions
        delegate: ColumnLayout {
            id: regionRow
            required property var modelData
            required property int index
            Layout.fillWidth: true
            spacing: 4
            VRow {
                label: (regionRow.modelData.ellipse?qsTr("圆形 "):qsTr("矩形 "))+(regionRow.index+1)
                value: Math.round(regionRow.modelData.left*100)+","+Math.round(regionRow.modelData.top*100)+" → "
                       +Math.round(regionRow.modelData.right*100)+","+Math.round(regionRow.modelData.bottom*100)+" %"
                Row {
                    spacing: 6
                    VButton {
                        visible: editor.hud
                        objectName: editor.namePrefix+"-select-"+regionRow.index
                        text: editor.maskState.selected === regionRow.modelData.index ? qsTr("已选中") : qsTr("选中")
                        primary: editor.maskState.selected === regionRow.modelData.index
                        onClicked: editor.bridge.selectExclusionRegion(regionRow.modelData.index)
                    }
                    VButton {
                        objectName: editor.namePrefix+"-remove-"+regionRow.index
                        icon: true; ghost: true; iconName: "x"
                        onClicked: editor.bridge.removeMaskRegion(editor.hud,regionRow.modelData.index)
                    }
                }
            }
        }
    }
    VRow {
        label: qsTr("羽化像素")
        value: Number(editor.maskState.feather || 0).toFixed(0)+" px"
        VSlider {
            objectName: editor.namePrefix+"-feather"
            implicitWidth: 110
            valueFromModel: true
            from: 0; to: 64
            value: editor.maskState.feather || 0
            onMoved: value=>editor.bridge.setMaskFeather(editor.hud,Math.round(value))
        }
    }
}
