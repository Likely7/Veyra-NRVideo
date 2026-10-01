
    // ---- split compare pauses frame generation, and says so ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) { veyra.openUrl("@VIDEO@"); veyra.fgBackendName = "xess"; veyra.fgEnabled = true }
            if (ticks === 8) veyra.logUi("uitest", "UITEST_NOTE before status=" + veyra.runStatus)
            if (ticks === 9) { veyra.compareMode = 2; root.goPage("pro") }
            if (ticks === 10) veyra.logUi("uitest", "UITEST_SHOT compare")
            if (ticks === 12) veyra.logUi("uitest", "UITEST_NOTE compare status=" + veyra.runStatus)
            if (ticks === 13) veyra.compareMode = 0
            if (ticks === 16) {
                veyra.logUi("uitest", "UITEST_NOTE after status=" + veyra.runStatus)
                veyra.logUi("uitest", "UITEST_DONE"); running = false
            }
        }
    }
