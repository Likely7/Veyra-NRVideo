
    // ---- capture smoke (scripts/ui-check/ui-check.py) ---------------------------------------
    // Connects the default capture device for 25 s: frames must keep arriving through
    // DirectShow and the driver frame-age line ([capture-driver-age]) must appear.
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 250; running: true; repeat: true
        property int step: 0
        property int ticks: 0
        function log(text) { veyra.logUi("uitest", text) }
        onTriggered: { try {
            ++ticks
            if (step === 0 && ticks === 8) { dialogs.open("capture"); step = 1; ticks = 0; return }
            if (step === 1 && ticks === 16) {
                if (!veyra.startCaptureSession()) throw new Error("startCaptureSession refused")
                dialogs.close(); step = 2; ticks = 0; return
            }
            if (step === 2 && ticks === 20) { log("UITEST_SHOT capture-running"); log("UITEST_NOTE fps=" + veyra.captureFps.toFixed(1)); return }
            if (step === 2 && ticks === 100) {
                log("UITEST_NOTE fps=" + veyra.captureFps.toFixed(1) + " dropped=" + veyra.captureDropped)
                if (!(veyra.captureFps > 20)) throw new Error("capture frame rate " + veyra.captureFps)
                running = false; log("UITEST_DONE")
            }
        } catch (e) { running = false; log("UITEST_FAIL " + e.message) } }
    }
