// Loaded only into a private staging copy, never the user's running package.
import QtQuick
Item {
    id: test
    property string media: ""
    property int phase: 0
    property int ticks: 0
    property int total: 0
    property real beforePosition: 0
    function check(ok,why){if(!ok)throw new Error(why)}
    function next(n){phase=n;ticks=0;beforePosition=veyra.position}
    function active(){check(veyra.running&&veyra.fgActive&&veyra.fgBackendName==="vfg","VFG not active: "+veyra.statusText);check(veyra.position>beforePosition+1,"media clock stopped")}
    Timer {id: timer;interval: 250;repeat: true;running: test.media!==""
        onTriggered: {try {
            ++test.ticks;if(++test.total>280)throw new Error("70 second deadline phase "+test.phase)
            if(test.phase===0){
                if(test.ticks<8)return
                root.page="pro";veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false
                check(veyra.addEffect("vfg-fg")>=0,"VFG stage creation")
                veyra.vfgQuality=1;veyra.fgMultiplier=2;veyra.openPath(test.media);next(1)
            }else if(test.phase===1){
                if(!veyra.running||!veyra.fgActive||veyra.applying||veyra.position<1)return
                next(2)
            }else if(test.phase===2){
                if(test.ticks<24||veyra.applying)return
                active();console.log("FG_DISPLAY_CASE","2X",veyra.submitFps,veyra.runStatusDetail)
                veyra.outputRateMode=1;veyra.variableRefresh=true;next(3)
            }else if(test.phase===3){
                if(test.ticks<20||veyra.applying)return
                active();console.log("FG_DISPLAY_CASE","follow-display-VRR",veyra.presentationStatus)
                veyra.fgMultiplier=4;next(4)
            }else if(test.phase===4){
                if(test.ticks<20||veyra.applying)return
                active();check(veyra.fgMultiplier===4,"4X rollback")
                console.log("FG_DISPLAY_CASE","4X",veyra.submitFps,veyra.runStatusDetail)
                veyra.togglePlayPause();next(5)
            }else if(test.phase===5){
                if(test.ticks<4)return
                check(veyra.paused,"pause")
                veyra.togglePlayPause();next(6)
            }else if(test.phase===6){
                if(test.ticks<16||veyra.applying)return
                active();veyra.stopPlayback();next(7)
            }else if(test.phase===7){
                if(test.ticks<8)return
                check(!veyra.running,"close drains")
                veyra.fgMultiplier=2;veyra.openPath(test.media);next(8)
            }else if(test.phase===8){
                if(test.ticks<24||veyra.applying||!veyra.fgActive)return
                active();console.log("FG_DISPLAY_PASS",veyra.submitFps,veyra.runStatusDetail)
                timer.stop();Qt.quit()
            }
        }catch(e){console.error("FG_DISPLAY_FAIL",test.phase,e.message);timer.stop();Qt.quit()}}
    }
}
