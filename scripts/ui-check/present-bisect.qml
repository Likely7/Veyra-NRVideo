
    // ---- which part of the scene keeps the main window presenting --------------------------
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
            if (phase === 2) pages.visible = false
            if (phase === 3) dock.visible = false
            if (phase === 4) backdrop.visible = false
            if (phase === 5) dialogs.visible = false
            if (phase === 6) { for (const c of root.contentItem.children) if (c.objectName.indexOf("window-resize") === 0) c.visible = false }
            if (phase === 7) root.contentItem.visible = false
            if (phase === 8) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
