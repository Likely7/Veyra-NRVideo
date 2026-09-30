    Timer {
        interval: 250; running: true; repeat: true
        property int step: 0
        property int ticks: 0
        property int total: 0
        property double positionBefore: 0
        function check(value,label) { if(!value)throw new Error(label) }
        function next(n,label) { step=n;ticks=0;positionBefore=veyra.position;veyra.logUi("display-test",label) }
        onTriggered: { try {
            ++ticks;++total;if(total>280)throw new Error("bounded step="+step)
            if(step===0) {
                if(ticks<8)return
                root.page="pro";veyra.muted=true
                veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
                veyra.videoHdr=false;veyra.colorEnabled=false;veyra.outputRateMode=0
                check(veyra.displaySync===2,"fresh default Automatic")
                veyra.openPath("E:/项目/Veyra/tests/nr-sfv2-lecram-20260930/media/gta6-native-4k30-silent-90s.mp4")
                next(1,"DISPLAY_WINDOW_AUTO_BEGIN");return
            }
            if(ticks<24||veyra.applying||!veyra.running)return
            check(!veyra.nrActive&&!veyra.srActive&&!veyra.fgActive,"all effects remain inactive")
            check(veyra.position>positionBefore+0.5,"playback advances")
            if(step===1) {
                check(!root.fullscreen&&veyra.presentationStatus.indexOf("低延迟队列已关闭")>=0,"queue disabled")
                root.toggleFullscreen();next(2,"DISPLAY_FULLSCREEN_AUTO_BEGIN");return
            }
            if(step===2) {
                check(root.fullscreen&&veyra.presentationStatus.indexOf("防撕裂")>=0,"Automatic fullscreen: no vsync, no tearing")
                veyra.displaySync=0;next(3,"DISPLAY_FULLSCREEN_TEARING_BEGIN");return
            }
            if(step===3) {
                check(root.fullscreen&&veyra.presentationStatus.indexOf("允许撕裂")>=0,"explicit tearing preserved")
                veyra.displaySync=1;next(4,"DISPLAY_FULLSCREEN_VSYNC_BEGIN");return
            }
            if(step===4) {
                check(root.fullscreen&&veyra.presentationStatus.indexOf("垂直同步")>=0,"explicit VSync with queue disabled")
                root.toggleFullscreen();next(5,"DISPLAY_WINDOW_VSYNC_BEGIN");return
            }
            if(step===5) {
                check(!root.fullscreen&&veyra.presentationStatus.indexOf("垂直同步")>=0,"window VSync preserved")
                veyra.displaySync=2;next(6,"DISPLAY_WINDOW_AUTO_RETURN_BEGIN");return
            }
            if(step===6) {
                check(!root.fullscreen&&veyra.presentationStatus.indexOf("防撕裂")>=0,"Automatic window restored")
                veyra.logUi("display-test","DISPLAY_UI_PASS");Qt.quit()
            }
        } catch(e) {veyra.logUi("display-test","DISPLAY_UI_FAIL "+e.message);Qt.quit()} }
    }
