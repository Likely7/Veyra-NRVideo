// Private-profile regression for the 2026-10-28 freeze; never opens a device.
import QtQuick
Item {
    id: test
    property string media: ""
    Timer {
        interval: 500; running: test.media.length > 0; repeat: true
        property int ticks: 0
        property int step: 0
        property int played: 0
        function check(ok,label) { if (!ok) throw new Error(label); veyra.logUi("sr-safety-test","PASS " + label) }
        onTriggered: { try {
            if (++ticks > 100) throw new Error("bounded safety test step=" + step)
            if (step === 0) {
                if (ticks < 4) return
                check(veyra.srTargetIndex === 3 && veyra.srPrescale === 3, "restore preserves internal 1080p to 8K")
                check(veyra.srOutput === 3, "restore bounds final output before any source opens")
                check(veyra.nrEnabled && veyra.fgEnabled, "restore retains NR and frame-generation selections")
                // Attempt to undo the output fit while the other stages are on.
                veyra.srOutput = 0
                check(veyra.srOutput === 3, "unsafe output edit is bounded atomically")
                veyra.muted = true
                veyra.openPath(test.media); step = 1; return
            }
            if (step === 1) {
                if (!veyra.running || veyra.applying) return
                if (++played < 24) return
                check(veyra.outputSummary === "3840x2160", "running output stays 4K")
                check(veyra.srStageSummary === "3840x2160 → 1920x1080 → 7680x4320 → 3840x2160", "SR evaluates 8K and fits before the downstream stages")
                check(veyra.srActive && veyra.nrActive && veyra.fgActive, "SR, NR and frame generation all actually active")
                veyra.logUi("sr-safety-test","SR_SAFETY_UI_PASS"); console.log("SR_SAFETY_UI_PASS"); Qt.quit()
            }
        } catch(e) { veyra.logUi("sr-safety-test","SR_SAFETY_UI_FAIL " + e.message); console.log("SR_SAFETY_UI_FAIL " + e.message); Qt.quit() } }
    }
}
