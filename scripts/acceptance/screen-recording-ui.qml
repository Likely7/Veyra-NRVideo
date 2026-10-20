// Used only in the task's staging copy of Main.qml, with the production bridge.
// Drives the real recording path: open a file, start the recorder, let it run,
// stop it, and report the file the engine wrote.
import QtQuick
Item {
    id: test
    property string media: ""
    property string evidence: ""
    property var appWindow: Window.window
    property int step: 0
    property int ticks: 0
    property int recordedTicks: 0

    function find(item, name) {
        if (item.objectName === name) return item
        for (const child of item.children || []) {
            const hit = find(child, name)
            if (hit) return hit
        }
        return null
    }
    function require(ok, why) {
        if (!ok) { console.error("RECORD_UI_FAIL", step, why); timer.stop(); Qt.quit(); throw new Error(why) }
    }
    Connections {
        target: veyra
        function onNotice(message, error) { console.log("RECORD_NOTICE", message, error) }
    }
    Timer {
        id: timer
        interval: 200; repeat: true; running: true
        onTriggered: {
            if (++test.ticks > 900) { test.require(false, "180-second deadline"); return }
            if (!test.media.length) return
            if (test.step === 0) {
                // The source is opened from the command line; wait for the picture.
                if (!veyra.hasSource || test.ticks < 20) return
                test.require(veyra.setPreference("recordDir", test.evidence + "/recordings"),
                             "the recording folder preference was refused")
                veyra.startRecording()
                test.step = 1
            } else if (test.step === 1) {
                if (!veyra.recording) {
                    if (test.ticks > 100) test.require(false, "the recorder never started")
                    return
                }
                test.recordedTicks = test.ticks
                test.step = 2
            } else if (test.step === 2) {
                // Record a few seconds of the presented picture.
                if (test.ticks - test.recordedTicks < 25) return
                test.require(veyra.recording, "the recording stopped on its own")
                veyra.stopRecording()
                test.step = 3
            } else if (test.step === 3) {
                if (veyra.recording) return
                const state = veyra.recordingState
                test.require(state && state.path && state.path.length > 0, "no file was reported")
                test.require(state.frames > 0, "no frame reached the encoder")
                test.require(!state.failed, "the recorder reported a failure: " + (state.error || ""))
                console.log("RECORD_UI_PASS frames=" + state.frames + " dropped=" + state.dropped
                            + " seconds=" + state.seconds + " path=" + state.path
                            + " encoder=" + state.encoder)
                timer.stop()
                Qt.quit()
            }
        }
    }
}
