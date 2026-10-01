
    // ---- pro list stage timings show every NR layer when two or more run ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) { veyra.openUrl("@VIDEO@"); veyra.nrEnabled = true }
            if (ticks === 8) veyra.logUi("uitest", "UITEST_NOTE one layer: " + veyra.stageTimings.map(r => r.label + "=" + r.ms.toFixed(2)).join(", "))
            if (ticks === 9) { const i = veyra.addEffect("nr"); veyra.setEffectEnabled(i, true); veyra.logUi("uitest", "UITEST_NOTE addEffect nr -> " + i + " enabled=" + veyra.nrLayers.map(l => l.enabled).join(",")) }
            if (ticks === 16) {
                const rows = veyra.stageTimings
                veyra.logUi("uitest", "UITEST_NOTE two layers: " + rows.map(r => r.label + "=" + r.ms.toFixed(2)).join(", "))
                root.goPage("pro")
            }
            if (ticks === 18) veyra.logUi("uitest", "UITEST_SHOT pro")
            if (ticks === 19) {
                const labels = veyra.stageTimings.map(r => r.label)
                const ok = labels.indexOf("NR 第1层") >= 0 && labels.indexOf("NR 第2层") >= 0
                veyra.logUi("uitest", ok ? "UITEST_DONE" : "UITEST_FAIL " + labels.join(","))
                running = false
            }
        }
    }
