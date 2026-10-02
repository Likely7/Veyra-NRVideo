    // ---- colour 效果 group: texture / clarity / dehaze must change the picture (field report 2026-10-02) ----
    // Paused on one frame, so a screenshot difference can only come from the setting.
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1200; running: true; repeat: true
        property int ticks: 0
        readonly property var steps: [
            ["base", "", 0], ["clarity+", "clarity", 100], ["clarity-", "clarity", -100],
            ["texture+", "texture", 100], ["dehaze+", "dehaze", 100], ["dehaze-", "dehaze", -100], ["base2", "", 0]]
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.openUrl("@VIDEO@")
            if (ticks === 4) { root.goPage("pro"); veyra.colorEnabled = true }
            if (ticks === 5) { veyra.seekTo(12); if (!veyra.paused) veyra.togglePlayPause() }
            const k = ticks - 7
            if (k >= 0 && k < steps.length * 2) {
                const s = steps[Math.floor(k / 2)]
                if (k % 2 === 0) {
                    for (const n of ["clarity", "texture", "dehaze"]) veyra.setColourParameter(n, 0)
                    if (s[1]) veyra.setColourParameter(s[1], s[2])
                    veyra.logUi("uitest", "UITEST_NOTE " + s[0] + " paused=" + veyra.paused + " clarity=" + veyra.colourParameter("clarity"))
                } else veyra.logUi("uitest", "UITEST_SHOT " + s[0])
            }
            if (k === steps.length * 2 + 2) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
