    // ---- export: closes playback at start, VBR 8 Mbps default, ETA holds while paused ----
    function uitestFind(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const kids = item.children || []
        for (let k = 0; k < kids.length; ++k) { const f = uitestFind(kids[k], name); if (f) return f }
        return null
    }
    Connections { target: veyra; function onNotice(text, isError) { veyra.logUi("uitest-notice", (isError ? "ERR " : "") + text) } }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        property string failure: ""
        property real etaPaused: 0
        function fail(why) { if (!failure) failure = why }
        function eta() { const n = uitestFind(root.contentItem, "export-eta"); return n ? n.text : "(none)" }
        onTriggered: {
            ++ticks
            if (ticks === 2) {
                veyra.logUi("uitest", "UITEST_NOTE default rate=" + veyra.exportRateControl + " bitrate=" + veyra.exportBitrateMbps)
                if (veyra.exportRateControl !== 1 || veyra.exportBitrateMbps !== 8) fail("default not VBR 8")
                veyra.openUrl("@VIDEO@")
            }
            if (ticks === 5) {
                root.goPage("exp")

                veyra.addExportFiles(["@VIDEO@"])
            }
            if (ticks === 8) { veyra.logUi("uitest", "UITEST_NOTE before start hasSource=" + veyra.hasSource + " ready=" + veyra.exportReadyCount + " target=" + veyra.exportTarget + " items=" + JSON.stringify(veyra.exportItems.map(i => i.state + ":" + i.note))); veyra.startExport() }
            if (ticks === 10) {
                veyra.logUi("uitest", "UITEST_NOTE after start hasSource=" + veyra.hasSource + " running=" + veyra.exportRunning)
                if (veyra.hasSource) fail("playback still open after export start")
            }
            if (ticks === 16) { veyra.logUi("uitest", "UITEST_NOTE running eta=" + veyra.exportEtaSeconds.toFixed(1) + " text=" + eta()); veyra.pauseExport(true) }
            if (ticks === 18) { etaPaused = veyra.exportEtaSeconds; veyra.logUi("uitest", "UITEST_NOTE paused eta=" + etaPaused.toFixed(1) + " text=" + eta()); veyra.logUi("uitest", "UITEST_SHOT paused") }
            if (ticks === 24) {
                const later = veyra.exportEtaSeconds
                veyra.logUi("uitest", "UITEST_NOTE paused 6s later eta=" + later.toFixed(1) + " paused=" + veyra.exportPaused)
                if (Math.abs(later - etaPaused) > 1.0) fail("eta moved while paused " + etaPaused.toFixed(1) + " -> " + later.toFixed(1))
                if (eta().indexOf("已暂停") < 0) fail("paused text missing: " + eta())
                veyra.pauseExport(false)
            }
            if (ticks === 28) { veyra.logUi("uitest", "UITEST_NOTE resumed eta=" + veyra.exportEtaSeconds.toFixed(1) + " text=" + eta()); veyra.cancelExport() }
            if (ticks === 32) { veyra.logUi("uitest", failure ? "UITEST_FAIL " + failure : "UITEST_DONE"); running = false }
        }
    }
