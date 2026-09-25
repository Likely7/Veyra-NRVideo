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
import QtQuick.Layouts
import QtQuick.Shapes

Item {
    id: root
    signal requestPage(string page)
    signal requestDialog(string key)

    // Design's st.tab: quality / fg / color / audio / display.
    property string tab: "quality"
    property int selectedEffect: -1

    // --- header (32px) ----------------------------------------------------
    RowLayout {
        id: head
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        height: 32
        spacing: 8

        VButton {
            glyph: "🎬"
            text: veyra.isCapture ? "采集卡" : "片源"
            onClicked: sourceMenu.popup()
        }
        VTag { text: veyra.sourceSummary.length > 0 ? veyra.sourceSummary : "未打开" }
        Item { Layout.fillWidth: true }

        // View toggle. Choosing 节点 asks first, because switching rebuilds the
        // whole processing chain (design note: 切换需要确认，会重建处理链).
        VSeg {
            options: [{ id: "list", label: "列表" }, { id: "node", label: "节点" }]
            current: "list"
            onPicked: id => { if (id === "node") switchDialog.open() }
        }
        VButton { glyph: "📷"; text: "截图"; onClicked: veyra.takeScreenshot() }
        VButton {
            text: "预设：" + veyra.currentPresetName
            onClicked: presetMenu.popup()
        }
    }

    // --- video + transport bar -------------------------------------------
    Rectangle {
        id: vwrap
        anchors.left: parent.left
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
                Text {
                    anchors.centerIn: parent
                    text: (veyra.running && !veyra.paused) ? "❚❚" : "▶"
                    color: Theme.t2
                    font.pixelSize: 13
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
                Layout.fillWidth: true
                implicitHeight: 14
                Rectangle {
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width
                    height: 3
                    radius: 9
                    color: Qt.rgba(1, 1, 1, 0.14)
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, veyra.progress))
                        height: parent.height
                        radius: 9
                        color: "#FFFFFF"
                    }
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: point => veyra.seekTo(Math.max(0, Math.min(1, point.position.x / width)) * veyra.duration)
                }
            }
            Text {
                text: veyra.durationText
                color: Theme.t2
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSmall
            }
            VTag { text: veyra.sourceSummary }
            VTag { text: veyra.outputSummary }
            VTag {
                visible: veyra.fgActive
                kind: "acc"
                text: veyra.fgMultiplier + "X"
            }
        }
    }

    // --- meters (fixed 172px, three cards) --------------------------------
    RowLayout {
        anchors.left: parent.left
        anchors.top: vwrap.bottom
        anchors.topMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: vwrap.width
        spacing: 10

        // Card 1: signal chain. Reported values only.
        Rectangle {
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
                VEyebrow { text: "信号" }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 4
                    Text { text: "输入"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.sourceSummary.length > 0
                              ? veyra.sourceSummary + " · " + veyra.sourceFps.toFixed(2)
                              : "—"
                        color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                    }
                    Text { text: "输出"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.outputSummary.length > 0 ? veyra.outputSummary : "—"
                        color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                    }
                    Text { text: "光流"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.flowBackend.length > 0 ? veyra.flowBackend : "未执行"
                        color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                    }
                    Text { text: "色彩"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
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
                    VEyebrow { text: "各阶段 GPU 耗时"; Layout.fillWidth: true }
                    Text {
                        text: "预算 " + veyra.stageBudgetMs.toFixed(1) + " ms / 源帧"
                        color: Theme.t3
                        font.family: Theme.fontMono
                        font.pixelSize: 11
                    }
                }
                Repeater {
                    model: veyra.stageTimings
                    delegate: RowLayout {
                        required property var modelData
                        Layout.fillWidth: true
                        spacing: 10
                        Text {
                            Layout.preferredWidth: 44
                            text: modelData.label
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSmall
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
                                    width: parent.width * Math.max(0, Math.min(1, modelData.fraction))
                                    height: parent.height
                                    radius: 9
                                    color: modelData.color
                                }
                            }
                        }
                        Text {
                            Layout.preferredWidth: 52
                            text: modelData.measured ? modelData.ms.toFixed(1) + " ms" : "未测量"
                            color: modelData.measured ? Theme.t1 : Theme.t3
                            font.family: Theme.fontMono
                            font.pixelSize: 11
                            horizontalAlignment: Text.AlignRight
                        }
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }

        // Card 3: frame rate and cadence.
        Rectangle {
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
                VEyebrow { text: "帧率与节奏" }

                // The design puts "显示 fps" first and "提交 fps" after it, and its
                // own note is explicit: a display rate is only real if the system
                // display event can be read, and when it cannot it must say 未测
                // rather than reuse the submit rate.
                RowLayout {
                    spacing: 6
                    Text {
                        text: veyra.displayFpsKnown ? veyra.displayFps.toFixed(1) : "未测"
                        color: veyra.displayFpsKnown ? Theme.ok : Theme.t3
                        font.family: Theme.fontMono
                        font.pixelSize: 22
                        font.weight: Font.DemiBold
                    }
                    Text { text: "显示 fps"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11 }
                    Rectangle { implicitWidth: 1; implicitHeight: 12; color: Theme.stroke2 }
                    Text {
                        text: veyra.submitFpsKnown ? veyra.submitFps.toFixed(1) : "未测"
                        color: Theme.t1
                        font.family: Theme.fontMono
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                    }
                    Text { text: "提交"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11 }
                }
                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 14
                    rowSpacing: 3
                    Text { text: "进程内排队"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.queuedFrames.toFixed(1) + " 帧"
                        color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                    }
                    Text { text: "跳过源帧"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        text: String(veyra.skippedFrames)
                        color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 12
                        horizontalAlignment: Text.AlignRight
                    }
                }
                Item { Layout.fillHeight: true }
            }
        }
    }

    // --- inspector (376px, rows 2-3) --------------------------------------
    Rectangle {
        id: insp
        anchors.right: parent.right
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

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // .insp-tabs: five tabs, equal width, 30px, radius 9.
            RowLayout {
                Layout.fillWidth: true
                Layout.margins: 8
                spacing: 2
                Repeater {
                    model: [
                        { id: "quality", label: "画质" },
                        { id: "fg", label: "补帧" },
                        { id: "color", label: "色彩" },
                        { id: "audio", label: "声音" },
                        { id: "display", label: "显示" }
                    ]
                    delegate: Rectangle {
                        required property var modelData
                        Layout.fillWidth: true
                        implicitHeight: 30
                        radius: 9
                        color: root.tab === modelData.id ? Theme.card3
                             : tabHover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                        Behavior on color { ColorAnimation { duration: 200 } }
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
                        VEyebrow { text: "处理顺序"; Layout.fillWidth: true }
                        Text { text: "点击定位"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11 }
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 2
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
                                    TapHandler { onTapped: root.selectedEffect = modelData.index }
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
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.topMargin: 8
                contentHeight: body.implicitHeight
                clip: true
                ScrollBar.vertical: ScrollBar { }

                ColumnLayout {
                    id: body
                    width: parent.width - 20
                    x: 10
                    spacing: 6

                    // --- 画质 -------------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.tab === "quality"

                        // The design builds the inspector from accordion cards, not
                        // flat rows: an icon, a title, a live summary line, a switch
                        // and a chevron, with nested groups inside.
                        VAccordion {
                            Layout.fillWidth: true
                            glyph: "✨"
                            hue: "#4F7BFF"
                            title: "超分辨率"
                            summary: (veyra.srEnabled ? "RTX 视频超分" : "已关闭")
                                     + " · " + veyra.srTargetLabel
                                     + (veyra.videoSrQuality > 0 ? " · 质量 " + veyra.videoSrQuality : "")
                            on: veyra.srEnabled
                            open: true
                            onToggled: veyra.srEnabled = on

                            VRow {
                                label: "目标尺寸"
                                hint: "决定输出分辨率"
                                VSeg {
                                    options: [
                                        { id: "1", label: "2K" },
                                        { id: "2", label: "4K" },
                                        { id: "3", label: "8K" }
                                    ]
                                    current: String(veyra.srTargetIndex)
                                    onPicked: id => veyra.srTargetIndex = parseInt(id)
                                }
                            }
                            VRow {
                                label: "质量"
                                hint: "RTX 视频超分档位"
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

                        VAccordion {
                            Layout.fillWidth: true
                            glyph: "🪄"
                            hue: Theme.accent
                            title: "NR 画面增强"
                            summary: "每层参数独立 · 强度 " + veyra.nrIntensity.toFixed(2)
                            on: veyra.nrEnabled
                            onToggled: veyra.nrEnabled = on

                            VRow {
                                label: "模型强度"
                                value: veyra.nrIntensity.toFixed(2)
                                VSlider {
                                    implicitWidth: 140
                                    from: 0; to: 1; value: veyra.nrIntensity
                                    onMoved: veyra.nrIntensity = value
                                }
                            }
                            VSubGroup {
                                label: "模型参数"
                                count: 3
                                VRow {
                                    label: "局部明暗"
                                    value: veyra.nrTone.toFixed(2)
                                    VSlider {
                                        implicitWidth: 130
                                        from: 0; to: 1; value: veyra.nrTone
                                        onMoved: veyra.nrTone = value
                                    }
                                }
                                VRow {
                                    label: "局部结构"
                                    value: veyra.nrStructure.toFixed(2)
                                    VSlider {
                                        implicitWidth: 130
                                        from: 0; to: 1; value: veyra.nrStructure
                                        onMoved: veyra.nrStructure = value
                                    }
                                }
                                VRow {
                                    label: "肤质 · 未证实"
                                    value: veyra.nrSkin.toFixed(2)
                                    VSlider {
                                        implicitWidth: 130
                                        from: -1; to: 1; value: veyra.nrSkin
                                        onMoved: veyra.nrSkin = value
                                    }
                                }
                            }
                            VSubGroup {
                                label: "实验"
                                count: 2
                                VRow {
                                    label: "时间域防闪烁"
                                    hint: "在 NR 之后稳定残差"
                                    VSwitch {
                                        checked: veyra.nrTemporal
                                        onToggled: veyra.nrTemporal = checked
                                    }
                                }
                                VRow {
                                    label: "低延迟模式"
                                    hint: "先 NR 再超分，仅预览"
                                    VSwitch {
                                        checked: veyra.lowLatency
                                        onToggled: veyra.lowLatency = checked
                                    }
                                }
                            }
                        }

                        // Adding an NR layer. List mode allows adding layers and
                        // toggling them, never reordering.
                        Rectangle {
                            id: addLayer
                            Layout.fillWidth: true
                            implicitHeight: 36
                            radius: 10
                            // A dashed outline as in the design: a Rectangle border
                            // cannot be dashed, so the outline is drawn as a shape.
                            color: addHover.hovered ? Theme.accentSoft : "transparent"
                            Shape {
                                anchors.fill: parent
                                ShapePath {
                                    strokeWidth: 1
                                    strokeColor: addHover.hovered ? Theme.accent : Qt.rgba(1, 1, 1, 0.2)
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
                            Text {
                                anchors.centerIn: parent
                                text: "+  增加 NR 层"
                                color: addHover.hovered ? Theme.accent : Theme.t2
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                                font.weight: Font.Medium
                            }
                            HoverHandler { id: addHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: veyra.addEffect("nrEnhance") }
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
                    // Per the design: DLSS frame generation and Intel XeSS
                    // (experimental) only. AMD FSR frame generation stays hidden.
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.tab === "fg"

                        VGroup {
                            VRow {
                                label: "补帧方式"
                                VSelect {
                                    value: veyra.fgBackendName
                                    options: [
                                        { id: "dlss", label: "DLSS 帧生成" },
                                        { id: "xess", label: "Intel XeSS · 实验" }
                                    ]
                                    onPicked: id => veyra.fgBackendName = id
                                }
                            }
                            VRow {
                                label: "倍率"
                                hint: veyra.fgMaxMultiplier + "X 为上限"
                                VSeg {
                                    options: veyra.fgMultiplierChoices
                                    current: String(veyra.fgMultiplier)
                                    onPicked: id => veyra.fgMultiplier = parseInt(id)
                                }
                            }
                            VRow {
                                label: "严格补帧节奏"
                                hint: "帧同步 · 默认关闭"
                                VSwitch {
                                    checked: veyra.fgStrict
                                    onToggled: veyra.fgStrict = checked
                                }
                            }
                        }
                        VGroup {
                            VRow {
                                label: "低延迟队列"
                                hint: "减少排队；本机无法验收，不宣称延迟下降"
                                VSwitch {
                                    checked: veyra.fgLowQueue
                                    onToggled: veyra.fgLowQueue = checked
                                }
                            }
                        }
                    }

                    // --- 色彩 -------------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.tab === "color"
                        VGroup {
                            VRow {
                                label: "调色"
                                VSwitch {
                                    checked: veyra.colorEnabled
                                    onToggled: veyra.colorEnabled = checked
                                }
                            }
                        }
                        VGroup {
                            visible: veyra.colorEnabled
                            VRow {
                                label: "曝光（EV）"
                                value: veyra.colorExposure.toFixed(2)
                                VSlider {
                                    implicitWidth: 150
                                    center: true
                                    from: -5; to: 5; value: veyra.colorExposure
                                    onMoved: veyra.colorExposure = value
                                }
                            }
                            VRow {
                                label: "对比度"
                                value: String(Math.round(veyra.colorContrast))
                                VSlider {
                                    implicitWidth: 150
                                    center: true
                                    from: -100; to: 100; value: veyra.colorContrast
                                    onMoved: veyra.colorContrast = value
                                }
                            }
                            VRow {
                                label: "饱和度"
                                value: String(Math.round(veyra.colorSaturation))
                                VSlider {
                                    implicitWidth: 150
                                    center: true
                                    from: -100; to: 100; value: veyra.colorSaturation
                                    onMoved: veyra.colorSaturation = value
                                }
                            }
                            VRow {
                                label: "色温（相对）"
                                value: String(Math.round(veyra.colorTemperature))
                                VSlider {
                                    implicitWidth: 150
                                    center: true
                                    from: -100; to: 100; value: veyra.colorTemperature
                                    onMoved: veyra.colorTemperature = value
                                }
                            }
                        }
                        // Honest scope note: the design's colour page is much larger
                        // (curves, mixer, grading wheels, calibration, LUT). Those
                        // controls are not wired to the bridge yet, so this panel
                        // says so rather than showing dead sliders.
                        Rectangle {
                            Layout.fillWidth: true
                            implicitHeight: noteText.implicitHeight + 20
                            radius: 9
                            color: Qt.rgba(0.961, 0.784, 0.294, 0.06)
                            Text {
                                id: noteText
                                anchors.fill: parent
                                anchors.margins: 10
                                text: "曲线、混色器、颜色分级、校准与 LUT 尚未接入界面；引擎已支持，但本轮未接。"
                                color: Theme.t2
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSmall
                                wrapMode: Text.WordWrap
                            }
                        }
                    }

                    // --- 声音 -------------------------------------------------
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.tab === "audio"
                        VGroup {
                            VRow {
                                label: "音量"
                                value: Math.round(veyra.volume * 100) + "%"
                                VSlider {
                                    implicitWidth: 150
                                    from: 0; to: 1; value: veyra.volume
                                    onMoved: veyra.volume = value
                                }
                            }
                            VRow {
                                label: "静音"
                                VSwitch {
                                    checked: veyra.muted
                                    onToggled: veyra.muted = checked
                                }
                            }
                            VRow {
                                label: "音频偏移"
                                hint: "正值延后音频"
                                value: veyra.audioOffsetMs + " ms"
                                VSlider {
                                    implicitWidth: 150
                                    center: true
                                    from: -2000; to: 2000; value: veyra.audioOffsetMs
                                    onMoved: veyra.audioOffsetMs = Math.round(value)
                                }
                            }
                        }
                        VEyebrow { text: "音轨" }
                        Text {
                            visible: veyra.audioTracks.length === 0
                            text: "当前源没有可选音轨。"
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
                                    VDot { off: veyra.selectedAudioTrack !== modelData.index }
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
                                            text: modelData.channels > 0 ? modelData.channels + " 声道" : ""
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
                        Layout.fillWidth: true
                        spacing: 6
                        visible: root.tab === "display"
                        VGroup {
                            VRow {
                                label: "提交帧率"
                                value: veyra.submitFpsKnown ? veyra.submitFps.toFixed(2) : "未测量"
                            }
                            VRow {
                                label: "显示帧率"
                                hint: "需要系统显示事件才能测得真实值；取不到时显示未测"
                                value: veyra.displayFpsKnown ? veyra.displayFps.toFixed(2) : "未测"
                            }
                            VRow {
                                label: "晚点 P95"
                                value: veyra.lateP95Ms.toFixed(2) + " ms"
                            }
                            VRow {
                                label: "调度等待 P95"
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
                    text: "重置本页"
                    onClicked: veyra.resetCurrentPage()
                }
            }
        }
    }

    Menu {
        id: sourceMenu
        width: 260
        MenuItem { text: "打开文件…"; onTriggered: veyra.openFileDialog() }
        MenuItem { text: "采集卡设置…"; onTriggered: veyra.openCaptureDialog() }
        MenuItem { text: "PS5 串流…"; onTriggered: veyra.openPs5Dialog() }
        MenuItem { text: "屏幕捕获…"; onTriggered: veyra.openScreenCaptureDialog() }
    }
    // The design puts 另存为 / 管理 in the professional page's preset menu.
    Menu {
        id: presetMenu
        width: 260
        Repeater {
            model: veyra.presets
            delegate: MenuItem {
                required property var modelData
                text: modelData.name + (modelData.builtin ? "  （内置）" : "")
                onTriggered: veyra.applyPresetIndex(modelData.index)
            }
        }
        MenuSeparator { }
        MenuItem { text: "另存为预设…"; onTriggered: root.requestDialog("save") }
        MenuItem { text: "管理预设…"; onTriggered: root.requestDialog("manage") }
    }

    // Switching to node mode rebuilds the chain, so the design asks first.
    Dialog {
        id: switchDialog
        anchors.centerIn: parent
        modal: true
        title: "切换到节点模式？"
        standardButtons: Dialog.Ok | Dialog.Cancel
        Text {
            width: 380
            wrapMode: Text.WordWrap
            text: "列表与节点是两套独立配置和预设。切换会重建处理链；切回列表时恢复列表原来的设置。"
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }
        onAccepted: root.requestPage("node")
    }
}
