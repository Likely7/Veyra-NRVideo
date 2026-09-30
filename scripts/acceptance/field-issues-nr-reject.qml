    Timer {
        interval: 250; running: true; repeat: true
        property int step: 0
        property int ticks: 0
        property int total: 0
        property double positionBefore: 0
        function check(value,label) { if(!value)throw new Error(label) }
        onTriggered: { try {
            ++ticks;++total;if(total>240)throw new Error("bounded step="+step)
            if(step===0) {
                if(ticks<8)return
                root.page="pro";veyra.muted=true
                veyra.srEnabled=false;veyra.fgEnabled=false;veyra.nrEnabled=true
                check(veyra.nrLayers[0].runtime===0,"initial Lecram")
                veyra.openPath("E:/项目/Veyra/tests/nr-sfv2-lecram-20260930/media/gta6-native-4k30-silent-90s.mp4")
                step=1;ticks=0;return
            }
            if(step===1) {
                if(ticks<20||!veyra.running||veyra.applying)return
                check(veyra.nrActive,"Lecram active")
                positionBefore=veyra.position
                check(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"runtime",2),"enqueue SF-v2 request")
                step=2;ticks=0;return
            }
            if(step===2) {
                if(ticks<24||veyra.applying)return
                check(veyra.running&&veyra.nrActive&&veyra.position>positionBefore+0.5,"working Lecram playback restored")
                check(veyra.nrLayers.every(n=>n.runtime===0),"selector restored to actual Lecram")
                veyra.logUi("nr-reject-test","NR_UI_REJECT_PASS");Qt.quit()
            }
        } catch(e) {veyra.logUi("nr-reject-test","NR_UI_REJECT_FAIL "+e.message);Qt.quit()} }
    }
