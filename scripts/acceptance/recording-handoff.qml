// Loaded only into a private test copy of Main.qml (production bridge).
// Drives a real recording: configure, start, act on schedule, stop, wait for
// the file, and print what the recorder reported. Config is JSON:
//   {dir, seconds, audio, fps, bitrate, hevc, effects:{...}, actions:[{at, do}]}
import QtQuick
import QtQuick.Window
Item {
    id: test
    property string config: "{}"
    property var cfg: JSON.parse(config)
    property int step: 0
    property int ticks: 0
    property int mark: 0
    property int done: 0
    property int rounds: 0
    function out(tag, value) { console.log("RECTEST", tag, typeof value === "string" ? value : JSON.stringify(value)) }
    function fail(why) { out("FAIL", why); Qt.quit() }
    function stateJson() {
        const s = veyra.recordingState || {}
        return { active: veyra.recording, frames: s.frames, dropped: s.dropped, seconds: s.seconds, files: s.files,
                 path: s.path, encoder: s.encoder, audio: s.audio, failed: s.failed, error: s.error, note: s.note, finishing: s.finishing }
    }
    function findControl(item, name) {
        if (item.objectName === name) return item
        for (const child of item.children || []) {
            const found = findControl(child, name)
            if (found) return found
        }
        return null
    }
    function control(name) { return findControl(test.Window.window.contentItem, name) }
    function clickRecord() {
        const mouse = control("pro-record-mouse")
        if (!mouse || !mouse.enabled) { fail("record control unavailable"); return false }
        // Invoke the production MouseArea signal. This checks the real handler
        // and timer, not native hit-testing or physical pointer coordinates.
        mouse.clicked(null)
        return true
    }
    function act(name) {
        out("ACTION", name)
        if (name === "colour-on") veyra.colorEnabled = true
        else if (name === "colour-off") veyra.colorEnabled = false
        else if (name === "nr-on") veyra.nrEnabled = true
        else if (name === "nr-off") veyra.nrEnabled = false
        else if (name === "sr-on") { veyra.srTargetIndex = 2; veyra.srEnabled = true }
        else if (name === "sr-off") veyra.srEnabled = false
        else if (name === "fg-on") { veyra.fgMultiplier = test.cfg.fgMultiplier || 2; veyra.fgEnabled = true }
        else if (name === "fg-off") veyra.fgEnabled = false
        else if (name === "pause" || name === "resume") veyra.togglePlayPause()
        else if (name === "seek") veyra.seekTo(2)
        else if (name === "stop-playback") veyra.stopPlayback()
    }
    Connections {
        target: veyra
        function onNotice(message, error) { console.log("RECTEST notice", error, message) }
    }
    Timer {
        interval: 100; repeat: true; running: true
        onTriggered: {
            ++test.ticks
            if (test.ticks > 2400) { test.fail("deadline"); return }
            if (test.step === 0) {
                if (!veyra.hasSource || test.ticks < 30 || !test.cfg.dir) return
                veyra.nodeMode = 0
                veyra.muted = !!test.cfg.muted
                const fx = test.cfg.effects || {}
                veyra.nrEnabled = !!fx.nr
                veyra.srEnabled = false
                if (test.cfg.fsr) veyra.videoSrQuality = 5
                veyra.colorEnabled = !!fx.colour
                if (fx.sr) { veyra.srTargetIndex = fx.srTarget || 2; veyra.srEnabled = true }
                if (fx.fg) { veyra.fgMultiplier = fx.fg; veyra.fgEnabled = true } else veyra.fgEnabled = false
                if (!test.cfg.noRecord) {
                    if (!test.cfg.expectFailure && !veyra.setPreference("recordDir", test.cfg.dir)) { test.fail("recordDir rejected"); return }
                    if (test.cfg.expectFailure && veyra.recordingDirectory !== test.cfg.dir) { test.fail("invalid-directory profile not loaded"); return }
                    veyra.setPreference("recordAudio", test.cfg.audio !== false)
                    veyra.setPreference("recordFps", test.cfg.fps || 0)
                    veyra.setPreference("recordBitrateMbps", test.cfg.bitrate || 0)
                    veyra.setPreference("recordHevc", !!test.cfg.hevc)
                }
                test.mark = test.ticks; test.step = 1
            } else if (test.step === 1) {
                // Let the effect settings apply before the file opens.
                if (test.ticks - test.mark < (test.cfg.settle || 30)) return
                if (test.cfg.noRecord) { test.mark = test.ticks; test.step = 5; return }
                if (test.cfg.useButton) {
                    if (!test.clickRecord()) return
                    if (test.control("pro-record-control").countdown !== 3) { test.fail("countdown did not start"); return }
                    test.mark = test.ticks; test.step = 6; return
                }
                veyra.startRecording()
                test.mark = test.ticks; test.step = 2
            } else if (test.step === 6) {
                if (test.ticks - test.mark < 12) return
                if (test.control("pro-record-control").countdown !== 2) { test.fail("countdown did not tick"); return }
                test.clickRecord()
                if (test.control("pro-record-control").countdown !== 0 || veyra.recording) { test.fail("countdown cancellation failed"); return }
                test.out("BUTTON_CANCELLED", test.stateJson())
                test.clickRecord()
                test.mark = test.ticks; test.step = 2
            } else if (test.step === 2) {
                const s = veyra.recordingState || {}
                if (!veyra.recording && test.cfg.expectFailure) { test.out("FINAL", test.stateJson()); Qt.quit(); return }
                if (!veyra.recording) { if (test.ticks - test.mark > 50) test.fail("not recording: " + JSON.stringify(test.stateJson())); return }
                if (!(s.frames > 0)) { if (test.ticks - test.mark > 100) test.fail("no frame reached the encoder"); return }
                test.out("STARTED", test.stateJson())
                test.mark = test.ticks; test.done = 0; test.step = 3
            } else if (test.step === 3) {
                const elapsed = (test.ticks - test.mark) / 10
                const actions = test.cfg.actions || []
                while (test.done < actions.length && elapsed >= actions[test.done].at) { test.act(actions[test.done].do); ++test.done }
                if (!veyra.recording && test.cfg.expectStop !== true) { test.fail("recording stopped early: " + JSON.stringify(test.stateJson())); return }
                if (elapsed < test.cfg.seconds && veyra.recording) return
                test.out("BEFORE_STOP", test.stateJson())
                if (veyra.recording) { if (test.cfg.useButton) test.clickRecord(); else veyra.stopRecording() }
                test.mark = test.ticks; test.step = 4
            } else if (test.step === 5) {
                const elapsed = (test.ticks - test.mark) / 10
                const actions = test.cfg.actions || []
                while (test.done < actions.length && elapsed >= actions[test.done].at) { test.act(actions[test.done].do); ++test.done }
                if (elapsed < test.cfg.seconds) return
                test.out("FINAL", test.stateJson()); Qt.quit()
            } else if (test.step === 4) {
                const s = veyra.recordingState || {}
                if (veyra.recording || s.finishing) { if (test.ticks - test.mark > 200) test.fail("file not finished"); return }
                test.out("FINAL", test.stateJson())
                if (++test.rounds < (test.cfg.repeats || 1)) { test.mark = test.ticks; test.step = 1; return }
                Qt.quit()
            }
        }
    }
}
