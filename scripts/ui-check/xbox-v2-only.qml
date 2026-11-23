    // ---- v2-only Xbox dialog: no engine/plan selectors, bitrate always visible ----
    function uitestFind(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const kids = item.children || []
        for (let k = 0; k < kids.length; ++k) { const f = uitestFind(kids[k], name); if (f) return f }
        return null
    }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 700; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks < 4) return
            if (ticks === 4) {
                if (dialogs.dialog !== "xbox") { veyra.logUi("uitest", "UITEST_FAIL dialog=" + dialogs.dialog); running = false; return }
                const gone1 = uitestFind(root.contentItem, "xbox-transport-engine")
                const gone2 = uitestFind(root.contentItem, "xbox-plan")
                if (gone1 || gone2) { veyra.logUi("uitest", "UITEST_FAIL selectors still present"); running = false; return }
                const bitrate = uitestFind(root.contentItem, "xbox-bitrate")
                const backend = uitestFind(root.contentItem, "xbox-decode-backend")
                if (!bitrate || !bitrate.visible) { veyra.logUi("uitest", "UITEST_FAIL bitrate row missing/hidden"); running = false; return }
                if (!backend || !backend.visible) { veyra.logUi("uitest", "UITEST_FAIL decode backend missing"); running = false; return }
                veyra.logUi("uitest", "UITEST_NOTE v2-only dialog: no selectors, bitrate+backend visible")
                veyra.logUi("uitest", "UITEST_SHOT v2-only-dialog")
                // bitrate persists
                veyra.xbox.set("videoBitrateKbps", 20000)
            } else if (ticks === 6) {
                const bitrate = uitestFind(root.contentItem, "xbox-bitrate")
                const ok = bitrate && bitrate.text === "20"
                veyra.xbox.set("videoBitrateKbps", 0)
                veyra.logUi("uitest", ok ? "UITEST_DONE" : "UITEST_FAIL bitrate text=" + (bitrate ? bitrate.text : "gone"))
                running = false
            }
        }
    }