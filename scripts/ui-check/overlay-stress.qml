    // ---- monitoring overlay (RTSS / MSI Afterburner OSD) stress: every presenter path while the OSD draws ----
    // Run with RTSS running and something writing OSD text. Each step logs the run status; a lost device or a
    // crash ends the run early. Shots: windowed, fullscreen, each frame-generation backend, back to windowed.
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        function note(what) { veyra.logUi("uitest", "UITEST_NOTE " + what + " status=" + veyra.runStatus + " fg=" + veyra.fgBackendName + "/" + veyra.fgEnabled) }
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.openUrl("@VIDEO@")
            if (ticks === 6) { note("plain"); veyra.logUi("uitest", "UITEST_SHOT plain") }
            if (ticks === 7) { veyra.nrEnabled = true; veyra.srEnabled = true }
            if (ticks === 11) { note("nr+sr"); veyra.logUi("uitest", "UITEST_SHOT nrsr") }
            if (ticks === 12) { veyra.fgBackendName = "dlss"; veyra.fgEnabled = true }
            if (ticks === 17) { note("fg dlss"); veyra.logUi("uitest", "UITEST_SHOT fg-dlss") }
            if (ticks === 18) root.toggleFullscreen()
            if (ticks === 23) { note("fullscreen dlss"); veyra.logUi("uitest", "UITEST_SHOT full-dlss") }
            if (ticks === 24) veyra.fgBackendName = "xess"
            if (ticks === 30) { note("fullscreen xess"); veyra.logUi("uitest", "UITEST_SHOT full-xess") }
            if (ticks === 31) veyra.fgBackendName = "fsr3"
            if (ticks === 37) { note("fullscreen fsr3"); veyra.logUi("uitest", "UITEST_SHOT full-fsr3") }
            if (ticks === 38) root.toggleFullscreen()
            if (ticks === 42) { note("windowed fsr3"); veyra.logUi("uitest", "UITEST_SHOT win-fsr3") }
            if (ticks === 43) { veyra.fgEnabled = false; root.goPage("pro") }
            if (ticks === 47) { note("pro page"); veyra.logUi("uitest", "UITEST_SHOT pro") }
            if (ticks === 48) root.goPage("home")
            if (ticks === 51) { note("home"); veyra.logUi("uitest", "UITEST_SHOT home") }
            if (ticks === 52) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
