    // ---- hover tips over the picture: pro page bar (main window) and the 极简 pill window ----
    function uitestFind(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const kids = item.children || []
        for (let k = 0; k < kids.length; ++k) { const f = uitestFind(kids[k], name); if (f) return f }
        return null
    }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.openUrl("@VIDEO@")
            if (ticks === 6) root.goPage("pro")
            if (ticks === 8) {
                const b = uitestFind(root.contentItem, "pro-fullscreen")
                if (!b) { veyra.logUi("uitest", "UITEST_FAIL no fullscreen button"); running = false; return }
                const p = b.mapToGlobal(b.width / 2, b.height / 2)
                veyra.logUi("uitest", "UITEST_HOVER " + p.x + " " + p.y)
            }
            if (ticks === 10) veyra.logUi("uitest", "UITEST_SHOT pro-tip")
            if (ticks === 11) root.goPage("min")
            if (ticks === 14) {
                const b = uitestFind(fullBar.contentItem, "cine-load")
                if (!b) { veyra.logUi("uitest", "UITEST_FAIL no load button"); running = false; return }
                const p = b.mapToGlobal(b.width / 2, b.height / 2)
                veyra.logUi("uitest", "UITEST_HOVER " + p.x + " " + p.y)
            }
            if (ticks === 16) { veyra.logUi("uitest", "UITEST_NOTE pill hovered=" + fullBar.hovered + " hit=" + fullBar.hitRect); veyra.logUi("uitest", "UITEST_SHOT pill-tip") }
            if (ticks === 17) { veyra.logUi("uitest", "UITEST_DONE"); running = false }
        }
    }
