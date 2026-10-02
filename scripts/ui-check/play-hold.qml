    // ---- plays @VIDEO@ for @SECONDS@ s and ends (for outside observers: OBS, RTSS) ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.openUrl("@VIDEO@")
            if (ticks % 10 === 0) veyra.logUi("uitest", "UITEST_NOTE t=" + ticks + " status=" + veyra.runStatus)
            if (ticks === @SECONDS@) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
