import QtQuick
Item {
 id:test
 property var config:({})
 property var appWindow:Window.window
 property int ticks:0
 property int phase:0
 property int readyTick:0
 property bool exportStarted:false
 property double pausePosition:0
 property int uiFrames:0
 function require(v,s){if(!v)throw new Error(s)}
 function preset(){const p=veyra.presets.find(p=>p.name==="FSR repair regression");require(p!==undefined,"saved list preset missing");require(veyra.applyPresetIndex(p.index),"apply preset refused")}
 function saved(){console.log("REPAIR_UI_SAVED",JSON.stringify({backend:veyra.fgBackendName,multiplier:veyra.fgMultiplier,enabled:veyra.fgEnabled,flow:veyra.flowQuality,content:veyra.contentRate}));require(veyra.fgBackendName==="fsr3"&&veyra.fgMultiplier===2&&veyra.fgEnabled,"FSR saved choice");require(veyra.flowQuality===2&&veyra.contentRate===2,"shared flow saved in list preset")}
 Connections {target:veyra;function onNotice(text,error){console.log("REPAIR_UI_NOTICE",text,error)}}
 Connections {target:test.appWindow;function onFrameSwapped(){++test.uiFrames}}
 Timer {interval:300;running:true;repeat:true;onTriggered:{try{
  ++test.ticks;test.require(test.ticks<800,"240s deadline phase="+test.phase)
  if(test.phase===0){
   if(test.ticks<10)return
   if(test.config.case==="seed-dlss"){
    veyra.nodeMode=0;veyra.nrEnabled=false;veyra.srEnabled=false
    veyra.fgBackendName="dlss";veyra.fgMultiplier=6;veyra.fgEnabled=true
    test.require(veyra.fgBackendName==="dlss"&&veyra.fgEnabled&&veyra.fgMultiplier===6,"seed live list DLSS 6X")
    veyra.nodeMode=1;veyra.fgBackendName="dlss";veyra.fgMultiplier=4;veyra.fgEnabled=false
    test.require(veyra.nodeMode===1&&veyra.fgBackendName==="dlss","seed inactive node DLSS choice")
    veyra.nodeMode=0;test.require(veyra.fgEnabled&&veyra.fgMultiplier===6,"seed mode roundtrip")
    console.log("REPAIR_UI_PASS");Qt.quit();return
   }
   if(["migration-before","migration","nvidia-keep","xess-keep"].includes(test.config.case)){
    console.log("REPAIR_UI_DEFAULT",test.config.case,veyra.fgBackendName,veyra.fgMultiplier,veyra.fgEnabled,veyra.nodeMode)
    const old=test.config.case==="migration-before"
    const nv=test.config.case==="nvidia-keep"
    const expected=old||nv?"dlss":test.config.case==="xess-keep"?"xess":"fsr3"
    test.require(veyra.nodeMode===0&&veyra.fgBackendName===expected,"restored list backend")
    test.require(veyra.fgMultiplier===(old?1:nv?6:2)&&veyra.fgEnabled===!old,"list multiplier and enabled state")
    veyra.nodeMode=1
    test.require(veyra.nodeMode===1&&veyra.fgBackendName===(old||nv?"dlss":"fsr3"),"inactive node backend restored")
    test.require(!veyra.fgEnabled,"disabled node stage must remain disabled")
    veyra.nodeMode=0
    test.require(veyra.fgBackendName===expected&&veyra.fgEnabled===!old,"list return preserves backend and switch")
    if(test.config.case==="migration"){
     veyra.fgBackendName="xess";test.require(veyra.fgBackendName==="xess"&&veyra.fgMultiplier===2,"manual XeSS choice remains available")
    }
    console.log(old?"REPAIR_UI_EXPECTED_OLD":"REPAIR_UI_PASS");Qt.quit();return
   }
   if(test.config.restore){
    test.saved();veyra.flowQuality=0;veyra.contentRate=0;veyra.fgEnabled=false
    test.preset();test.saved();console.log("REPAIR_UI_PASS");Qt.quit();return
   }
   if(test.config.case==="telemetry"){
    veyra.nodeMode=0;veyra.muted=true;veyra.reducedMotion=false
    veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false;veyra.videoHdr=false;veyra.colorEnabled=false
    if(!veyra.nrLayers.length)veyra.addEffect("nr")
    test.require(veyra.nrLayers.length===1,"single list NR layer")
    const n=veyra.nrLayers[0].index
    for(const p of [["runtime",3],["sizePolicy",0],["style",0],["total",1],["temporal",0],["antiFlicker",0],["correctionEnabled",0]])
     test.require(veyra.setNrLayerParameter(n,p[0],p[1]),"list NR parameter "+p[0])
    veyra.nrEnabled=true;veyra.displaySync=2
    veyra.openPath(test.config.media);test.phase=10;return
   }
   veyra.nodeMode=0;veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
   console.log("REPAIR_UI_CATALOG",JSON.stringify(veyra.fgBackendChoices),JSON.stringify(veyra.effectCatalog))
   const generic=veyra.effectCatalog.find(p=>p.id==="frame-generation")
   const dlss=veyra.fgBackendChoices.find(p=>p.id==="dlss")
   const fsr=veyra.fgBackendChoices.find(p=>p.id==="fsr3")
   test.require(generic!==undefined&&fsr!==undefined&&!fsr.disabled,"FSR component available")
   if(test.config.catalogOnly){
    test.require(dlss.disabled,"missing NVIDIA runtime is required to reproduce old generic-entry gate")
    if(!test.config.before)test.require(veyra.fgBackendName==="fsr3","missing DLSS provider defaults to FSR before adding")
    const added=veyra.addEffect("frame-generation")
    if(test.config.before){test.require(generic.disabled&&added<0,"old gate did not reproduce");console.log("REPAIR_UI_EXPECTED_OLD");Qt.quit();return}
    test.require(!generic.disabled&&added>=0,"generic FG entry must use any available provider")
   }else{
    // The fresh NVIDIA list already contains its disabled FG stage.
    // The missing-runtime package above exercises adding the absent stage.
    veyra.fgBackendName="dlss";veyra.fgMultiplier=6
   }
   veyra.fgBackendName="fsr3";veyra.fgMultiplier=2;veyra.fgEnabled=true
   veyra.flowQuality=2;veyra.contentRate=2;veyra.fgMotionSource=0
   test.saved();test.require(veyra.savePresetAs("FSR repair regression",21,false),"save list Chain+FG+Flow preset")
   veyra.flowQuality=0;veyra.contentRate=0;veyra.fgEnabled=false
   test.preset();test.saved()
   if(test.config.catalogOnly){console.log("REPAIR_UI_PASS");Qt.quit();return}
   veyra.displaySync=2;veyra.outputRateMode=0
   veyra.openPath(test.config.media);test.phase=1
  }else if(test.phase>=10){
   test.require(!veyra.failed,"telemetry lifecycle player failed")
   const dwell=test.ticks-test.readyTick
   if(test.phase===10){
    if(!veyra.running||!veyra.nrActive||veyra.position<2)return
    if(!test.readyTick)test.readyTick=test.ticks
    if(dwell<10)return
    test.pausePosition=veyra.position;veyra.togglePlayPause();test.phase=11;test.readyTick=test.ticks
   }else if(test.phase===11){
    if(dwell<6)return
    test.require(veyra.paused&&Math.abs(veyra.position-test.pausePosition)<0.2,"pause must retain media position")
    console.log("REPAIR_UI_TELEMETRY","paused",test.uiFrames,JSON.stringify(veyra.stageTimings))
    veyra.togglePlayPause();test.phase=12;test.readyTick=test.ticks
   }else if(test.phase===12){
    if(dwell<10)return
    test.require(!veyra.paused&&veyra.nrActive,"resume NR")
    test.appWindow.showFullScreen();test.phase=13;test.readyTick=test.ticks
   }else if(test.phase===13){
    if(dwell<10)return
    test.require(veyra.nrActive,"fullscreen NR")
    test.appWindow.showNormal();veyra.seekTo(12);test.phase=14;test.readyTick=test.ticks
   }else if(test.phase===14){
    if(dwell<10||veyra.position<12)return
    test.appWindow.page="node";test.require(veyra.nodeMode===1,"node page mode")
    const i=veyra.addEffect("nr");test.require(i>=0,"add node NR")
    for(const p of [["runtime",3],["sizePolicy",0],["temporal",0],["antiFlicker",0],["correctionEnabled",0]])
     test.require(veyra.setNrLayerParameter(i,p[0],p[1]),"node NR parameter "+p[0])
    const node=veyra.chain.find(p=>p.index===i)
    test.require(node!==undefined&&veyra.insertNodeAfter(node.id,0),"connect NR between input and output")
    test.phase=15;test.readyTick=test.ticks
   }else if(test.phase===15){
    if(dwell<12||!veyra.nrActive)return
    console.log("REPAIR_UI_TELEMETRY","node",JSON.stringify(veyra.nodeTimings),JSON.stringify(veyra.nodeConnections))
    veyra.nodeMode=0;test.appWindow.page="pro";test.phase=16;test.readyTick=test.ticks
   }else if(test.phase===16){
    if(dwell<10||!veyra.nrActive)return
    test.require(veyra.nrLayers.length===1&&veyra.nrLayers[0].runtime===3&&!veyra.fgEnabled&&!veyra.srEnabled,"list configuration survives mode roundtrip")
    veyra.reducedMotion=true;test.phase=17;test.readyTick=test.ticks
   }else if(test.phase===17){
    if(dwell<5)return
    test.require(veyra.reducedMotion&&veyra.nrActive,"reduced-motion switch preserves NR")
    veyra.stopPlayback();test.phase=18;test.readyTick=test.ticks
   }else if(test.phase===18){
    if(dwell<5)return
    test.require(!veyra.running,"stop must finish")
    console.log("REPAIR_UI_PASS");Qt.quit()
   }
  }else if(test.phase===1){
   test.require(!veyra.failed,"FSR preview failed "+veyra.runStatus)
   if(!veyra.running||veyra.position<2)return
   test.require(veyra.fgBackendName==="fsr3"&&veyra.fgEnabled,"live FSR selection rolled back")
   if(!test.readyTick)test.readyTick=test.ticks
   console.log("REPAIR_UI_SAMPLE",JSON.stringify({position:veyra.position,fps:veyra.submitFps,backend:veyra.fgBackendName,status:veyra.presentationStatus,detail:veyra.runStatusDetail}))
   if(test.ticks-test.readyTick<12)return
   test.appWindow.showFullScreen();test.phase=2;test.readyTick=test.ticks
  }else if(test.phase===2){
   test.require(!veyra.failed,"fullscreen preview failed")
   if(test.ticks-test.readyTick<12)return
   test.appWindow.showNormal();test.phase=3;test.readyTick=test.ticks
  }else if(test.phase===3){
   if(test.ticks-test.readyTick<8)return
   veyra.exportCompletionSound=false;veyra.exportHevc=false;veyra.exportRateControl=1
   veyra.addExportFiles([test.config.exportMedia]);test.phase=4
  }else if(test.phase===4){
   if(veyra.exportReadyCount<1)return
   veyra.startExport();test.require(veyra.exportRunning,"FSR GUI export rejected")
   test.exportStarted=true;test.phase=5
  }else if(test.phase===5){
   if(veyra.exportRunning)return
   test.require(veyra.exportItems.some(p=>p.state==="done"),"FSR GUI export failed "+JSON.stringify(veyra.exportItems))
   console.log("REPAIR_UI_EXPORT",JSON.stringify(veyra.exportItems));test.saved()
   console.log("REPAIR_UI_PASS");Qt.quit()
  }
 }catch(e){console.error("REPAIR_UI_FAIL",test.phase,e.message);Qt.exit(3)}}}
}
