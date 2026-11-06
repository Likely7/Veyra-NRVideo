import QtQuick
// RTX Video SR families in the real list page: algorithm (standard / high bitrate /
// streaming) and tier selection, double SR with a second family, and the card summary.
// Runner: the same Loader injection as sr-stage-ui.py; expects a 720p clip of 30 s or more.
Item {
    id: test
    property string media: ""
    property string shot: ""   // screenshot path from the runner; empty skips it
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
        function ids(list){return (list||[]).map(o=>o.id).join(",")}
        function labels(list){return (list||[]).map(o=>o.label).join("")}
        onTriggered:{try{
            ++ticks;if(++total>220)throw new Error("bounded SR family test step="+step)
            if(step===0){
                if(ticks<4)return
                veyra.muted=true;veyra.nrEnabled=false;veyra.fgEnabled=false
                veyra.srEnabled=true;veyra.videoSrQuality=4;veyra.dualSrEnabled=false;veyra.srOutput=0;veyra.srTargetIndex=2
                veyra.openPath(test.media);next(1);return
            }
            if(step===1){
                if(!settled())return
                const backend=find("list-sr-backend"),tiers=find("list-sr-quality")
                check(backend!==null&&tiers!==null,"list SR algorithm and tier controls exist")
                check(ids(backend.options)==="0,rtx,rtx-hb,rtx-st,5","algorithms: DLSS, three RTX Video SR families, FSR ("+ids(backend.options)+")")
                check(backend.value===backend.options[1].label&&tiers.current==="4"&&labels(tiers.options)==="低中高超高","standard family at ultra with four named tiers")
                check(find("list-sr").summary.indexOf(backend.options[1].label)>=0&&find("list-sr").summary.indexOf(" · 超高")>=0,
                      "card summary names the family and tier: "+find("list-sr").summary)
                backend.picked("rtx-hb");next(2);return
            }
            if(step===2){
                if(!settled())return
                check(veyra.videoSrQuality===19&&veyra.srActive&&veyra.outputSummary==="3840x2160","high bitrate keeps the ultra tier and runs 720p -> 4K")
                find("list-sr-quality").picked("2");next(3);return
            }
            if(step===3){
                if(!settled())return
                check(veyra.videoSrQuality===17&&veyra.srActive,"high bitrate medium tier")
                find("list-sr-backend").picked("rtx-st");next(4);return
            }
            if(step===4){
                if(!settled())return
                const tiers=find("list-sr-quality")
                check(veyra.videoSrQuality===21&&labels(tiers.options)==="中超高"&&tiers.current==="2"&&veyra.srActive,"streaming offers medium and ultra, keeps medium and runs")
                find("list-dual-sr-toggle").toggled(true);next(5);return
            }
            if(step===5){
                if(!settled())return
                check(veyra.dualSrEnabled&&find("list-dual-sr-panel").visible,"double SR opens on the streaming family")
                check(veyra.srStageSummary==="1280x720 → 3840x2160 → 1920x1080 → 3840x2160","streaming base, reduction, second pass: "+veyra.srStageSummary)
                const algo=find("list-dual-sr-algorithm")
                check(algo!==null&&ids(algo.options)==="rtx,rtx-hb,rtx-st","second pass offers the three RTX Video SR families")
                algo.picked("rtx-hb");next(6);return
            }
            if(step===6){
                if(!settled())return
                check(veyra.videoSrQuality===21&&veyra.dualSrQuality===19,"the second family changes only the second pass")
                check(veyra.srActive&&veyra.srStageSummary==="1280x720 → 3840x2160 → 1920x1080 → 3840x2160","streaming + high bitrate double SR runs")
                check(find("list-sr").summary.indexOf("中 / 高码率 超高")>=0,"card summary shows both passes: "+find("list-sr").summary)
                test.saved=false
                if(test.shot.length>0)test.parent.parent.grabToImage(r=>{r.saveToFile(test.shot);test.saved=true})
                else test.saved=true
                next(7);return
            }
            if(step===7){
                if(!test.saved||ticks<3)return
                find("list-dual-sr-quality").picked("1");next(8);return
            }
            if(step===8){
                if(!settled())return
                check(veyra.dualSrQuality===16&&veyra.videoSrQuality===21&&veyra.srActive,"second pass low tier, base unchanged")
                find("list-sr-backend").picked("rtx");next(9);return
            }
            if(step===9){
                if(!settled())return
                check(veyra.videoSrQuality===2&&veyra.dualSrEnabled&&veyra.dualSrQuality===16&&veyra.srActive,
                      "back to the standard family at the same tier; the high bitrate second pass stays")
                veyra.logUi("sr-stage-test","SR_STAGE_UI_PASS");console.log("SR_STAGE_UI_PASS");Qt.quit()
            }
        }catch(e){veyra.logUi("sr-stage-test","SR_STAGE_UI_FAIL "+e.message);console.log("SR_STAGE_UI_FAIL "+e.message);Qt.quit()}}
    }
}
