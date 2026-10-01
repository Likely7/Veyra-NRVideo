
    // ---- live capture: UI presents vs video, pages detached; node page survives a round trip --
    property int uiFrames: 0
    Connections { target: root; function onFrameSwapped() { root.uiFrames += 1 } }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 3000; running: true; repeat: true
        property int phase: 0
        onTriggered: {
            veyra.logUi("uitest", "UITEST_NOTE phase " + phase + " page=" + root.page + " uiFrames/3s=" + root.uiFrames + " captureFps=" + veyra.captureFps.toFixed(1))
            root.uiFrames = 0
            phase += 1
            if (phase === 1) dialogs.open("capture")
            if (phase === 2) { veyra.startCaptureSession(); dialogs.close() }
            if (phase === 6) root.goPage("node")
            if (phase === 7) { veyra.logUi("uitest", "UITEST_SHOT node-1"); root.goPage("min") }
            if (phase === 8) root.goPage("node")
            if (phase === 9) { veyra.logUi("uitest", "UITEST_SHOT node-2"); root.goPage("min") }
            if (phase === 12) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
