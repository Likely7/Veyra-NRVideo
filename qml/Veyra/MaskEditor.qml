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
                ? qsTr("区域内恢复原始画面，补帧也不参与，按原始帧节奏更新。最多 4 个区域。")
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
    Repeater {
        model: editor.regions
        delegate: VRow {
            required property var modelData
            required property int index
            label: (modelData.ellipse?qsTr("圆形 "):qsTr("矩形 "))+(index+1)
            value: Math.round(modelData.left*100)+","+Math.round(modelData.top*100)+" → "
                   +Math.round(modelData.right*100)+","+Math.round(modelData.bottom*100)+" %"
            VButton {
                objectName: editor.namePrefix+"-remove-"+index
                icon: true; ghost: true; iconName: "x"
                onClicked: veyra.removeMaskRegion(editor.allEffects,modelData.index)
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
