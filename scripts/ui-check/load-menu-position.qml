    // ---- the 极简 pill's 载入 menu opens in the same place every time ----
    function uitestFind(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const kids = item.children || []
        for (let k = 0; k < kids.length; ++k) { const f = uitestFind(kids[k], name); if (f) return f }
        return null
    }
    function uitestText(item, text) {
        if (!item) return null
        if (item.text === text && item.visible) return item
        const kids = item.children || []
        for (let k = 0; k < kids.length; ++k) { const f = uitestText(kids[k], text); if (f) return f }
        return null
    }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 800; running: true; repeat: true
        property int ticks: 0
        property var spots: []
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.openUrl("@VIDEO@")
            if (ticks === 6) root.goPage("min")
            const open = [9, 13, 17]
            const read = [11, 15, 19]
            if (open.indexOf(ticks) >= 0) {
                const b = uitestFind(fullBar.contentItem, "cine-load")
                const p = b.mapToGlobal(b.width / 2, b.height / 2)
                veyra.logUi("uitest", "UITEST_CLICK " + p.x + " " + p.y)
            }
            if (read.indexOf(ticks) >= 0) {
                const ov = fullBar.contentItem.Window.window.contentItem.parent
                const ps = uitestText(ov, "PS5 串流"), xb = uitestText(ov, "Xbox 串流")
                const a = ps ? ps.mapToGlobal(0, 0) : null, c = xb ? xb.mapToGlobal(0, 0) : null
                const s = (a ? Math.round(a.y) : "none") + "/" + (c ? Math.round(c.y) : "none")
                spots.push(s)
                veyra.logUi("uitest", "UITEST_NOTE open " + spots.length + " ps5/xbox y=" + s)
                veyra.logUi("uitest", "UITEST_SHOT menu" + spots.length)
                veyra.logUi("uitest", "UITEST_KEY 27")
            }
            if (ticks === 21) { veyra.logUi("uitest", spots.every(v => v === spots[0]) ? "UITEST_DONE" : "UITEST_FAIL moved " + spots.join(" ")); running = false }
        }
    }
