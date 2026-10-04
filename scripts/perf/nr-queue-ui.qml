import QtQuick
Item {
    id: probe
    property string media: ""
    property string backend: "fsr3"
    property int stage: 0
    property double began: Date.now()
    property double phaseAt: 0
    function require(ok,why){if(!ok){console.log("QUEUE_UI_FAIL",why,veyra.diagnosticsReport());Qt.exit(3);throw new Error("QUEUE_UI_FAIL: "+why)}}
    function mark(name){console.log("QUEUE_UI_PHASE",JSON.stringify({name:name,backend:veyra.fgBackendName,at:Date.now(),position:veyra.position,nr:veyra.nrActive,sr:veyra.srActive,fg:veyra.fgActive,detail:veyra.runStatusDetail}));phaseAt=Date.now();++stage}
    Timer {
        interval: 200;running: true;repeat: true
        onTriggered: {
            require(Date.now()-probe.began<100000,"timeout");require(!veyra.failed,"engine failure")
            if(probe.stage===0){
                veyra.muted=true;veyra.lowLatency=false;veyra.colorEnabled=false;veyra.videoHdr=false
                veyra.nrEnabled=true;veyra.srEnabled=true;veyra.videoSrQuality=0;veyra.srTargetIndex=2
                while(veyra.nrLayers.length<2)require(veyra.duplicateNrLayer(veyra.nrLayers[0].index)>=0,"duplicate")
                for(let i=0;i<2;++i){require(veyra.setNrLayerParameter(veyra.nrLayers[i].index,"runtime",0),"runtime");require(veyra.setNrLayerParameter(veyra.nrLayers[i].index,"sizePolicy",0),"size");require(veyra.setNrLayerParameter(veyra.nrLayers[i].index,"temporal",0),"temporal")}
                veyra.fgBackendName="dlss";veyra.fgMultiplier=2;veyra.fgEnabled=true;veyra.openPath(probe.media);probe.mark("open");return
            }
            require(Date.now()-probe.phaseAt<18000,"phase did not settle")
            if(veyra.applying||!veyra.running||veyra.position<1||Date.now()-probe.phaseAt<2200||!veyra.nrActive||!veyra.srActive)return
            if(probe.stage===1){if(!veyra.fgActive)return;probe.mark("dlss2-active");veyra.fgBackendName=probe.backend;veyra.fgMultiplier=2;return}
            if(probe.stage===2){if(!veyra.fgActive||veyra.fgBackendName!==probe.backend)return;probe.mark(probe.backend+"-active");veyra.fgBackendName="dlss";veyra.fgMultiplier=3;return}
            if(probe.stage===3){if(!veyra.fgActive||veyra.fgBackendName!=="dlss"||veyra.fgMultiplier!==3||veyra.runStatusDetail.indexOf("3X")<0)return;probe.mark("dlss3-active");veyra.fgEnabled=false;return}
            if(probe.stage===4){if(veyra.fgActive)return;probe.mark("fg-off");veyra.fgEnabled=true;veyra.colorEnabled=true;return}
            if(probe.stage===5){if(!veyra.fgActive||!veyra.colorEnabled)return;probe.mark("color-active");veyra.colorEnabled=false;return}
            if(probe.stage===6){if(!veyra.fgActive||veyra.colorEnabled)return;probe.mark("color-off");require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"temporal",1),"temporal on");return}
            if(probe.stage===7){if(!veyra.fgActive||!veyra.nrLayers[0].temporal)return;probe.mark("temporal-active");require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"temporal",0),"temporal off");return}
            if(probe.stage===8){if(!veyra.fgActive||veyra.nrLayers[0].temporal)return;probe.mark("temporal-off");veyra.stopPlayback();return}
            if(probe.stage===9&&!veyra.running){console.log("QUEUE_UI_PASS",probe.backend);Qt.quit()}
        }
    }
    Timer {interval:200;running:true;repeat:true;onTriggered:if(probe.stage===9&&!veyra.running){console.log("QUEUE_UI_PASS",probe.backend);Qt.quit()}}
}
