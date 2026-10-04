import QtQuick
Item {
    id: probe
    property var appWindow: Window.window
    property string commandFile: ""
    property int sequence: -1
    property bool configured: false
    Timer { id: exitResize; interval: 50; onTriggered: { console.log("OBS_UI_PASS"); Qt.quit() } }
    function require(ok, why) { if (!ok) { console.log("OBS_UI_FAIL", why); Qt.exit(3) } }
    Timer {
        interval: 500; running: true; repeat: true
        onTriggered: {
            if (!probe.commandFile) return
            const request = new XMLHttpRequest()
            request.open("GET", probe.commandFile, false); request.send()
            const c = JSON.parse(request.responseText)
            if (!probe.configured) {
                probe.appWindow.title = c.title
                veyra.muted = true; veyra.lowLatency = false
                veyra.srEnabled = false; veyra.fgEnabled = false
                veyra.colorEnabled = false; veyra.videoHdr = false
                veyra.nrEnabled = true
                const index = veyra.nrLayers[0].index
                probe.require(veyra.setNrLayerParameter(index, "runtime", 0), "runtime")
                probe.require(veyra.setNrLayerParameter(index, "sizePolicy", 1), "native NR")
                probe.require(veyra.setNrLayerParameter(index, "temporal", 0), "temporal")
                veyra.openPath(c.media)
                probe.configured = true
            }
            probe.require(!veyra.failed, "engine failed")
            if (probe.sequence !== c.sequence) {
                probe.sequence = c.sequence
                if (c.action === "pause" && !veyra.paused) veyra.togglePlayPause()
                if (c.action === "resume" && veyra.paused) veyra.togglePlayPause()
                if (c.action === "resize") { probe.appWindow.width = 1000; probe.appWindow.height = 700 }
                if (c.action === "fullscreen" && !probe.appWindow.fullscreen) probe.appWindow.toggleFullscreen()
                if (c.action === "windowed" && probe.appWindow.fullscreen) probe.appWindow.toggleFullscreen()
                if (c.action === "exit-resize") { probe.appWindow.width = 1100; probe.appWindow.height = 750; exitResize.start() }
                if (c.action === "quit") { console.log("OBS_UI_PASS"); Qt.quit() }
                console.log("OBS_UI_ACTION", c.action, probe.sequence)
            }
            console.log("OBS_UI_SAMPLE", JSON.stringify({position:veyra.position, paused:veyra.paused,
                nr:veyra.nrActive, failed:veyra.failed, fullscreen:probe.appWindow.fullscreen,
                width:probe.appWindow.width, height:probe.appWindow.height}))
        }
    }
}
