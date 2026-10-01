
    // ---- capture card: pause stops processing, play resumes ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) dialogs.open("capture")
            if (ticks === 4) { veyra.nrEnabled = true; veyra.startCaptureSession(); dialogs.close() }
            if (ticks === 10) { veyra.logUi("uitest", "UITEST_NOTE pause"); veyra.togglePlayPause() }
            if (ticks === 12) veyra.logUi("uitest", "UITEST_SHOT paused")
            if (ticks === 16) { veyra.logUi("uitest", "UITEST_NOTE resume paused=" + veyra.paused); veyra.togglePlayPause() }
            if (ticks === 21) { veyra.logUi("uitest", "UITEST_NOTE end paused=" + veyra.paused); veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
