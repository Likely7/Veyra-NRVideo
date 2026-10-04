// Runs only in an owned test copy, with the production bridge and actual GPU.
import QtQuick
Item {
 id: test
 property var appWindow: Window.window
 property string flavor: ""
 property string media: ""
 property string evidence: ""
 property bool migrated: false
 property int step: 0
 property int ticks: 0
 property int waited: 0
 function find(item,name){if(item.objectName===name)return item;for(const c of item.children||[]){const r=find(c,name);if(r)return r}return null}
 function check(ok,why){if(!ok){console.error("POLICY_UI_FAIL",flavor,step,why);timer.stop();Qt.quit();throw new Error(why)}}
 function next(n){step=n;waited=0}
 function shot(name){appWindow.contentItem.grabToImage(r=>check(r.saveToFile(evidence+"/"+name+".png"),"screenshot "+name))}
 Connections {target: veyra;function onNotice(message,error){console.log("POLICY_NOTICE",message,error)}}
 Timer {id:timer;interval:200;repeat:true;running:true
  onTriggered:{try{
   if(++test.ticks>600)test.check(false,"120 second deadline");++test.waited
   if(!test.flavor.length||test.waited<8)return
   const nv=test.flavor==="NVIDIA"
   if(test.step===0){
    const caps=veyra.effectCapabilities
    test.check(veyra.nrRuntimeChoices.length===4,"four NR versions retained")
    for(const row of veyra.nrRuntimeChoices)test.check(row.disabled===(row.id==="4"||!nv),"NR availability "+row.id)
    test.check(veyra.fgBackendChoices.length===5,"five FG backends retained")
    for(const row of veyra.fgBackendChoices)test.check(row.disabled===(row.id==="fsr4"||(!nv&&(row.id==="dlss"||row.id==="vfg"))),"FG availability "+row.id)
    test.check(veyra.srBackendChoices.length===3,"three SR backends retained")
    for(const row of veyra.srBackendChoices)test.check(row.disabled===(!nv&&row.id!=="5"),"SR availability "+row.id)
    test.check(!caps.fsr4.available&&caps.fsr4.reason.length>0,"NVIDIA hardware rejects ML despite shared FSR components")
    test.check(caps.xess.available&&caps.fsr3.available&&caps.sr5.available,"cross-vendor components retained")
    if(test.migrated){
     test.check(!veyra.fgEnabled&&veyra.fgBackendName==="vfg"&&veyra.vfgQuality===1,"unavailable restored FG disabled without losing list parameters")
     veyra.nodeMode=1;test.check(!veyra.fgEnabled&&veyra.fgBackendName==="vfg"&&veyra.vfgQuality===2,"node restore retains parameters and disables FG")
     veyra.nodeMode=0
    }
    const before=veyra.fgBackendName,count=veyra.chain.length
    veyra.fgBackendName="fsr4";test.check(veyra.fgBackendName===before,"direct unsupported FG rejected")
    test.check(veyra.addEffect("fsr4-fg")<0&&veyra.chain.length===count,"unsupported stage addition rejected atomically")
    veyra.nrEnabled=false;veyra.fgEnabled=false;veyra.srEnabled=false
    test.appWindow.page="pro";test.next(1)
   }else if(test.step===1){
    const selector=test.find(test.appWindow.contentItem,"list-sr-backend")
    test.check(selector&&selector.options.length===3,"real List SR selector")
    for(const o of selector.options)test.check(o.disabled===(!nv&&o.id!=="5"),"List disabled choice "+o.id)
    const old=veyra.videoSrQuality
    if(!nv){selector.picked("4");test.check(veyra.videoSrQuality===old,"List cannot select missing NVIDIA runtime")}
    selector.picked("5");test.check(veyra.videoSrQuality===5,"List FSR selection works")
    veyra.srEnabled=true;test.check(veyra.srEnabled,"supported SR enable")
    veyra.setOpticalFlowChoice(1);veyra.openPath(test.media);test.next(2)
   }else if(test.step===2){
    if(veyra.applying||!veyra.running||veyra.position<0.7)return
    test.check(veyra.srActive&&veyra.videoSrQuality===5,"actual FSR SR playback")
    test.shot("list-fsr");veyra.stopPlayback();test.next(3)
   }else if(test.step===3){
    if(veyra.hasSource)return
    veyra.srEnabled=false;veyra.nodeMode=1;test.appWindow.page="node";test.next(4)
   }else if(test.step===4){
    const flow=test.find(test.appWindow.contentItem,"node-flow-choice")
    test.check(flow&&flow.options.map(o=>o.id).join(",")==="0,1","node optical-flow IDs")
    for(const row of veyra.effectCatalog){
     if(["dlss-fg","vfg-fg","video-hdr"].indexOf(row.id)>=0)test.check(row.disabled===!nv,"Node disabled catalog "+row.id)
     if(row.id==="fsr4-fg")test.check(row.disabled&&row.note.length>0,"FSR4 Node entry remains gray with reason")
    }
    test.shot("node");test.next(5)
   }else if(test.step===5){
    console.log("POLICY_UI_PASS",test.flavor,"actual RTX5070; no AMD inference claim");timer.stop();Qt.quit()
   }
  }catch(e){console.error("POLICY_UI_FAIL",e.message);timer.stop();Qt.quit()}}
 }
}
