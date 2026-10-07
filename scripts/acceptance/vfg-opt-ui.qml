// Disposable staging loader using the actual bridge, engine and export worker.
import QtQuick
Item {
    id: test
    property var appWindow: Window.window
    property string media: ""
    property string shortMedia: ""
    property string evidence: ""
    property string phase: "run"
    property int step: 0
    property int ticks: 0
    property int waited: 0
    property int current: 0
    property real beforePosition: 0
    property var cases: [{m:2,q:1},{m:3,q:1},{m:4,q:1},{m:5,q:1},{m:6,q:1},{m:7,q:1},{m:8,q:1},{m:8,q:0},{m:8,q:2}]
    function check(ok,why){if(!ok){console.error("VFG_UI_FAIL",phase,step,why);timer.stop();Qt.quit();throw new Error(why)}}
    function next(n){step=n;waited=0}
    function find(item,name){if(item.objectName===name)return item;for(const child of item.children||[]){const r=find(child,name);if(r)return r}return null}
    function findNodePage(item){if(typeof item.selectId==="function")return item;for(const child of item.children||[]){const r=findNodePage(child);if(r)return r}return null}
    function findProPage(item){if(typeof item.overlayContains==="function"&&typeof item.tab==="string")return item;for(const child of item.children||[]){const r=findProPage(child);if(r)return r}return null}
    function preset(name){return veyra.presets.findIndex(x=>x.name===name)}
    function startCase(){const c=cases[current];beforePosition=veyra.position;veyra.fgMultiplier=c.m;veyra.vfgQuality=c.q;next(2)}
    Connections {target: veyra;function onNotice(message,error){console.log("VFG_NOTICE",message,error)}}
    Timer {id: timer;interval: 200;running: true;repeat: true
        onTriggered: {try {
            if(++test.ticks>1150)test.check(false,"230-second deadline");++test.waited
            if(!test.media.length)return
            const rtss=test.find(test.appWindow.contentItem,"overlay-restart-confirm")
            if(rtss&&rtss.shown){rtss.close();rtss.rejected();console.log("VFG_UI_RTSS_DEFER")}
            if(test.step===0){
                if(test.waited<10)return
                test.check(veyra.fgBackendChoices.some(x=>x.id==="vfg"),"VFG backend menu")
                if(test.phase==="restore"){
                    test.check(veyra.nodeMode===0&&veyra.fgBackendName==="vfg"&&veyra.fgMultiplier===8&&veyra.vfgQuality===1,"list restart preserves 8X Medium")
                    test.check(test.preset("VFG List 8 Medium")>=0&&test.preset("VFG Node 8 High")>=0,"preset restart")
                    veyra.nodeMode=1;test.check(veyra.fgBackendName==="vfg"&&veyra.fgMultiplier===8&&veyra.vfgQuality===2,"node restart preserves independent 8X High")
                    test.check(veyra.applyPresetIndex(test.preset("VFG Node 8 High")),"node VFG preset applies")
                    veyra.nodeMode=0;test.check(veyra.vfgQuality===1,"switch back restores list Medium")
                    console.log("VFG_UI_RESTORE_PASS");timer.stop();Qt.quit();return
                }
                veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false
                test.appWindow.page="pro"
                const professional=test.findProPage(test.appWindow.contentItem)
                if(professional)professional.tab="fg"
                if(test.phase==="missing"){
                    veyra.openPath(test.media);test.next(20);return
                }
                test.check(veyra.addEffect("vfg-fg")>=0,"add VFG list stage")
                const factor=test.find(test.appWindow.contentItem,"list-vfg-multiplier")
                const quality=test.find(test.appWindow.contentItem,"list-vfg-quality")
                test.check(factor&&quality,"list controls created")
                test.check(factor.options.map(x=>x.id).join(",")==="2,3,4,5,6,7,8","all seven multiplier controls")
                for(let q=0;q<3;++q){quality.picked(String(q));test.check(veyra.vfgQuality===q,"quality control commits "+q)}
                for(let m=2;m<=8;++m){factor.picked(String(m));test.check(veyra.fgMultiplier===m,"multiplier control commits "+m)}
                veyra.vfgQuality=1;veyra.fgMultiplier=2;veyra.openPath(test.media);test.next(1)
            }else if(test.step===1){
                if(!veyra.running||!veyra.hasSource||veyra.applying)return
                test.startCase()
            }else if(test.step===2){
                if(test.waited<12||veyra.applying)return
                const c=test.cases[test.current]
                test.check(veyra.running&&veyra.fgActive&&veyra.fgBackendName==="vfg","VFG inactive "+veyra.statusText)
                test.check(veyra.fgMultiplier===c.m&&veyra.vfgQuality===c.q,"hot settings rollback")
                test.check(veyra.position>test.beforePosition+0.3,"playback clock stopped")
                test.check(veyra.runStatusDetail.indexOf("NVIDIA VFG")>=0,"actual running backend label")
                console.log("VFG_UI_CASE",c.m,c.q,"submitFps",veyra.submitFps,"detail",veyra.runStatusDetail)
                if(++test.current<test.cases.length){test.startCase();return}
                veyra.vfgQuality=1;test.next(3)
            }else if(test.step===3){
                if(test.waited<8||veyra.applying)return
                test.check(veyra.savePresetAs("VFG List 8 Medium",15,false),"save list VFG preset")
                test.next(12)
                test.appWindow.contentItem.grabToImage(r=>{test.check(r.saveToFile(test.evidence+"/vfg-list.png"),"list screenshot");veyra.fgBackendName="dlss";veyra.fgMultiplier=6;test.next(15)})
            }else if(test.step===15){
                if(test.waited<12||veyra.applying)return
                if(veyra.runStatusDetail.indexOf("DLSS")<0||veyra.runStatusDetail.indexOf("6X")<0){if(test.waited<100)return;test.check(false,"DLSS 6X actual running label")}
                test.check(veyra.fgActive&&veyra.fgBackendName==="dlss"&&veyra.fgMultiplier===6,"legacy DLSS 6X works after VFG 8X")
                console.log("VFG_UI_DLSS6_PASS",veyra.runStatusDetail)
                veyra.fgBackendName="vfg";veyra.fgMultiplier=8;test.next(16)
            }else if(test.step===16){
                if(test.waited<12||veyra.applying)return
                if(veyra.runStatusDetail.indexOf("NVIDIA VFG")<0||veyra.runStatusDetail.indexOf("8X")<0){if(test.waited<100)return;test.check(false,"VFG 8X actual running label after DLSS")}
                test.check(veyra.fgActive&&veyra.fgBackendName==="vfg"&&veyra.fgMultiplier===8,"VFG 8X restores after DLSS 6X")
                veyra.togglePlayPause();test.next(4)
            }else if(test.step===4){
                if(!veyra.paused)return
                veyra.seekTo(2);test.appWindow.width=1160;test.appWindow.height=740;test.next(5)
            }else if(test.step===5){
                if(test.waited<10)return
                test.check(veyra.paused&&veyra.position>=1.9,"paused seek")
                veyra.togglePlayPause();test.next(6)
            }else if(test.step===6){
                if(test.waited<10||veyra.applying)return
                test.check(veyra.running&&!veyra.paused&&veyra.position>2.5,"resume after seek/resize")
                test.beforePosition=veyra.position;test.appWindow.toggleFullscreen();test.next(17)
            }else if(test.step===17){
                if(test.waited<10||veyra.applying)return
                test.check(test.appWindow.fullscreen&&veyra.running&&veyra.fgActive&&veyra.position>test.beforePosition+0.5,"VFG fullscreen playback")
                test.beforePosition=veyra.position;test.appWindow.toggleFullscreen();test.next(18)
            }else if(test.step===18){
                if(test.waited<10||veyra.applying)return
                test.check(!test.appWindow.fullscreen&&veyra.running&&veyra.fgActive&&veyra.position>test.beforePosition+0.5,"VFG return to window playback")
                console.log("VFG_UI_FULLSCREEN_PASS")
                veyra.nodeMode=1;test.appWindow.page="node";test.check(veyra.addEffect("vfg-fg")>=0,"add VFG node")
                veyra.fgMultiplier=8;veyra.vfgQuality=2;test.next(7)
            }else if(test.step===7){
                if(test.waited<10||veyra.applying)return
                test.check(veyra.chainValid&&veyra.fgActive&&veyra.vfgQuality===2,"node VFG running")
                test.check(veyra.effectCatalog.some(x=>x.id==="vfg-fg"),"node add menu includes VFG")
                const page=test.findNodePage(test.appWindow.contentItem),fg=veyra.chain.find(x=>x.type==="frame-generation")
                if(page&&fg)page.selectId(fg.id)
                test.check(veyra.savePresetAs("VFG Node 8 High",15,true),"save node VFG preset")
                test.next(12)
                test.appWindow.contentItem.grabToImage(r=>{test.check(r.saveToFile(test.evidence+"/vfg-node.png"),"node screenshot");veyra.stopPlayback();test.next(8)})
            }else if(test.step===8){
                if(test.waited<8||veyra.hasSource)return
                veyra.nodeMode=0;test.appWindow.page="pro"
                test.check(veyra.fgMultiplier===8&&veyra.vfgQuality===1,"list state survives node High")
                veyra.vfgQuality=2
                veyra.addExportFiles([test.shortMedia]);veyra.exportCompletionSound=false;veyra.exportHevc=true;veyra.exportSrTargetIndex=-1;veyra.exportRateControl=1;veyra.exportBitrateMbps=18
                test.next(9)
            }else if(test.step===9){
                if(veyra.exportReadyCount!==1)return
                veyra.startExport();test.check(veyra.exportRunning,"VFG worker started")
                veyra.vfgQuality=0;veyra.fgMultiplier=2;test.next(10)
            }else if(test.step===10){
                if(veyra.exportRunning)return
                test.check(veyra.exportItems.filter(x=>x.state==="done").length===1,"native VFG worker export")
                veyra.fgMultiplier=8;veyra.vfgQuality=1;test.next(11)
            }else if(test.step===11){
                if(test.waited<10)return
                console.log("VFG_UI_PASS");timer.stop();Qt.quit()
            }else if(test.step===20){
                if(test.waited<20||veyra.applying||!veyra.running)return
                console.log("VFG_UI_MISSING_STATE",JSON.stringify({active:veyra.fgActive,enabled:veyra.fgEnabled,position:veyra.position,backend:veyra.fgBackendName,multiplier:veyra.fgMultiplier,quality:veyra.vfgQuality,warning:veyra.backendWarning}))
                // Existing startup capability migration chooses FSR when a
                // saved VFG runtime is unavailable. Preserve that policy.
                test.check(veyra.fgActive&&veyra.fgEnabled&&veyra.position>0.4&&veyra.fgBackendName==="fsr3"&&veyra.fgMultiplier===2,"existing missing-runtime FSR fallback continues playback")
                test.check(veyra.runStatusDetail.indexOf("FSR")>=0,"actual fallback backend label")
                console.log("VFG_UI_MISSING_PASS",veyra.runStatusDetail);timer.stop();Qt.quit()
            }
        }catch(e){console.error("VFG_UI_FAIL",e.message);timer.stop();Qt.quit()}}
    }
}
