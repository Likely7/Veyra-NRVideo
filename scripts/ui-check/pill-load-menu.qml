
    // ---- 极简 pill: the 载入 icon opens the sources menu; picking 采集卡 opens its dialog ----
    function uitestFind(item, test) {
        if (!item) return null
        if (test(item)) return item
        const kids = item.children || []
        for (let k = 0; k < kids.length; ++k) { const f = uitestFind(kids[k], test); if (f) return f }
        return null
    }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) veyra.openUrl("@VIDEO@")
            if (ticks === 6) {
                const b = uitestFind(fullBar.contentItem, it => it.objectName === "cine-load")
                if (!b) { veyra.logUi("uitest", "UITEST_FAIL no load button"); running = false; return }
                const p = b.mapToGlobal(b.width / 2, b.height / 2)
                veyra.logUi("uitest", "UITEST_CLICK " + p.x + " " + p.y)
            }
            if (ticks === 8) veyra.logUi("uitest", "UITEST_SHOT menu")
            if (ticks === 9) {
                const t = uitestFind(fullBar.contentItem, it => it.text === "采集卡" && it.visible)
                if (!t) { veyra.logUi("uitest", "UITEST_FAIL menu item not found"); running = false; return }
                const p = t.mapToGlobal(t.width / 2, t.height / 2)
                veyra.logUi("uitest", "UITEST_CLICK " + p.x + " " + p.y)
            }
            if (ticks === 11) veyra.logUi("uitest", "UITEST_SHOT dialog")
            if (ticks === 12) {
                veyra.logUi("uitest", dialogs.dialog === "capture" ? "UITEST_DONE" : "UITEST_FAIL dialog=" + dialogs.dialog)
                running = false
            }
        }
    }
