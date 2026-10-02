    // ---- GPU usage orb measures one adapter, chosen in settings (default: high-performance) ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        property var choices: []
        onTriggered: {
            ++ticks
            if (ticks === 1) {
                choices = veyra.gpuMonitorChoices
                veyra.logUi("uitest", "UITEST_NOTE choices " + choices.map(c => (c.id || "auto") + "=" + c.label).join(" | ") + " monitor=" + veyra.gpuMonitorName)
                veyra.openUrl("@VIDEO@")
            }
            if (ticks >= 5 && ticks <= 9) veyra.logUi("uitest", "UITEST_NOTE auto gpu=" + veyra.gpuUtilization.toFixed(1) + " known=" + veyra.gpuUtilizationKnown)
            if (ticks === 10) {
                const other = choices.length > 2 ? choices[2].id : (choices.length > 1 ? choices[1].id : "")
                veyra.logUi("uitest", "UITEST_NOTE switch to " + other + " ok=" + veyra.setPreference("monitorGpu", other) + " monitor=" + veyra.gpuMonitorName)
            }
            if (ticks >= 13 && ticks <= 16) veyra.logUi("uitest", "UITEST_NOTE chosen gpu=" + veyra.gpuUtilization.toFixed(1))
            if (ticks === 17) {
                const bad = veyra.setPreference("monitorGpu", "dead:beef:00000000")
                veyra.setPreference("monitorGpu", "")
                root.goPage("set")
                veyra.logUi("uitest", "UITEST_NOTE unknown id accepted=" + bad + " back to auto monitor=" + veyra.gpuMonitorName)
            }
            if (ticks === 19) veyra.logUi("uitest", "UITEST_SHOT settings")
            if (ticks === 20) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
