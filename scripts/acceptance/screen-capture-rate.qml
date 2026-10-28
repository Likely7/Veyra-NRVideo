// Loaded only into a private staging copy, never the user's running package.
// claude-capture-fps-audio-20261027: window capture (WGC, no game mode, no
// effects) of a probe window that presents exactly 60 FPS, at the 60 and the 120
// FPS limit; 10 s steady state each. screen-capture-rate.py reads the rates from
// the engine log between the marks.
import QtQuick
Item {
    id: test
    property string targetId: ""
    property int phase: 0
    property int ticks: 0
    property var limits: [2, 3]          // setScreenOption("fps") index: 60, 120
    property int limitIndex: 0
    function log(text) { veyra.logUi("screen-rate-test", text); console.log("SR", text) }
    function state() { return "running=" + veyra.running + " applying=" + veyra.applying + " source=" + veyra.sourceSummary + " submitFps=" + veyra.submitFps.toFixed(2) }
    Timer {
        interval: 250; repeat: true; running: test.targetId.length > 0
        onTriggered: { try {
            ++test.ticks
            if (test.phase === 0) {
                if (test.ticks < 8) return
                const fps = test.limits[test.limitIndex]
                for (const [k, v] of [["kind", 0], ["method", 0], ["fps", fps], ["cursor", false], ["passthrough", false], ["fill", false]])
                    if (!veyra.setScreenOption(k, v)) throw new Error("setScreenOption " + k)
                veyra.refreshCaptureTargets()
                veyra.screenTargetId = test.targetId
                if (veyra.screenTargetId !== test.targetId) throw new Error("probe window not listed: " + test.targetId)
                if (!veyra.startScreenCapture()) throw new Error("startScreenCapture refused")
                test.log("start limitIndex=" + fps + " | " + test.state())
                test.phase = 1; test.ticks = 0
            } else if (test.phase === 1) {
                if (!veyra.running || veyra.applying) { if (test.ticks > 80) throw new Error("capture did not start"); return }
                if (test.ticks === 12) test.log("steady-start fps=" + test.limits[test.limitIndex] + " | " + test.state())
                if (test.ticks < 12 + 40) return
                test.log("steady-end fps=" + test.limits[test.limitIndex] + " | " + test.state())
                veyra.stopPlayback()
                test.phase = 2; test.ticks = 0
            } else if (test.phase === 2) {
                if (test.ticks < 8) return
                if (++test.limitIndex < test.limits.length) { test.phase = 0; test.ticks = 8; return }
                console.log("SR_PASS"); Qt.quit()
            }
        } catch (e) { console.error("SR_FAIL", test.phase, e.message); test.log("FAIL " + e.message); Qt.quit() } }
    }
}
