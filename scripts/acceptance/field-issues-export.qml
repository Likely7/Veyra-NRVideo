    Timer {
        interval: 500; running: true; repeat: true
        property int step: 0
        property int ticks: 0
        property int total: 0
        onTriggered: { try {
            ++ticks;++total;if(total>480)throw new Error("bounded export step="+step)
            if(step===0) {
                if(ticks<4)return
                root.page="pro";veyra.muted=true;veyra.nrEnabled=false;veyra.srEnabled=false;veyra.fgEnabled=false
                veyra.openPath("E:/项目/Veyra/tests/nr-sfv2-lecram-20260930/media/gta6-native-4k30-silent-90s.mp4")
                step=1;ticks=0;return
            }
            if(step===1) {
                if(ticks<6||!veyra.running)return
                // Exclude the exact 4.000 endpoint: the export trim includes it.
                veyra.exportTrimStart=0;veyra.exportTrimEnd=3.98
                veyra.stopPlayback();step=2;ticks=0;return
            }
            if(step===2) {
                if(ticks<4||veyra.running)return
                veyra.nrEnabled=Qt.application.arguments.indexOf("--without-nr")<0
                veyra.srEnabled=true;veyra.srTargetIndex=3;veyra.exportSrTargetIndex=3;veyra.exportHevc=true
                veyra.exportRateControl=2;veyra.startExport();step=3;ticks=0;return
            }
            if(step===3) {
                if(ticks<4)return
                if(veyra.exportRunning)return
                if(veyra.exportEncoded!==120)throw new Error("encoded="+veyra.exportEncoded+" status="+veyra.exportStatus+" failures="+veyra.exportQueueFailure)
                running=false;veyra.logUi("export-repro","EXPORT_8K_120_PASS "+veyra.exportStatus);Qt.quit()
            }
        } catch(e) {running=false;veyra.cancelExport();veyra.logUi("export-repro","EXPORT_8K_FAIL "+e.message);Qt.quit()} }
    }
