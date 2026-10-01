    // Injected by scripts/moonlight/ui-demo.py (with this file as the snippet): screenshots of the home page
    // and the Xbox dialog in the signed-out state. Contacts no service.
    Timer {
        interval: 250; running: true; repeat: true
        property int step: 0
        property int ticks: 0
        function log(text) { veyra.logUi("mltest", text) }
        onTriggered: { try {
            ++ticks
            if (step === 0 && ticks === 10) { log("MLTEST_SHOT x1-home"); step = 1; ticks = 0; return }
            if (step === 1 && ticks === 14) {
                if (veyra.xbox === null) throw new Error("no Xbox model in this build")
                dialogs.open("xbox"); step = 2; ticks = 0; return
            }
            if (step === 2 && ticks === 8) {
                if (veyra.xbox.state.signedIn) throw new Error("unexpectedly signed in")
                log("MLTEST_SHOT x2-xbox-signed-out"); step = 3; ticks = 0; return
            }
            if (step === 3 && ticks === 14) {
                dialogs.open("moonlight"); step = 4; ticks = 0; return
            }
            if (step === 4 && ticks === 8) { log("MLTEST_SHOT x3-moonlight"); step = 5; ticks = 0; return }
            if (step === 5 && ticks === 14) { running = false; log("MLTEST_DONE"); console.log("MLTEST_PASS"); Qt.quit() }
        } catch (e) { running = false; log("MLTEST_FAIL " + e.message); Qt.quit() } }
    }
