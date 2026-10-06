import QtQuick
Item {
 id:probe
 property var config:({})
 property var appWindow:Window.window
 property int ticks:0
 property int uiFrames:0
 property double ready:0
 function require(v,s){if(!v)throw new Error(s)}
 Connections {target:veyra;function onNotice(text,error){console.log("REGRESSION_NOTICE",text,error)}}
 Connections {target:probe.appWindow;function onFrameSwapped(){++probe.uiFrames}}
 Timer {interval:1000;running:true;repeat:true;onTriggered:{try{
  ++probe.ticks;probe.require(probe.ticks<125,"125s deadline")
  if(probe.ticks===3){
   probe.appWindow.width=1280;probe.appWindow.height=800;veyra.nodeMode=0;veyra.muted=true;veyra.reducedMotion=probe.config.reducedMotion===true
   veyra.displaySync=probe.config.display===0?0:2
   veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false;veyra.videoHdr=false;veyra.colorEnabled=false;veyra.lowLatency=false
   if(!veyra.nrLayers.length)veyra.addEffect("nr")
   probe.require(veyra.nrLayers.length===1,"one NR layer")
   const n=veyra.nrLayers[0].index
   for(const p of [["runtime",3],["sizePolicy",0],["style",0],["total",1],["temporal",0],["antiFlicker",0]])
    probe.require(veyra.setNrLayerParameter(n,p[0],p[1]),p[0])
   if(probe.config.modern)probe.require(veyra.setNrLayerParameter(n,"correctionEnabled",0),"correction off")
   veyra.nrEnabled=true
   console.log("REGRESSION_CONFIG",JSON.stringify({layers:veyra.nrLayers,nr:veyra.nrEnabled,sr:veyra.srEnabled,fg:veyra.fgEnabled,lowLatency:veyra.lowLatency,reducedMotion:veyra.reducedMotion}))
   veyra.openPath(probe.config.media)
  }
  probe.require(!veyra.failed,"engine failed")
  if(!probe.ready&&veyra.running&&veyra.position>0&&veyra.nrActive)probe.ready=Date.now()
  console.log("REGRESSION_SAMPLE",JSON.stringify({tick:probe.ticks,uiFrames:probe.uiFrames,active:probe.appWindow.active,ready:probe.ready>0,position:veyra.position,fps:veyra.submitFps,nr:veyra.nrActive,stages:veyra.stageTimings,total:veyra.chainTotalMs,detail:veyra.runStatusDetail}))
  if(probe.ready&&Date.now()-probe.ready>=probe.config.seconds*1000){console.log("REGRESSION_PASS");Qt.quit()}
 }catch(e){console.error("REGRESSION_FAIL",e.message);Qt.exit(3)}}}
}
