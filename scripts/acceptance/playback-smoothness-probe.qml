import QtQuick
Item {
    id: probe
    property var appWindow: Window.window
    property string media: ""
    property string mode: "plain"
    property int limit: 220
    property int ticks: 0
    property double previous: 0
    property var intervals: []
    property double started: Date.now()
    property int notifications: 0
    Connections {
        target: veyra
        ignoreUnknownSignals: true
        function onFgChoicesChanged() { ++probe.notifications }
        function onNotice(text, error) { console.log("SMOOTH_NOTICE", text, error) }
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
            if (probe.ticks === 1) {
                probe.appWindow.width = 1280; probe.appWindow.height = 800
                veyra.muted = true
                veyra.nrEnabled = false; veyra.srEnabled = false; veyra.fgEnabled = false
                if (probe.mode === "heavy") {
                    veyra.nrEnabled = true; veyra.srEnabled = true
                    veyra.videoSrQuality = 4; veyra.srTargetIndex = 2
                    veyra.fgBackendName = "dlss"; veyra.fgMultiplier = 2; veyra.fgEnabled = true
                }
                if (probe.mode === "dlss6" || probe.mode === "vfg4") {
                    veyra.fgBackendName = probe.mode === "dlss6" ? "dlss" : "vfg"
                    veyra.fgMultiplier = probe.mode === "dlss6" ? 6 : 4
                    veyra.vfgQuality = 1; veyra.fgEnabled = true
                }
                veyra.openPath(probe.media)
            }
            const a = probe.intervals.sort((a,b) => a-b)
            const percentile = p => a.length ? a[Math.min(a.length-1,Math.floor(a.length*p))] : 0
            console.log("SMOOTH_SAMPLE", JSON.stringify({seconds:(Date.now()-probe.started)/1000,
                active:probe.appWindow.active, visibility:probe.appWindow.visibility,
                p50:percentile(.5), p95:percentile(.95), max:percentile(1), count:a.length,
                fgNotifications:probe.notifications, running:veyra.running, position:veyra.position,
                fps:veyra.submitFps, detail:veyra.runStatusDetail,
                nr:veyra.nrActive,sr:veyra.srActive,fg:veyra.fgActive,failed:veyra.failed}))
            probe.intervals=[]; probe.notifications=0
            if (probe.ticks === 6 && veyra.effectCapabilities !== undefined) {
                const begin=Date.now(); let available=0
                for(let k=0;k<100;++k) available += veyra.effectCapabilities.nr0.available ? 1 : 0
                console.log("SMOOTH_GETTER_100", Date.now()-begin, available)
            }
            if (probe.ticks >= probe.limit) Qt.quit()
        }
    }
}
