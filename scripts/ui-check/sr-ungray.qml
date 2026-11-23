    // ---- SR card must un-gray when FSR picked on a non-NVIDIA GPU ----
    function findCard(item, name) {
        if (!item) return null
        if (item.objectName === name) return item
        const kids = item.children || []
        for (let k = 0; k < kids.length; ++k) { const f = findCard(kids[k], name); if (f) return f }
        return null
    }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        interval: 700; running: true; repeat: true
        property int ticks: 0
        onTriggered: {
            ++ticks
            if (ticks < 5) return
            if (ticks === 4) { veyra.navigate("pro"); }
            if (ticks === 5) {
                // force an NVIDIA-only saved algorithm to reproduce the gray card
                veyra.videoSrQuality = 0
                veyra.logUi("uitest", "UITEST_NOTE set quality=0 (DLSS, unavailable here)")
            } else if (ticks === 7) {
                const card = findCard(proPage, "list-sr")
                const grayBefore = card ? !card.switchAvailable : "nocard"
                veyra.logUi("uitest", "UITEST_NOTE grayBefore=" + grayBefore)
                // now pick FSR via the same path the dropdown uses
                veyra.videoSrQuality = 5
            } else if (ticks === 9) {
                const card = findCard(proPage, "list-sr")
                const avail = card ? card.switchAvailable : "nocard"
                const cap = veyra.effectAvailability("sr")
                veyra.logUi("uitest", "UITEST_NOTE afterFSR switchAvailable=" + avail + " cap=" + cap.available + " quality=" + veyra.videoSrQuality)
                veyra.logUi("uitest", "UITEST_SHOT sr-fsr-ungray")
                const ok = (avail === true) && cap.available && veyra.videoSrQuality === 5
                veyra.logUi("uitest", ok ? "UITEST_DONE" : "UITEST_FAIL SR card still gray after FSR")
                running = false
            }
        }
    }