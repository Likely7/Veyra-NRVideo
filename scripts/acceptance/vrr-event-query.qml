// Loaded only into a private staging copy, never the user's running package.
// claude-vrr-event-query-20261025: marks each step in the engine log
// (vrr-test) so vrr-event-query.py can count display queries between them.
import QtQuick
Item {
    id: test
    property string media: ""
    property string mode: "native"   // "novrr": VEYRA_TEST_VRR_LEVEL=1 (a monitor without VRR)
    property int phase: 0
    property int ticks: 0
    property int total: 0
    property real beforePosition: 0
    property var notices: []
    property string shot: ""         // PNG of the QML scene (显示 tab): the 重新检测 button beside the switch
    property bool shotSaved: false
    function check(ok,why){if(!ok)throw new Error(why)}
    function mark(name){veyra.logUi("vrr-test","mark "+name);console.log("VRR_MARK",name)}
    function next(n,name){phase=n;ticks=0;beforePosition=veyra.position;mark(name)}
    function active(){check(veyra.running&&veyra.fgActive&&veyra.fgBackendName==="vfg","VFG not active: "+veyra.statusText);check(veyra.position>beforePosition+1,"media clock stopped")}
    Connections {
        target: veyra
        function onNotice(text,isError){test.notices.push(text);console.log("VRR_NOTICE",isError,text)}
    }
    Timer {id: timer;interval: 250;repeat: true;running: test.media!==""
        onTriggered: {try {
            ++test.ticks;if(++test.total>400)throw new Error("100 second deadline phase "+test.phase)
            if(test.phase===0){
                // The startup query answers during these 2 s; nothing follows while idle.
                if(test.ticks<8)return
                next(1,"idle-start")
            }else if(test.phase===1){
                if(test.ticks<16)return
                root.page="pro";veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false
                check(veyra.addEffect("vfg-fg")>=0,"VFG stage creation")
                veyra.vfgQuality=1;veyra.fgMultiplier=2;veyra.openPath(test.media);next(2,"open")
            }else if(test.phase===2){
                if(!veyra.running||!veyra.fgActive||veyra.applying||veyra.position<1)return
                next(3,"play-quiet")
            }else if(test.phase===3){
                // 15 s of VFG 2X playback: no display query at all.
                if(test.ticks<60||veyra.applying)return
                active();console.log("VRR_CASE","play",veyra.submitFps,veyra.runStatusDetail)
                // Mark first: the presenter may start the query before this handler returns.
                test.notices=[];next(4,"recheck");veyra.recheckDisplay()
                // The spinner shows at once; the next 250 ms status update may already clear it.
                check(veyra.displayChecking,"刷新显示器状态 shows the spinner at once")
            }else if(test.phase===4){
                if(test.ticks<16)return
                check(test.notices.some(t=>t.indexOf("已刷新显示器状态")===0),"re-check notice: "+JSON.stringify(test.notices))
                check(!veyra.displayChecking,"displayChecking cleared after the answer")
                next(5,"vrr-on");veyra.variableRefresh=true
            }else if(test.phase===5){
                // Switch on: one query, plus one VRR confirmation 3 s later when VRR presents.
                if(test.ticks<24||veyra.applying)return
                active()
                proPage.tab="display"
                if(test.mode==="novrr")check(veyra.vrrState===1&&veyra.presentationStatus.indexOf("可变刷新")<0,"no VRR presentation: "+veyra.vrrState+" "+veyra.presentationStatus)
                else if(veyra.vrrState>=3)check(veyra.presentationStatus.indexOf("可变刷新")>=0,"VRR presentation: "+veyra.presentationStatus)
                console.log("VRR_CASE","vrr-on",veyra.vrrState,veyra.presentationStatus)
                // A window size change (as entering/leaving fullscreen does).
                next(6,"resize");root.width=root.width-120
            }else if(test.phase===6){
                // VRR presenting: one confirmation 3 s after the size settles; otherwise nothing.
                if(test.ticks<32||veyra.applying)return
                active();console.log("VRR_CASE","end-play",veyra.submitFps,veyra.vrrState,veyra.vrrHint,veyra.presentationStatus)
                if(test.shot!=="")root.contentItem.grabToImage(r=>{test.shotSaved=r.saveToFile(test.shot)})
                veyra.stopPlayback();next(7,"stopped")
            }else if(test.phase===7){
                // Idle 5 s: nothing; then the node page with its output box.
                if(test.ticks<20)return
                check(!veyra.running,"close drains")
                check(test.shot===""||test.shotSaved,"screenshot saved")
                root.page="node";next(8,"node-page")
            }else if(test.phase===8){
                if(test.ticks<8)return
                check(root.page==="node","node page shown")
                test.mark("end");console.log("VRR_PASS");timer.stop();Qt.quit()
            }
        }catch(e){console.error("VRR_FAIL",test.phase,e.message);timer.stop();Qt.quit()}}
    }
}
