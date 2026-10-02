    // ---- home 「Xbox 串流」 card must open the Xbox dialog every time ----
    function uitestCard(item, act) {
        if (!item) return null
        if (item.modelData !== undefined && item.modelData && item.modelData.act === act && item.width === 164) return item
        const kids = item.children || []
        for (let k = 0; k < kids.length; ++k) { const f = uitestCard(kids[k], act); if (f) return f }
        return null
    }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 700; running: true; repeat: true
        property int ticks: 0
        property int round: 0
        property string seen: ""
        onTriggered: {
            ++ticks
            const phase = ticks % 4
            if (ticks < 3) return
            if (phase === 0) {
                const c = uitestCard(root.contentItem, "xbox")
                if (!c) { veyra.logUi("uitest", "UITEST_FAIL no xbox card"); running = false; return }
                const p = c.mapToGlobal(c.width / 2, c.height / 2)
                veyra.logUi("uitest", "UITEST_CLICK " + p.x + " " + p.y)
            } else if (phase === 2) {
                ++round
                seen += (seen ? "," : "") + (dialogs.dialog || "none")
                veyra.logUi("uitest", "UITEST_NOTE round " + round + " dialog=" + dialogs.dialog)
                if (round === 1) veyra.logUi("uitest", "UITEST_SHOT round1")
                dialogs.close()
                if (round >= 8) { veyra.logUi("uitest", seen.split(",").every(d => d === "xbox") ? "UITEST_DONE" : "UITEST_FAIL " + seen); running = false }
            }
        }
    }
