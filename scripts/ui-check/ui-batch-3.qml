
    // ---- field requests 2026-10-01 (third batch): page drag strip, slider arrow keys, ---------
    // pro subtitle/audio buttons, hiding the cinema pill. Needs --page pro and VIDEO.
    property var uiStart: ({})
    property var uiSlider: null
    function uiLog(m) { veyra.logUi("uitest", m) }
    function uiFail(m) { uiRun.failed = true; uiLog("UITEST_FAIL " + m) }
    function uiFind(item, test, out) {
        if (test(item)) out.push(item)
        for (const c of item.children) uiFind(c, test, out)
        return out
    }
    Timer { interval: 150; running: true; repeat: true; onTriggered: veyra.logUi("uitest-tick", "") }
    onXChanged: veyra.logUi("uitest", "win x=" + x)
    onYChanged: veyra.logUi("uitest", "win y=" + y)
    readonly property var uiSteps: [
        [1500, () => veyra.openUrl("@VIDEO@")],
        [3000, () => {
            if (root.page !== "pro") return uiFail("start page " + root.page)
            const a = root.screenArea
            uiLog("UITEST_NOTE window=" + root.x + "," + root.y + " " + root.width + "x" + root.height + " screenArea=" + a.x + "," + a.y + " " + a.width + "x" + a.height)
            const cc = uiFind(proPage, i => i.objectName === "pro-subtitles", [])[0]
            const au = uiFind(proPage, i => i.objectName === "pro-audio-tracks", [])[0]
            if (!cc || !cc.visible || !au || !au.visible) return uiFail("pro subtitle/audio buttons missing")
            const p = au.mapToGlobal(au.width / 2, au.height / 2)
            uiLog("UITEST_CLICK " + p.x + " " + p.y)
        }],
        [1200, () => uiLog("UITEST_SHOT 01-pro-audio-menu")],
        [800, () => uiLog("UITEST_KEY 27")],
        [1000, () => {
            root.uiStart = { x: root.x, y: root.y }
            // Empty middle of the pro page header.
            const x = root.x + root.width * 0.45, y = root.y + 30
            uiLog("UITEST_DRAG " + x + " " + y + " " + (x + 120) + " " + (y + 70))
        }],
        [1500, () => {
            const dx = root.x - root.uiStart.x, dy = root.y - root.uiStart.y
            uiLog("UITEST_NOTE page drag moved " + dx + "," + dy)
            if (dx < 95 || dy < 45) return uiFail("dragging the pro header moved the window by " + dx + "," + dy)
            proPage.tab = "audio"
            veyra.volume = 0.5
        }],
        [1200, () => {
            // A slider in the inspector: the first visible one (the 声音 tab's volume).
            const sliders = uiFind(proPage, i => i.keyStep !== undefined && i.visible && i.enabledControl && i.width > 40, [])
            if (!sliders.length) return uiFail("no slider found")
            root.uiSlider = sliders[0]
            const s = root.uiSlider
            root.uiStart = { v: s.value }
            const k = s.mapToGlobal(s.width * s.frac, s.height / 2)
            uiLog("UITEST_NOTE slider value=" + s.value.toFixed(3) + " range=" + s.from + ".." + s.to + " keyStep=" + s.keyStep)
            uiLog("UITEST_CLICK " + k.x + " " + k.y)
        }],
        [1000, () => { uiLog("UITEST_NOTE before keys focus=" + root.uiSlider.activeFocus + " windowActive=" + root.active); uiLog("UITEST_KEY 39 3") }],
        [1500, () => {
            const s = root.uiSlider
            const moved = s.value - root.uiStart.v
            uiLog("UITEST_NOTE slider after 3x Right: " + s.value.toFixed(3) + " (moved " + moved.toFixed(3) + ", focus=" + s.activeFocus + ")")
            if (!s.activeFocus || Math.abs(moved - 3 * s.keyStep) > s.keyStep * 0.6 + 0.0001)
                return uiFail("arrow keys moved the slider by " + moved + " (step " + s.keyStep + ")")
            uiLog("UITEST_KEY 37 3")
        }],
        [1500, () => {
            const s = root.uiSlider
            if (Math.abs(s.value - root.uiStart.v) > s.keyStep * 0.6 + 0.0001) return uiFail("Left did not move back: " + s.value)
            uiLog("UITEST_KEY 27")
            root.goPage("min")
        }],
        [2500, () => {
            if (!fullBar.visible) return uiFail("pill not shown on 极简")
            dock.requestTogglePill()
        }],
        [1500, () => {
            if (fullBar.visible || !root.pillHidden) return uiFail("pill still shown after hiding")
            uiLog("UITEST_SHOT 02-min-pill-hidden")
            dock.requestTogglePill()
        }],
        [1500, () => {
            if (!fullBar.visible || root.pillHidden) return uiFail("pill not back")
            uiLog("UITEST_DONE")
        }]
    ]
    Timer {
        id: uiRun
        property int step: 0
        property bool failed: false
        running: true
        interval: root.uiSteps[0][0]
        onTriggered: {
            root.uiSteps[step][1]()
            step += 1
            if (!failed && step < root.uiSteps.length) { interval = root.uiSteps[step][0]; restart() }
        }
    }
