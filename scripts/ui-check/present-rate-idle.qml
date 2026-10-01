
    // ---- UI present rate: idle 极简 (12 s), then a paused video (12 s) ----------------------
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 250; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.logUi("uitest", "UITEST_NOTE phase idle")
            if (ticks === 50) veyra.openUrl("@VIDEO@")
            if (ticks === 58) { veyra.togglePlayPause(); veyra.logUi("uitest", "UITEST_NOTE phase paused") }
            if (ticks === 110) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
