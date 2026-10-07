import QtQuick
Item {
 id: probe
 property var config: ({})
 property var appWindow: Window.window
 property int ticks: 0
 property int state: 0
 property double ready: 0
 property int uiFrames: 0
 function require(ok, why) { if (!ok) throw new Error(why) }
 Connections { target: veyra; function onNotice(text,error) { console.log("VFG_DIAG_NOTICE",text,error) } }
 Connections { target: probe.appWindow; function onFrameSwapped() { ++probe.uiFrames } }
 Timer { interval: 500; running: true; repeat: true; onTriggered: { try {
  if (!probe.config.media) return
  ++probe.ticks; probe.require(probe.ticks<260,"130s deadline")
  if (probe.state===0 && probe.ticks>=5) {
   probe.appWindow.page="pro"; probe.appWindow.width=1280; probe.appWindow.height=800
   veyra.nodeMode=0; veyra.muted=true; veyra.reducedMotion=false
   veyra.nrEnabled=false; veyra.srEnabled=false; veyra.fgEnabled=false
   veyra.videoHdr=false; veyra.colorEnabled=false; veyra.lowLatency=false
   veyra.displaySync=probe.config.display; veyra.outputRateMode=0
   veyra.nrMotionSource=probe.config.flow?1:0
   if (!veyra.nrLayers.length) veyra.addEffect("nr")
   probe.require(veyra.nrLayers.length===1,"one NR layer")
   const n=veyra.nrLayers[0].index
   for (const p of [["runtime",3],["sizePolicy",0],["style",0],["total",1],["temporal",0],["antiFlicker",0],["correctionEnabled",0]])
    probe.require(veyra.setNrLayerParameter(n,p[0],p[1]),p[0])
   veyra.nrEnabled=probe.config.nr
   probe.require(veyra.addEffect("vfg-fg")>=0,"add VFG")
   veyra.fgMultiplier=probe.config.multiplier; veyra.vfgQuality=probe.config.quality
   veyra.fgStrict=probe.config.strict
   console.log("VFG_DIAG_CONFIG",JSON.stringify({config:probe.config,layers:veyra.nrLayers,chain:veyra.chain,strict:veyra.fgStrict,nrMotion:veyra.nrMotionSource,cap:veyra.outputRateMode,backend:veyra.fgBackendName,quality:veyra.vfgQuality,multiplier:veyra.fgMultiplier}))
   veyra.openPath(probe.config.media); probe.state=1
  }
  probe.require(!veyra.failed,"engine failed")
  if (probe.state===1 && veyra.running && veyra.fgActive && veyra.position>0 && !veyra.applying) {
   probe.ready=Date.now(); probe.state=2
  }
  if (probe.ticks%2===0) console.log("VFG_DIAG_SAMPLE",JSON.stringify({wall:Date.now(),seconds:probe.ready?(Date.now()-probe.ready)/1000:-1,position:veyra.position,ready:probe.state===2,active:probe.appWindow.active,uiFrames:probe.uiFrames,fps:veyra.submitFps,ratio:veyra.outputRateRatio,total:veyra.chainTotalMs,totalKnown:veyra.chainTotalMsKnown,budget:veyra.stageBudgetMs,stages:veyra.stageTimings,status:veyra.runStatus,detail:veyra.runStatusDetail,nr:veyra.nrActive,fg:veyra.fgActive,backend:veyra.fgBackendName,multiplier:veyra.fgMultiplier,quality:veyra.vfgQuality,skipped:veyra.skippedFrames,queued:veyra.queuedFrames,gpu:veyra.gpuUtilization,gpuKnown:veyra.gpuUtilizationKnown,strict:veyra.fgStrict,cap:veyra.outputRateMode}))
  if (probe.state===2 && Date.now()-probe.ready>=(probe.config.seconds+5)*1000) {
   probe.require(veyra.fgActive && veyra.fgBackendName==="vfg","VFG stayed active")
   probe.require(veyra.fgMultiplier===probe.config.multiplier && veyra.vfgQuality===probe.config.quality,"requested settings retained")
   console.log("VFG_DIAG_PASS"); Qt.quit()
  }
 } catch(e) { console.error("VFG_DIAG_FAIL",e.message); Qt.exit(3) } } }
}
