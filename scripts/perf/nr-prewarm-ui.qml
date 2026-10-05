import QtQuick
Item {
    id: probe
    property string media: ""
    property string group: "nr"
    property bool referenceBuild: false
    property int stage: 0
    property double startedAt: Date.now()
    property double openedAt: 0
    function require(ok,why){if(!ok){console.log("PREWARM_UI_FAIL",why,veyra.diagnosticsReport());Qt.exit(3)}}
    Timer {
        interval: 100;repeat: true;running: true
        onTriggered: {
            const elapsed=Date.now()-probe.startedAt
            require(elapsed<90000,"timeout");require(!veyra.failed,"engine failure")
            if(probe.stage===0){
                veyra.muted=true;veyra.lowLatency=false;veyra.videoHdr=false;veyra.colorEnabled=false;veyra.fgEnabled=false
                veyra.nrEnabled=probe.group!=="alloff"&&probe.group!=="sr"
                veyra.srEnabled=probe.group==="srnr"||probe.group==="sr"
                veyra.videoSrQuality=0;veyra.srTargetIndex=2
                require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"temporal",0),"temporal")
                require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePolicy",0),"size")
                console.log("PREWARM_UI_CONFIG",JSON.stringify({group:probe.group,at:Date.now(),nr:veyra.nrEnabled,sr:veyra.srEnabled}));probe.stage=1;return
            }
            if(probe.stage===1){
                if(probe.group==="quit"&&elapsed>=2300){console.log("PREWARM_UI_QUIT_DURING");Qt.quit();return}
                if(probe.group==="alloff"&&elapsed>=6500){console.log("PREWARM_UI_ALL_OFF_PASS");Qt.quit();return}
                if(probe.group==="disable"&&elapsed>=2500){require(veyra.setPreference("prewarmEnhancement",false),"disable prewarm");probe.stage=2;return}
                const openAt=probe.group==="during"?2300:8000
                if(elapsed<openAt)return
                if(probe.group==="mismatch")require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePolicy",2),"size key change")
                probe.openedAt=Date.now();console.log("PREWARM_UI_OPEN",probe.openedAt);veyra.openPath(probe.media);probe.stage=3;return
            }
            if(probe.stage===2){
                if(elapsed<8000)return
                probe.openedAt=Date.now();console.log("PREWARM_UI_OPEN",probe.openedAt);veyra.openPath(probe.media);probe.stage=3;return
            }
            if(probe.stage===3){
                if(!veyra.running||veyra.applying||veyra.position<1.5)return
                require(veyra.nrActive===(probe.group!=="sr"),"NR active")
                require(veyra.srActive===(probe.group==="srnr"||probe.group==="sr"),"SR active")
                console.log("PREWARM_UI_PASS",JSON.stringify({group:probe.group,observedOpenMs:Date.now()-probe.openedAt,
                    nr:veyra.nrActive,sr:veyra.srActive,position:veyra.position,details:veyra.diagnosticsReport()}));Qt.quit()
            }
        }
    }
}
