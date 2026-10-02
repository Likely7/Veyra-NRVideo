// 专业模式 · 列表视图, rebuilt from the design (pages-pro.js + pages.css .pro).
//
// The design's grid: 1fr | 376px columns, rows auto | 1fr | auto, gap 10,
// padding 14. The inspector spans rows 2-3 on the right; the three meters sit
// under the video at a fixed 172px.
//
// The chain order is fixed (SR → NR layers → protect → HDR → 补帧) and is NOT
// reorderable here: the user's reason was that dragging breaks the pipeline for
// beginners. Node mode is where free arrangement lives, and it is a separate page
// reached through the view toggle (which confirms first, per the design).
import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

VPage {
    id: root
    signal requestPage(string page)
    signal requestDialog(string key)
    signal requestFullscreen()
    // Fullscreen quick-adjust (Home, Main.qml): only the inspector is shown, moved
    // to the left of the picture, with the performance orbs beside it. Both are cut
    // out of the native video window like any other cover.
    property bool overlay: false
    function overlayContains(x, y) {
        const hit = item => x >= item.x && x <= item.x + item.width && y >= item.y && y <= item.y + item.height
        return overlay && (hit(insp) || hit(quickOrbs))
    }
    // Leaving the page (or switching to the node editor) disarms region drawing;
    // showing it replays the GPU bars' staggered entrance (M27).
    onVisibleChanged: {
        if (!visible && veyra.protectionDrawShape.length > 0) veyra.beginProtectionDraw("")
        if (visible) armBars()
    }

    // M27 .bar u: width 0 -> value over .8s --spring-soft, bar i starting at
    // 120 + i*60 ms after the page shows; later readings move without the delay.
    property bool barsArmed: false
    property bool barsSettled: false
    function armBars() { barsSettled = false; barsArmed = false; barsArmTimer.restart() }
    Timer { id: barsArmTimer; interval: 16; onTriggered: { root.barsArmed = true; barsSettleTimer.restart() } }
    Timer { id: barsSettleTimer; interval: 1600; onTriggered: root.barsSettled = true }
    Component.onCompleted: armBars()

    // M31: switching tabs redraws the tab's cards one after another
    // (pages-pro.js render(true): 460 ms, i*30 ms, y 10).
    Component { id: staggerComp; VRise.Stagger {} }
    function stagger(col, step, span, dy) {
        if (Theme.reduced || !col) return
        let i = 0
        for (let k = 0; k < col.children.length; ++k) {
            const c = col.children[k]
            if (!c.visible || c.motionDy === undefined) continue
            staggerComp.createObject(root, { target: c, delay: i * step, span: span, dy: dy }).start()
            ++i
        }
    }
    onTabChanged: Qt.callLater(() => stagger(tab === "quality" ? qualityCol : tab === "fg" ? fgCol
                                           : tab === "color" ? colourPanel.sectionColumn
                                           : tab === "audio" ? audioCol : displayCol, 30, 460, 10))

    // M30: the NR layer card that was just added pops in; the number is reset
    // shortly after so a later rebuild of the list does not pop it again.
    property int popLayer: -1
    Timer { id: popReset; interval: 900; onTriggered: root.popLayer = -1 }
    function markPop(number) { popLayer = number; popReset.restart() }

    // Design's st.tab: quality / fg / color / audio / display.
    property string tab: "quality"
    property int selectedEffect: -1
    // engine::ContentRate order.
    readonly property var contentRates: [
        { id: "0", label: qsTr("采用源时间戳") }, { id: "1", label: qsTr("自动识别内容节奏") },
        { id: "2", label: qsTr("识别 30fps 内容") }, { id: "3", label: qsTr("识别 50fps 内容") },
        { id: "4", label: qsTr("识别 60fps 内容") }, { id: "5", label: qsTr("采集 60→30（PS5 30 帧）") }
    ]
    // 处理顺序 "点击定位": switch to the chip's tab, open its card and scroll
    // the inspector so the card sits at the top.
    function locateNode(chip) {
        selectedEffect = chip.index
        let target = ""
        if (chip.type === "frame-generation") { tab = "fg"; target = "list-fg-group" }
        else if (chip.type === "color") { tab = "color"; target = "" }
        else {
            tab = "quality"
            if (chip.type === "sr") target = "list-sr"
            else if (chip.type === "video-hdr") target = "list-video-hdr"
            else if (chip.type === "protection") target = "list-global-protection"
            else if (chip.type === "nr") {
                nrStage.open = true
                let ordinal = 0
                for (const n of veyra.chain) { if (n.type === "nr") ++ordinal; if (n.index === chip.index) break }
                target = "nr-card-" + ordinal
            }
        }
        Qt.callLater(() => {
            const find = item => {
                if (!item) return null
                if (target.length > 0 && item.objectName === target) return item
                for (let i = 0; i < item.children.length; ++i) { const r = find(item.children[i]); if (r) return r }
                return null
            }
            const card = target.length > 0 ? find(body) : null
            if (card && card.open !== undefined) card.open = true
            if (card && card.flash !== undefined) card.flash()
            const y = card ? card.mapToItem(body, 0, 0).y - 4 : 0
            inspFlick.contentY = Math.max(0, Math.min(y, Math.max(0, inspFlick.contentHeight - inspFlick.height)))
        })
    }
    onSelectedEffectChanged: {
        veyra.selectedNrLayer = selectedEffect
        veyra.selectedColourLayer = selectedEffect
    }

    // The NR layer card wraps the shared editor (NrLayerEditor.qml).
    // 显示同步 and 输出上限 (1.4.4 PresentationSettings), shared by the list 补帧
    // page and the node editor's output box (ProPage.PresentationRows).
    component PresentationRows: ColumnLayout {
        Layout.fillWidth: true
        spacing: 0
        VRow {
            label: qsTr("显示同步")
            hint: qsTr("独立于低延迟队列 · 自动：不撕裂，也不等待垂直同步")
            VSeg {
                objectName: "presentation-display-sync"
                options: [{ id: "0", label: qsTr("允许撕裂") }, { id: "1", label: qsTr("垂直同步") }, { id: "2", label: qsTr("自动") }]
                current: String(veyra.displaySync)
                onPicked: id => veyra.displaySync = Number(id)
            }
        }
        VRow {
            label: qsTr("输出上限")
            enabled: !veyra.presentationOwned
            hint: veyra.presentationStatus
            VSeg {
                objectName: "presentation-output-rate"
                options: [{ id: "0", label: qsTr("关闭", "off") }, { id: "1", label: qsTr("跟随显示器") }, { id: "2", label: qsTr("自定义") }]
                current: String(veyra.outputRateMode)
                onPicked: id => veyra.outputRateMode = Number(id)
            }
        }
        VRow {
            label: qsTr("自定义上限")
            enabled: !veyra.presentationOwned
            visible: veyra.outputRateMode === 2
            RowLayout {
                spacing: 6
                DialogHost.VTextField {
                    objectName: "presentation-custom-fps"
                    implicitWidth: 90
                    text: veyra.outputCustomFps.toFixed(3)
                    onEdited: text => { const v = Number(text); if (isFinite(v)) veyra.outputCustomFps = v }
                }
                Text { text: "FPS"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11 }
            }
        }
    }
    // A performance orb: a ring filled to `fraction` (green under 70 %, amber
    // under 100 %, red above), the value in the middle, the label under it.
    // Shared with the node page as ProPage.PerfOrb.
    component PerfOrb: Item {
        id: orb
        property real fraction: 0
        property bool known: false
        property string value: "—"
        property string label: ""
        property string tip: ""
        property int size: 56
        // Compact (the node page's 40px bar): no label under the ring; the
        // tooltip names the orb instead.
        property bool showLabel: true
        readonly property color tone: !known ? Qt.rgba(1, 1, 1, 0.25)
                                    : fraction < 0.7 ? Theme.ok : fraction < 1.0 ? "#F2B941" : Theme.err
        implicitWidth: Math.max(size, labelText.implicitWidth)
        implicitHeight: showLabel ? size + 4 + labelText.implicitHeight : size
        property real shown: known ? Math.min(1, Math.max(0, fraction)) : 0
        // Live data changes every second; animating it behind another page redrew the
        // window continuously (see VDot.qml), so only while shown.
        Behavior on shown { enabled: orb.visible; NumberAnimation { duration: Theme.d(600); easing.bezierCurve: Theme.springSoft } }
        onShownChanged: ring.requestPaint()
        onToneChanged: ring.requestPaint()
        Canvas {
            id: ring
            width: orb.size; height: orb.size
            anchors.horizontalCenter: parent.horizontalCenter
            onPaint: {
                const c = getContext("2d"); c.reset()
                const r = width / 2 - 4, cx = width / 2, cy = height / 2
                c.lineWidth = 5; c.lineCap = "round"
                c.strokeStyle = Qt.rgba(1, 1, 1, 0.08)
                c.beginPath(); c.arc(cx, cy, r, 0, Math.PI * 2); c.stroke()
                if (orb.shown > 0.001) {
                    c.strokeStyle = orb.tone
                    c.shadowColor = orb.tone; c.shadowBlur = 8
                    c.beginPath(); c.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * orb.shown); c.stroke()
                }
            }
        }
        Text {
            anchors.centerIn: ring
            width: orb.size - 12
            horizontalAlignment: Text.AlignHCenter
            text: orb.value
            color: orb.known ? Theme.t1 : Theme.t3
            font.family: Theme.fontMono
            font.pixelSize: orb.size >= 50 ? 11 : 9
            font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
            fontSizeMode: Text.HorizontalFit; minimumPixelSize: 8
        }
        Text {
            id: labelText
            visible: orb.showLabel
            anchors.top: ring.bottom; anchors.topMargin: 4
            anchors.horizontalCenter: parent.horizontalCenter
            text: orb.label
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: 10
        }
        HoverHandler { id: orbHover }
        ToolTip.visible: orbHover.hovered && (orb.tip.length > 0 || !orb.showLabel)
        ToolTip.text: (orb.showLabel ? "" : orb.label + "：") + orb.tip
    }
    // The three orbs every performance readout shows: total processing time
    // against the source frame budget, GPU utilization, and whether the output
    // keeps up with its target rate.
    component PerfOrbs: RowLayout {
        property int size: 56
        property bool showLabels: true
        spacing: 10
        PerfOrb {
            size: parent.size
            showLabel: parent.showLabels
            known: veyra.chainTotalMsKnown && veyra.stageBudgetMs > 0
            fraction: known ? veyra.chainTotalMs / veyra.stageBudgetMs : 0
            value: veyra.chainTotalMsKnown ? veyra.chainTotalMs.toFixed(1) + "ms" : "—"
            label: qsTr("处理耗时")
            tip: qsTr("增强链总耗时（最近一秒平均，与 1.4.4 相同口径），环 = 占源帧预算") + (veyra.stageBudgetMs > 0 ? " " + veyra.stageBudgetMs.toFixed(1) + " ms" : "") + qsTr(" 的比例")
        }
        PerfOrb {
            size: parent.size
            showLabel: parent.showLabels
            known: veyra.gpuUtilizationKnown
            fraction: veyra.gpuUtilization / 100
            value: known ? Math.round(veyra.gpuUtilization) + "%" : "—"
            label: qsTr("GPU 占用")
            tip: qsTr("Windows GPU 引擎占用率（") + (veyra.gpuMonitorName || qsTr("全部显卡")) + qsTr("，最忙的引擎类型，全系统），每秒采样；在 设置 → 通用与外观 中更换监控的显卡")
        }
        // How much of one source frame's time the enhancement chain uses (the plan's
        // "负载预算 = 耗时 / 预算"). It used to show output fps / target fps, which
        // reads 100% whenever the output keeps up, however light the load.
        PerfOrb {
            size: parent.size
            showLabel: parent.showLabels
            known: veyra.chainTotalMsKnown && veyra.stageBudgetMs > 0
            fraction: known ? veyra.chainTotalMs / veyra.stageBudgetMs : 0
            value: known ? Math.round(veyra.chainTotalMs / veyra.stageBudgetMs * 100) + "%" : "—"
            label: qsTr("负载预算")
            tip: qsTr("增强链 GPU 耗时（最近一秒平均）占一个源帧时间（") + veyra.stageBudgetMs.toFixed(1)
                 + qsTr(" ms）的比例；超过 100% 就跟不上源帧率。当前状态：") + veyra.runStatus
        }
    }
    // The status light: 1.4.4's 当前状态 (正常 / 补帧调度降档 / 输出未达标 /
    // 输入帧率不足 …) as a glowing strip with the detail beside it.
    component StatusLight: RowLayout {
        id: light
        spacing: 8
        readonly property color tone: veyra.runStatusLevel === "ok" ? Theme.ok
                                    : veyra.runStatusLevel === "warn" ? "#F2B941"
                                    : veyra.runStatusLevel === "err" ? Theme.err : Qt.rgba(1, 1, 1, 0.3)
        Item {
            Layout.fillWidth: true
            Layout.preferredHeight: 4
            Rectangle {
                id: strip
                anchors.fill: parent
                radius: 2
                color: light.tone
                opacity: veyra.runStatusLevel === "idle" ? 0.35 : 0.9
                Behavior on color { enabled: strip.visible; ColorAnimation { duration: Theme.d(300) } }
            }
            // A sibling MultiEffect, not layer.effect: Qt recreates a layer's effect item on
            // a screen DPI change while it walks the parent's children, and the walk then touched
            // the deleted item (crash moving the window to a 200 % monitor, field 2026-10-01).
            MultiEffect {
                source: strip
                anchors.fill: strip
                visible: veyra.runStatusLevel !== "idle"
                opacity: strip.opacity
                shadowEnabled: true; shadowColor: light.tone; shadowBlur: 0.6; shadowVerticalOffset: 0; blurMax: 12; autoPaddingEnabled: true
            }
        }
        Text {
            text: veyra.runStatus + (veyra.runStatusDetail.length > 0 ? " · " + veyra.runStatusDetail : "")
            color: light.tone
            font.family: Theme.fontUi
            font.pixelSize: 11
            font.weight: Font.Medium
        }
    }

    component NrLayerCard: VAccordion {
        id: card
        required property var layerData
        property int layerNumber: 1
        property int layerCount: 1
        property bool nrFirst: false
        signal edited(int nodeIndex, string key, double amount)
        signal enabledEdited(int nodeIndex, bool enabled)
        signal duplicateRequested(int nodeIndex)
        signal removeRequested(int nodeIndex)
        signal orderEdited(bool enabled)
        objectName: "nr-card-" + layerNumber
        Layout.fillWidth: true
        glyph: "wand"; hue: Theme.accent
        compactHeader: true
        popIn: layerNumber === root.popLayer
        title: qsTr("NR 层 ") + layerNumber
        // .lnum: the layer number where a stage card has its icon.
        headerLeadingActions: Rectangle {
            implicitWidth: 22; implicitHeight: 22; radius: 7
            color: Qt.rgba(1, 138 / 255, 61 / 255, 0.16)
            Text {
                anchors.centerIn: parent
                text: card.layerNumber
                color: Theme.accent
                font.family: Theme.fontMono; font.pixelSize: 11
                font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
            }
        }
        headerActions: VButton {
            objectName: "nr-card-more-" + card.layerNumber
            icon: true; ghost: true; iconName: "dup"
            implicitWidth: 26; implicitHeight: 26
            tip: qsTr("复制 · 恢复默认 · 删除")
            onClicked: { nrLayerMenu.layer = card; nrLayerMenu.openAt(this, "down") }
        }
        // This layer's own GPU time (list mode keys a node by its index + 2).
        readonly property var timing: veyra.nodeTimings[String(layerData.index + 2)] || ({})
        readonly property string timingText: timing.state === "measured" ? timing.ms.toFixed(2) + " ms"
                                           : timing.state === "pending" ? qsTr("等待样本") : ""
        summary: nrEditor.sizeLabel + qsTr(" · 强度 ") + layerData.intensity.toFixed(2)
                 + (layerData.enabled ? (timingText.length > 0 ? " · " + timingText : "") : qsTr(" · 已旁路"))
        on: layerData.enabled
        onToggled: on => enabledEdited(layerData.index, on)
        NrLayerEditor {
            id: nrEditor
            Layout.fillWidth: true
            layerData: card.layerData
            layerCount: card.layerCount
            nrFirst: card.nrFirst
            onEdited: (i, k, v) => card.edited(i, k, v)
            onDuplicateRequested: i => card.duplicateRequested(i)
            onRemoveRequested: i => card.removeRequested(i)
            onOrderEdited: e => card.orderEdited(e)
        }
    }

    // --- header (32px) ----------------------------------------------------
    RowLayout {
        id: head
        visible: !root.overlay
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        height: 32
        spacing: 8

        // pages-pro.js data-srcbtn: what is playing, with a chevron for the menu.
        VButton {
            id: sourceBtn
            objectName: "pro-source-button"
            iconName: veyra.sourceKind === "ps5" ? "gamepad" : veyra.sourceKind === "moonlight" ? "cast" : veyra.sourceKind === "xbox" ? "gamepad" : veyra.sourceKind === "screen" ? "monitor"
                    : veyra.sourceKind === "image" ? "image" : veyra.sourceKind === "file" ? "film" : "video"
            text: veyra.sourceTitle.length > 0 ? veyra.sourceTitle : qsTr("片源")
            maxTextWidth: 260
            trailingIcon: "down"
            onClicked: sourceMenu.openAt(this, "down")
        }
        // "1080p60 · YUY2 · SDR": reported values only.
        VTag {
            objectName: "pro-format-tag"
            text: veyra.sourceFormatText.length > 0 ? veyra.sourceFormatText : qsTr("未打开")
        }
        Item { Layout.fillWidth: true }

        // View toggle. Choosing 节点 asks first, because switching rebuilds the
        // whole processing chain (design note: 切换需要确认，会重建处理链).
        VSeg {
            objectName: "pro-mode-switch"
            options: [{ id: "list", label: qsTr("列表"), icon: "list" }, { id: "node", label: qsTr("节点"), icon: "nodes" }]
            current: veyra.nodeMode === 1 ? "node" : "list"
            onPicked: id => {
                const target = id === "node" ? 1 : 0
                if (target === veyra.nodeMode) { if (target === 1) root.requestPage("node"); return }
                switchDialog.targetMode = target
                switchDialog.open()
            }
        }
        VButton { iconName: "camera"; text: qsTr("截图"); onClicked: veyra.takeScreenshot() }
        VButton {
            id: presetBtn
            iconName: "layers"
            trailingIcon: "down"
            text: qsTr("预设：") + veyra.currentPresetName
            onClicked: presetMenu.openAt(this, "down")
        }
    }

    // --- video + transport bar -------------------------------------------
    Rectangle {
        id: vwrap
        visible: !root.overlay
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.top: head.bottom
        anchors.topMargin: 10
        width: root.width - 376 - 14 * 2 - 10
        height: root.height - 14 - 32 - 10 - 172 - 10 - 14
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke
        clip: true

        // The native video window sits over the top of this; the bar is its foot.
        Item {
            id: videoArea
            objectName: "videoArea"
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            height: parent.height - 34
            // Drawing an NR protection region: the native video window lets the
            // pointer through, so this catches the drag; the outline itself is
            // drawn by the bridge on a layered child of the video window.
            MouseArea {
                id: protectionDraw
                objectName: "pro-protection-draw"
                anchors.fill: parent
                enabled: veyra.protectionDrawShape.length > 0
                visible: enabled
                cursorShape: Qt.CrossCursor
                preventStealing: true
                property point start
                // Fractions of the video host, whatever the page layout is.
                function hostFraction(x, y) {
                    const content = protectionDraw.Window.contentItem
                    let host = null
                    for (let i = 0; content && i < content.children.length; ++i)
                        if (content.children[i].objectName === "videoHost") host = content.children[i]
                    const p = protectionDraw.mapToItem(content, x, y)
                    if (!host || host.width <= 0 || host.height <= 0) return Qt.point(x / width, y / height)
                    return Qt.point((p.x - host.x) / host.width, (p.y - host.y) / host.height)
                }
                onPressed: mouse => { start = hostFraction(mouse.x, mouse.y) }
                onPositionChanged: mouse => {
                    const p = hostFraction(mouse.x, mouse.y)
                    veyra.updateProtectionDraw(start.x, start.y, p.x, p.y)
                }
                onReleased: mouse => {
                    const p = hostFraction(mouse.x, mouse.y)
                    veyra.commitProtectionDraw(start.x, start.y, p.x, p.y)
                }
            }
            // 分屏对比: drag the divider (the presenter draws it) across the picture.
            MouseArea {
                objectName: "pro-compare-drag"
                anchors.fill: parent
                enabled: veyra.compareMode === 2 && veyra.protectionDrawShape.length === 0
                visible: enabled
                cursorShape: Qt.SplitHCursor
                preventStealing: true
                function drag(mouse) { veyra.setCompareSplitAt(protectionDraw.hostFraction(mouse.x, mouse.y).x) }
                onPressed: mouse => drag(mouse)
                onPositionChanged: mouse => { if (pressed) drag(mouse) }
            }
            Shortcut {
                sequence: "Escape"
                enabled: veyra.protectionDrawShape.length > 0
                onActivated: veyra.beginProtectionDraw("")
            }
            Text {
                anchors.centerIn: parent
                visible: !veyra.hasSource
                text: veyra.statusText
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsH3
            }
        }

        // .vbar: mono transport row with source/output tags.
        RowLayout {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            height: 34
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            spacing: 10
            Item {
                implicitWidth: 28; implicitHeight: 28
                VIcon {
                    anchors.centerIn: parent
                    name: (veyra.running && !veyra.paused) ? "pause" : "play"
                    color: Theme.t2
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: veyra.togglePlayPause() }
            }
            Text {
                text: veyra.positionText
                color: Theme.t2
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSmall
            }
            Item {
                id: proSeekArea
                objectName: "pro-seek"
                Layout.fillWidth: true
                implicitHeight: 14
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 3
                    radius: 9
                    color: Qt.rgba(1, 1, 1, 0.14)
                    Rectangle {
                        width: parent.width * proSeekArea.frac
                        height: parent.height
                        radius: 9
                        color: "#FFFFFF"
                    }
                }
                // Press or drag anywhere on the rail: the picture follows the
                // pointer (a seek every 120 ms) and lands on release.
                property bool scrubbing: false
                property real scrubFrac: 0
                readonly property real frac: scrubbing ? scrubFrac : Math.max(0, Math.min(1, veyra.progress))
                Rectangle {
                    width: 11; height: 11; radius: 5.5
                    color: "#FFFFFF"
                    anchors.verticalCenter: parent.verticalCenter
                    x: proSeekArea.frac * proSeekArea.width - width / 2
                    visible: proSeekMouse.containsMouse || proSeekArea.scrubbing
                }
                Timer {
                    id: proScrubSeek
                    interval: 120
                    onTriggered: if (proSeekArea.scrubbing) veyra.seekTo(proSeekArea.scrubFrac * veyra.duration)
                }
                MouseArea {
                    id: proSeekMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: veyra.duration > 0 && !veyra.isCapture
                    cursorShape: Qt.PointingHandCursor
                    preventStealing: true
                    function fracAt(x) { return Math.max(0, Math.min(1, x / Math.max(1, proSeekArea.width))) }
                    onPressed: mouse => { proSeekArea.scrubFrac = fracAt(mouse.x); proSeekArea.scrubbing = true; proScrubSeek.start() }
                    onPositionChanged: mouse => {
                        if (!proSeekArea.scrubbing) return
                        proSeekArea.scrubFrac = fracAt(mouse.x)
                        if (!proScrubSeek.running) proScrubSeek.start()
                    }
                    onReleased: mouse => {
                        proScrubSeek.stop()
                        const target = fracAt(mouse.x) * veyra.duration
                        proSeekArea.scrubbing = false
                        veyra.seekTo(target)
                    }
                    onCanceled: { proScrubSeek.stop(); proSeekArea.scrubbing = false }
                }
            }
            Text {
                text: veyra.durationText
                color: Theme.t2
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSmall
            }
            VTag { visible: veyra.sourceRateText.length > 0; text: qsTr("源 ") + veyra.sourceRateText }
            PlaybackRateButton { objectName: "pro-playback-rate" }
            VTag { visible: veyra.outputSummary.length > 0; text: qsTr("输出 ") + veyra.outputSummary.replace("x", "×") }
            VTag {
                objectName: "pro-fg-tag"
                visible: veyra.hasSource
                kind: veyra.fgEnabled ? "acc" : ""
                text: veyra.fgEnabled ? veyra.fgMultiplier + "X" : qsTr("补帧关")
            }
            // Subtitle and audio-track menus (field request 2026-10-01), the same menus as the
            // 极简 pill's, beside the fullscreen button.
            component BarButton: Item {
                id: barBtn
                property string glyph: ""
                property string tip: ""
                signal tapped()
                implicitWidth: 28; implicitHeight: 28
                VIcon { anchors.centerIn: parent; name: barBtn.glyph; color: barHover.hovered ? Theme.t1 : Theme.t2 }
                HoverHandler { id: barHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: barBtn.tapped() }
                ToolTip.visible: barHover.hovered
                ToolTip.text: barBtn.tip
            }
            BarButton {
                id: proCcButton
                objectName: "pro-subtitles"
                glyph: "cc"
                tip: qsTr("字幕")
                visible: veyra.hasSource && !veyra.isCapture
                onTapped: proCcMenu.openAt(proCcButton, "up")
            }
            BarButton {
                id: proAudioButton
                objectName: "pro-audio-tracks"
                glyph: "music"
                tip: qsTr("音轨")
                visible: veyra.hasSource && !veyra.isCapture
                onTapped: proAudioMenu.openAt(proAudioButton, "up")
            }
            // Fullscreen, at the picture's bottom-right corner where players put it.
            Item {
                objectName: "pro-fullscreen"
                implicitWidth: 28; implicitHeight: 28
                VIcon { anchors.centerIn: parent; name: "max"; color: fullHover.hovered ? Theme.t1 : Theme.t2 }
                HoverHandler { id: fullHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.requestFullscreen() }
                ToolTip.visible: fullHover.hovered
                ToolTip.text: qsTr("全屏（双击画面 / F11）· 全屏后按 Home 打开快速调节")
            }
        }
    }

    // --- meters (fixed 172px, three cards) --------------------------------
    RowLayout {
        id: meters
        visible: !root.overlay
        anchors.left: vwrap.left
        anchors.top: vwrap.bottom
        anchors.topMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: vwrap.width
        spacing: 10

        // Card 1: signal chain. Reported values only.
        Rectangle {
            id: meter1
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1.05
            radius: Theme.rCard
            color: Theme.card
            border.width: 1
            border.color: Theme.stroke
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 6
                VEyebrow { text: qsTr("信号") }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 4
                    Text { text: qsTr("输入"); color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.sourceSummary.length > 0
                              ? veyra.sourceSummary + " · " + veyra.sourceFps.toFixed(2)
                              : "—"
                        color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                    }
                    Text { text: qsTr("输出"); color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.outputSummary.length > 0 ? veyra.outputSummary : "—"
                        color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                    }
                    Text { text: qsTr("光流"); color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.flowBackend.length > 0 ? veyra.flowBackend : qsTr("未执行")
                        color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                    }
                    Text { text: qsTr("色彩"); color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.colorStatus.length > 0 ? veyra.colorStatus : "—"
                        color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                        elide: Text.ElideRight
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // Card 2: per-stage GPU cost. Only stages the engine actually timed are
        // listed; a stage with no measurement is drawn as unmeasured.
        Rectangle {
            id: meter2
            objectName: "pro-meter-stages"
            clip: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1.5
            radius: Theme.rCard
            color: Theme.card
            border.width: 1
            border.color: Theme.stroke
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 5
                RowLayout {
                    Layout.fillWidth: true
                    VEyebrow { text: qsTr("GPU 阶段耗时 · 最近一秒平均"); Layout.fillWidth: true }
                    Text {
                        text: qsTr("预算 ") + veyra.stageBudgetMs.toFixed(1) + qsTr(" ms / 源帧")
                        color: Theme.t3
                        font.family: Theme.fontMono
                        font.pixelSize: 11
                    }
                }
                // Eight stages in two columns of four: a single column of eight
                // overflowed the fixed 172px card.
                GridLayout {
                    objectName: "pro-stage-grid"
                    // The label column fits the longest stage name in the interface
                    // language (52px held two Chinese characters; translations run longer).
                    FontMetrics { id: stageLabelMetrics; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall }
                    readonly property real stageLabelWidth: {
                        let w = 52
                        for (const r of veyra.stageTimings) w = Math.max(w, Math.ceil(stageLabelMetrics.advanceWidth(r.label || "")) + 2)
                        return Math.min(w, 104)
                    }
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 16
                    rowSpacing: 6
                    flow: GridLayout.TopToBottom
                    rows: Math.ceil(veyra.stageTimings.length / 2)
                // The model is the row count, not the list: the list is a new array on
                // every snapshot, and a list model rebuilt the rows each time, so every
                // bar restarted from 0 (the 0 / 5.8 / 0 / 5.8 flicker in the field).
                Repeater {
                    model: veyra.stageTimings.length
                    delegate: RowLayout {
                        id: stageRow
                        required property int index
                        readonly property var modelData: veyra.stageTimings[index] || ({ label: "", ms: 0, fraction: 0, measured: false, color: "transparent" })
                        Layout.fillWidth: true
                        Layout.preferredWidth: 1
                        spacing: 8
                        Text {
                            Layout.preferredWidth: stageRow.parent ? stageRow.parent.stageLabelWidth : 52
                            text: modelData.label
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSmall
                            elide: Text.ElideRight
                        }
                        Item {
                            Layout.fillWidth: true
                            implicitHeight: 5
                            Rectangle {
                                anchors.verticalCenter: parent.verticalCenter
                                width: parent.width
                                height: 5
                                radius: 9
                                color: Qt.rgba(1, 1, 1, 0.08)
                                Rectangle {
                                    objectName: "stage-bar-fill"
                                    width: root.barsArmed ? parent.width * Math.max(0, Math.min(1, modelData.fraction)) : 0
                                    height: parent.height
                                    radius: 9
                                    color: modelData.color
                                    Behavior on width { enabled: stageRow.visible; SequentialAnimation {
                                        PauseAnimation { duration: root.barsSettled ? 0 : Theme.d(120 + stageRow.index * 60) }
                                        NumberAnimation { duration: Theme.d(800); easing.bezierCurve: Theme.springSoft } } }
                                }
                            }
                        }
                        Text {
                            Layout.preferredWidth: 48
                            text: modelData.measured ? modelData.ms.toFixed(1) + " ms" : qsTr("未测量")
                            HoverHandler { id: stageValueHover }
                            ToolTip.visible: stageValueHover.hovered && modelData.measured
                            ToolTip.text: qsTr("平均 ") + modelData.ms.toFixed(2) + " ms · P95 " + (modelData.p95 || 0).toFixed(2) + qsTr(" ms（最近一秒）")
                            color: modelData.measured ? Theme.t1 : Theme.t3
                            font.family: Theme.fontMono
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // Card 3: frame rate and cadence.
        Rectangle {
            id: meter3
            readonly property bool wide: width >= 380
            Layout.fillWidth: true
            Layout.fillHeight: true
            Layout.preferredWidth: 1.15
            radius: Theme.rCard
            color: Theme.card
            border.width: 1
            border.color: Theme.stroke
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 6
                RowLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 12
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignTop
                        spacing: 6
                        VEyebrow { text: qsTr("帧率与节奏") }

                        // The design puts "显示 fps" first and "提交 fps" after it, and its
                        // own note is explicit: a display rate is only real if the system
                        // display event can be read, and when it cannot it must say 未测
                        // rather than reuse the submit rate.
                        RowLayout {
                            spacing: 6
                            Text {
                                text: veyra.displayFpsKnown ? veyra.displayFps.toFixed(1) : qsTr("未测")
                                color: veyra.displayFpsKnown ? Theme.ok : Theme.t3
                                font.family: Theme.fontMono
                                font.pixelSize: 22
                                font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                            }
                            Text { text: qsTr("显示 fps"); color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11 }
                            Rectangle { implicitWidth: 1; implicitHeight: 12; color: Theme.stroke2 }
                            Text {
                                text: veyra.submitFpsKnown ? veyra.submitFps.toFixed(1) : qsTr("未测")
                                color: Theme.t1
                                font.family: Theme.fontMono
                                font.pixelSize: 13
                                font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                            }
                            Text { text: veyra.submitFpsLabel; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11 }
                        }
                        // Frame-time line (design .spark): submit interval over the last
                        // 12 s, the dashed line is the source frame budget.
                        Canvas {
                            id: spark
                            objectName: "fps-sparkline"
                            visible: meter3.wide
                            Layout.fillWidth: true
                            Layout.preferredHeight: 24
                            readonly property var points: veyra.frameTimes
                            onPointsChanged: requestPaint()
                            onWidthChanged: requestPaint()
                            onPaint: {
                                const g = getContext("2d"); g.reset()
                                const pts = points || []
                                const budget = veyra.stageBudgetMs
                                if (pts.length < 2) return
                                let lo = budget > 0 ? budget : pts[0], hi = lo
                                for (const v of pts) { lo = Math.min(lo, v); hi = Math.max(hi, v) }
                                const pad = Math.max(0.5, (hi - lo) * 0.2); lo -= pad; hi += pad
                                const y = v => height - 2 - (v - lo) / (hi - lo) * (height - 4)
                                if (budget > 0) {
                                    g.strokeStyle = Qt.rgba(1, 1, 1, 0.12); g.lineWidth = 1; g.setLineDash([3, 3])
                                    g.beginPath(); g.moveTo(0, y(budget)); g.lineTo(width, y(budget)); g.stroke(); g.setLineDash([])
                                }
                                const x = i => i / 47 * width + (48 - pts.length) / 47 * width
                                const grad = g.createLinearGradient(0, 0, 0, height)
                                grad.addColorStop(0, Qt.rgba(61 / 255, 220 / 255, 132 / 255, 0.25)); grad.addColorStop(1, Qt.rgba(61 / 255, 220 / 255, 132 / 255, 0))
                                g.beginPath(); pts.forEach((v, i) => i ? g.lineTo(x(i), y(v)) : g.moveTo(x(i), y(v)))
                                g.lineTo(x(pts.length - 1), height); g.lineTo(x(0), height); g.closePath(); g.fillStyle = grad; g.fill()
                                g.beginPath(); pts.forEach((v, i) => i ? g.lineTo(x(i), y(v)) : g.moveTo(x(i), y(v)))
                                g.strokeStyle = Theme.ok; g.lineWidth = 1.5; g.stroke()
                            }
                        }
                        GridLayout {
                            visible: meter3.wide
                            Layout.fillWidth: true
                            columns: 2
                            columnSpacing: 14
                            rowSpacing: 3
                            Text { text: qsTr("进程内待呈现"); color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                            Text {
                                Layout.fillWidth: true
                                text: veyra.queuedFramesKnown ? veyra.queuedFrames.toFixed(0) + qsTr(" 帧") : qsTr("未测")
                                color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                            }
                            Text { text: qsTr("跳过源帧"); color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                            Text {
                                Layout.fillWidth: true
                                text: String(veyra.skippedFrames)
                                color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                                horizontalAlignment: Text.AlignRight
                            }
                        }
                        PerfOrbs { visible: !meter3.wide; size: 38; spacing: 6 }
                    }
                    PerfOrbs { visible: meter3.wide; size: 50; Layout.alignment: Qt.AlignVCenter }
                }
                StatusLight { Layout.fillWidth: true }
            }
        }
    }

    // --- inspector (376px, rows 2-3) --------------------------------------
    // Performance orbs beside the quick-adjust panel (fullscreen only).
    Rectangle {
        id: quickOrbs
        objectName: "pro-quick-orbs"
        readonly property bool videoCover: root.overlay
        property real coverRadius: radius
        visible: root.overlay
        x: insp.x + insp.width + 10
        y: insp.y
        width: quickOrbRow.implicitWidth + 24
        height: quickOrbRow.implicitHeight + 16
        radius: 18
        color: Qt.rgba(22 / 255, 22 / 255, 26 / 255, 0.94)
        border.width: 1
        border.color: Theme.stroke2
        RowLayout {
            id: quickOrbRow
            anchors.centerIn: parent
            spacing: 12
            PerfOrbs { size: 46; spacing: 10 }
            Text {
                text: qsTr("Home 关闭")
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 11
            }
        }
    }

    Rectangle {
        id: insp
        objectName: "pro-inspector"
        readonly property bool videoCover: root.overlay
        property real coverRadius: radius
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.top: head.bottom
        anchors.topMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: 376
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke
        clip: true
        // Quick-adjust: the same panel on the left edge of the fullscreen picture.
        states: State {
            name: "overlay"
            when: root.overlay
            AnchorChanges { target: insp; anchors.right: undefined; anchors.left: root.left; anchors.top: root.top }
            PropertyChanges { target: insp; anchors.leftMargin: 16; anchors.topMargin: 16; anchors.bottomMargin: 16 }
        }
        // A click on the panel's own background is not a click on the picture.
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; onWheel: wheel => wheel.accepted = true }

        ColumnLayout {
            anchors.fill: parent
            anchors.leftMargin: root.tab === "color" ? insp.border.width : 0
            anchors.rightMargin: root.tab === "color" ? insp.border.width : 0
            spacing: 0

            // .insp-tabs: five tabs, equal width, 30px, radius 9.
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 8
                spacing: 2
                Repeater {
                    model: [
                        { id: "quality", label: qsTr("画质") },
                        { id: "fg", label: qsTr("补帧") },
                        { id: "color", label: qsTr("色彩") },
                        { id: "audio", label: qsTr("声音") },
                        { id: "display", label: qsTr("显示") }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 30
                        radius: 9
                        color: root.tab === modelData.id ? Theme.card3
                             : tabHover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                        Behavior on color { ColorAnimation { duration: Theme.d(200) } }
                        Text {
                            anchors.centerIn: parent
                            text: modelData.label
                            color: root.tab === modelData.id ? Theme.t1 : Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsBody
                            font.weight: Font.Medium
                        }
                        HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.tab = modelData.id }
                    }
                }
            }

            // .chainbar: the processing order, chips joined by arrows. Fixed order:
            // 补帧 is always last and RTX Video HDR sits immediately before it.
            Rectangle {
                Layout.fillWidth: true
                Layout.leftMargin: 10
                Layout.rightMargin: 10
                implicitHeight: chainCol.implicitHeight + 20
                objectName: "pro-processing-order"
                // The design draws the chain bar on the 画质 tab only.
                visible: root.tab === "quality"
                radius: 12
                color: Qt.rgba(1, 1, 1, 0.03)
                border.width: 1
                border.color: Theme.stroke
                ColumnLayout {
                    id: chainCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 7
                    RowLayout {
                        Layout.fillWidth: true
                        VEyebrow { text: qsTr("处理顺序"); Layout.fillWidth: true }
                        Text { text: qsTr("点击定位"); color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11 }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 2
                        // .c.io: the chain starts at the input, which is not a node.
                        Rectangle {
                            width: inChip.implicitWidth + 18
                            height: 24
                            radius: 99
                            color: "transparent"
                            border.width: 1
                            border.color: Theme.stroke
                            Text {
                                id: inChip
                                anchors.centerIn: parent
                                text: qsTr("输入")
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: 12
                            }
                        }
                        Repeater {
                            model: veyra.chain
                            delegate: Row {
                                required property var modelData
                                spacing: 2
                                Text { text: "→"; color: Theme.t3; font.pixelSize: 10; anchors.verticalCenter: parent.verticalCenter }
                                Rectangle {
                                    width: chipLabel.implicitWidth + 18
                                    height: 24
                                    radius: 99
                                    color: Theme.card3
                                    border.width: 1
                                    border.color: Theme.stroke
                                    opacity: modelData.enabled ? 1.0 : 0.4
                                    Text {
                                        id: chipLabel
                                        anchors.centerIn: parent
                                        text: modelData.label
                                        color: Theme.t1
                                        font.family: Theme.fontUi
                                        font.pixelSize: 12
                                        font.strikeout: !modelData.enabled
                                    }
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    TapHandler { gesturePolicy: TapHandler.WithinBounds; onTapped: root.locateNode(modelData) }
                                }
                            }
                        }
                    }
                }
            }

            // Body. Each tab shows the controls the engine actually has; where the
            // engine has no control yet, the panel says so instead of offering one
            // that would do nothing.
            Flickable {
                id: inspFlick
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 8
                contentHeight: body.implicitHeight
                clip: true
                ScrollBar.vertical: VScrollBar {
                    id: inspectorScroll
                    objectName: "pro-inspector-scrollbar"
                    wide: root.tab === "color"
                }

                ColumnLayout {
                    id: body
                    // CSS thin scrollbars consume 10px on the reference host;
                    // reserve their gutter instead of drawing over the colour card.
                    width: parent.width - 20 - (root.tab === "color" && inspectorScroll.size < 1 ? inspectorScroll.implicitWidth : 0)
                    x: 10
                    spacing: 6

                    // --- 画质 -------------------------------------------------
                    ColumnLayout {
                        id: qualityCol
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.tab === "quality"

                        // The design builds the inspector from accordion cards, not
                        // flat rows: an icon, a title, a live summary line, a switch
                        // and a chevron, with nested groups inside.
                        VAccordion {
                            objectName: "list-sr"
                            Layout.fillWidth: true
                            glyph: "sparkles"
                            hue: "#4F7BFF"
                            title: qsTr("超分辨率")
                            summary: (veyra.srEnabled ? qsTr("RTX 视频超分") : qsTr("已关闭"))
                                     + " · " + veyra.srTargetLabel
                                     + (veyra.videoSrQuality > 0 ? qsTr(" · 质量 ") + veyra.videoSrQuality : "")
                            on: veyra.srEnabled
                            open: true
                            onToggled: on => veyra.srEnabled = on

                            VRow {
                                label: qsTr("目标尺寸")
                                hint: qsTr("决定输出分辨率")
                                VSeg {
                                    options: [
                                        { id: "1", label: "2K" },
                                        { id: "2", label: "4K" },
                                        { id: "4", label: "5K" },
                                        { id: "5", label: "6K" },
                                        { id: "6", label: "7K" },
                                        { id: "3", label: "8K" }
                                    ]
                                    current: String(veyra.srTargetIndex)
                                    onPicked: id => veyra.srTargetIndex = parseInt(id)
                                }
                            }
                            VRow {
                                label: qsTr("超分运动来源")
                                hint: qsTr("零运动会限制时序重建；RTX 视频超分不使用此输入")
                                VSeg { options: [{id:"0",label:qsTr("零运动")},{id:"1",label:qsTr("光流")}]; current: String(veyra.srMotionSource); onPicked: id => veyra.srMotionSource = Number(id) }
                            }
                            VRow {
                                label: qsTr("质量")
                                hint: qsTr("RTX 视频超分档位")
                                VSeg {
                                    options: [
                                        { id: "1", label: "1" },
                                        { id: "2", label: "2" },
                                        { id: "3", label: "3" },
                                        { id: "4", label: "4" }
                                    ]
                                    current: String(veyra.videoSrQuality)
                                    onPicked: id => veyra.videoSrQuality = parseInt(id)
                                }
                            }
                        }

                        // RTX Video HDR: SDR -> HDR before frame generation. Runs
                        // only on an HDR display path; the status says why not.
                        VAccordion {
                            objectName: "list-video-hdr"
                            Layout.fillWidth: true
                            glyph: "sun"
                            hue: "#E58BD9"
                            title: "RTX Video HDR"
                            summary: veyra.videoHdr ? (veyra.videoHdrStatus.length > 0 ? veyra.videoHdrStatus : qsTr("已开启")) : qsTr("已关闭")
                            on: veyra.videoHdr
                            onToggled: on => veyra.videoHdr = on
                            Repeater {
                                model: [{key:"contrast",label:qsTr("对比度"),from:0,to:200,def:125},
                                        {key:"saturation",label:qsTr("饱和度"),from:0,to:200,def:75},
                                        {key:"middleGray",label:qsTr("中灰"),from:10,to:100,def:44},
                                        {key:"peakNits",label:qsTr("峰值亮度 (nit)"),from:400,to:2000,def:1000}]
                                delegate: VRow {
                                    required property var modelData
                                    label: modelData.label
                                    value: String(veyra.videoHdrParams[modelData.key] ?? "—")
                                    VSlider {
                                        objectName: "list-hdr-" + modelData.key
                                        implicitWidth: 120
                                        valueFromModel: true
                                        resettable: true; defaultValue: modelData.def
                                        from: modelData.from; to: modelData.to
                                        value: veyra.videoHdrParams[modelData.key] ?? modelData.def
                                        onMoved: value => veyra.setVideoHdrParameter(modelData.key, Math.round(value))
                                    }
                                }
                            }
                        }

                        // .stage: one NR card, its layers inside (pages-pro.js nrStage):
                        // "N 层依次处理 · 每层参数独立", a bar per layer lit when it
                        // runs, the add button at the bottom of the stage.
                        VAccordion {
                            id: nrStage
                            objectName: "list-nr-stage"
                            Layout.fillWidth: true
                            glyph: "wand"
                            hue: Theme.accent
                            title: qsTr("NR 画面增强")
                            summary: veyra.nrLayers.length > 0 ? veyra.nrLayers.length + qsTr(" 层依次处理 · 每层参数独立") : qsTr("未添加 NR 层")
                            // Master switch (field request 2026-10-02): every layer off
                            // at once; on again brings back the layers that were on.
                            enabledSwitch: veyra.nrLayers.length > 0
                            switchObjectName: "list-nr-master"
                            on: veyra.nrAnyEnabled
                            bypassed: veyra.nrLayers.length > 0 && !veyra.nrAnyEnabled
                            onToggled: on => veyra.setAllNrEnabled(on)
                            open: true
                            headerActions: [
                                VTag { text: qsTr("实验"); kind: "exp" },
                                Row {
                                    objectName: "list-nr-count"
                                    spacing: 3
                                    Repeater {
                                        model: veyra.nrLayers
                                        delegate: Rectangle {
                                            required property var modelData
                                            width: 14; height: 4; radius: 3
                                            color: modelData.enabled ? Theme.accent : Qt.rgba(1, 1, 1, 0.18)
                                            Behavior on color { ColorAnimation { duration: Theme.d(200) } }
                                        }
                                    }
                                }
                            ]
                            Repeater {
                                // Numeric count preserves delegates during slider edits.
                                model: veyra.nrLayers.length
                                delegate: NrLayerCard {
                                    id: layerCard
                                    required property int index
                                    Layout.topMargin: 4
                                    layerData: veyra.nrLayers[index]
                                    layerNumber: index + 1
                                    layerCount: veyra.nrLayers.length
                                    nrFirst: veyra.lowLatency
                                    onEdited: (nodeIndex,key,amount) => veyra.setNrLayerParameter(nodeIndex,key,amount)
                                    onEnabledEdited: (nodeIndex,enabled) => veyra.setEffectEnabled(nodeIndex,enabled)
                                    onDuplicateRequested: nodeIndex => { root.markPop(layerNumber + 1); root.selectedEffect = veyra.duplicateNrLayer(nodeIndex) }
                                    onRemoveRequested: nodeIndex => layerCard.bye(() => veyra.removeEffect(nodeIndex))
                                    onOrderEdited: enabled => veyra.lowLatency = enabled
                                }
                            }

                            // .addlayer: dashed, inside the stage; list mode adds and
                            // toggles layers, never reorders them.
                            Rectangle {
                                id: addLayer
                                objectName: "list-nr-add"
                                Layout.fillWidth: true
                                Layout.topMargin: 4
                                implicitHeight: 36
                                radius: 10
                                readonly property bool full: veyra.nrLayers.length >= 4
                                opacity: full ? 0.45 : 1
                                color: addHover.hovered && !full ? Theme.accentSoft : "transparent"
                                Shape {
                                    anchors.fill: parent
                                    ShapePath {
                                        strokeWidth: 1
                                        strokeColor: addHover.hovered && !addLayer.full ? Theme.accent : Qt.rgba(1, 1, 1, 0.2)
                                        fillColor: "transparent"
                                        strokeStyle: ShapePath.DashLine
                                        dashPattern: [5, 4]
                                        PathRectangle {
                                            x: 0; y: 0
                                            width: addLayer.width
                                            height: addLayer.height
                                            radius: 10
                                        }
                                    }
                                }
                                Row {
                                    anchors.centerIn: parent
                                    spacing: 6
                                    Text {
                                        text: qsTr("+  添加 NR 层")
                                        color: addHover.hovered && !addLayer.full ? Theme.accent : Theme.t2
                                        font.family: Theme.fontUi
                                        font.pixelSize: Theme.fsBody
                                        font.weight: Font.Medium
                                    }
                                    Text {
                                        anchors.baseline: parent.children[0].baseline
                                        text: addLayer.full ? qsTr("（最多 4 层）") : qsTr("（加在 NR ") + veyra.nrLayers.length + qsTr(" 之后 · 最多 4 层）")
                                        color: Theme.t3
                                        font.family: Theme.fontUi
                                        font.pixelSize: 11
                                    }
                                }
                                HoverHandler { id: addHover; cursorShape: addLayer.full ? Qt.ArrowCursor : Qt.PointingHandCursor }
                                TapHandler {
                                    enabled: !addLayer.full
                                    onTapped: { root.markPop(veyra.nrLayers.length + 1); root.selectedEffect = veyra.addEffect("nr") }
                                }
                            }
                        }

                        // Stage-5 output stabiliser. Global, not per-layer: it
                        // runs once after the whole NR stack. Default off, and
                        // off means the pass is never even dispatched.
                        VAccordion {
                            objectName: "list-output-stabiliser"
                            Layout.fillWidth: true
                            visible: veyra.nodeMode === 0
                            title: qsTr("输出稳定器 · 抗闪烁")
                            glyph: "shield"; hue: Theme.accent
                            summary: veyra.nrHoldStrength > 0
                                     ? qsTr("已开启 · 强度 ") + Math.round(veyra.nrHoldStrength * 100) + "%"
                                     : qsTr("默认关闭 · 只稳住没变化的画面，动的地方不拖影")
                            on: veyra.nrHoldStrength > 0
                            onToggled: on => veyra.nrHoldStrength = on ? 0.8 : 0
                            Text {
                                Layout.fillWidth: true
                                text: qsTr("比较源画面与上一帧：没有明显变化的像素沿用上一帧输出，明显变化的位置直接用新结果。") +
                                      qsTr("不重投影、不用光流，所以运动区域不会拖影。放在最后一层 NR 之后、超分与补帧之前。")
                                wrapMode: Text.WordWrap
                                color: Theme.t3
                                font.family: Theme.fontUi; font.pixelSize: 11
                            }
                            VRow {
                                label: qsTr("稳定强度")
                                hint: qsTr("越高越稳，过高会显得发闷")
                                visible: veyra.nrHoldStrength > 0
                                VSlider {
                                    objectName: "list-hold-strength"
                                    from: 0.1; to: 1.0
                                    value: veyra.nrHoldStrength
                                    onMoved: value => veyra.nrHoldStrength = value
                                }
                            }
                            VRow {
                                label: qsTr("变化容差")
                                hint: qsTr("越小越敏感，越大稳得越多")
                                visible: veyra.nrHoldStrength > 0
                                VSlider {
                                    objectName: "list-hold-tolerance"
                                    from: 0.005; to: 0.10
                                    value: veyra.nrHoldTolerance
                                    onMoved: value => veyra.nrHoldTolerance = value
                                }
                            }
                        }

                        VAccordion {
                            objectName: "list-global-protection"
                            Layout.fillWidth: true
                            visible: veyra.nodeMode === 0
                            title: qsTr("NR 全局保护区域")
                            glyph: "shield"; hue: Theme.accent
                            summary: qsTr("区域内排除所有 NR 层 · 仅列表模式")
                            on: veyra.protectionEnabled
                            onToggled: on => veyra.protectionEnabled = on
                            Text {
                                Layout.fillWidth: true
                                text: veyra.protectionDrawShape.length > 0
                                      ? qsTr("在画面上按住左键拖出") + (veyra.protectionDrawShape === "ellipse" ? qsTr("圆形 / 椭圆") : qsTr("矩形")) + qsTr("区域 · Esc 取消")
                                      : qsTr("在画面上画出矩形或圆形区域（最多 4 个），区域内的画面不做任何一层 NR 处理。")
                                wrapMode: Text.WordWrap
                                color: veyra.protectionDrawShape.length > 0 ? Theme.accent : Theme.t3
                                font.family: Theme.fontUi; font.pixelSize: 11
                            }
                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6
                                VButton {
                                    objectName: "list-protection-draw-rect"
                                    text: qsTr("画矩形")
                                    primary: veyra.protectionDrawShape === "rect"
                                    onClicked: veyra.beginProtectionDraw(veyra.protectionDrawShape === "rect" ? "" : "rect")
                                }
                                VButton {
                                    objectName: "list-protection-draw-ellipse"
                                    text: qsTr("画圆形")
                                    primary: veyra.protectionDrawShape === "ellipse"
                                    onClicked: veyra.beginProtectionDraw(veyra.protectionDrawShape === "ellipse" ? "" : "ellipse")
                                }
                                Item { Layout.fillWidth: true }
                                VButton {
                                    text: qsTr("清空"); ghost: true
                                    visible: veyra.protectionRegions.length > 0
                                    onClicked: veyra.clearProtectionRegions()
                                }
                            }
                            Repeater {
                                model: veyra.protectionRegions
                                delegate: VRow {
                                    required property var modelData
                                    required property int index
                                    label: (modelData.ellipse ? qsTr("圆形 ") : qsTr("矩形 ")) + (index + 1)
                                    value: Math.round(modelData.left * 100) + "," + Math.round(modelData.top * 100) + " → "
                                           + Math.round(modelData.right * 100) + "," + Math.round(modelData.bottom * 100) + " %"
                                    VButton {
                                        objectName: "list-protection-remove-" + index
                                        icon: true; ghost: true; iconName: "x"
                                        onClicked: veyra.removeProtectionRegion(modelData.index)
                                    }
                                }
                            }
                            VRow {
                                label: qsTr("羽化像素")
                                value: Number(veyra.protectionState.feather).toFixed(0) + " px"
                                VSlider {
                                    objectName: "list-protection-feather"
                                    implicitWidth: 110
                                    from: 0; to: 64
                                    value: veyra.protectionState.feather
                                    onMoved: value => veyra.setProtectionFeather(Math.round(value))
                                }
                            }
                        }

                        // A refused edit explains itself here rather than in a modal.
                        Text {
                            Layout.fillWidth: true
                            visible: !veyra.chainValid
                            text: veyra.chainError
                            color: Theme.err
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSmall
                            wrapMode: Text.WordWrap
                        }
                    }

                    // --- 补帧 -------------------------------------------------
                    // pages-pro.js fg tab: one 补帧 card with its switch ("始终在所有
                    // 画质效果之后"), the optical-flow and presentation groups nested
                    // as sub-panels. Backend choices share the node editor's
                    // bridge contract; FSR 4 requests never silently become 3.1.
                    ColumnLayout {
                        id: fgCol
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.tab === "fg"
                        objectName: "list-fg-group"

                        VAccordion {
                            id: fgCard
                            objectName: "list-fg-card"
                            Layout.fillWidth: true
                            glyph: "layers"
                            hue: "#3DDC84"
                            title: qsTr("补帧")
                            summary: qsTr("始终在所有画质效果之后") + (veyra.fgEnabled
                                     ? " · " + veyra.fgMultiplier + "X " + (veyra.fgBackendChoices.find(o => o.id === veyra.fgBackendName) || {}).label
                                     : qsTr(" · 已关闭"))
                            switchObjectName: "list-fg-enabled"
                            on: veyra.fgEnabled
                            onToggled: on => veyra.fgEnabled = on
                            open: true

                            VRow {
                                label: qsTr("补帧方式")
                                hint: veyra.fgProviderText
                                VSelect {
                                    id: fgBackendSelect
                                    objectName: "list-fg-backend"
                                    options: veyra.fgBackendChoices
                                    // The label of the chosen backend, not its id.
                                    value: (options.find(o => o.id === veyra.fgBackendName) || options[0]).label
                                    onPicked: id => veyra.fgBackendName = id
                                }
                            }
                            VRow {
                                label: qsTr("补帧运动来源")
                                hint: qsTr("XeSS 默认零运动；FSR 仍有自身的光流计算")
                                VSeg { objectName: "list-fg-motion"; options: [{id:"0",label:qsTr("零运动")},{id:"1",label:qsTr("光流")}]; current: String(veyra.fgMotionSource); onPicked: id => veyra.fgMotionSource = Number(id) }
                            }
                            VRow {
                                label: qsTr("倍率")
                                hint: veyra.fgMaxMultiplier + qsTr("X 为上限")
                                VSeg {
                                    options: veyra.fgMultiplierChoices
                                    current: String(veyra.fgMultiplier)
                                    onPicked: id => veyra.fgMultiplier = parseInt(id)
                                }
                            }
                            VRow {
                                label: qsTr("严格补帧节奏")
                                hint: qsTr("帧同步 · 默认关闭")
                                VSwitch {
                                    checked: veyra.fgStrict
                                    onToggled: checked => veyra.fgStrict = checked
                                }
                            }
                            VSubGroup {
                                objectName: "list-fg-flow-group"
                                Layout.fillWidth: true
                                Layout.topMargin: 6
                                label: qsTr("光流 · 运动估算")
                                count: veyra.opticalFlowChoice === 1 ? 4 : 3
                                expanded: true
                                VRow {
                                    label: qsTr("光流算法")
                                    hint: qsTr("补帧与 NR 共用 · 输入后计算一次")
                                    VSelect {
                                        objectName: "list-flow-choice"
                                        options: [{id:"0",label:"NVIDIA NVOF"},{id:"1",label:"AMD FidelityFX"}]
                                        value: options[veyra.opticalFlowChoice]?.label ?? (veyra.opticalFlowChoice === 2 ? qsTr("GPU DIS 已移除，请重选") : qsTr("未知配置"))
                                        onPicked: id => veyra.setOpticalFlowChoice(Number(id))
                                    }
                                }
                                VRow {
                                    label: qsTr("AMD 性能档")
                                    hint: qsTr("光流宽高减半，更省但更糙")
                                    visible: veyra.opticalFlowChoice === 1
                                    VSwitch {
                                        objectName: "list-amd-half"
                                        checked: veyra.amdFlowHalf
                                        onToggled: checked => veyra.amdFlowHalf = checked
                                    }
                                }
                                VRow {
                                    label: qsTr("运动估算质量")
                                    VSeg {
                                        objectName: "list-flow-quality"
                                        options: [{ id: "0", label: qsTr("性能") }, { id: "1", label: qsTr("平衡") }, { id: "2", label: qsTr("质量") }]
                                        current: String(veyra.flowQuality)
                                        onPicked: id => veyra.flowQuality = Number(id)
                                    }
                                }
                                VRow {
                                    label: qsTr("内容节奏")
                                    hint: qsTr("游戏 30 帧、采集卡输出 60 帧时选“采集 60→30”")
                                    VSelect {
                                        objectName: "list-content-rate"
                                        implicitWidth: 170
                                        options: root.contentRates
                                        value: (root.contentRates[veyra.contentRate] || root.contentRates[0]).label
                                        onPicked: id => veyra.contentRate = Number(id)
                                    }
                                }
                            }
                            VRow {
                                label: qsTr("低延迟队列")
                                hint: qsTr("减少软件排队；屏幕延迟需要实测")
                                VSwitch {
                                    objectName: "list-low-queue"
                                    checked: veyra.fgLowQueue
                                    onToggled: checked => veyra.fgLowQueue = checked
                                }
                            }
                        }
                    }

                    // --- 色彩 -------------------------------------------------
                    // The same real-instance editor is used by the node inspector.
                    ColourPanel {
                        id: colourPanel
                        Layout.fillWidth: true
                        visible: root.tab === "color"
                        api: veyra
                    }

                    // --- 声音 -------------------------------------------------
                    ColumnLayout {
                        id: audioCol
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.tab === "audio"
                        VGroup {
                            VRow {
                                label: qsTr("音量")
                                value: Math.round(veyra.volume * 100) + "%"
                                VSlider {
                                    implicitWidth: 150
                                    from: 0; to: 1; value: veyra.volume; inputScale: 100
                                    onMoved: value => veyra.volume = value
                                }
                            }
                            VRow {
                                label: qsTr("静音")
                                VSwitch {
                                    checked: veyra.muted
                                    onToggled: checked => veyra.muted = checked
                                }
                            }
                            VRow {
                                label: qsTr("声音同步")
                                hint: veyra.audioSyncMode === 0 ? (veyra.audioSyncLive ? qsTr("软件估算补偿 ") + veyra.audioCompensationMs.toFixed(0) + " ms" : qsTr("自动估算 · 采集卡 / PS5 实时输入时生效"))
                                      : veyra.audioSyncMode === 1 ? qsTr("手动偏移 · 实时输入时生效") : qsTr("关闭补偿")
                                VSeg {
                                    options: [{ id: "0", label: qsTr("自动估算") }, { id: "1", label: qsTr("手动") }, { id: "2", label: qsTr("关闭", "off") }]
                                    current: String(veyra.audioSyncMode)
                                    onPicked: id => veyra.audioSyncMode = Number(id)
                                }
                            }
                            VRow {
                                label: qsTr("音频偏移")
                                hint: qsTr("正值延后音频")
                                value: veyra.audioOffsetMs + " ms"
                                VSlider {
                                    implicitWidth: 150
                                    center: true
                                    from: -250; to: 250; value: veyra.audioOffsetMs; enabledControl: veyra.audioSyncMode === 1
                                    onMoved: value => veyra.audioOffsetMs = Math.round(value)
                                }
                            }
                        }
                        VEyebrow { text: qsTr("音轨") }
                        Text {
                            visible: veyra.audioTracks.length === 0
                            text: qsTr("当前源没有可选音轨。")
                            color: Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSmall
                        }
                        Repeater {
                            model: veyra.audioTracks
                            delegate: Rectangle {
                                required property var modelData
                                Layout.fillWidth: true
                                implicitHeight: 44
                                radius: 12
                                color: Theme.card2
                                border.width: 1
                                border.color: veyra.selectedAudioTrack === modelData.index ? Theme.accent : Theme.stroke
                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 12
                                    spacing: 12
                                    VEqBars {
                                        active: veyra.selectedAudioTrack === modelData.index && veyra.running && !veyra.paused
                                        opacity: veyra.selectedAudioTrack === modelData.index ? 1 : 0.6
                                    }
                                    ColumnLayout {
                                        Layout.fillWidth: true
                                        spacing: 2
                                        Text {
                                            text: modelData.label
                                            color: Theme.t1
                                            font.family: Theme.fontUi
                                            font.pixelSize: Theme.fsBody
                                        }
                                        Text {
                                            text: modelData.channels > 0 ? modelData.channels + qsTr(" 声道") : ""
                                            color: Theme.t3
                                            font.family: Theme.fontUi
                                            font.pixelSize: 11
                                        }
                                    }
                                }
                                HoverHandler { cursorShape: Qt.PointingHandCursor }
                                TapHandler { onTapped: veyra.selectedAudioTrack = modelData.index }
                            }
                        }
                    }

                    // --- 显示 -------------------------------------------------
                    ColumnLayout {
                        id: displayCol
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.tab === "display"
                        VGroup {
                            PresentationRows { }
                            VRow {
                                label: qsTr("原画 / 增强对比")
                                hint: (veyra.compareMode === 2 ? qsTr("在画面上按住左键拖动分割线：左边原画、右边增强。") : "") + (veyra.compareMode !== 0 ? qsTr("对比时补帧暂停，关闭对比后恢复") : "")
                                VSeg {
                                    objectName: "display-compare"
                                    options: [{ id: "0", label: qsTr("关闭", "off") }, { id: "2", label: qsTr("分屏") }, { id: "1", label: qsTr("只看原画") }]
                                    current: String(veyra.compareMode)
                                    onPicked: id => veyra.compareMode = Number(id)
                                }
                            }
                            VRow {
                                label: qsTr("对照底图")
                                hint: qsTr("低延迟 NR 先行时，“增强前底图”是原图缩放，不是独立超分对照")
                                visible: veyra.compareMode !== 0
                                VSeg {
                                    objectName: "display-compare-base"
                                    options: [{ id: "0", label: qsTr("输入原画") }, { id: "1", label: qsTr("增强前底图") }]
                                    current: veyra.compareBase ? "1" : "0"
                                    onPicked: id => veyra.compareBase = id === "1"
                                }
                            }
                            VRow {
                                label: qsTr("按住 V 查看原画")
                                hint: qsTr("按键可在 设置 → 快捷键 里改")
                                VSwitch {
                                    objectName: "display-hold-compare"
                                    checked: veyra.preferences.holdCompare !== false
                                    onToggled: checked => veyra.setPreference("holdCompare", checked)
                                }
                            }
                            VRow {
                                label: qsTr("画面比例")
                                hint: veyra.aspectMode === 1 ? qsTr("原始 = 一个输出像素对一个屏幕像素") : veyra.aspectMode === 2 ? qsTr("填满窗口，超出部分裁掉") : ""
                                VSelect {
                                    objectName: "display-aspect"
                                    options: [{ id: "0", label: qsTr("适应") }, { id: "1", label: qsTr("原始像素") }, { id: "2", label: qsTr("填充裁切") }, { id: "3", label: qsTr("拉伸铺满") }, { id: "4", label: "16:9" }, { id: "5", label: "4:3" }, { id: "6", label: "21:9" }]
                                    value: options[veyra.aspectMode].label
                                    onPicked: id => veyra.aspectMode = Number(id)
                                }
                            }
                            VRow {
                                label: qsTr("强制 SDR 预览")
                                hint: qsTr("HDR 片源也按 SDR 显示；与采集卡的“转为 SDR 显示”是同一设置")
                                VSwitch {
                                    objectName: "display-force-sdr"
                                    checked: veyra.captureForceSdr
                                    onToggled: checked => veyra.captureForceSdr = checked
                                }
                            }
                            VRow {
                                label: qsTr("HDR 输出格式")
                                hint: qsTr("补帧路径固定 HDR10；需要 Windows HDR 开启")
                                VSeg { objectName: "display-hdr-output"; options: [{id:"0",label:"HDR10"},{id:"1",label:qsTr("scRGB 浮点")}]; current: String(veyra.hdrOutputMode); onPicked: id => veyra.hdrOutputMode = Number(id) }
                            }
                        }
                        VGroup {
                            VRow {
                                label: qsTr("提交帧率")
                                value: veyra.submitFpsKnown ? veyra.submitFps.toFixed(2) : qsTr("未测量")
                            }
                            VRow {
                                label: qsTr("显示帧率")
                                hint: qsTr("需要系统显示事件才能测得真实值；取不到时显示未测")
                                value: veyra.displayFpsKnown ? veyra.displayFps.toFixed(2) : qsTr("未测")
                            }
                            VRow {
                                label: qsTr("晚点 P95")
                                value: veyra.lateP95Ms.toFixed(2) + " ms"
                            }
                            VRow {
                                label: qsTr("调度等待 P95")
                                value: veyra.scheduleP95Ms.toFixed(2) + " ms"
                            }
                        }
                    }

                    Item { Layout.preferredHeight: 12 }
                }
            }

            // .insp-foot
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 10
                spacing: 8
                VDot { off: !veyra.running; warn: veyra.captureRecovering; err: veyra.failed }
                Text {
                    Layout.fillWidth: true
                    text: veyra.statusText
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSmall
                    elide: Text.ElideRight
                }
                VButton {
                    ghost: true
                    text: qsTr("重置本页")
                    onClicked: veyra.resetCurrentPage()
                }
            }
        }
    }

    // Test hook (--menu, motion probe "menu"): open a header menu from its button.
    function openTestMenu(name) {
        if (name === "source") sourceMenu.openAt(sourceBtn, "down")
        else if (name === "preset") presetMenu.openAt(presetBtn, "down")
    }
    readonly property real testMenuScale: sourceMenu.motionScale

    // The pill's 字幕 / 音轨 menus, for the video bar's buttons.
    VMenu {
        id: proCcMenu
        title: qsTr("字幕")
        items: [{ label: qsTr("关闭", "off"), checked: veyra.subtitlePrimary < 0, track: -1 }]
            .concat(veyra.subtitleTracks.map(t => ({ label: t.label, note: t.note, track: t.index,
                                                     disabled: !t.usable, checked: t.index === veyra.subtitlePrimary })))
            .concat([{ label: qsTr("加载外部字幕…"), icon: "import", act: "load" },
                     { sep: true },
                     { label: qsTr("字幕设置…"), note: qsTr("字体、字号、描边、位置、延时"), icon: "type", act: "dlg" }])
        onPicked: (i, o) => {
            if (o.act === "load") veyra.loadSubtitleDialog()
            else if (o.act === "dlg") root.requestDialog("subtitle")
            else if (o.track !== undefined) veyra.subtitlePrimary = o.track
        }
    }
    VMenu {
        id: proAudioMenu
        title: qsTr("音轨")
        readonly property var mk: t => ({ label: t.label, index: t.index,
                                          note: t.channels > 0 ? (t.channels + qsTr(" 声道")) : "",
                                          checked: t.index === veyra.selectedAudioTrack })
        items: veyra.audioTracks.length > 0
               ? veyra.audioTracks.map(mk).concat([{ sep: true },
                     { label: qsTr("音频设置…"), note: qsTr("输出设备、音画同步、偏移"), icon: "music", act: "dlg" }])
               : [{ label: qsTr("片源没有音轨或尚未打开"), disabled: true }]
        onPicked: (i, o) => {
            if (o.act === "dlg") return root.requestDialog("audio")
            if (o.index !== undefined) veyra.selectedAudioTrack = o.index
        }
    }
    // pages-pro.js data-srcbtn: the open source first (checked), then the ways in.
    VMenu {
        id: sourceMenu
        title: qsTr("片源")
        items: (veyra.sourceName.length > 0
                ? [{ label: (veyra.isCapture ? qsTr("采集卡 · ") : "") + veyra.sourceName, note: veyra.sourceSummary,
                     checked: true, icon: "video", act: "" }] : [])
            .concat([{ label: qsTr("打开文件…"), icon: "folder", act: "file" },
                     { label: qsTr("PS5 串流…"), icon: "gamepad", act: "ps5" },
                     { label: qsTr("PC 串流…"), icon: "cast", act: "moonlight" },
                     { label: qsTr("Xbox 串流…"), icon: "gamepad", act: "xbox" },
                     { label: qsTr("屏幕捕获…"), icon: "monitor", act: "screen" },
                     { sep: true },
                     { label: qsTr("采集卡设置…"), icon: "settings", act: "capture" }])
        onPicked: (i, o) => {
            if (o.act === "file") veyra.openFileDialog()
            else if (o.act === "ps5") veyra.openPs5Dialog()
            else if (o.act === "moonlight") veyra.openMoonlightDialog()
            else if (o.act === "xbox") veyra.openXboxDialog()
            else if (o.act === "screen") veyra.openScreenCaptureDialog()
            else if (o.act === "capture") veyra.openCaptureDialog()
        }
    }
    // The design puts 另存为 / 管理 in the professional page's preset menu.
    // presets.js VY.presetMenu(app, anchor, 'list').
    VMenu {
        id: presetMenu
        title: veyra.nodeMode === 1 ? qsTr("节点预设") : qsTr("列表预设")
        items: veyra.presets.filter(p => p.nodeMode === (veyra.nodeMode === 1))
            .map(p => ({ label: p.name, note: p.note, checked: p.name === veyra.currentPresetName,
                         tag: p.builtin ? qsTr("内置") : "", preset: p.index }))
            .concat([{ sep: true },
                     { label: qsTr("把当前设置另存为预设…"), icon: "plus", act: "save" },
                     { label: qsTr("管理预设…"), note: qsTr("重命名 · 复制 · 删除"), icon: "settings", act: "manage" }])
        onPicked: (i, o) => {
            if (o.act) root.requestDialog(o.act)
            else veyra.applyPresetIndex(o.preset)
        }
    }

    // pages-pro.js layer "更多": copy as a new layer, back to defaults, remove.
    VMenu {
        id: nrLayerMenu
        objectName: "nr-layer-menu"
        property Item layer: null
        title: layer ? qsTr("NR 层 ") + layer.layerNumber : ""
        items: layer ? [
            { label: qsTr("复制为新层"), icon: "dup", act: "dup", disabled: veyra.nrLayers.length >= 4 },
            { label: qsTr("恢复默认"), icon: "reset", act: "reset" },
            { label: qsTr("删除这一层"), icon: "trash", act: "del", disabled: veyra.nrLayers.length <= 1 }
        ] : []
        onPicked: (i, o) => {
            const card = layer
            if (!card) return
            const nodeIndex = card.layerData.index
            if (o.act === "dup") { root.markPop(card.layerNumber + 1); root.selectedEffect = veyra.duplicateNrLayer(nodeIndex) }
            else if (o.act === "reset") veyra.resetNrLayer(nodeIndex)
            else if (o.act === "del") card.bye(() => veyra.removeEffect(nodeIndex))
        }
    }

    // presets.js DIALOGS.toNode / toList: the two modes side by side, then what
    // switching means. Independent configurations; the node canvas starts empty
    // the first time and keeps its chain afterwards.
    component ModeCard: Rectangle {
        id: mc
        property string glyph: ""
        property string heading: ""
        property string detail: ""
        property string tag: ""
        property bool active: false
        Layout.fillWidth: true
        Layout.preferredWidth: 1
        implicitHeight: modeCol.implicitHeight + 24
        radius: 12
        color: active ? Qt.rgba(1, 138 / 255, 61 / 255, 0.08) : Theme.card2
        border.width: 1
        border.color: active ? Theme.accent : Theme.stroke
        ColumnLayout {
            id: modeCol
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 6
            Rectangle {
                implicitWidth: 28; implicitHeight: 28; radius: 8
                color: Qt.rgba(1, 1, 1, 0.06)
                VIcon { anchors.centerIn: parent; name: mc.glyph; size: 14 }
            }
            Text {
                text: mc.heading
                color: Theme.t1
                font.family: Theme.fontUi; font.pixelSize: 13
                font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
            }
            Text {
                Layout.fillWidth: true
                text: mc.detail
                color: Theme.t3
                font.family: Theme.fontUi; font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            VTag { text: mc.tag; kind: mc.active ? "acc" : "" }
        }
    }
    component Bullet: RowLayout {
        id: bl
        property string text: ""
        Layout.fillWidth: true
        spacing: 8
        Rectangle { Layout.alignment: Qt.AlignTop; Layout.topMargin: 7; implicitWidth: 4; implicitHeight: 4; radius: 2; color: Theme.t3 }
        Text {
            Layout.fillWidth: true
            text: bl.text
            color: Theme.t2
            font.family: Theme.fontUi; font.pixelSize: 12
            wrapMode: Text.WordWrap
            lineHeight: 1.25
        }
    }

    VConfirm {
        id: switchDialog
        property int targetMode: 1
        objectName: "node-switch-dialog"
        cardWidth: targetMode === 1 ? 560 : 480
        glyph: targetMode === 1 ? "nodes" : "list"
        title: targetMode === 1 ? qsTr("切换到节点模式？") : qsTr("切换回列表模式？")
        acceptText: targetMode === 1 ? qsTr("切换到节点模式") : qsTr("切换到列表模式")
        acceptIcon: targetMode === 1 ? "nodes" : "list"
        RowLayout {
            objectName: "node-switch-cards"
            visible: switchDialog.targetMode === 1
            Layout.fillWidth: true
            spacing: 8
            ModeCard {
                glyph: "list"
                heading: qsTr("列表模式")
                detail: qsTr("固定顺序：超分 → NR → 保护 → HDR → 补帧")
                tag: qsTr("当前 · 设置会原样保留")
            }
            VIcon { name: "right"; color: Theme.t3 }
            ModeCard {
                glyph: "nodes"
                heading: qsTr("节点模式")
                detail: qsTr("自由排列节点，按连线顺序运行")
                tag: qsTr("首次进入为空白画布，之后保留上次的节点链")
                active: true
            }
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Bullet { visible: switchDialog.targetMode === 1; text: qsTr("两种模式的设置、预设完全分开保存，切回列表时恢复列表原来的设置。") }
            Bullet { visible: switchDialog.targetMode === 1; text: qsTr("切换会重建处理链，画面可能停顿几百毫秒。") }
            Bullet { visible: switchDialog.targetMode === 1; text: qsTr("节点模式按连线顺序真实执行；离线导出仅支持列表模式。") }
            Bullet { visible: switchDialog.targetMode === 0; text: qsTr("回到列表模式，恢复列表之前的设置和预设。") }
            Bullet { visible: switchDialog.targetMode === 0; text: qsTr("节点链会保留，下次切回节点模式时继续使用。") }
            Bullet { visible: switchDialog.targetMode === 0; text: qsTr("切换会重建处理链，画面可能停顿几百毫秒。") }
        }
        onAccepted: {
            veyra.nodeMode = targetMode
            if (veyra.nodeMode === targetMode) root.requestPage(targetMode === 1 ? "node" : "pro")
        }
    }

    // [data-in] entrance order from pages-pro.js.
    VRise { id: headRise; target: head; d: 0 }
    readonly property alias probeRise: headRise
    VRise { target: vwrap; d: 1 }
    VRise { target: meter1; d: 2 }
    VRise { target: meter2; d: 3 }
    VRise { target: meter3; d: 4 }
    VRise { target: insp; d: 2 }
}
