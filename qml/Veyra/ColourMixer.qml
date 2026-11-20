import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    property int band: 0
    property var state: ({blackWhite:false,mixerHue:[0,0,0,0,0,0,0,0],mixerSaturation:[0,0,0,0,0,0,0,0],mixerLuminance:[0,0,0,0,0,0,0,0],blackWhiteMix:[0,0,0,0,0,0,0,0]})
    readonly property var names:[qsTr("红色"),qsTr("橙色"),qsTr("黄色"),qsTr("绿色"),qsTr("浅绿色"),qsTr("蓝色"),qsTr("紫色"),qsTr("洋红")]
    readonly property var colors:["#FF5A5A","#FF9A3D","#F5D547","#4CD964","#3DD6C8","#4F8BFF","#9B6BFF","#FF5AC8"]
    signal edited(string name, real value)
    signal modeEdited(bool blackWhite)
    spacing:0
    RowLayout {
        id: modeRow
        objectName:"colour-mixer-mode-row"
        property string label:qsTr("模式")
        Layout.fillWidth:true
        Layout.minimumHeight:Theme.rowMinHeight
        spacing:12
        Text {
            Layout.fillWidth:true;Layout.minimumWidth:0
            text:modeRow.label;color:Theme.t2
            font.family:Theme.fontUi;font.pixelSize:Theme.fsBody
            wrapMode:Text.Wrap
        }
        VSeg {
            objectName:"colour-mixer-mode"
            options:[{id:"hsl",label:"HSL"},{id:"bw",label:qsTr("黑白")}]
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
        model:root.state.blackWhite?[{key:"blackWhiteMix",label:qsTr("黑白")}]:
            [{key:"mixerHue",label:qsTr("色相")},{key:"mixerSaturation",label:qsTr("饱和度")},{key:"mixerLuminance",label:qsTr("明亮度")}]
        delegate:Item {
            required property var modelData
            objectName:"colour-mixer-row-"+modelData.key
            Layout.fillWidth:true
            implicitHeight: Math.max(Theme.rowMinHeight, parameterLabel.implicitHeight + 6)
            Text {
                id: parameterLabel
                anchors.left: parent.left
                anchors.right: mixerSlider.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                text:root.names[root.band]+" · "+modelData.label;color:Theme.t2
                font.family:Theme.fontUi;font.pixelSize:Theme.fsBody
                wrapMode: Text.Wrap
            }
            VSlider {
                id:mixerSlider
                resettable:true;defaultValue:0
                valueFromModel:true
                objectName:"colour-mixer-"+modelData.key
                anchors.right: parameterValue.left
                anchors.rightMargin: 12
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(150, parent.width * 0.5)
                from:-100;to:100;center:true
                showCenterFill:true
                Accessible.name:root.names[root.band]+" · "+modelData.label
                value:root.state[modelData.key][root.band]
                onMoved:value=>root.edited(modelData.key+"."+root.band,Math.round(value))
            }
            VSliderValue {
                objectName:"colour-mixer-value-"+modelData.key
                id: parameterValue
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(40, implicitWidth)
                slider:mixerSlider
                readonly property int amount:Math.round(root.state[modelData.key][root.band])
                text:(amount>0?"+":"")+String(amount)
            }
        }
    }
}
