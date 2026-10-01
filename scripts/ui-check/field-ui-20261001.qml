
    // ---- field UI fixes 2026-10-01 (scripts/ui-check/ui-check.py) ----------------------
    // Needs --page min. Opens a portrait PNG and a landscape video (file URLs given to
    // ui-check.py as PORTRAIT=... LANDSCAPE=...).
    readonly property var uiFiles: ["@PORTRAIT@", "@LANDSCAPE@"]
    property int uiStep: 0
    property bool uiFailed: false
    property var uiStart: ({})
    property var uiSamples: []
    function uiLog(m) { veyra.logUi("uitest", m) }
    function uiFail(m) { uiFailed = true; uiLog("UITEST_FAIL " + m) }
    function uiFind(item, name, out) {
        if (item.objectName === name) out.push(item)
        for (const c of item.children) uiFind(c, name, out)
        return out
    }
    function uiGeometry(tag) {
        const s = root.screen
        uiLog("UITEST_NOTE " + tag + " window=" + root.x + "," + root.y + " " + root.width + "x" + root.height
              + " picture=" + root.pictureHeight + " max=" + root.maxPictureHeight + " aspect=" + root.filmAspect.toFixed(4)
              + " screen=" + (s ? s.virtualX + "," + s.virtualY + " " + s.width + "x" + s.height + " avail=" + s.desktopAvailableHeight : "?")
              + " page=" + root.page + " fullscreen=" + root.fullscreen)
    }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    Timer {
        id: uiSampler
        interval: 100
        repeat: true
        onTriggered: root.uiSamples.push(root.uiFind(proPage, "stage-bar-fill", []).map(b => b.width))
    }
    readonly property var uiSteps: [
        [1800, () => { if (root.page !== "min") return uiFail("start page " + root.page); uiLog("UITEST_SHOT 01-min-empty") }],
        [1200, () => { const p = minPage.mapToGlobal(minPage.width / 2, root.pictureHeight * 0.7); uiLog("UITEST_CLICK " + p.x + " " + p.y) }],
        [1200, () => { if (!minPage.sourceMenuOpen) return uiFail("a click on the empty picture did not open the source menu"); uiLog("UITEST_SHOT 02-min-menu") }],
        [900, () => { minPage.closeSourceMenu(); veyra.openUrl(root.uiFiles[0]) }],
        [3500, () => {
            uiGeometry("portrait")
            const s = root.screen
            const bottom = s.virtualY + Math.min(s.height, s.desktopAvailableHeight)
            if (root.height > root.maxPictureHeight + 1) return uiFail("portrait window taller than the screen allows: " + root.height)
            if (root.y + root.height + 46 > bottom + 1) return uiFail("portrait pill below the screen: y=" + root.y + " h=" + root.height)
            if (Math.abs(root.height - root.width / root.filmAspect) > 2) return uiFail("portrait picture does not fill the window width")
            uiLog("UITEST_SHOT 03-portrait")
        }],
        [900, () => {
            if (!fullBar.visible) return uiFail("cinema pill not shown")
            root.uiStart = { x: root.x, y: root.y }
            const x = fullBar.x + 16 + 220, y = fullBar.y + fullBar.menuRoom + 84
            // Left and up: the window sits on the screen's lower edge after the portrait fit.
            uiLog("UITEST_DRAG " + x + " " + y + " " + (x - 160) + " " + (y - 40))
        }],
        [1800, () => {
            uiGeometry("dragged")
            // The drag threshold (about 10px) is not part of the move.
            const dx = root.x - root.uiStart.x, dy = root.y - root.uiStart.y
            if (dx > -140 || dx < -170 || (root.uiStart.y > 60 && (dy > -20 || dy < -50)))
                return uiFail("dragging the pill moved the window by " + dx + "," + dy)
            uiLog("UITEST_SHOT 04-dragged")
        }],
        [900, () => veyra.openUrl(root.uiFiles[1])],
        [3500, () => {
            uiGeometry("landscape")
            if (root.width < 1000) return uiFail("landscape film did not get the width back: " + root.width)
            uiLog("UITEST_SHOT 05-landscape")
        }],
        // NR on, so the stage bars carry real GPU time (the field case: NR 5.8 ms).
        [900, () => { veyra.nrEnabled = true; veyra.seekTo(0); if (veyra.paused) veyra.togglePlayPause(); root.goPage("pro") }],
        [6000, () => { root.uiSamples = []; uiSampler.start() }],
        [3200, () => {
            uiSampler.stop()
            const n = root.uiSamples.length ? root.uiSamples[0].length : 0
            let summary = []
            for (let i = 0; i < n; ++i) {
                const w = root.uiSamples.map(row => row[i])
                const lo = Math.min(...w), hi = Math.max(...w)
                summary.push(lo.toFixed(1) + "-" + hi.toFixed(1))
                if (hi > 0.5 && lo < hi * 0.5) return uiFail("stage bar " + i + " jumps between " + lo + " and " + hi)
            }
            uiLog("UITEST_NOTE bars samples=" + root.uiSamples.length + " widths(min-max)=" + summary.join(" ")
                  + " chainTotalMs=" + veyra.chainTotalMs.toFixed(2) + " budgetMs=" + veyra.stageBudgetMs.toFixed(2)
                  + " load=" + (veyra.stageBudgetMs > 0 ? Math.round(veyra.chainTotalMs / veyra.stageBudgetMs * 100) : -1) + "%"
                  + " outputRatio=" + veyra.outputRateRatio.toFixed(3) + " nrActive=" + veyra.nrActive
                  + " position=" + veyra.position.toFixed(1) + "/" + veyra.duration.toFixed(1))
            uiLog("UITEST_SHOT 06-pro")
        }],
        [900, () => { root.goPage("min"); root.toggleFullscreen() }],
        [2000, () => { uiGeometry("fullscreen"); if (!root.fullscreen) return uiFail("fullscreen did not start"); dock.requestPage("pro") }],
        [1800, () => {
            uiGeometry("after dock")
            if (root.fullscreen || (root.page !== "pro" && root.page !== "node")) return uiFail("dock 专业 in fullscreen: page=" + root.page + " fullscreen=" + root.fullscreen)
            uiLog("UITEST_SHOT 07-pro-from-fullscreen")
        }],
        [1200, () => uiLog("UITEST_DONE")]
    ]
    Timer {
        id: uiRun
        running: true
        interval: root.uiSteps[0][0]
        onTriggered: {
            const step = root.uiSteps[root.uiStep]
            step[1]()
            root.uiStep += 1
            if (!root.uiFailed && root.uiStep < root.uiSteps.length) { interval = root.uiSteps[root.uiStep][0]; restart() }
        }
    }
