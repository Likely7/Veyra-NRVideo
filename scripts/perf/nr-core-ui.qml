import QtQuick

// Owned test loader: uses only the existing public QML bridge, no input hooks.
Item {
    id: probe
    property string media: ""
    property string group: "nr"
    property bool started: false
    property int cycle: 0
    property bool awaiting: false
    property double changeAt: 0
    property double startedAt: Date.now()
    property double settledAt: 0
    property bool targetOn: true
    property int targetLayers: 1
    property int targetPolicy: 0
    property int targetRuntime: 0
    function require(value, why) {
        if (!value) { console.log("CORE_UI_FAIL", why); Qt.exit(3) }
    }
    function change() {
        targetOn = group === "nr" ? cycle % 2 === 1 : cycle % 2 === 0
        if (group === "nr") veyra.nrEnabled = targetOn
        if (group === "sr") veyra.srEnabled = targetOn
        if (group === "fg") veyra.fgEnabled = targetOn
        if (group === "runtime") {
            targetRuntime = cycle % 2 ? 0 : 2
            require(veyra.setNrLayerParameter(veyra.nrLayers[0].index, "runtime", targetRuntime), "runtime request")
        }
        if (group === "sizes") {
            targetPolicy = [1, 3, 2][cycle % 3]
            require(veyra.setNrLayerParameter(veyra.nrLayers[0].index, "sizePolicy", targetPolicy), "size request")
        }
        if (group === "layers") {
            targetLayers = 1 + (cycle + 1) % 3
            for (let i=0; i<veyra.nrLayers.length; ++i)
                require(veyra.setEffectEnabled(veyra.nrLayers[i].index, i < targetLayers), "enable layer")
        }
        changeAt = Date.now(); awaiting = true
        console.log("CORE_UI_CHANGE", JSON.stringify({group:group, cycle:cycle, on:targetOn,
            layers:targetLayers, policy:targetPolicy, runtime:targetRuntime, position:veyra.position}))
    }
    function matches() {
        if (group === "nr") return veyra.nrActive === targetOn
        if (group === "sr") return veyra.nrActive && veyra.srActive === targetOn
        if (group === "fg") return veyra.nrActive && veyra.fgActive === targetOn
        if (group === "runtime") return veyra.nrActive && veyra.nrLayers[0].runtime === targetRuntime
        if (group === "sizes") return veyra.nrActive && veyra.nrLayers[0].sizePolicy === targetPolicy
        if (group === "layers") return veyra.nrActive && veyra.nrLayers.filter(x=>x.enabled).length === targetLayers
        return veyra.nrActive
    }
    Timer {
        interval: 200; running: true; repeat: true
        onTriggered: {
            require(!veyra.failed, "engine failure")
            require(Date.now()-probe.startedAt < 250000, "timeout")
            if (!probe.started) {
                probe.started = true
                veyra.muted = true; veyra.lowLatency = false
                veyra.nrEnabled = true; veyra.srEnabled = false; veyra.fgEnabled = false
                veyra.videoHdr = false; veyra.colorEnabled = false; veyra.videoSrQuality = 0
                veyra.srTargetIndex = 2; veyra.fgBackendName = "dlss"; veyra.fgMultiplier = 2
                // Selecting a multiplier intentionally enables FG in the
                // product bridge; reset it after configuring the saved level.
                veyra.fgEnabled = false
                while (veyra.nrLayers.length < (probe.group === "layers" ? 3 : 1))
                    require(veyra.duplicateNrLayer(veyra.nrLayers[0].index)>=0,"duplicate NR")
                for (let i=0;i<veyra.nrLayers.length;++i) {
                    require(veyra.setNrLayerParameter(veyra.nrLayers[i].index,"temporal",0),"temporal")
                    require(veyra.setNrLayerParameter(veyra.nrLayers[i].index,"sizePolicy",0),"initial size")
                    require(veyra.setEffectEnabled(veyra.nrLayers[i].index,i===0),"initial enable")
                }
                veyra.openPath(probe.media)
                console.log("CORE_UI_INITIAL",JSON.stringify({group:probe.group,fg:veyra.fgEnabled,
                    multiplier:veyra.fgMultiplier,sr:veyra.srEnabled,layers:veyra.nrLayers}))
                return
            }
            if (!veyra.running || veyra.applying || veyra.position <= 0) return
            if (probe.awaiting) {
                if (!probe.matches()) return
                console.log("CORE_UI_SETTLED", JSON.stringify({group:probe.group,cycle:probe.cycle,
                    observedBridgeMs:Date.now()-probe.changeAt, position:veyra.position, nr:veyra.nrActive,
                    sr:veyra.srActive,fg:veyra.fgActive,layers:veyra.nrLayers,detail:veyra.runStatusDetail}))
                probe.awaiting = false; probe.settledAt = Date.now(); ++probe.cycle
                if (probe.cycle>=20) {console.log("CORE_UI_PASS",probe.group);Qt.quit();return}
            }
            if (veyra.position>45) {veyra.seekTo(0);return}
            if (Date.now()-probe.settledAt<600) return
            probe.change()
        }
    }
}
