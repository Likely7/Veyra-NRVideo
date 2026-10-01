
    // ---- UI present rate during playback (OBS game capture, field 2026-10-01) ---------------
    // Plays a file in 极简 for 25 s; [qml-frames] lines show how often the Qt windows present.
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 250; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 4) veyra.openUrl("@VIDEO@")
            if (ticks === 110) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
