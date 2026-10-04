import QtQuick
Item {
    id: probe
    property string media: ""
    property string group: "pressure"
    property int stage: 0
    property double startedAt: Date.now()
    property double phaseAt: 0
    function require(ok,why){if(!ok){console.log("CACHE_UI_FAIL",why,veyra.diagnosticsReport());Qt.exit(3)}}
    Timer {
        interval: 200;running: true;repeat: true
        onTriggered: {
            require(Date.now()-probe.startedAt<90000,"timeout")
            require(!veyra.failed,"engine failure")
            if(probe.stage===0){
                veyra.muted=true;veyra.lowLatency=false
                veyra.nrEnabled=true;veyra.srEnabled=false;veyra.fgEnabled=false
                veyra.videoHdr=false;veyra.colorEnabled=false
                require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"temporal",0),"temporal")
                require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePolicy",0),"size")
                veyra.openPath(probe.media);probe.stage=1;return
            }
            if(probe.stage===1){
                if(!veyra.running||veyra.applying||veyra.position<1||!veyra.nrActive)return
                veyra.nrEnabled=false;probe.phaseAt=Date.now();probe.stage=2;return
            }
            if(probe.stage===2){
                if(veyra.applying||veyra.nrActive||Date.now()-probe.phaseAt<(probe.group==="pressure"?24000:1500))return
                if(probe.group==="invalidate")require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePolicy",2),"new size key")
                veyra.nrEnabled=true;probe.phaseAt=Date.now();probe.stage=3;return
            }
            if(probe.stage===3){
                if(veyra.applying||!veyra.nrActive||Date.now()-probe.phaseAt<1500)return
                console.log("CACHE_UI_RECOVERED",JSON.stringify({group:probe.group,position:veyra.position,details:veyra.diagnosticsReport()}))
                veyra.nrEnabled=false;probe.phaseAt=Date.now();probe.stage=4;return
            }
            if(probe.stage===4){
                if(veyra.applying||veyra.nrActive||Date.now()-probe.phaseAt<1500)return
                veyra.stopPlayback();probe.phaseAt=Date.now();probe.stage=5;return
            }
            if(probe.stage===5){
                if(veyra.running||Date.now()-probe.phaseAt<1000)return
                console.log("CACHE_UI_PASS",probe.group);Qt.quit()
            }
        }
    }
}
