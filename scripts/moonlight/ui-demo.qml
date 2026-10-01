    // Injected into a staged copy of Main.qml by scripts/moonlight/ui-demo.py: drives the PC streaming
    // dialog against the mock host (add host, pair, app list, settings, a launch that fails at RTSP) and
    // reports through the log. Never part of the product.
    Timer {
        id: mlDemo
        interval: 250; running: true; repeat: true
        property int step: 0
        property int ticks: 0
        property int total: 0
        readonly property string address: {
            // One argument (--ml-address=host:port): a bare value would be taken for a file to open.
            const a = Qt.application.arguments
            for (let i = 0; i < a.length; ++i) if (a[i].indexOf("--ml-address=") === 0) return a[i].substring(13)
            return ""
        }
        function log(text) { veyra.logUi("mltest", text) }
        function shot(name) { log("MLTEST_SHOT " + name) }
        function check(ok, label) { if (!ok) throw new Error(label) }
        function next(n) { step = n; ticks = 0 }
        readonly property var ml: veyra.moonlight
        onTriggered: { try {
            ++ticks; ++total
            if (total > 900) throw new Error("bounded step=" + step)
            const st = ml.state
            if (step === 0) {
                if (ticks < 8) return
                check(ml !== null, "the build has the PC streaming model")
                dialogs.open("moonlight")
                next(1); return
            }
            if (step === 1) {
                if (ticks < 8) return
                check(ml.hosts.length === 0, "no hosts at first")
                shot("01-empty")
                next(11); return
            }
            if (step === 11) {
                if (ticks < 14) return   // the screenshot is taken by another process; give it time
                ml.addHost(address)
                next(2); return
            }
            if (step === 2) {
                if (ml.hosts.length !== 1) { if (ticks > 40) throw new Error("host did not appear: " + st.status); return }
                check(ml.hosts[0].state === "unpaired", "host listed as unpaired: " + ml.hosts[0].state)
                if (ticks < 6) return
                shot("02-host-unpaired")
                next(12); return
            }
            if (step === 12) {
                if (ticks < 14) return
                ml.pair(ml.hosts[0].id)
                next(3); return
            }
            if (step === 3) {
                if (!st.pin) { if (ticks > 20) throw new Error("no pin shown"); return }
                log("MLTEST_PIN " + st.pin)   // the script photographs the PIN box, then hands the PIN to the mock host
                next(4); return
            }
            if (step === 4) {
                if (!st.paired || ml.apps.length === 0 || st.busy) { if (ticks > 80) throw new Error("not paired in time: " + st.status); return }
                check(ml.apps.length === 3, "three apps")
                if (ticks < 4) return
                shot("04-paired-apps")
                next(14); return
            }
            if (step === 14) {
                if (ticks < 14) return
                ml.set("res", "4k"); ml.set("fps", 120); ml.set("codec", 2); ml.set("hdr", true); ml.set("bitrate", 100)
                next(5); return
            }
            if (step === 5) {
                if (ticks < 6) return
                shot("05-settings")
                next(15); return
            }
            if (step === 15) {
                if (ticks < 14) return
                ml.connectStream(881448767)
                next(6); return
            }
            if (step === 6) {
                // The mock has no RTSP server: the launch request succeeds, the connection then fails and the
                // player reports it (the library retries for about a minute before giving up).
                if (ticks < 8 || !veyra.failed) {
                    if (ticks > 360) throw new Error("no failure reported: status=" + st.status)
                    return
                }
                log("MLTEST_STREAM failed " + veyra.statusText)
                check(veyra.statusText.indexOf("10061") >= 0 || veyra.statusText.indexOf("连接") >= 0, "the failure is explained: " + veyra.statusText)
                shot("06-connect-failed")
                next(7); return
            }
            if (step === 7) {
                if (ticks < 12) return
                shot("07-after-failure")
                running = false
                log("MLTEST_DONE")
                console.log("MLTEST_PASS")
                Qt.quit(); return
            }
        } catch (e) { running = false; log("MLTEST_FAIL " + e.message); console.log("MLTEST_FAIL " + e.message); Qt.quit() } }
    }
