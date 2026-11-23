    // ---- engine selector: V1/V2 switch visible, default V2, persists ----
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
                const sel = uitestFind(root.contentItem, "xbox-transport-engine")
                if (!sel || !sel.visible) { veyra.logUi("uitest", "UITEST_FAIL no engine selector"); running = false; return }
                if (String(sel.value).indexOf("V2") < 0) { veyra.logUi("uitest", "UITEST_FAIL default not V2: " + sel.value); running = false; return }
                veyra.logUi("uitest", "UITEST_NOTE engine selector visible, default=" + sel.value)
                veyra.logUi("uitest", "UITEST_SHOT engine-default-v2")
                veyra.xbox.set("transportEngine", 1)
            } else if (ticks === 6) {
                const sel = uitestFind(root.contentItem, "xbox-transport-engine")
                if (!sel || String(sel.value).indexOf("V1") < 0) { veyra.logUi("uitest", "UITEST_FAIL V1 not applied: " + (sel ? sel.value : "gone")); running = false; return }
                veyra.logUi("uitest", "UITEST_NOTE switched to " + sel.value)
                veyra.logUi("uitest", "UITEST_SHOT engine-v1")
                veyra.xbox.set("transportEngine", 2)
            } else if (ticks === 8) {
                const sel = uitestFind(root.contentItem, "xbox-transport-engine")
                const ok = sel && String(sel.value).indexOf("V2") >= 0
                veyra.logUi("uitest", ok ? "UITEST_DONE" : "UITEST_FAIL restore=" + (sel ? sel.value : "gone"))
                running = false
            }
        }
    }