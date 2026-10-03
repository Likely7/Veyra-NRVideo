// Used only in the task's staging copy of Main.qml, with the production bridge.
import QtQuick
Item {
    id: test
    property string media: ""
    property string evidence: ""
    property var appWindow: Window.window
    property int step: 0
    property int ticks: 0
    property bool imageSaved: false
    property bool expectRtss: false
    function find(item,name) {
        if(item.objectName===name)return item
        for(const child of item.children||[]){const hit=find(child,name);if(hit)return hit}
        return null
    }
    function require(ok,why) {
        if(!ok){console.error("FIELD_UI_FAIL",step,why);timer.stop();Qt.quit();throw new Error(why)}
    }
    Connections {
        target: veyra
        function onNotice(message,error) {
            console.log("FIELD_NOTICE",message,error)
            test.require(message.indexOf("NVIDIA App")<0,"obsolete NVIDIA module warning still shown")
        }
    }
    Timer {
        id: timer
        interval: 200;repeat: true;running: true
        onTriggered: {
            if(++test.ticks>1200){test.require(false,"240-second deadline");return}
            if(!test.media.length)return
            const model=veyra.exportQueueModel
            const field=test.find(test.appWindow.contentItem,"export-bitrate-field")
            const start=test.find(test.appWindow.contentItem,"export-start")
            if(test.step===0){
                if(test.ticks<15||!field||!start)return
                test.require(vySoftwareUi&&test.appWindow.color.a===1,"software window background must be opaque")
                if(test.expectRtss)test.require(veyra.overlayCompatActive,"real RTSS automatic compatibility not active")
                test.appWindow.contentItem.grabToImage(result=>{
                    test.imageSaved=result.saveToFile(test.evidence+"/software-background.png")
                })
                veyra.nrEnabled=true
                veyra.srEnabled=true
                veyra.videoSrQuality=4
                veyra.exportCompletionSound=false
                veyra.addExportFiles([test.media])
                veyra.exportRateControl=1
                field.forceActiveFocus();field.text="18";field.textEdited()
                test.require(veyra.exportBitrateMbps===18,"valid text not immediately committed")
                veyra.exportHevc=true
                veyra.exportSrTargetIndex=2
                veyra.exportContainer=0
                test.require(field.text==="18"&&veyra.exportBitrateMbps===18,"other export options replaced bitrate draft")
                test.step=1
            }else if(test.step===1){
                if(veyra.exportReadyCount!==1)return
                test.require(start.enabled,"18 Mbps export incorrectly disabled")
                start.clicked()
                test.require(veyra.exportRunning,"production VBR export rejected as invalid")
                veyra.exportBitrateMbps=7 // active worker must keep its frozen 18 Mbps
                test.step=2
            }else if(test.step===2){
                if(veyra.exportRunning)return
                test.require(veyra.exportItems.filter(i=>i.state==="done").length===1,"VBR worker failed")
                veyra.addExportFiles([test.media])
                veyra.exportRateControl=0
                field.forceActiveFocus();field.text="24";field.textEdited()
                test.require(veyra.exportBitrateMbps===24,"24 Mbps input not committed")
                test.step=3
            }else if(test.step===3){
                if(veyra.exportReadyCount!==1)return
                // No other export option is edited between the input and start.
                test.require(start.enabled,"direct CBR export incorrectly disabled")
                start.clicked()
                test.require(veyra.exportRunning,"direct export rejected as invalid")
                veyra.exportBitrateMbps=8
                test.step=4
            }else if(test.step===4){
                if(veyra.exportRunning)return
                test.require(veyra.exportItems.filter(i=>i.state==="done").length===2,"CBR worker failed")
                test.require(test.imageSaved,"background evidence not saved")
                console.log("FIELD_UI_PASS: real bridge + worker, 2K30 AVI -> NR + highest RTX SR 4K, 18 VBR changed-options, 24 CBR direct-start, frozen jobs, opaque software UI, old NVIDIA marker")
                timer.stop();Qt.quit()
            }
        }
    }
}
