
    // ---- capture card running in 极简 for 70 s (external OBS game-capture check) -----------
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) dialogs.open("capture")
            if (ticks === 5) { veyra.startCaptureSession(); dialogs.close() }
            if (ticks === 75) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
