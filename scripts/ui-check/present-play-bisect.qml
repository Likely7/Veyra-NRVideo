
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
            if (phase === 1) veyra.openUrl("@VIDEO@")
            if (phase === 3) proPage.parent = null
            if (phase === 4) { for (const p of pages.children) if (p.pageId === "node") p.parent = null }
            if (phase === 5) { for (const p of pages.children) if (p.pageId === "exp" || p.pageId === "set" || p.pageId === "home") p.parent = null }
            if (phase === 6) dock.visible = false
            if (phase === 8) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
