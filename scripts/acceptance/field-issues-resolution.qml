    Timer {
        interval: 500; running: true; repeat: true
        property int step: 0
        property int ticks: 0
        property int total: 0
        property double began: 0
        property double before: 0
        function check(ok,label) { if(!ok)throw new Error(label) }
        function next(n) { step=n;ticks=0 }
        onTriggered: { try {
            ++ticks;++total;if(total>440)throw new Error("bounded step="+step)
            if(step===0) {
                if(ticks<4)return
                root.page="pro";veyra.muted=true;veyra.nrEnabled=true;veyra.srEnabled=true;veyra.fgEnabled=false
                veyra.srTargetIndex=4
                veyra.openPath("E:/项目/Veyra/tests/field-issues-20260930/media/gta6-1080p30-45s.mp4")
                next(1);return
            }
            if(step===1||step===3||step===5) {
                if(ticks<6||veyra.applying||!veyra.running)return
                check(veyra.srActive&&veyra.nrActive,"NR+SR active "+veyra.statusText)
                before=veyra.position;began=Date.now()
                veyra.logUi("resolution-test","RESOLUTION_BEGIN "+veyra.srTargetLabel+" "+veyra.outputSummary)
                next(step+1);return
            }
            if(step===2||step===4||step===6) {
                if(Date.now()-began<30000)return
                check(veyra.running&&veyra.srActive&&veyra.nrActive&&veyra.position>before+20,"30s high resolution playback "+veyra.srTargetLabel+" "+veyra.statusText)
                veyra.logUi("resolution-test","RESOLUTION_30S_PASS "+veyra.srTargetLabel+" "+veyra.outputSummary)
                if(step===6){veyra.logUi("resolution-test","RESOLUTION_UI_PASS");Qt.quit();return}
                veyra.seekTo(0);veyra.srTargetIndex=step===2?5:6;next(step+1);return
            }
        } catch(e) {veyra.logUi("resolution-test","RESOLUTION_UI_FAIL "+e.message);Qt.quit()} }
    }
