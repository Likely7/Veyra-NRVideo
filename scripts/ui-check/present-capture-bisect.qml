
    // ---- which page keeps presenting during playback ---------------------------------------
    property int uiFrames: 0
    Connections { target: root; function onFrameSwapped() { root.uiFrames += 1 } }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 3000; running: true; repeat: true
        property int phase: 0
        onTriggered: {
            veyra.logUi("uitest", "UITEST_NOTE phase " + phase + " frames/3s=" + root.uiFrames)
            root.uiFrames = 0
            phase += 1
            if (phase === 1) { dialogs.open("capture") }
            if (phase === 2) { veyra.startCaptureSession(); dialogs.close() }
            if (phase === 5) minPage.parent = null
            if (phase === 6) dialogs.parent = null
            if (phase === 7) { streamHud.parent = null; toast.parent = null }
            if (phase === 8) root.contentItem.visible = false
            if (phase === 10) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
