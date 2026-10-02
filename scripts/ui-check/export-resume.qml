    // ---- export continues after a GPU reset (field report 2026-10-02: 8K NR export lost to a TDR) ----
    // Run with VEYRA_TEST_EXPORT_REMOVE_DEVICE_AT=<frame> so the first worker loses its D3D12 device.
    Connections { target: veyra; function onNotice(text, isError) { veyra.logUi("uitest-notice", (isError ? "ERR " : "") + text) } }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        property int finishedAt: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) { veyra.openUrl("@VIDEO@") }
            if (ticks === 4) { root.goPage("pro"); if (@NR@) { if (veyra.nrLayers.length === 0) veyra.addEffect("nr"); veyra.setAllNrEnabled(true) } }
            if (ticks === 6) { root.goPage("exp"); veyra.addExportFiles(["@VIDEO@"]) }
            if (ticks === 8) { veyra.logUi("uitest", "UITEST_NOTE start target=" + veyra.exportTarget); veyra.startExport() }
            if (ticks > 8 && ticks % 2 === 0)
                veyra.logUi("uitest", "UITEST_NOTE running=" + veyra.exportRunning + " progress=" + veyra.exportProgress.toFixed(3) + " status=" + veyra.exportStatus)
            if (ticks > 12 && !veyra.exportRunning && !finishedAt) {
                finishedAt = ticks
                veyra.logUi("uitest", "UITEST_NOTE finished status=" + veyra.exportStatus + " items=" + JSON.stringify(veyra.exportItems.map(i => i.state + ":" + i.note)))
            }
            if (finishedAt && ticks === finishedAt + 2) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
            if (ticks === 250) { veyra.logUi("uitest", "UITEST_FAIL export did not finish"); running = false }
        }
    }
