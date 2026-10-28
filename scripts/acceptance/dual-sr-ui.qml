import QtQuick
Item {
    id: test
    property string media: ""
    property bool saved: false
    function find(name) {
        let hit=null
        const walk=item=>{if(hit)return;if(item.objectName===name){hit=item;return}for(const c of item.children)walk(c)}
        walk(test.parent.parent);return hit
    }
    Timer {
        interval:500;running:test.media.length>0;repeat:true
        property int step:0
        property int ticks:0
        property int total:0
        function next(n){step=n;ticks=0}
        function check(ok,label){if(!ok)throw new Error(label);veyra.logUi("sr-stage-test","PASS "+label)}
        function settled(){return ticks>=7&&veyra.running&&!veyra.applying}
        onTriggered:{try{
            ++ticks;if(++total>200)throw new Error("bounded dual test step="+step)
            if(step===0){
                if(ticks<4)return
                veyra.muted=true;veyra.nrEnabled=false;veyra.fgEnabled=false
                veyra.srEnabled=true;veyra.videoSrQuality=1;veyra.dualSrEnabled=false;veyra.srTargetIndex=3
                veyra.openPath(test.media);next(1);return
            }
            if(step===1){
                if(!settled())return
                check(veyra.srTargetIndex===3&&veyra.outputSummary==="7680x4320","ordinary 8K remains an actual 8K output")
                check(!find("list-dual-sr-panel").visible,"second-pass settings start collapsed")
                veyra.srTargetIndex=2;next(2);return
            }
            if(step===2){
                if(!settled())return
                find("list-dual-sr-toggle").toggled(true);next(3);return
            }
            if(step===3){
                if(!settled())return
                check(veyra.dualSrEnabled&&find("list-dual-sr-panel").visible,"Double SR control expands the second-pass settings")
                check(veyra.srTargetIndex===2&&veyra.srStageSummary==="1920x1080 → 3840x2160 → 1920x1080 → 3840x2160","first SR then reduce then second SR in the running plan")
                find("list-dual-sr-quality").picked("2");find("list-dual-sr-target").picked("1");next(4);return
            }
            if(step===4){
                if(!settled())return
                check(veyra.srTargetIndex===2&&veyra.videoSrQuality===1&&veyra.dualSrTargetIndex===1&&veyra.dualSrQuality===2,"second controls do not change base target or quality")
                check(veyra.outputSummary==="2560x1440","second target owns the final output")
                test.parent.parent.grabToImage(r=>{r.saveToFile("E:/项目/Veyra/tests/codex-dual-sr-20261028/dual-controls.png");test.saved=true})
                next(45);return
            }
            if(step===45){
                if(!test.saved||ticks<3)return
                find("list-dual-sr-toggle").toggled(false);next(5);return
            }
            if(step===5){
                if(!settled())return
                check(!find("list-dual-sr-panel").visible&&veyra.outputSummary==="3840x2160"&&veyra.srStageSummary==="","disable collapses controls and restores base-only output")
                veyra.nodeMode=1;check(veyra.addEffect("sr")>=0,"add base SR to an empty node graph");veyra.navigate("node")
                veyra.videoSrQuality=1;veyra.srTargetIndex=2;next(6);return
            }
            if(step===6){
                if(!settled())return
                check(find("node-dual-sr-toggle")&&!find("node-dual-sr-panel").visible,"node editor has the same collapsed control")
                find("node-dual-sr-toggle").toggled(true);veyra.srPrescale=3;find("node-dual-sr-target").picked("3");next(7);return
            }
            if(step===7){
                if(!settled())return
                check(veyra.srTargetIndex===2&&veyra.dualSrTargetIndex===3&&veyra.outputSummary==="7680x4320","node second 8K target is independent and is not forced to 4K")
                check(veyra.srStageSummary==="1920x1080 → 3840x2160 → 1920x1080 → 7680x4320","node running sizes contain both SR operations")
                veyra.dualSrEnabled=false;veyra.nodeMode=0;veyra.navigate("pro")
                veyra.srTargetIndex=2;veyra.dualSrTargetIndex=2;veyra.dualSrEnabled=true
                veyra.nrEnabled=true;veyra.fgBackendName="vfg";veyra.fgMultiplier=2;veyra.fgEnabled=true
                next(8);return
            }
            if(step===8){
                if(ticks<24||!settled())return
                check(veyra.srActive&&veyra.nrActive&&veyra.fgActive&&veyra.fgBackendName==="vfg","bounded 4K two-pass SR with NR and VFG actually runs")
                check(veyra.outputSummary==="3840x2160"&&veyra.srStageSummary==="1920x1080 → 3840x2160 → 1920x1080 → 3840x2160","combined graph retains both SR operations and the requested 4K output")
                veyra.logUi("sr-stage-test","SR_STAGE_UI_PASS");console.log("SR_STAGE_UI_PASS");Qt.quit()
            }
        }catch(e){veyra.logUi("sr-stage-test","SR_STAGE_UI_FAIL "+e.message);console.log("SR_STAGE_UI_FAIL "+e.message);Qt.quit()}}
    }
}
