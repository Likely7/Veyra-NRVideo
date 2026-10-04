import QtQuick
Item {
    id: probe
    property string media: ""
    property string mode: "lifecycle"
    property int stage: 0
    property double began: Date.now()
    property double phaseAt: Date.now()
    property double pausePosition: 0
    function require(ok, why) {
        if (!ok) { console.log("AUTO_UI_FAIL", why, veyra.diagnosticsReport()); Qt.exit(3); throw new Error(why) }
    }
    function mark(name) {
        console.log("AUTO_UI_PHASE", JSON.stringify({name:name, at:Date.now(), position:veyra.position,
            nr:veyra.nrActive, sr:veyra.srActive, auto:veyra.nrAutoActive, percent:veyra.nrAutoPercent,
            status:veyra.nrAutoStatus, layers:veyra.nrLayers, detail:veyra.runStatusDetail}))
        phaseAt = Date.now(); ++stage
    }
    NrLayerEditor { id: firstEditor; visible:false; layerData:veyra.nrLayers[0]; layerCount:veyra.nrLayers.length }
    NrLayerEditor { id: secondEditor; visible:false; layerData:veyra.nrLayers.length>1?veyra.nrLayers[1]:veyra.nrLayers[0]; layerCount:veyra.nrLayers.length }
    Timer {
        interval:200; running:true; repeat:true
        onTriggered: {
            probe.require(Date.now()-probe.began<180000,"timeout")
            probe.require(!veyra.failed,"engine failure")
            if (probe.stage===0) {
                if(probe.mode==="persist") {
                    probe.require(veyra.nrLayers.length===2&&veyra.nrLayers[0].sizePolicy===6,"saved Auto lost")
                } else {
                    veyra.muted=true; veyra.lowLatency=false; veyra.videoHdr=false; veyra.colorEnabled=false
                    veyra.fgEnabled=false; veyra.srEnabled=false; veyra.nrEnabled=true
                    probe.require(veyra.setOpticalFlowChoice(0),"NVIDIA flow")
                    while(veyra.nrLayers.length<2)probe.require(veyra.duplicateNrLayer(veyra.nrLayers[0].index)>=0,"duplicate")
                    for(let i=0;i<2;++i) {
                        probe.require(veyra.setNrLayerParameter(veyra.nrLayers[i].index,"runtime",0),"runtime")
                        probe.require(veyra.setNrLayerParameter(veyra.nrLayers[i].index,"temporal",0),"temporal")
                        probe.require(veyra.setNrLayerParameter(veyra.nrLayers[i].index,"sizePolicy",0),"fixed size")
                    }
                    probe.require(veyra.nrAutoSelectionReason(veyra.nrLayers[0].index)==="","first layer admission")
                    probe.require(!firstEditor.sizeChoices.find(o=>o.id==="6").disabled,"first layer choice")
                    probe.require(secondEditor.sizeChoices.find(o=>o.id==="6").disabled,"second layer must be gray")
                    probe.require(!veyra.setNrLayerParameter(veyra.nrLayers[1].index,"sizePolicy",6),"second Auto refused")
                    probe.require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePolicy",6),"select actual Auto")
                }
                veyra.openPath(probe.media); probe.mark("open"); return
            }
            if(probe.stage===1) {
                if(!veyra.running||veyra.applying||!veyra.nrActive||veyra.position<3)return
                if(probe.mode==="refuse") {
                    probe.require(!veyra.nrAutoActive&&veyra.nrAutoStatus.length>0,"pool refusal must preserve NR with fallback")
                    probe.mark("pool-refused-fixed-nr"); console.log("AUTO_UI_PASS",probe.mode); Qt.quit(); return
                }
                if(!veyra.nrAutoActive||veyra.nrAutoPercent>70)return
                probe.require(veyra.nrAutoStatus.indexOf("70%")>=0||veyra.nrAutoPercent<70,"actual percent status")
                probe.mark("auto-downshift")
                if(probe.mode==="persist") { console.log("AUTO_UI_PASS",probe.mode); Qt.quit(); return }
                veyra.togglePlayPause(); return
            }
            if(probe.stage===2) {
                if(!veyra.paused||Date.now()-probe.phaseAt<600)return
                probe.pausePosition=veyra.position; probe.mark("paused"); return
            }
            if(probe.stage===3) {
                if(Date.now()-probe.phaseAt<1200)return
                probe.require(veyra.paused&&Math.abs(veyra.position-probe.pausePosition)<0.08,"paused position")
                veyra.seekTo(12); probe.mark("seek-paused"); return
            }
            if(probe.stage===4) {
                if(Date.now()-probe.phaseAt<1000||veyra.applying||Math.abs(veyra.position-12)>0.3)return
                probe.require(veyra.paused&&veyra.nrAutoActive,"pause seek kept Auto")
                veyra.playbackRate=0.5; veyra.togglePlayPause(); probe.mark("resume-half-speed"); return
            }
            if(probe.stage===5) {
                if(veyra.applying||veyra.paused||!veyra.nrActive||veyra.nrAutoPercent!==100)return
                probe.mark("auto-upshift")
                probe.require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"intensity",0.7),"model uniform")
                probe.mark("model-edit"); return
            }
            if(probe.stage===7) {
                if(Date.now()-probe.phaseAt<2000||veyra.applying)return
                probe.require(veyra.nrAutoActive&&Math.abs(veyra.nrLayers[0].intensity-0.7)<0.000001,"uniform retained")
                probe.require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePolicy",0),"Auto off")
                probe.mark("select-fixed"); return
            }
            if(probe.stage===8) {
                if(veyra.applying||veyra.nrAutoActive||!veyra.nrActive)return
                probe.mark("fixed-active")
                probe.require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePolicy",6),"Auto reenable")
                return
            }
            if(probe.stage===9) {
                if(veyra.applying||!veyra.nrAutoActive||!veyra.nrActive)return
                probe.mark("auto-reenabled"); veyra.colorEnabled=true; return
            }
            if(probe.stage===10) {
                if(veyra.applying||veyra.nrAutoActive||!veyra.nrActive||!veyra.colorEnabled)return
                probe.require(veyra.nrLayers[0].sizePolicy===6&&veyra.nrAutoStatus.length>0,"unsupported saved Auto fallback")
                probe.require(firstEditor.sizeChoices.find(o=>o.id==="6").disabled,"color gray choice")
                probe.mark("color-fixed-fallback"); veyra.colorEnabled=false; return
            }
            if(probe.stage===11) {
                if(veyra.applying||!veyra.nrAutoActive||!veyra.nrActive)return
                probe.mark("color-off-auto-restored")
                probe.require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"temporal",1),"temporal on"); return
            }
            if(probe.stage===12) {
                if(veyra.applying||veyra.nrAutoActive||!veyra.nrActive||!veyra.nrLayers[0].temporal)return
                probe.require(firstEditor.sizeChoices.find(o=>o.id==="6").disabled,"temporal gray choice")
                probe.mark("temporal-fixed-fallback")
                probe.require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"temporal",0),"temporal off"); return
            }
            if(probe.stage===13) {
                if(veyra.applying||!veyra.nrAutoActive||!veyra.nrActive)return
                probe.mark("temporal-off-auto-restored"); veyra.playbackRate=1
                console.log("AUTO_UI_PASS",probe.mode); Qt.quit()
            }
        }
    }
}
