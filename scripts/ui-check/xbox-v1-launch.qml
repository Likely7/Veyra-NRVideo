    // ---- V1 live launch: switch engine to 1, then resumeLast ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        property bool launched: false
        onTriggered: {
            ++ticks
            if (ticks < 4) return
            if (!launched) {
                if (!veyra.xbox.state.signedIn) { veyra.logUi("uitest", "UITEST_FAIL not signed in"); running = false; return }
                veyra.xbox.set("transportEngine", 1)
                const ok = veyra.xbox.resumeLast()
                veyra.logUi("uitest", "UITEST_NOTE V1 resumeLast=" + ok)
                if (!ok) { veyra.logUi("uitest", "UITEST_FAIL no last console"); running = false; return }
                launched = true
                return
            }
            const st = veyra.xbox.state
            const stream = st.stream || {}
            const ss = String(stream.state || "")
            if (ticks % 5 === 0) veyra.logUi("uitest", "UITEST_NOTE t=" + ticks + " streamState=" + ss)
            if (ss === "streaming") { veyra.logUi("uitest", "UITEST_NOTE V1 STREAMING UP"); veyra.logUi("uitest", "UITEST_SHOT v1-live"); veyra.logUi("uitest", "UITEST_DONE"); running = false; return }
            const status = String(st.status || "")
            if (status.indexOf("失败") >= 0 || status.indexOf("出错") >= 0 || ss === "failed" || ss === "ended") {
                veyra.logUi("uitest", "UITEST_NOTE V1 FAILURE: " + status)
                veyra.logUi("uitest", "UITEST_SHOT v1-failure")
                veyra.logUi("uitest", "UITEST_DONE")
                running = false
                return
            }
            if (ticks > 120) { veyra.logUi("uitest", "UITEST_NOTE V1 timeout status=" + status); veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }