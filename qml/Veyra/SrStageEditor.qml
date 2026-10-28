import QtQuick
import QtQuick.Layouts
import "EffectSupport.js" as Support

ColumnLayout {
    id: editor
    property string namePrefix: "list"
    spacing: 6
    readonly property bool supported: veyra.videoSrQuality >= 1 && veyra.videoSrQuality <= 4 && Support.available(veyra,"sr")
    readonly property bool manual: veyra.srPrescale >= 150 && veyra.srPrescale <= 190
    readonly property var targets: [{id:"1",label:"2K"},{id:"2",label:"4K"},{id:"4",label:"5K"},{id:"5",label:"6K"},{id:"6",label:"7K"},{id:"3",label:"8K"}]
    VRow {
        label: qsTr("目标尺寸")
        hint: qsTr("基础超分的输出尺寸；关闭双重超分时保持原有单次超分。")
        VSeg { objectName: editor.namePrefix+"-sr-target"; options: editor.targets; current: String(veyra.srTargetIndex); onPicked: id => veyra.srTargetIndex=Number(id) }
    }
    VRow {
        label: qsTr("超分运动来源")
        hint: qsTr("零运动会限制时序重建；RTX 视频超分不使用此输入")
        VSeg { options:[{id:"0",label:qsTr("零运动")},{id:"1",label:qsTr("光流")}]; current:String(veyra.srMotionSource); onPicked:id=>veyra.srMotionSource=Number(id) }
    }
    VRow {
        label: qsTr("质量")
        enabled: editor.supported; opacity: enabled ? 1 : 0.4
        VSeg { objectName:editor.namePrefix+"-sr-quality"; options:[{id:"1",label:"1"},{id:"2",label:"2"},{id:"3",label:"3"},{id:"4",label:"4"}]; current:String(veyra.videoSrQuality); onPicked:id=>veyra.videoSrQuality=Number(id) }
    }
    VRow {
        label: qsTr("双重超分")
        hint: editor.supported ? qsTr("先完成基础超分，再将结果降低分辨率并执行第二次超分；两次执行独立，会增加耗时和显存。") : qsTr("双重超分目前使用 RTX 视频超分")
        enabled: editor.supported
        opacity: enabled ? 1 : 0.4
        VSwitch { objectName: editor.namePrefix+"-dual-sr-toggle"; checked: veyra.dualSrEnabled; onToggled: checked => veyra.dualSrEnabled=checked }
    }
    ColumnLayout {
        objectName: editor.namePrefix+"-dual-sr-panel"
        visible: veyra.dualSrEnabled
        enabled: editor.supported
        Layout.fillWidth: true
        spacing: 6
        VRow {
            label: qsTr("再次降低到")
            hint: qsTr("降低的是基础超分后的画面，不是原始输入；选项必须小于基础结果和第二次超分目标。")
            VSelect {
                objectName: editor.namePrefix+"-sr-prescale"
                options: [{id:"1",label:"720p"},{id:"2",label:"900p"},{id:"3",label:"1080p"},{id:"4",label:"1440p"},{id:"manual",label:qsTr("手动")}]
                value: editor.manual ? qsTr("手动") : (options.find(o=>o.id===String(veyra.srPrescale))?.label||"1080p")
                onPicked: id => veyra.srPrescale=id==="manual"?175:Number(id)
            }
        }
        VRow {
            visible: editor.manual
            label: qsTr("二次处理比例")
            value: Math.round(percent.displayedValue)+"%"
            VSlider {
                id: percent; objectName: editor.namePrefix+"-sr-prescale-percent"
                valueFromModel: true; from: 50; to: 90; stepSize: 5; live: false
                value: editor.manual?veyra.srPrescale-100:75
                onMoved: value => veyra.srPrescale=100+Math.round(value)
            }
        }
        VRow {
            label: qsTr("第二次超分到")
            VSeg { objectName: editor.namePrefix+"-dual-sr-target"; options: editor.targets; current: String(veyra.dualSrTargetIndex); onPicked: id=>veyra.dualSrTargetIndex=Number(id) }
        }
        VRow {
            label: qsTr("第二次超分质量")
            hint: qsTr("RTX 视频超分，独立于基础超分质量")
            VSeg { objectName: editor.namePrefix+"-dual-sr-quality"; options:[{id:"1",label:"1"},{id:"2",label:"2"},{id:"3",label:"3"},{id:"4",label:"4"}]; current:String(veyra.dualSrQuality); onPicked:id=>veyra.dualSrQuality=Number(id) }
        }
        Text { text: veyra.srStageSummary; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize:11; wrapMode:Text.WordWrap; Layout.fillWidth:true }
        Text { text: veyra.srStageNote; visible:text.length>0; color:Theme.t2; font.pixelSize:11; wrapMode:Text.WordWrap; Layout.fillWidth:true }
    }
}
