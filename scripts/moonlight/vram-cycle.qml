    // Injected by scripts/moonlight/ui-demo.py (with this file as the snippet): plays the clip named by
    // __CLIP__ (replaced before injection) with NR on, switches NR's optical flow off and on twenty times (every switch
    // rebuilds the graph) and quits. The [memory] lines in the log then show whether adapter memory
    // returns to the same level after each rebuild.
    Timer {
        interval: 250; running: true; repeat: true
        property int step: 0
        property int ticks: 0
        property int cycles: 0
        function log(text) { veyra.logUi("mltest", text) }
        onTriggered: { try {
            ++ticks
            if (step === 0 && ticks === 8) { veyra.openPath("__CLIP__"); step = 1; ticks = 0; return }
            if (step === 1 && ticks === 16) { veyra.nrEnabled = true; step = 2; ticks = 0; return }
            if (step === 2 && ticks === 16) {
                veyra.nrMotionSource = veyra.nrMotionSource === 1 ? 0 : 1
                log("MLTEST_CYCLE " + (++cycles) + " flow=" + veyra.nrMotionSource)
                ticks = 0
                if (cycles >= 20) step = 3
                return
            }
            if (step === 3 && ticks === 16) { running = false; log("MLTEST_DONE"); console.log("MLTEST_PASS"); Qt.quit() }
        } catch (e) { running = false; log("MLTEST_FAIL " + e.message); Qt.quit() } }
    }
