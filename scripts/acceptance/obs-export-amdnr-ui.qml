import QtQuick
Item {
 id:test
 property string caseName:"obs"
 property string media:""
 property var appWindow:Window.window
 property int phase:0
 property int ticks:0
 property int job:0
 property int waitTicks:0
 property var scroller:null
 function findNamed(item,name){if(item.objectName===name)return item;for(const c of item.children||[]){const r=findNamed(c,name);if(r)return r}return null}
 function require(v,s){if(!v)throw new Error(s)}
 function findScroll(item){
  if(item.visible&&typeof item.contentY==="number"&&item.contentHeight>item.height+50){const p=item.mapToItem(test.appWindow.contentItem,0,0);if(p.x>test.appWindow.width/2)return item}
  for(const c of item.children||[]){const r=findScroll(c);if(r)return r}return null
 }
 Timer {
  interval:200;running:true;repeat:true
  onTriggered:{try{
   ++test.ticks;test.require(test.ticks<1250,"250s deadline phase="+test.phase)
   if(test.phase===0){
    if(test.ticks<12)return
    test.appWindow.width=1280;test.appWindow.height=800;test.appWindow.page="pro"
    veyra.nodeMode=0;veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
    if(!veyra.nrLayers.length)veyra.addEffect("nr")
    veyra.setNrLayerParameter(veyra.nrLayers[0].index,"runtime",3)
    test.waitTicks=0;test.phase=1
   }else if(test.phase===1){
    ++test.waitTicks
    if(test.caseName==="obs"&&test.waitTicks===8){
     const stage=test.findNamed(test.appWindow.contentItem,"list-nr-stage")
     const layer=test.findNamed(test.appWindow.contentItem,"nr-card-1")
     test.require(stage!==null&&layer!==null,"NR inspector cards missing")
     stage.open=true;layer.open=true
    }
    if(test.waitTicks<16)return
    if(test.caseName==="obs"){
     test.scroller=test.findScroll(test.appWindow.contentItem);test.require(test.scroller!==null,"visible inspector scroll missing")
     console.log("OBS_RECT",JSON.stringify(test.scroller.mapToItem(test.appWindow.contentItem,0,0)),test.scroller.width,test.scroller.height)
     test.scroller.contentY=0;test.waitTicks=0;test.phase=2
    }else{
     veyra.exportCompletionSound=false;veyra.exportHevc=false;veyra.exportRateControl=1;veyra.exportBitrateMbps=12
     test.phase=20
    }
   }else if(test.phase===2){
    if(++test.waitTicks<8)return
    console.log("OBS_BASE_READY",test.scroller.contentY)
    test.waitTicks=0;test.phase=3
   }else if(test.phase===3){
    const sequence=[0,90,210,380,200,90,0,360,0]
    const i=Math.floor(test.waitTicks/8);if(i>=sequence.length){test.waitTicks=0;test.phase=4;return}
    if(test.waitTicks%8===0)test.scroller.contentY=Math.min(sequence[i],test.scroller.contentHeight-test.scroller.height)
    if(test.waitTicks%8===6)console.log("OBS_SCROLL_READY",i,test.scroller.contentY)
    ++test.waitTicks
   }else if(test.phase===4){
    if(++test.waitTicks<8)return
    console.log("OBS_DONE");Qt.quit()
   }else if(test.phase===20){
    veyra.srEnabled=test.job===3;veyra.nrEnabled=test.job>0
    const n=veyra.nrLayers[0].index
    veyra.setNrLayerParameter(n,"sizePolicy",2)
    veyra.setNrLayerParameter(n,"style",1);veyra.setNrLayerParameter(n,"total",2)
    veyra.setNrLayerParameter(n,"correctionEnabled",1)
    if(test.job===2){const d=veyra.duplicateNrLayer(n);test.require(d>=0,"second layer add");veyra.setNrLayerParameter(d,"style",2)}
    if(test.job===3&&veyra.nrLayers.length>1)veyra.removeEffect(veyra.nrLayers[1].index)
    veyra.addExportFiles([test.media]);test.phase=21;test.waitTicks=0
   }else if(test.phase===21){
    if(veyra.exportReadyCount<1)return
    veyra.startExport();test.require(veyra.exportRunning,"start refused")
    console.log("FIELD_EXPORT_STARTED",test.job,JSON.stringify(veyra.nrLayers));test.phase=22;test.waitTicks=0
   }else if(test.phase===22){
    if(veyra.exportRunning)return
    test.require(veyra.exportItems.some(i=>i.state==="done"),"export failed "+JSON.stringify(veyra.exportItems))
    console.log("FIELD_EXPORT_DONE",test.job,JSON.stringify(veyra.exportItems));veyra.clearFinishedExportItems()
    if(++test.job<4){test.phase=20;return}
    console.log("FIELD_EXPORT_PASS");Qt.quit()
   }
  }catch(e){console.error("FIELD_UI_FAIL",test.phase,e.message);Qt.quit()}}
 }
}
