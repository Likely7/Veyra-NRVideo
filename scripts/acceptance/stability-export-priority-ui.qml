import QtQuick
Item {
 id:test
 property var config:({})
 property var appWindow:Window.window
 property int ticks:0
 property int phase:0
 property int readyTick:0
 property bool exportStarted:false
 function require(v,s){if(!v)throw new Error(s)}
 function preset(){const p=veyra.presets.find(p=>p.name==="FSR repair regression");require(p!==undefined,"saved list preset missing");require(veyra.applyPresetIndex(p.index),"apply preset refused")}
 function saved(){console.log("REPAIR_UI_SAVED",JSON.stringify({backend:veyra.fgBackendName,multiplier:veyra.fgMultiplier,enabled:veyra.fgEnabled,flow:veyra.flowQuality,content:veyra.contentRate}));require(veyra.fgBackendName==="fsr3"&&veyra.fgMultiplier===2&&veyra.fgEnabled,"FSR saved choice");require(veyra.flowQuality===2&&veyra.contentRate===2,"shared flow saved in list preset")}
 Connections {target:veyra;function onNotice(text,error){console.log("REPAIR_UI_NOTICE",text,error)}}
 Timer {interval:300;running:true;repeat:true;onTriggered:{try{
  ++test.ticks;test.require(test.ticks<800,"240s deadline phase="+test.phase)
  if(test.phase===0){
   if(test.ticks<10)return
   if(test.config.restore){
    test.saved();veyra.flowQuality=0;veyra.contentRate=0;veyra.fgEnabled=false
    test.preset();test.saved();console.log("REPAIR_UI_PASS");Qt.quit();return
   }
   veyra.nodeMode=0;veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
   console.log("REPAIR_UI_CATALOG",JSON.stringify(veyra.fgBackendChoices),JSON.stringify(veyra.effectCatalog))
   const generic=veyra.effectCatalog.find(p=>p.id==="frame-generation")
   const dlss=veyra.fgBackendChoices.find(p=>p.id==="dlss")
   const fsr=veyra.fgBackendChoices.find(p=>p.id==="fsr3")
   test.require(generic!==undefined&&fsr!==undefined&&!fsr.disabled,"FSR component available")
   if(test.config.catalogOnly){
    test.require(dlss.disabled,"missing NVIDIA runtime is required to reproduce old generic-entry gate")
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
