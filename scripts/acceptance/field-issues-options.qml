    Timer {
        interval: 250; running: true; repeat: true
        property int step: 0
        property int ticks: 0
        property int total: 0
        property bool restore: Qt.application.arguments.indexOf("--options-restore") >= 0
        function check(ok, label) { if(!ok) throw new Error(label) }
        function next(n) { step=n; ticks=0 }
        function preset(name) { for(var i=0;i<veyra.presets.length;++i)if(veyra.presets[i].name===name)return i;return -1 }
        onTriggered: { try {
            ++ticks;++total;if(total>650)throw new Error("bounded step="+step)
            if(step===0) {
                if(ticks<8)return
                if(restore) {
                    check(veyra.hdrOutputMode===1&&veyra.srTargetIndex===5&&veyra.srMotionSource===0&&veyra.nrMotionSource===0&&veyra.fgMotionSource===0,"saved common choices restored")
                    check(veyra.aspectMode===6,"saved preview ratio restored")
                    veyra.nodeMode=1
                    check(veyra.hdrOutputMode===1&&veyra.srTargetIndex===5&&veyra.nrMotionSource===0,"node session common choices")
                    running=false;console.log("OPTIONS_UI_RESTORE_PASS");Qt.quit();return
                }
                root.page="pro";veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
                check(veyra.hdrOutputMode===0,"fresh HDR10 default")
                veyra.fgBackendName="xess";check(veyra.fgMotionSource===0,"XeSS fresh zero motion")
                veyra.fgBackendName="dlss";check(veyra.fgMotionSource===1,"DLSS preserves prior flow default")
                veyra.openPath("E:/项目/Veyra/tests/field-issues-20260930/media/gta6-1080p30-45s.mp4")
                next(1);return
            }
            if(ticks<16||veyra.applying||!veyra.running)return
            if(step>=1&&step<=7) {
                veyra.aspectMode=step-1
                check(veyra.aspectMode===step-1,"geometry selection")
                if(step===5)check(Math.abs(veyra.previewAspect-16/9)<.001,"forced 16:9")
                if(step===6)check(Math.abs(veyra.previewAspect-4/3)<.001,"forced 4:3")
                if(step===7)check(Math.abs(veyra.previewAspect-21/9)<.001,"forced 21:9")
                if(step<7){next(step+1);return}
                veyra.aspectMode=0;veyra.seekTo(0);veyra.srTargetIndex=2;veyra.srMotionSource=0;veyra.srEnabled=true;next(8);return
            }
            if(step===8) {
                check(veyra.srActive&&veyra.srMotionSource===0,"SR zero motion initializes")
                veyra.videoSrQuality=5;next(14);return
            }
            if(step===14) {
                check(veyra.srActive&&veyra.videoSrQuality===5&&veyra.srMotionSource===0,"FSR SR zero motion initializes")
                veyra.srMotionSource=1;next(15);return
            }
            if(step===15) {
                check(veyra.srActive&&veyra.srMotionSource===1,"FSR SR flow initializes")
                veyra.srMotionSource=0;next(16);return
            }
            if(step===16) {
                check(veyra.srActive&&veyra.srMotionSource===0,"FSR SR returns to zero")
                veyra.seekTo(0);veyra.videoSrQuality=0;veyra.nrMotionSource=0;veyra.nrEnabled=true;next(9);return
            }
            if(step===9) {
                check(veyra.nrActive&&veyra.nrMotionSource===0&&veyra.srActive,"NR zero motion initializes")
                veyra.fgBackendName="xess";veyra.fgMultiplier=4;veyra.fgEnabled=true;next(10);return
            }
            if(step===10) {
                check(veyra.fgActive&&veyra.fgMotionSource===0,"XeSS zero motion 4X active "+veyra.statusText)
                veyra.fgMotionSource=1;next(11);return
            }
            if(step===11) {
                check(veyra.fgActive&&veyra.fgMotionSource===1,"XeSS external flow active")
                veyra.fgEnabled=false;veyra.srEnabled=false;veyra.nrEnabled=false;next(12);return
            }
            if(step===12) {
                veyra.hdrOutputMode=1;veyra.srTargetIndex=5;veyra.fgMotionSource=0;veyra.srMotionSource=0;veyra.nrMotionSource=0
                check(veyra.savePresetAs("Field Rendering Choices",15,false),"save list common choices")
                veyra.hdrOutputMode=0;veyra.srTargetIndex=2;veyra.srMotionSource=1
                check(veyra.applyPresetIndex(preset("Field Rendering Choices")),"apply list common choices")
                check(veyra.hdrOutputMode===1&&veyra.srTargetIndex===5&&veyra.srMotionSource===0,"list common choices restored")
                veyra.nodeMode=1;root.page="node";next(13);return
            }
            if(step===13) {
                check(veyra.hdrOutputMode===1&&veyra.srTargetIndex===5&&veyra.nrMotionSource===0,"node common choices")
                check(veyra.savePresetAs("Field Node Rendering Choices",15,true),"save node common choices")
                veyra.nodeMode=0;veyra.aspectMode=6;running=false;console.log("OPTIONS_UI_PASS");Qt.quit()
            }
        } catch(e) {running=false;console.log("OPTIONS_UI_FAIL "+e.message);Qt.quit()} }
    }
