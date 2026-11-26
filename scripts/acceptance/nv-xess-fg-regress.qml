import QtQuick
// NVIDIA regression for the shared XeSS FG changes (MAX init, multiplier switch).
// The fixture keeps the session alive across 2X -> 4X -> 2X; the authoritative
// evidence is the engine log: xess-fg enabled=1 result=0 with generatedTotal
// growing in every phase, and the unlock applied/rolled back cleanly on NVIDIA.
// submitFps is only a sanity signal (provider-owned presents); do not gate on it.
Item {
    id: test
    property string media: ""
    property string shot: ""
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
        function check(ok,label){if(!ok)throw new Error(label);veyra.logUi("nv-xess-fg","PASS "+label)}
        function settled(){return ticks>=7&&veyra.running&&!veyra.applying}
        onTriggered:{try{
            ++ticks;if(++total>240)throw new Error("bounded nv xess fg test step="+step)
            if(step===0){
                if(ticks<4)return
                veyra.nodeMode=0;veyra.navigate("pro")
                veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false
                veyra.fgBackendName="xess";veyra.fgMultiplier=2;veyra.fgEnabled=true
                veyra.openPath(test.media);next(1);return
            }
            if(step===1){
                if(!settled())return
                if(veyra.paused)veyra.togglePlayPause()
                check(veyra.fgActive,"XeSS FG 2X active on NVIDIA")
                next(2);return
            }
            if(step===2){
                if(ticks<10)return
                // Sanity only; generatedTotal growth in the log is the real check.
                veyra.logUi("nv-xess-fg","2X phase submitFps="+veyra.submitFps.toFixed(1))
                veyra.fgMultiplier=4;next(3);return
            }
            if(step===3){
                if(!settled())return
                // Shared change: an over-ceiling request must not kill the session.
                check(veyra.fgActive,"4X request did not kill the XeSS session")
                next(4);return
            }
            if(step===4){
                if(ticks<10)return
                veyra.logUi("nv-xess-fg","4X phase submitFps="+veyra.submitFps.toFixed(1))
                veyra.fgMultiplier=2;next(5);return
            }
            if(step===5){
                if(!settled())return
                check(veyra.fgActive,"back to 2X still active")
                veyra.logUi("nv-xess-fg","NV_XESS_FG_PASS");console.log("NV_XESS_FG_PASS");Qt.quit()
                return
            }
        }catch(e){veyra.logUi("nv-xess-fg","NV_XESS_FG_FAIL "+e.message);console.log("NV_XESS_FG_FAIL "+e.message);Qt.quit()}}
    }
}