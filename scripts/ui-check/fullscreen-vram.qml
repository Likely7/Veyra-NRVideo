    // ---- fullscreen VRAM growth (field report 2026-10-02: +700 MiB/min only in fullscreen) ----
    // Windowed for 40 s, fullscreen for @FULLS@ s, windowed again; [vram-watch]/[frame-rate] give the numbers.
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        readonly property int full: @FULLS@
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.openUrl("@VIDEO@")
            if (ticks === 4) { @EXTRA@ }
            if (ticks === 40) { veyra.logUi("uitest", "UITEST_NOTE fullscreen on"); root.toggleFullscreen() }
            if (ticks === 40 + full) { veyra.logUi("uitest", "UITEST_NOTE fullscreen off"); root.toggleFullscreen() }
            if (ticks === 40 + full + 40) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
