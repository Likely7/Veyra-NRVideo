import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: editor
    property bool allEffects: false
    property string namePrefix: allEffects ? "effect-exclusion" : "protection"
    readonly property var maskState: allEffects ? veyra.exclusionState : veyra.protectionState
    readonly property var regions: maskState.regions || []
    readonly property string drawShape: maskState.drawShape || ""
    Layout.fillWidth: true
    spacing: 8
    Text {
        Layout.fillWidth: true
        text: editor.drawShape.length>0
              ? qsTr("在画面上拖出区域，Esc 取消")
              : editor.allEffects
                ? qsTr("最多 4 个区域。只想减少准心拖影时，推荐选「准心/HUD」。")
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
            onClicked: veyra.beginMaskDraw(editor.allEffects,editor.drawShape==="rect"?"":"rect")
        }
        VButton {
            objectName: editor.namePrefix+"-draw-ellipse"
            text: qsTr("画圆形")
            primary: editor.drawShape==="ellipse"
            onClicked: veyra.beginMaskDraw(editor.allEffects,editor.drawShape==="ellipse"?"":"ellipse")
        }
        Item { Layout.fillWidth: true }
        VButton {
            text: qsTr("清空")
            ghost: true
            visible: editor.regions.length>0
            onClicked: veyra.clearMaskRegions(editor.allEffects)
        }
    }
    VRow {
        visible: editor.allEffects
        label: qsTr("显示区域红框")
        hint: qsTr("选中区域后可拖动位置；红框不会写入录制或导出")
        VSwitch {
            objectName: editor.namePrefix+"-show-regions"
            checked: veyra.exclusionOverlayVisible
            onToggled: checked => veyra.exclusionOverlayVisible = checked
        }
    }
    Text {
        Layout.fillWidth: true
        visible: editor.allEffects && (veyra.videoHdr === true || veyra.nrEnabled === true)
                 && editor.regions.some(region => region.mode === 0)
        text: qsTr("「全部效果」显示未处理原画。开启 RTX Video HDR 后，框内可能比周围暗。")
        wrapMode: Text.WordWrap
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 11
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
                        visible: editor.allEffects
                        objectName: editor.namePrefix+"-select-"+regionRow.index
                        text: editor.maskState.selected === regionRow.modelData.index ? qsTr("已选中") : qsTr("选中")
                        primary: editor.maskState.selected === regionRow.modelData.index
                        onClicked: veyra.selectExclusionRegion(regionRow.modelData.index)
                    }
                    VButton {
                        objectName: editor.namePrefix+"-remove-"+regionRow.index
                        icon: true; ghost: true; iconName: "x"
                        onClicked: veyra.removeMaskRegion(editor.allEffects,regionRow.modelData.index)
                    }
                }
            }
            VRow {
                visible: editor.allEffects
                label: qsTr("剔除内容")
                hint: regionRow.modelData.mode === 1
                      ? qsTr("框内按真实帧刷新，保留超分、NR 和 HDR")
                      : regionRow.modelData.mode === 2
                        ? qsTr("识别并保护准心，框内背景继续补帧")
                        : qsTr("框内按真实帧刷新并显示原始画面")
                VSelect {
                    objectName: editor.namePrefix+"-mode-"+regionRow.index
                    options: [
                        {id:"0", label:qsTr("全部效果")},
                        {id:"1", label:qsTr("仅补帧")},
                        {id:"2", label:qsTr("准心/HUD")}
                    ]
                    value: (options[regionRow.modelData.mode] || options[0]).label
                    onPicked: id => veyra.setExclusionRegionMode(regionRow.modelData.index,Number(id))
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
            onMoved: value=>veyra.setMaskFeather(editor.allEffects,Math.round(value))
        }
    }
}
