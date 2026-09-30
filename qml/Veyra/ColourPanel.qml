import QtQuick
import QtQuick.Layouts
import QtQuick.Window

// Shared by list and node inspectors: the bridge selects the actual Color node.
ColumnLayout {
    id: panel
    required property var api
    readonly property var state:api.colourState
    property bool originalHeld: false
    onOriginalHeldChanged: api.holdOriginal(originalHeld)
    onVisibleChanged: { if (!visible) originalHeld = false }
    Component.onDestruction: { if (originalHeld) api.holdOriginal(false) }
    Connections {
        target: panel.Window.window
        function onActiveChanged() {
            if (panel.Window.window && !panel.Window.window.active) panel.originalHeld = false
        }
    }
    property string curveMode:"points"
    // Node cards (N2): only the groups, all folded, each with its item count;
    // the card header carries the switch.
    property bool compact:false
    // The tab redraw staggers this column's cards (ProPage M31).
    readonly property Item sectionColumn:panel
    readonly property var sectionCounts:[6,4,1,1,3,7,3,3]
    readonly property var sections:["亮","颜色","曲线","混色器","颜色分级","校准","LUT","效果"]
    readonly property var light:["exposure","contrast","highlights","shadows","whites","blacks"]
    readonly property var colour:["temperature","tint","vibrance","saturation"]
    readonly property var parametric:["paramHighlights","paramLights","paramDarks","paramShadows","splitHighlights","splitMidtones","splitShadows"]
    spacing:6
    Gradient {id:temperatureTrack;orientation:Gradient.Horizontal
        GradientStop {position:0;color:"#4F8BFF"}
        GradientStop {position:0.5;color:"#DDDDDD"}
        GradientStop {position:1;color:"#FFB547"}}
    Gradient {id:tintTrack;orientation:Gradient.Horizontal
        GradientStop {position:0;color:"#4CD964"}
        GradientStop {position:0.5;color:"#DDDDDD"}
        GradientStop {position:1;color:"#FF5AC8"}}
    Gradient {id:vibranceTrack;orientation:Gradient.Horizontal
        GradientStop {position:0;color:"#777777"}
        GradientStop {position:1;color:"#FF9A3D"}}
    Gradient {id:saturationTrack;orientation:Gradient.Horizontal
        GradientStop {position:0;color:"#777777"}
        GradientStop {position:1;color:"#FF5A5A"}}
    Gradient {id:greenCalibrationTrack;orientation:Gradient.Horizontal
        GradientStop {position:0;color:"#777777"}
        GradientStop {position:1;color:"#4CD964"}}
    Gradient {id:blueCalibrationTrack;orientation:Gradient.Horizontal
        GradientStop {position:0;color:"#777777"}
        GradientStop {position:1;color:"#4F8BFF"}}
    function valueFor(key){
        let value=state
        for(const part of key.split("."))value=value[part]
        return Number(value)
    }
    function descriptor(key){
        const found=api.colourParameters.find(p=>p.name===key)
        if(found)return found
        const parts=key.split(".")
        return {name:key,label:["红原色","绿原色","蓝原色"][Number(parts[1])]+
            (parts[0]==="calibrationHue"?" · 色相":" · 饱和度"),min:-100,max:100,step:1,defaultValue:0}
    }
    component NameField: Rectangle {
        property alias text: field.text
        property string placeholder: ""
        signal accepted()
        implicitHeight: 30
        radius: 9
        color: Qt.rgba(1, 1, 1, 0.04)
        border.width: 1
        border.color: field.activeFocus ? Theme.accent : Theme.stroke
        TextInput {
            id: field
            anchors.fill: parent; anchors.leftMargin: 10; anchors.rightMargin: 10
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody
            selectByMouse: true; clip: true; maximumLength: 48
            onAccepted: parent.accepted()
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left; anchors.leftMargin: 10
            visible: field.text.length === 0
            text: parent.placeholder; color: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: Theme.fsBody
        }
    }
    component ParameterRows: ColumnLayout {
        id: rows
        property var keys:[]
        spacing:0
        Repeater {
            model:rows.keys.length
            delegate:RowLayout {
                required property int index
                readonly property string key:rows.keys[index]
                readonly property var spec:panel.descriptor(key)
                Layout.fillWidth:true
                Layout.preferredHeight:Theme.rowMinHeight
                spacing:12
                Text {
                    Layout.fillWidth:true;Layout.minimumWidth:0
                    text:spec.label;color:Theme.t2
                    font.family:Theme.fontUi;font.pixelSize:Theme.fsBody
                    elide:Text.ElideRight
                }
                VSlider {
                    objectName:"colour-param-"+key
                    valueFromModel:true
                    resettable:true;defaultValue:spec.defaultValue
                    Layout.minimumWidth:150;Layout.maximumWidth:150;Layout.preferredWidth:150
                    from:spec.min;to:spec.max;center:from<0&&to>0
                    showCenterFill:true
                    Accessible.name:spec.label
                    value:panel.valueFor(key)
                    trackGradient:key==="temperature"?temperatureTrack:key==="tint"?tintTrack:
                        key==="vibrance"?vibranceTrack:key==="saturation"||key==="calibrationHue.0"?saturationTrack:
                        key==="calibrationHue.1"?greenCalibrationTrack:key==="calibrationHue.2"?blueCalibrationTrack:null
                    onMoved:value=>panel.api.setColourParameter(key,Math.round(value/spec.step)*spec.step)
                }
                Text {
                    Layout.minimumWidth:40;Layout.maximumWidth:40;Layout.preferredWidth:40
                    text:panel.valueFor(key).toFixed(spec.step<1?2:0);color:Theme.t1
                    font.family:Theme.fontMono;font.pixelSize:Theme.fsSmall
                    horizontalAlignment:Text.AlignRight
                }
            }
        }
    }
    // color.js .ctop: the 调色 switch with its note, then the tool row -
    // undo / redo / copy / paste on the left, 按住看原图 and 一键还原 on the right.
    Rectangle {
        id: colourTop
        objectName:"colour-top"
        visible:!panel.compact
        Layout.fillWidth:true
        implicitHeight:topCol.implicitHeight+10
        radius:12
        color:Theme.card2
        border.width:1
        border.color:Theme.stroke
        property real motionDy:0
        transform:Translate {y:colourTop.motionDy}
        ColumnLayout {
            id:topCol
            anchors.left:parent.left;anchors.right:parent.right;anchors.top:parent.top
            anchors.leftMargin:12;anchors.rightMargin:12;anchors.topMargin:2
            spacing:2
            RowLayout {
                Layout.fillWidth:true
                Layout.minimumHeight:44
                spacing:10
                ColumnLayout {
                    Layout.fillWidth:true
                    spacing:2
                    Text {
                        text:panel.state.index<0?"没有调色节点":veyra.nodeMode===1?"调色 · 实例 "+(panel.state.index+1):"调色"
                        color:Theme.t1;font.family:Theme.fontUi;font.pixelSize:13
                        font.weight:Font.DemiBold;font.variableAxes:Theme.axesDemiBold
                    }
                    Text {
                        Layout.fillWidth:true
                        text:panel.state.enabled?"关闭时这条链路不存在，零开销":"已关闭 · 参数保留但不生效"
                        color:Theme.t3;font.family:Theme.fontUi;font.pixelSize:11
                        elide:Text.ElideRight
                    }
                }
                VSwitch {objectName:"colour-master";checked:panel.state.enabled;onToggled:checked=>panel.api.colorEnabled=checked}
            }
            RowLayout {
                objectName:"colour-tools"
                Layout.fillWidth:true
                Layout.bottomMargin:6
                spacing:2
                VButton {
                    objectName:"colour-undo"
                    icon:true;ghost:true;iconName:"undo";implicitWidth:28;implicitHeight:28
                    tip:"撤销";enabled:panel.api.colourCanUndo
                    onClicked:panel.api.colourUndo()
                }
                VButton {
                    objectName:"colour-redo"
                    icon:true;ghost:true;iconName:"refresh";implicitWidth:28;implicitHeight:28
                    tip:"重做";enabled:panel.api.colourCanRedo
                    onClicked:panel.api.colourRedo()
                }
                VButton {
                    objectName:"colour-copy"
                    icon:true;ghost:true;iconName:"copy";implicitWidth:28;implicitHeight:28
                    tip:"复制调色参数";enabled:panel.state.index>=0
                    onClicked:panel.api.colourCopy()
                }
                VButton {
                    objectName:"colour-paste"
                    icon:true;ghost:true;iconName:"import";implicitWidth:28;implicitHeight:28
                    tip:"粘贴调色参数";enabled:panel.api.colourCanPaste&&panel.state.index>=0
                    onClicked:panel.api.colourPaste()
                }
                Item {Layout.fillWidth:true}
                VButton {
                    objectName:"colour-original-hold"
                    implicitHeight:28
                    iconName:"eye"
                    text:panel.originalHeld?"松开恢复":"按住看原图"
                    Accessible.name:"按住查看原图，松开恢复增强画面"
                    MouseArea {
                        anchors.fill:parent
                        acceptedButtons:Qt.LeftButton
                        onPressed:panel.originalHeld=true
                        onReleased:panel.originalHeld=false
                        onCanceled:panel.originalHeld=false
                    }
                }
                VButton {
                    objectName:"colour-reset-all"
                    implicitHeight:28
                    ghost:true;iconName:"reset";text:"一键还原"
                    onClicked:panel.api.resetColourGroup(-1)
                }
            }
        }
    }
    // Colour presets: one Color node's settings (ColorLookStore), not the
    // whole-chain presets. Applying one replaces this instance only.
    VRow {
        visible:!panel.compact
        label:"颜色预设"
        VSelect {
            objectName:"colour-look-select"
            implicitWidth:170
            value:panel.api.colourLooks.length?"应用预设…":"暂无颜色预设"
            options:panel.api.colourLooks.map(l=>({id:String(l.index),label:l.name+(l.lut?" · LUT":"")}))
            onPicked:id=>panel.api.applyColourLook(Number(id))
        }
    }
    RowLayout {
        visible:!panel.compact
        Layout.fillWidth:true
        spacing:6
        NameField {
            id:lookName
            objectName:"colour-look-name"
            Layout.fillWidth:true
            placeholder:"新预设名称"
            onAccepted:saveLook.clicked()
        }
        VButton {
            id:saveLook
            objectName:"colour-look-save"
            text:"保存"
            enabled:lookName.text.trim().length>0&&panel.state.index>=0
            onClicked:{
                const name=lookName.text.trim()
                const exists=panel.api.colourLooks.some(l=>l.name===name)
                if(panel.api.saveColourLook(name,exists))lookName.text=""
            }
        }
        VButton {
            id:lookMore
            objectName:"colour-look-more"
            text:"管理";ghost:true
            Accessible.name:"颜色预设 · 导入/导出/删除"
            onClicked:lookMenu.openAt(lookMore,"down")
        }
        VMenu {
            id:lookMenu
            title:"颜色预设"
            items:[{act:"import",label:"导入 .vpcolor…",icon:"import"}]
                .concat(panel.api.colourLooks.map(l=>({act:"export",look:l.index,label:"导出「"+l.name+"」…",icon:"upload"})))
                .concat(panel.api.colourLooks.map(l=>({act:"delete",look:l.index,label:"删除「"+l.name+"」",icon:"trash"})))
            onPicked:(i,item)=>{
                if(item.act==="import")panel.api.importColourLookDialog()
                else if(item.act==="export")panel.api.exportColourLookDialog(item.look)
                else if(item.act==="delete")panel.api.deleteColourLook(item.look)
            }
        }
    }
    Repeater {
        model:8
        delegate:VAccordion {
            id: section
            required property int index
            objectName:"colour-section-"+index
            Layout.fillWidth:true
            title:panel.sections[index];glyph:"palette";hue:"#E0C341"
            summary:panel.compact?panel.sectionCounts[index]+" 项":""
            open:!panel.compact&&index<2
            enabledSwitch:false
            compactHeader:true
            bypassed:index<7&&(panel.state.groupBypassMask&(1<<index))!==0
            headerLeadingActions: [
                VButton {
                    objectName:"colour-bypass-"+section.index
                    visible:section.index<7
                    icon:true;iconName:section.bypassed?"eyeoff":"eye"
                    ghost:true;implicitWidth:24;implicitHeight:24;radius:7
                    Accessible.name:panel.sections[section.index]+(section.bypassed?" · 恢复":" · 旁路")
                    onClicked:panel.api.setColourOption("groupBypassMask",panel.state.groupBypassMask^(1<<section.index))
                }
            ]
            headerActions: [
                VButton {
                    objectName:"colour-reset-"+section.index
                    icon:true;iconName:"reset";ghost:true;implicitWidth:26;implicitHeight:26
                    Accessible.name:panel.sections[section.index]+" · 还原"
                    onClicked:panel.api.resetColourGroup(section.index)
                }
            ]
            ParameterRows {
                Layout.fillWidth:true
                visible:section.index===0||section.index===1||section.index===7
                keys:section.index===0?panel.light:section.index===1?panel.colour:
                    section.index===7?["texture","clarity","dehaze"]:[]
            }
            Text {
                Layout.fillWidth:true;visible:section.index===1
                text:"色温为相对值；采集卡没有拍摄白平衡。"
                color:Theme.t3;font.pixelSize:11;wrapMode:Text.WordWrap
            }
            Loader {
                Layout.fillWidth:true;active:section.index===2;visible:active
                sourceComponent:ColumnLayout {
                VSeg {
                    objectName:"colour-curve-mode"
                    options:[{id:"points",label:"点曲线"},{id:"parametric",label:"参数曲线"}]
                    current:panel.curveMode;onPicked:id=>panel.curveMode=id
                }
                ColourCurve {
                    objectName:"colour-curve"
                    Layout.fillWidth:true;visible:panel.curveMode==="points"
                    curves:panel.state.curves;samples:panel.state.curveSamples
                    onEdited:(channel,points)=>panel.api.setColourCurve(channel,points)
                }
                ParameterRows {Layout.fillWidth:true;visible:panel.curveMode==="parametric";keys:panel.parametric}
                }
            }
            Loader {
                Layout.fillWidth:true;active:section.index===3;visible:active
                sourceComponent:ColourMixer {
                objectName:"colour-mixer"
                state:panel.state
                onEdited:(name,value)=>panel.api.setColourParameter(name,value)
                onModeEdited:blackWhite=>panel.api.setColourOption("blackWhite",blackWhite?1:0)
                }
            }
            Loader {
                Layout.fillWidth:true;active:section.index===4;visible:active
                sourceComponent:ColumnLayout {
                GridLayout {
                    Layout.fillWidth:true;columns:2;columnSpacing:14;rowSpacing:14
                    Repeater {
                        model:4
                        delegate:ColourWheel {
                            property bool sliderResetEnabled: true
                            property bool sliderValueFromModel: true
                            required property int index
                            objectName:"colour-wheel-"+index
                            Layout.fillWidth:true
                            title:["阴影","中间调","高光","全局"][index]
                            hue:panel.state.grading[index].hue
                            saturation:panel.state.grading[index].saturation
                            luminance:panel.state.grading[index].luminance
                            onEdited:(h,s,l)=>panel.api.setColourWheel(index,h,s,l)
                        }
                    }
                }
                ParameterRows {Layout.fillWidth:true;keys:["gradingBlending","gradingBalance"]}
                }
            }
            ParameterRows {
                Layout.fillWidth:true;visible:section.index===5
                keys:section.index===5?["calibrationShadowTint","calibrationHue.0","calibrationSaturation.0","calibrationHue.1","calibrationSaturation.1","calibrationHue.2","calibrationSaturation.2"]:[]
            }
            Loader {
                Layout.fillWidth:true;active:section.index===6;visible:active
                sourceComponent:ColumnLayout {
                VRow {
                    label:"LUT 文件"
                    VSelect {
                        objectName:"colour-lut-select"
                        implicitWidth:170
                        value:panel.state.lutName?panel.state.lutName:"未使用"
                        options:[{id:"",label:"不使用 LUT"}].concat(panel.api.lutLibrary.map(n=>({id:n,label:n})))
                        onPicked:id=>panel.api.setColourLut(id)
                    }
                }
                RowLayout {
                    Layout.fillWidth:true
                    VButton {
                        objectName:"colour-lut-import"
                        text:"导入 .cube…";ghost:true
                        onClicked:panel.api.importLutDialog()
                    }
                    Item {Layout.fillWidth:true}
                }
                ParameterRows {Layout.fillWidth:true;keys:["lutStrength"]}
                VSeg {
                    options:[{id:"0",label:"Cineon"},{id:"1",label:"sRGB"},{id:"2",label:"PQ"}]
                    current:String(panel.state.lutInputSpace)
                    onPicked:id=>panel.api.setColourOption("lutInputSpace",Number(id))
                }
                Text {
                    Layout.fillWidth:true;text:"导入的 .cube 保存在程序目录 runtime_local/luts，导出也使用同一份。"
                    color:Theme.t3;font.pixelSize:11;wrapMode:Text.WordWrap
                }
                }
            }
        }
    }
}
