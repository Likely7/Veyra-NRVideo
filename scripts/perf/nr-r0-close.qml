import QtQuick
Item {
    id: probe
    property var appWindow: Window.window
    property string media: ""
    property string action: ""
    property int phase: 0
    property double phaseAt: 0
    property double started: Date.now()
    function require(ok, why) { if (!ok) { console.log("CLOSE_UI_FAIL", why); Qt.exit(3); throw new Error(why) } }
    Timer {
        interval: 100; running: true; repeat: true
        onTriggered: {
            const now = Date.now()
            probe.require(now-probe.started < 55000, "timeout")
            if (!probe.media) return
            if (probe.phase === 0) {
                veyra.muted = true; veyra.lowLatency = false
                veyra.videoHdr = false; veyra.colorEnabled = false
                veyra.nrEnabled = true; veyra.srEnabled = false; veyra.fgEnabled = false
                probe.require(veyra.duplicateNrLayer(veyra.nrLayers[0].index) >= 0, "duplicate")
                for (let layer of veyra.nrLayers) {
                    probe.require(veyra.setNrLayerParameter(layer.index,"runtime",0), "runtime")
                    probe.require(veyra.setNrLayerParameter(layer.index,"sizePolicy",0), "1080 cap")
                    probe.require(veyra.setNrLayerParameter(layer.index,"temporal",0), "temporal")
                }
                veyra.videoSrQuality = 0; veyra.srTargetIndex = 2; veyra.srEnabled = true
                veyra.fgBackendName = "dlss"; veyra.fgMultiplier = 2; veyra.fgEnabled = true
                veyra.openPath(probe.media); probe.phase = 1; return
            }
            probe.require(!veyra.failed, "engine failure")
            if (probe.phase === 1) {
                if (!veyra.nrActive || !veyra.srActive || !veyra.fgActive || veyra.position < 2) return
                if (probe.action === "fsr3" || probe.action === "xess" || probe.action === "vfg") veyra.fgBackendName = probe.action
                else if (probe.action === "nr-runtime") probe.require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"runtime",2), "SF-v2 switch")
                else if (probe.action === "nr-size") probe.require(veyra.setNrLayerParameter(veyra.nrLayers[0].index,"sizePolicy",3), "size switch")
                else if (probe.action === "nr-layer") probe.require(veyra.duplicateNrLayer(veyra.nrLayers[0].index) >= 0, "third layer")
                else if (probe.action === "stop") veyra.stopPlayback()
                else probe.require(false,"unknown action")
                probe.phaseAt = now; probe.phase = 2; return
            }
            if (probe.phase === 2 && now-probe.phaseAt >= 200) {
                console.log("CLOSE_UI_QUIT", probe.action, "applying="+veyra.applying, veyra.runStatusDetail)
                console.log("CLOSE_UI_PASS"); Qt.quit(); probe.phase = 3
            }
        }
    }
}
