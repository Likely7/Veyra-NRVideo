    // ---- SR availability on a non-NVIDIA GPU: card must not be RTX-gated ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 700; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks < 5) return
            if (ticks === 5) {
                const cap = veyra.effectAvailability("sr")
                veyra.logUi("uitest", "UITEST_NOTE sr available=" + cap.available + " reason=" + cap.reason + " quality=" + veyra.videoSrQuality)
                const choices = veyra.srBackendChoices || []
                const fsr = choices.find(o => o.id === "5")
                veyra.logUi("uitest", "UITEST_NOTE fsr choice disabled=" + (fsr ? fsr.disabled : "missing"))
                // pick FSR then enable SR
                veyra.videoSrQuality = 5
                veyra.srEnabled = true
            } else if (ticks === 8) {
                const cap = veyra.effectAvailability("sr")
                const ok = cap.available && veyra.srEnabled && veyra.videoSrQuality === 5
                veyra.logUi("uitest", "UITEST_NOTE after: available=" + cap.available + " srEnabled=" + veyra.srEnabled + " quality=" + veyra.videoSrQuality)
                veyra.logUi("uitest", ok ? "UITEST_DONE" : "UITEST_FAIL sr not enableable on this GPU")
                running = false
            }
        }
    }