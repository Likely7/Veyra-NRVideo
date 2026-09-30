import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property int band: 0
    property var state: ({blackWhite:false,mixerHue:[0,0,0,0,0,0,0,0],mixerSaturation:[0,0,0,0,0,0,0,0],mixerLuminance:[0,0,0,0,0,0,0,0],blackWhiteMix:[0,0,0,0,0,0,0,0]})
    readonly property var names:["红色","橙色","黄色","绿色","浅绿色","蓝色","紫色","洋红"]
    readonly property var colors:["#FF5A5A","#FF9A3D","#F5D547","#4CD964","#3DD6C8","#4F8BFF","#9B6BFF","#FF5AC8"]
    signal edited(string name, real value)
    signal modeEdited(bool blackWhite)
    spacing:0
    RowLayout {
        id: modeRow
        objectName:"colour-mixer-mode-row"
        property string label:"模式"
        Layout.fillWidth:true
        Layout.preferredHeight:Theme.rowMinHeight
        spacing:12
        Text {
            Layout.fillWidth:true;Layout.minimumWidth:0
            text:modeRow.label;color:Theme.t2
            font.family:Theme.fontUi;font.pixelSize:Theme.fsBody
            elide:Text.ElideRight
        }
        VSeg {
            objectName:"colour-mixer-mode"
            options:[{id:"hsl",label:"HSL"},{id:"bw",label:"黑白"}]
            current:root.state.blackWhite?"bw":"hsl"
            onPicked:id=>root.modeEdited(id==="bw")
        }
    }
    RowLayout {
        Layout.fillWidth:true;Layout.topMargin:4;Layout.bottomMargin:6;spacing:6
        Repeater {
            model:8
            Rectangle {
                required property int index
                objectName:"colour-mixer-band-"+index
                Layout.fillWidth:true;implicitHeight:22;radius:7
                color:root.colors[index];border.width:2
                border.color:root.band===index?"#FFFFFF":"transparent"
                scale:root.band===index?1.08:1
                Behavior on scale { NumberAnimation {duration:Theme.d(400);easing.bezierCurve:Theme.spring} }
                Behavior on border.color { ColorAnimation {duration:Theme.d(200)} }
                TapHandler {onTapped:root.band=parent.index}
                Accessible.name:root.names[index]
            }
        }
    }
    Repeater {
        model:root.state.blackWhite?[{key:"blackWhiteMix",label:"黑白"}]:
            [{key:"mixerHue",label:"色相"},{key:"mixerSaturation",label:"饱和度"},{key:"mixerLuminance",label:"明亮度"}]
        delegate:RowLayout {
            required property var modelData
            objectName:"colour-mixer-row-"+modelData.key
            Layout.fillWidth:true
            Layout.preferredHeight:Theme.rowMinHeight
            spacing:12
            Text {
                Layout.fillWidth:true;Layout.minimumWidth:0
                text:root.names[root.band]+" · "+modelData.label;color:Theme.t2
                font.family:Theme.fontUi;font.pixelSize:Theme.fsBody
                elide:Text.ElideRight
            }
            VSlider {
                resettable:true;defaultValue:0
                valueFromModel:true
                objectName:"colour-mixer-"+modelData.key
                Layout.minimumWidth:150;Layout.maximumWidth:150;Layout.preferredWidth:150
                from:-100;to:100;center:true
                showCenterFill:true
                Accessible.name:root.names[root.band]+" · "+modelData.label
                value:root.state[modelData.key][root.band]
                onMoved:value=>root.edited(modelData.key+"."+root.band,Math.round(value))
            }
            Text {
                objectName:"colour-mixer-value-"+modelData.key
                Layout.minimumWidth:40;Layout.maximumWidth:40;Layout.preferredWidth:40
                readonly property int amount:Math.round(root.state[modelData.key][root.band])
                text:(amount>0?"+":"")+String(amount);color:Theme.t1
                font.family:Theme.fontMono;font.pixelSize:Theme.fsSmall
                horizontalAlignment:Text.AlignRight
            }
        }
    }
}
