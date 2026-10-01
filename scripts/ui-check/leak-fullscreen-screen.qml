
    // ---- VRAM-leak run: capture with NR, then fullscreen for a while (field log: +3 GB/min) ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.openScreenCaptureDialog()
            if (ticks === 5) { const t = veyra.screenTargets; veyra.logUi("uitest", "UITEST_NOTE targets=" + JSON.stringify(t).slice(0, 300)); if (t.length) veyra.screenTargetId = t[0].id; veyra.nrEnabled = true; veyra.logUi("uitest", "UITEST_NOTE screen=" + veyra.startScreenCapture()); dialogs.close() }
            if (ticks === 40) { veyra.logUi("uitest", "UITEST_NOTE windowed phase done"); root.toggleFullscreen() }
            if (ticks === @END@) { root.toggleFullscreen(); veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
