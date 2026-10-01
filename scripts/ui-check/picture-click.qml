
    // ---- click the picture: pause/resume a file; arrows seek 5 s; double click = fullscreen ----
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        property real pos: 0
        function center() { return root.contentItem.mapToGlobal(videoHost.x + videoHost.width / 2, videoHost.y + videoHost.height / 2) }
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.openUrl("@VIDEO@")
            if (ticks === 6) { const c = center(); veyra.logUi("uitest", "UITEST_NOTE playing=" + !veyra.paused); veyra.logUi("uitest", "UITEST_CLICK " + c.x + " " + c.y) }
            if (ticks === 8) { veyra.logUi("uitest", "UITEST_NOTE after click playing=" + !veyra.paused); const c = center(); veyra.logUi("uitest", "UITEST_CLICK " + c.x + " " + c.y) }
            if (ticks === 10) { veyra.logUi("uitest", "UITEST_NOTE after 2nd click playing=" + !veyra.paused); pos = veyra.position; veyra.logUi("uitest", "UITEST_KEY 39 1") }
            if (ticks === 11) { veyra.logUi("uitest", "UITEST_NOTE right arrow moved " + (veyra.position - pos).toFixed(2) + " s"); const c = center(); veyra.logUi("uitest", "UITEST_DCLICK " + c.x + " " + c.y) }
            if (ticks === 13) {
                veyra.logUi("uitest", "UITEST_NOTE after double click fullscreen=" + root.fullscreen + " playing=" + !veyra.paused)
                const ok = root.fullscreen && !veyra.paused
                root.toggleFullscreen()
                veyra.logUi("uitest", ok ? "UITEST_DONE" : "UITEST_FAIL")
                running = false
            }
        }
    }
