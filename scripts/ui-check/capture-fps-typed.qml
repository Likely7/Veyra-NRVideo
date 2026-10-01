
    // ---- capture dialog: a typed device frame rate must stick without Enter or a focus change ----
    function uitestFind(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const kids = item.children || []
        for (let k = 0; k < kids.length; ++k) { const f = uitestFind(kids[k], name); if (f) return f }
        return null
    }
    Connections { target: dialogs; function onDialogChanged() { veyra.logUi("uitest", "UITEST_NOTE dialog=" + dialogs.dialog + " stack=" + String(new Error().stack).split(String.fromCharCode(10)).join(" | ")) } }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 1000; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks === 2) dialogs.open("capture")
            if (ticks === 4) {
                const f = uitestFind(dialogs, "capture-fps")
                if (!f) {
                    const names = []
                    const walk = (it, d) => { if (!it || d > 40) return; if (it.objectName) names.push(it.objectName); for (const c of (it.children || [])) walk(c, d + 1) }
                    walk(dialogs, 0)
                    const kids = dialogs.children
                    let first = []
                    for (let k = 0; k < kids.length; ++k) first.push(String(kids[k]) + ":" + kids[k].objectName + ":" + kids[k].visible)
                    veyra.logUi("uitest", "UITEST_NOTE kids=" + kids.length + " " + first.join(" | "))
                    veyra.logUi("uitest", "UITEST_FAIL no fps field; dialog=" + dialogs.dialog + " names=" + names.join(","))
                    running = false; return
                }
                const p = f.mapToGlobal(f.width / 2, f.height / 2)
                veyra.logUi("uitest", "UITEST_NOTE before=" + veyra.captureRequestedFps)
                veyra.logUi("uitest", "UITEST_NOTE click at " + p.x + "," + p.y + " window " + root.x + "," + root.y)
                const h = f.mapToItem(dialogs, f.width / 2, f.height / 2)
                const top = dialogs.childAt(h.x, h.y)
                veyra.logUi("uitest", "UITEST_NOTE host point " + h.x + "," + h.y + " top=" + top + " name=" + (top ? top.objectName : "") + " fieldParentChain=" + (function () { let a = []; let it = f; while (it) { a.push(it.objectName || String(it).split("(")[0]); it = it.parent } return a.join("<") })())
                veyra.logUi("uitest", "UITEST_CLICK " + p.x + " " + p.y)
            }
            if (ticks === 5) {
                const f = uitestFind(dialogs, "capture-fps")
                veyra.logUi("uitest", "UITEST_NOTE focus=" + (f ? f.children[0].activeFocus : "?") + " dialog=" + dialogs.dialog)
                veyra.logUi("uitest", "UITEST_SHOT clicked")
            }
            if (ticks === 6) veyra.logUi("uitest", "UITEST_KEY 35 1")      // End
            if (ticks === 7) veyra.logUi("uitest", "UITEST_KEY 8 6")       // Backspace x6
            if (ticks === 9) { veyra.logUi("uitest", "UITEST_KEY 51 1"); }  // '3'
            if (ticks === 10) veyra.logUi("uitest", "UITEST_KEY 48 1")     // '0'
            if (ticks === 12) veyra.logUi("uitest", "UITEST_SHOT typed")
            if (ticks === 14) {
                dialogs.close()
                veyra.logUi("uitest", "UITEST_NOTE after close fps=" + veyra.captureRequestedFps)
            }
            if (ticks === 16) {
                dialogs.open("capture")
                const f = uitestFind(dialogs, "capture-fps")
                veyra.logUi("uitest", "UITEST_NOTE reopened field=" + (f ? f.text : "?"))
                veyra.logUi("uitest", veyra.captureRequestedFps === 30 ? "UITEST_DONE" : "UITEST_FAIL fps=" + veyra.captureRequestedFps)
                running = false
            }
        }
    }
