import QtQuick
Item {
    id: test
    property string media: ""
    property bool verifyOnly: false
    function find(name){let hit=null;const walk=x=>{if(hit)return;if(x.objectName===name){hit=x;return}for(const c of x.children)walk(c)};walk(test.parent.parent);return hit}
    Timer {
        interval:500;running:media.length>0;repeat:true
        property int step:0
        property int ticks:0
        property int total:0
        function next(n){step=n;ticks=0}
        function check(ok,label){if(!ok)throw new Error(label);veyra.logUi("nvidia-ll-test","PASS "+label)}
        function settled(){return ticks>=8&&veyra.running&&!veyra.applying}
        function finish(){veyra.logUi("nvidia-ll-test","NVIDIA_LL_UI_PASS");console.log("NVIDIA_LL_UI_PASS");Qt.quit()}
        onTriggered:{try{
            ++ticks;if(++total>180)throw new Error("bounded UI step="+step)
            if(step===0){
                if(ticks<4)return
                check(veyra.nvidiaLowLatencyAvailable,"NVIDIA device exposes the three-state control")
                if(test.verifyOnly){check(veyra.nvidiaLowLatency===2,"Boost choice restores after restart");finish();return}
                check(veyra.nvidiaLowLatency===1,"legacy lowQueue=true migrates to On, never Boost")
                veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
                veyra.openPath(test.media);next(1);return
            }
            if(step===1){
                if(!settled())return
                check(veyra.presentationStatus.includes("英伟达低延迟：开（已启用）"),"On is confirmed by the running native driver state")
                const c=find("list-nvidia-low-latency");check(c!==null,"list control exists")
                let p=c;while(p){if(p.tab!==undefined)p.tab="fg";p=p.parent}
                c.picked("2");next(2);return
            }
            if(step===2){
                if(!settled())return
                check(veyra.nvidiaLowLatency===2&&veyra.presentationStatus.includes("开＋增强（已启用）"),"Boost changes the actual native mode")
                veyra.fgBackendName="vfg";veyra.fgMultiplier=2;veyra.fgEnabled=true;next(3);return
            }
            if(step===3){
                if(!settled())return
                check(veyra.fgActive&&veyra.presentationStatus.includes("开＋增强（已启用）"),"Boost remains active with real VFG generation")
                const c=find("list-nvidia-low-latency");let p=c.parent
                while(p){if(p.contentY!==undefined&&p.contentItem){p.contentY=Math.max(0,Math.min(p.contentHeight-p.height,c.mapToItem(p.contentItem,0,0).y-30));break}p=p.parent}
                next(31);return
            }
            if(step===31){
                if(ticks<3)return
                test.parent.parent.grabToImage(r=>r.saveToFile("E:/项目/Veyra/tests/nvidia-low-latency-20261028/controls.png"))
                next(32);return
            }
            if(step===32){
                if(ticks<3)return
                find("list-nvidia-low-latency").picked("0");next(4);return
            }
            if(step===4){
                if(!settled())return
                check(veyra.nvidiaLowLatency===0&&veyra.fgActive&&veyra.presentationStatus.includes("英伟达低延迟：关"),"Off revokes native latency while retaining frame generation")
                veyra.nodeMode=1;check(veyra.addEffect("vfg-fg")>=0,"create frame-generation node for the node control");veyra.navigate("node");next(5);return
            }
            if(step===5){
                if(!settled())return
                check(find("node-nvidia-low-latency")!==null,"node control uses the same three states")
                find("node-nvidia-low-latency").picked("1");next(6);return
            }
            if(step===6){
                if(!settled())return
                check(veyra.nvidiaLowLatency===1&&veyra.presentationStatus.includes("英伟达低延迟：开（已启用）"),"node On applies the same global native setting")
                veyra.nvidiaLowLatency=2;next(7);return
            }
            if(step===7){if(!settled())return;check(veyra.nvidiaLowLatency===2,"save Boost for a separate restart check");finish()}
        }catch(e){veyra.logUi("nvidia-ll-test","NVIDIA_LL_UI_FAIL "+e.message);console.log("NVIDIA_LL_UI_FAIL "+e.message);Qt.quit()}}
    }
}
