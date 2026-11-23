    // ---- FG + optical flow must stay enabled (AMD OF compiled in) ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        property bool launched: false
        property bool flowSet: false
        onTriggered: {
            ++ticks
            if (ticks < 4) return
            if (!launched) {
                if (!veyra.xbox.state.signedIn) { veyra.logUi("uitest", "UITEST_FAIL not signed in"); running = false; return }
                // FSR FG + optical flow motion
                veyra.fgBackendName = "fsr3"
                veyra.fgMultiplier = 2
                veyra.fgMotionSource = 1 // OpticalFlow
                veyra.setOpticalFlowChoice(1) // AmdFidelityFx (I-card path)
                veyra.fgEnabled = true
                const ok = veyra.xbox.resumeLast()
                veyra.logUi("uitest", "UITEST_NOTE resumeLast=" + ok + " fgEnabled=" + veyra.fgEnabled + " motion=" + veyra.fgMotionSource)
                launched = true
                return
            }
            const st = veyra.xbox.state
            const ss = String((st.stream || {}).state || "")
            if (ss === "streaming" && !flowSet) {
                flowSet = true
                veyra.logUi("uitest", "UITEST_NOTE streaming fgEnabled=" + veyra.fgEnabled + " motion=" + veyra.fgMotionSource + " backend=" + veyra.fgBackendName)
                veyra.logUi("uitest", "UITEST_SHOT fg-flow")
                return
            }
            if (flowSet && ticks % 4 === 0) {
                const ok = veyra.fgEnabled && veyra.fgMotionSource === 1
                veyra.logUi("uitest", "UITEST_NOTE after fgEnabled=" + veyra.fgEnabled + " motion=" + veyra.fgMotionSource)
                veyra.logUi("uitest", ok ? "UITEST_DONE" : "UITEST_FAIL FG disabled by optical flow")
                running = false
            }
            if (ticks > 90) { veyra.logUi("uitest", "UITEST_NOTE timeout ss=" + ss); veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }