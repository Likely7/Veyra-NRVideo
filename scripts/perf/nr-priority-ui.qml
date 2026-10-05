import QtQuick
Item {
    id: probe
    property string media: ""
    property string expectedSaved: "normal"
    property bool started: false
    property int cycle: 0
    property bool awaiting: false
    property double changedAt: 0
    property double startedAt: Date.now()
    property string target: "normal"
    function require(ok, why) { if (!ok) { console.log("PRIORITY_UI_FAIL",why); Qt.exit(3) } }
    Timer {
        interval: 200; repeat: true; running: true
        onTriggered: {
            require(Date.now()-probe.startedAt<100000,"timeout")
            require(!veyra.failed,"engine failure")
            if (!probe.started) {
                probe.started=true
                require((veyra.preferences.gpuPriority||"normal")===probe.expectedSaved,"saved preference")
                require(!veyra.setPreference("gpuPriority","invalid"),"reject invalid priority")
                veyra.muted=true;veyra.lowLatency=false
                veyra.nrEnabled=true;veyra.srEnabled=false;veyra.fgEnabled=false
                veyra.videoHdr=false;veyra.colorEnabled=false
                require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"temporal",0),"temporal")
                require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePolicy",0),"size")
                veyra.openPath(probe.media)
                return
            }
            if (!veyra.running||veyra.applying||veyra.position<=0) return
            require(veyra.nrActive,"NR remains active")
            if (probe.awaiting) {
                if (Date.now()-probe.changedAt<1000) return
                require((veyra.preferences.gpuPriority||"normal")===probe.target,"accepted preference")
                console.log("PRIORITY_UI_EFFECTIVE",JSON.stringify({cycle:probe.cycle,target:probe.target,
                    status:veyra.gpuPriorityStatus,position:veyra.position,paused:veyra.paused,nr:veyra.nrActive}))
                probe.awaiting=false;++probe.cycle
                if (probe.cycle===20) { console.log("PRIORITY_UI_PASS",veyra.gpuPriorityStatus);Qt.quit();return }
                probe.changedAt=Date.now();return
            }
            if (Date.now()-probe.changedAt<400) return
            if (probe.cycle===9&&!veyra.paused)veyra.togglePlayPause()
            if (probe.cycle===14&&veyra.paused)veyra.togglePlayPause()
            probe.target=probe.cycle===19?"realtime":["normal","high","realtime"][probe.cycle%3]
            require(veyra.setPreference("gpuPriority",probe.target),"set priority")
            probe.changedAt=Date.now();probe.awaiting=true
            console.log("PRIORITY_UI_REQUEST",JSON.stringify({cycle:probe.cycle,target:probe.target}))
        }
    }
}
