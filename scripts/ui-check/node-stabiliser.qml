
    // ---- 输出稳定器 from the node page's output card (node mode) ----------------------------
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 2000; running: true; repeat: true
        property int phase: 0
        onTriggered: {
            phase += 1
            if (phase === 1) veyra.openUrl("@VIDEO@")
            if (phase === 2) root.goPage("node")
            if (phase === 3) veyra.nrHoldStrength = 0.8
            if (phase === 5) veyra.logUi("uitest", "UITEST_SHOT node-stabiliser")
            if (phase === 6) { veyra.logUi("uitest", "UITEST_NOTE nodeMode=" + veyra.nodeMode + " hold=" + veyra.nrHoldStrength); veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
