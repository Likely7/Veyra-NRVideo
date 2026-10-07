import QtQuick
Item {
 id: test
 property string media: ""
 property int step: 0
 property int ticks: 0
 Timer { interval: 500; running: true; repeat: true; onTriggered: { try {
  if(!test.media.length)return
  if(++test.ticks>240)throw new Error("120s fallback deadline")
  if(test.step===0&&test.ticks>4){
   veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.videoHdr=false;veyra.colorEnabled=false
   veyra.nodeMode=0;veyra.addEffect("vfg-fg");veyra.fgMultiplier=4;veyra.vfgQuality=1
   veyra.openPath(test.media);test.step=1
  }
  if(test.step===1&&!veyra.applying&&veyra.running&&veyra.position>2&&veyra.backendWarning.length){
   console.log("VFG_FAILURE_STATE",JSON.stringify({failed:veyra.failed,position:veyra.position,warning:veyra.backendWarning,backend:veyra.fgBackendName,enabled:veyra.fgEnabled,active:veyra.fgActive,multiplier:veyra.fgMultiplier}))
   // fgEnabled is the saved chain node request, not the active backend. The
   // engine recovery log separately verifies applied multiplier=1.
   if(veyra.failed||veyra.fgActive)throw new Error("failed SDK frame remained active")
   console.log("VFG_FAILURE_PASS",JSON.stringify({position:veyra.position,warning:veyra.backendWarning,backend:veyra.fgBackendName,enabled:veyra.fgEnabled,active:veyra.fgActive}))
   Qt.quit()
  }
 } catch(e){console.error("VFG_FAILURE_FAIL",e.message);Qt.exit(3)} } }
}
