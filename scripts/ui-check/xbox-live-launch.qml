    // ---- real-console launch: capture the actual WebView2 startup outcome ----
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
                const ok = veyra.xbox.resumeLast()
                veyra.logUi("uitest", "UITEST_NOTE resumeLast=" + ok)
                if (!ok) { veyra.logUi("uitest", "UITEST_FAIL no last console"); running = false; return }
                launched = true
                return
            }
            const st = veyra.xbox.state
            const stream = st.stream || {}
            if (ticks % 5 === 0) veyra.logUi("uitest", "UITEST_NOTE t=" + ticks + " status=" + (st.status || "") + " streaming=" + st.streaming + " streamState=" + (stream.state || ""))
            const ss = String(stream.state || "")
            if (ss === "streaming" || ss === "3") { veyra.logUi("uitest", "UITEST_NOTE STREAMING UP"); veyra.logUi("uitest", "UITEST_SHOT xbox-live"); veyra.logUi("uitest", "UITEST_DONE"); running = false; return }
            const status = String(st.status || "")
            if (status.indexOf("失败") >= 0 || status.indexOf("出错") >= 0 || ss === "failed" || ss === "5" || ss === "ended" || ss === "4") {
                veyra.logUi("uitest", "UITEST_NOTE FAILURE TEXT: " + status)
                veyra.logUi("uitest", "UITEST_SHOT xbox-failure")
                veyra.logUi("uitest", "UITEST_DONE")
                running = false
                return
            }
            if (ticks > 150) { veyra.logUi("uitest", "UITEST_NOTE timeout status=" + status); veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }