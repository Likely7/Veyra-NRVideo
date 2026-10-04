import QtQuick
Item {
    id: probe
    property var appWindow: Window.window
    property string media: ""
    property string group: "nr"
    property int stage: 0
    property double started: Date.now()
    property double phaseAt: 0
    property double pausedPosition: 0
    property int pausedNr: 0
    property int edits: 0
    property int editedNr: 0
    property double resumePosition: 0
    function require(ok, why) {if(!ok){console.log("PAUSED_UI_FAIL",why,veyra.diagnosticsReport());Qt.exit(3)}}
    function nrCount() {
        const report=veyra.diagnosticsReport()
        const found=/NR[^\r\n]*?(\d+)\s*\/\s*NVOF\s*(\d+)/.exec(report)
        require(found!==null,"NR counter unavailable");return found?Number(found[1]):-1
    }
    Connections {target: veyra;function onNotice(text,error){console.log("PAUSED_UI_NOTICE",text,error)}}
    Timer {
        interval: 500;running: true;repeat: true
        onTriggered: {
            const now=Date.now();probe.require(now-probe.started<220000,"timeout")
            probe.require(!veyra.failed,"engine failed")
            if(probe.stage===0){
                probe.appWindow.width=1280;probe.appWindow.height=800
                veyra.muted=true;veyra.lowLatency=false
                veyra.srEnabled=false;veyra.fgEnabled=false;veyra.videoHdr=false;veyra.colorEnabled=false
                veyra.nrEnabled=true
                const idx=veyra.nrLayers[0].index
                probe.require(veyra.setNrLayerParameter(idx,"sizePolicy",1),"native NR")
                probe.require(veyra.setNrLayerParameter(idx,"temporal",probe.group==="temporal"?1:0),"temporal")
                if(probe.group==="srnr"){veyra.videoSrQuality=0;veyra.srTargetIndex=2;veyra.srEnabled=true}
                console.log("PAUSED_UI_CONFIG",JSON.stringify({group:probe.group,layers:veyra.nrLayers,sr:veyra.srEnabled,fg:veyra.fgEnabled}))
                veyra.openPath(probe.media);probe.stage=1;return
            }
            if(probe.stage===1){
                if(!veyra.running||veyra.position<5||!veyra.nrActive||veyra.applying)return
                veyra.togglePlayPause();probe.phaseAt=now;probe.stage=2;return
            }
            if(probe.stage===2){
                if(!veyra.paused||now-probe.phaseAt<1500)return
                probe.pausedPosition=veyra.position;probe.pausedNr=probe.nrCount();probe.phaseAt=now;probe.stage=3
                console.log("PAUSED_UI_IDLE_BEGIN",JSON.stringify({nr:probe.pausedNr,position:probe.pausedPosition,at:now}));return
            }
            if(probe.stage===3){
                probe.require(veyra.paused&&!veyra.applying,"idle transport")
                probe.require(probe.nrCount()===probe.pausedNr,"NR evaluated while idle")
                probe.require(Math.abs(veyra.position-probe.pausedPosition)<.1,"position drift while paused")
                if(now-probe.phaseAt<30000)return
                console.log("PAUSED_UI_IDLE_PASS",JSON.stringify({nr:probe.nrCount(),durationMs:now-probe.phaseAt,position:veyra.position}));probe.stage=4;return
            }
            if(probe.stage===4){
                if(veyra.applying||now-probe.phaseAt<700)return
                if(probe.edits===20){probe.editedNr=probe.nrCount();console.log("PAUSED_UI_EDITS_PASS",JSON.stringify({nrBefore:probe.pausedNr,nrAfter:probe.editedNr,requests:probe.edits}));probe.resumePosition=veyra.position;veyra.togglePlayPause();probe.stage=5;return}
                const idx=veyra.nrLayers[0].index
                if(probe.edits===10)probe.require(veyra.setNrLayerParameter(idx,"intensity",.8),"model invalidation")
                probe.require(veyra.setNrLayerParameter(idx,"total",(probe.edits%10)*.2),"residual edit")
                console.log("PAUSED_UI_EDIT",JSON.stringify({request:probe.edits,nrBefore:probe.nrCount(),modelChanged:probe.edits===10}));++probe.edits;probe.phaseAt=now;return
            }
            if(probe.stage===5){
                if(veyra.paused||veyra.position<probe.resumePosition+2)return
                probe.require(probe.nrCount()>probe.editedNr,"resume did not evaluate NR")
                console.log("PAUSED_UI_RESUME_PASS",JSON.stringify({nr:probe.nrCount(),position:veyra.position}));veyra.togglePlayPause();probe.phaseAt=now;probe.stage=6;return
            }
            if(probe.stage===6){if(!veyra.paused||now-probe.phaseAt<1000)return;veyra.seekTo(2);probe.phaseAt=now;probe.stage=7;return}
            if(probe.stage===7){
                if(veyra.applying||now-probe.phaseAt<1500)return
                probe.require(veyra.paused&&Math.abs(veyra.position-2)<.2,"paused seek")
                console.log("PAUSED_UI_SEEK_PASS",JSON.stringify({nr:probe.nrCount(),position:veyra.position}));console.log("PAUSED_UI_PASS");Qt.quit()
            }
        }
    }
}
