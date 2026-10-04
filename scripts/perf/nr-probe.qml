import QtQuick

// Isolated test-only loader; never distributed with the product.
Item {
    id: probe
    property var appWindow: Window.window
    property var config: ({})
    property string media: ""
    property int seconds: 50
    property int ticks: 0
    property double readyAt: 0
    property double previous: 0
    property var intervals: []
    property bool configured: false
    property bool expectSr: false
    property bool expectFg: false
    property int expectNr: 1
    property var expectedPolicies: []

    function require(value, why) {
        if (!value) { console.log("NR_PERF_FAIL", why); Qt.exit(3) }
    }
    Connections {
        target: veyra
        function onNotice(text, error) { console.log("NR_PERF_NOTICE", text, error) }
    }
    Timer {
        interval: 16; running: true; repeat: true
        onTriggered: {
            const now = Date.now()
            if (probe.previous) probe.intervals.push(now - probe.previous)
            probe.previous = now
        }
    }
    Timer {
        interval: 1000; running: true; repeat: true
        onTriggered: {
            ++probe.ticks
            if (!probe.configured) {
                probe.appWindow.width = 1280; probe.appWindow.height = 800
                veyra.muted = true; veyra.lowLatency = false
                veyra.nrEnabled = true; veyra.srEnabled = false; veyra.fgEnabled = false
                veyra.videoHdr = false; veyra.colorEnabled = false
                probe.expectNr = probe.config.layers || 1
                const policies = probe.config.policies || [1]
                probe.expectedPolicies = policies
                while (veyra.nrLayers.length < probe.expectNr)
                    probe.require(veyra.duplicateNrLayer(veyra.nrLayers[0].index) >= 0, "duplicate layer")
                for (let i = 0; i < probe.expectNr; ++i) {
                    const idx = veyra.nrLayers[i].index
                    probe.require(veyra.setNrLayerParameter(idx, "sizePolicy", policies[Math.min(i, policies.length-1)]), "size policy")
                    probe.require(veyra.setNrLayerParameter(idx, "runtime", 0), "Lecram runtime")
                    probe.require(veyra.setNrLayerParameter(idx, "temporal", probe.config.temporal ? 1 : 0), "temporal")
                }
                probe.expectSr = Boolean(probe.config.sr)
                if (probe.expectSr) {
                    veyra.videoSrQuality = 0 // actual DLSS SR, not RTX Video SR
                    veyra.srTargetIndex = 2; veyra.srEnabled = true
                }
                probe.expectFg = Boolean(probe.config.fg)
                if (probe.expectFg) {
                    veyra.fgBackendName = "dlss"; veyra.fgMultiplier = 2; veyra.fgEnabled = true
                }
                console.log("NR_PERF_CONFIG", JSON.stringify({config:probe.config, layers:veyra.nrLayers,
                    sr:veyra.srEnabled, srQuality:veyra.videoSrQuality, srTarget:veyra.srTargetIndex,
                    fg:veyra.fgEnabled, multiplier:veyra.fgMultiplier, backend:veyra.fgBackendName}))
                veyra.openPath(probe.media)
                probe.configured = true
            }
            probe.require(!veyra.failed, "engine failure")
            if (!probe.readyAt && veyra.running && veyra.position > 0 && veyra.nrActive &&
                veyra.srActive === probe.expectSr && veyra.fgActive === probe.expectFg) {
                probe.readyAt = Date.now()
                console.log("NR_PERF_READY", probe.readyAt)
            }
            const a = probe.intervals.sort((a,b) => a-b)
            const p = value => a.length ? a[Math.min(a.length-1,Math.floor(a.length*value))] : 0
            console.log("NR_PERF_SAMPLE", JSON.stringify({tick:probe.ticks, ready:probe.readyAt > 0,
                active:probe.appWindow.active, width:probe.appWindow.width, height:probe.appWindow.height,
                position:veyra.position, fps:veyra.submitFps, nr:veyra.nrActive, sr:veyra.srActive,
                fg:veyra.fgActive, uiTimerP50Ms:p(.5), uiTimerP95Ms:p(.95), uiTimerMaxMs:p(1),
                uiTimerSamples:a.length, detail:veyra.runStatusDetail}))
            probe.intervals = []
            if (probe.readyAt && Date.now() - probe.readyAt >= probe.seconds * 1000) {
                probe.require(veyra.nrLayers.length === probe.expectNr, "layer count changed")
                console.log("NR_PERF_PASS"); Qt.quit()
            }
            if (probe.ticks >= 250) { console.log("NR_PERF_FAIL", "timeout"); Qt.exit(4) }
        }
    }
}
