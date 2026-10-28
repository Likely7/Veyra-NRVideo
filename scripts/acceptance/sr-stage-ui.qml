// Loaded only into a private staging copy, never the user's running package.
// claude-sr-stage-20261028: the three-step SR rows of the list SR card, driven
// through the controls themselves while a 4K file plays with RTX Video SR.
import QtQuick
Item {
    id: test
    property string media: ""
    function find(name) {
        let hit = null
        const walk = item => { if (hit) return; if (item.objectName === name) { hit = item; return } for (const c of item.children) walk(c) }
        walk(test.parent.parent)   // the Loader's parent: the window's content item
        return hit
    }
    Timer {
        interval: 500; running: test.media.length > 0; repeat: true
        property int step: 0
        property int ticks: 0
        property int total: 0
        function check(ok, label) { if (!ok) throw new Error(label); veyra.logUi("sr-stage-test", "PASS " + label) }
        function next(n) { step = n; ticks = 0 }
        function settled() { return ticks >= 6 && !veyra.applying && veyra.running }
        onTriggered: { try {
            ++ticks; ++total; if (total > 400) throw new Error("bounded step=" + step)
            if (step === 0) {
                if (ticks < 4) return
                veyra.muted = true; veyra.fgEnabled = false; veyra.nrEnabled = false
                veyra.srEnabled = true; veyra.videoSrQuality = 3; veyra.srTargetIndex = 2
                veyra.openPath(test.media); next(1); return
            }
            if (step === 1) {
                if (!settled()) return
                check(veyra.srActive, "RTX Video SR restores the 4K file at its size: " + veyra.statusText)
                check(veyra.srStageSummary === "", "single step shows no three-step sizes")
                const prescale = find("list-sr-prescale"), output = find("list-sr-output")
                check(prescale && output && prescale.enabled && output.enabled, "the list SR card has both rows, enabled for RTX Video SR")
                prescale.picked("3"); next(2); return   // 1080p, through the control
            }
            if (step === 2) {
                if (!settled()) return
                check(veyra.srPrescale === 3, "the prescale control sets 1080p")
                check(veyra.srActive && veyra.srStageSummary === "3840x2160 → 1920x1080 → 3840x2160", "running sizes " + veyra.srStageSummary)
                find("list-sr-target").picked("3"); next(3); return   // 8K
            }
            if (step === 3) {
                if (!settled()) return
                find("list-sr-output").picked("3"); next(4); return   // fit 4K
            }
            if (step === 4) {
                if (!settled()) return
                check(veyra.srOutput === 3, "the output control sets 4K")
                check(veyra.srStageSummary === "3840x2160 → 1920x1080 → 7680x4320 → 3840x2160", "running sizes " + veyra.srStageSummary)
                check(veyra.outputSummary === "3840x2160", "the chain output stays 4K: " + veyra.outputSummary)
                find("list-sr-prescale").picked("manual"); next(5); return
            }
            if (step === 5) {
                if (!settled()) return
                check(veyra.srPrescale === 175 && find("list-sr-prescale-percent-row").visible, "manual prescale starts at 75 %")
                check(veyra.srStageSummary === "3840x2160 → 2880x1620 → 7680x4320 → 3840x2160", "running sizes " + veyra.srStageSummary)
                // A 2K target after a 1440p prescale cannot enlarge: reported, not applied.
                find("list-sr-output").picked("0"); veyra.srTargetIndex = 1; veyra.srPrescale = 4; next(6); return
            }
            if (step === 6) {
                if (!settled()) return
                check(veyra.srStageNote.indexOf("先降到未生效") === 0, "an ineffective prescale is reported: " + veyra.srStageNote)
                check(find("list-sr-prescale").options.find(o => o.id === "4").disabled === true, "1440p is marked unavailable for a 2K target")
                veyra.videoSrQuality = 5; next(7); return   // AMD FSR
            }
            if (step === 7) {
                if (!settled()) return
                check(!find("list-sr-prescale").enabled && !find("list-sr-output").enabled, "another SR backend disables both rows")
                const before = veyra.srPrescale; veyra.srPrescale = 3
                check(veyra.srPrescale === before, "the bridge refuses a prescale edit for another backend")
                veyra.videoSrQuality = 3; veyra.srTargetIndex = 2; veyra.srPrescale = 0; next(8); return
            }
            if (step === 8) {
                if (!settled()) return
                check(veyra.srStageSummary === "" && veyra.srStageNote === "", "prescale off restores the single step")
                veyra.nodeMode = 1
                check(veyra.addEffect("sr") >= 0, "add an SR node to the initially empty node graph")
                veyra.navigate("node"); next(9); return
            }
            if (step === 9) {
                if (!settled()) return
                veyra.nrEnabled = false; veyra.srEnabled = true
                veyra.videoSrQuality = 3; veyra.srTargetIndex = 2
                const prescale = find("node-sr-prescale"), output = find("node-sr-output")
                if (!prescale || !output) return
                check(prescale.enabled && output.enabled, "node SR editor exposes both enabled rows")
                prescale.picked("3"); output.picked("0"); next(10); return
            }
            if (step === 10) {
                if (!settled()) return
                check(veyra.nodeMode === 1 && veyra.srActive && veyra.srPrescale === 3,
                      "node editor applies the prescale through its control")
                check(veyra.srStageSummary === "3840x2160 → 1920x1080 → 3840x2160",
                      "node editor running sizes " + veyra.srStageSummary)
                veyra.logUi("sr-stage-test", "SR_STAGE_UI_PASS"); console.log("SR_STAGE_UI_PASS"); Qt.quit(); return
            }
        } catch (e) { veyra.logUi("sr-stage-test", "SR_STAGE_UI_FAIL " + e.message); console.log("SR_STAGE_UI_FAIL " + e.message); Qt.quit() } }
    }
}
