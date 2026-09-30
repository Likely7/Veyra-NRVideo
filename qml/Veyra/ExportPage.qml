// 导出, rebuilt from the design (pages-b.js PAGES.exp + pages.css .exp).
//
// Design grid: 250px | 1fr | 330px, rows auto | 1fr | auto, padding 14. The queue
// card spans rows 2-3 on the left; the video sits in the middle with the trim range
// under it; output settings fill the right column.
//
// Scope: preset selection, codec, output size, rate control, queueing and
// progress are wired to the engine's export job. Queue execution is serialized
// by ExportJobManager; full multi-file cancellation/ordering acceptance remains
// a separate runtime check.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

VPage {
    id: root
    signal requestPage(string page)
    // pages-b.js 类型: 视频 (the encoder settings) or 图片 (frame / batch).
    property string exportKind: "video"
    readonly property var rows: veyra.exportItems
    // The playing file is the first row unless the queue already carries it.
    readonly property bool currentListed: rows.some(r => r.current)
    function stateText(r) {
        switch (r.state) {
        case "running": return "导出中 " + (r.progress * 100).toFixed(0) + "%"
        case "paused": return "已暂停 " + (r.progress * 100).toFixed(0) + "%"
        case "queued": return "排队中"
        case "done": return "已完成"
        case "failed": return "失败" + (r.note.length > 0 ? "：" + r.note : "")
        case "cancelled": return "已取消"
        }
        return r.state
    }
    // .qitem: thumbnail, name, what it is / where it stands, progress.
    component QueueItem: Rectangle {
        id: qi
        property string thumb: ""
        property string name: ""
        property string meta: ""
        property real progress: 0
        property bool showProgress: false
        property bool selected: false
        property bool failed: false
        Layout.fillWidth: true
        implicitHeight: 56
        radius: 10
        color: selected ? Qt.rgba(1, 1, 1, 0.06) : qiHover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
        border.width: 1
        border.color: selected ? Theme.stroke2 : "transparent"
        HoverHandler { id: qiHover }
        RowLayout {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 10
            Rectangle {
                implicitWidth: 64; implicitHeight: 40; radius: 7
                color: Theme.videoBlack
                clip: true
                Image {
                    objectName: qi.objectName + "-image"
                    anchors.fill: parent
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    source: qi.thumb
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: qi.name
                    color: Theme.t1
                    font.family: Theme.fontUi; font.pixelSize: 12; font.weight: Font.Medium
                    elide: Text.ElideMiddle
                }
                Text {
                    Layout.fillWidth: true
                    text: qi.meta
                    color: qi.failed ? Theme.err : Theme.t3
                    font.family: Theme.fontUi; font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Rectangle {
                    visible: qi.showProgress
                    Layout.fillWidth: true
                    implicitHeight: 3
                    radius: 9
                    color: Qt.rgba(1, 1, 1, 0.08)
                    Rectangle {
                        width: parent.width * Math.max(0, Math.min(1, qi.progress))
                        height: parent.height; radius: 9
                        color: Theme.accent
                        Behavior on width { NumberAnimation { duration: Theme.d(300) } }
                    }
                }
            }
        }
    }

    // --- header -----------------------------------------------------------
    RowLayout {
        id: head
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 14
        height: 32
        spacing: 8
        VH2 { text: "导出" }
        Text {
            text: "按所选预设导出，导出期间可继续观看"
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }
        Item { Layout.fillWidth: true }
        VButton { iconName: "plus"; text: "添加文件"; onClicked: veyra.addExportFilesDialog() }
    }

    // --- left column: what is being exported ------------------------------
    Rectangle {
        id: queueCard
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.top: head.bottom
        anchors.topMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: 250
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            RowLayout {
                Layout.fillWidth: true
                VEyebrow { text: "队列 · " + (root.rows.length + (root.currentListed || !veyra.hasSource ? 0 : 1)) }
                Item { Layout.fillWidth: true }
                VButton {
                    objectName: "export-clear-finished"
                    visible: root.rows.some(r => r.state !== "running" && r.state !== "queued" && r.state !== "paused")
                    ghost: true; implicitHeight: 24; text: "清除已结束"
                    onClicked: veyra.clearFinishedExportItems()
                }
            }
            // The playing file, first, unless the queue already holds it.
            QueueItem {
                objectName: "export-item-thumb"
                visible: veyra.hasSource && veyra.duration > 0 && !veyra.isCapture && !root.currentListed
                selected: true
                thumb: visible ? "image://veyra-thumb/" + veyra.thumbnailGeneration + "/" + Math.round(veyra.duration * 100) : ""
                name: veyra.sourceName
                meta: veyra.sourceRateText + " · " + veyra.durationText + " · 待导出"
            }
            Text {
                visible: !veyra.hasSource && root.rows.length === 0
                Layout.fillWidth: true
                text: "未打开文件；可用“添加文件”直接排队"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Repeater {
                model: root.rows
                delegate: QueueItem {
                    required property var modelData
                    required property int index
                    objectName: "export-queue-row-" + index
                    selected: modelData.current
                    thumb: modelData.current
                           ? "image://veyra-thumb/" + veyra.thumbnailGeneration + "/" + Math.round(veyra.duration * 100)
                           : veyra.fileThumbnailId(modelData.input, 10)
                    name: modelData.name
                    meta: root.stateText(modelData)
                    failed: modelData.state === "failed"
                    showProgress: modelData.state !== "queued"
                    progress: modelData.progress
                }
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.stroke }
            Text {
                visible: veyra.exportQueueCount > 0
                Layout.fillWidth: true
                text: "等待队列 " + veyra.exportQueueCount + " 项"
                color: Theme.accent
                font.family: Theme.fontMono
                font.pixelSize: 11
            }
            Text {
                objectName: "export-queue-failure"
                visible: veyra.exportQueueFailures > 0
                Layout.fillWidth: true
                text: veyra.exportQueueFailures + " 项未能开始 · 最近：" + veyra.exportQueueFailure
                color: Theme.err
                font.family: Theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Text {
                Layout.fillWidth: true
                text: "按加入顺序逐项导出；每项独立保留 partial。"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WordWrap
            }
            Item { Layout.fillHeight: true }
        }
    }

    // --- right column -----------------------------------------------------
    Rectangle {
        id: rightCol
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.top: head.bottom
        anchors.topMargin: 10
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: 330
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 4

            VH3 { text: "输出设置" }

            VRow {
                label: "类型"
                VSeg {
                    objectName: "export-kind"
                    options: [{ id: "video", label: "视频" }, { id: "image", label: "图片" }]
                    current: root.exportKind
                    onPicked: id => root.exportKind = id
                }
            }

            // 图片: each image runs through the player's list chain and the engine's
            // own frame save (PNG, JXR for HDR output).
            VGroup {
                visible: root.exportKind === "image"
                VRow {
                    label: "当前画面"
                    hint: "保存正在播放的这一帧"
                    hintBelow: true
                    VButton {
                        objectName: "export-frame"
                        text: "导出当前画面…"
                        enabled: veyra.hasSource && !veyra.imageBatchActive
                        onClicked: veyra.saveFrameDialog()
                    }
                }
                VRow {
                    label: "批量图片"
                    hint: "选择多张图片，按当前列表链逐张处理"
                    hintBelow: true
                    VButton {
                        objectName: "export-images"
                        text: veyra.imageBatchActive ? "取消批量" : "批量处理图片…"
                        onClicked: veyra.imageBatchActive ? veyra.cancelImageBatch() : veyra.exportImagesDialog()
                    }
                }
                Text {
                    objectName: "image-batch-status"
                    visible: veyra.imageBatchStatus.length > 0
                    Layout.fillWidth: true
                    Layout.bottomMargin: 8
                    text: veyra.imageBatchStatus
                    color: veyra.imageBatchFailures > 0 ? Theme.warn : Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }
            }

            VGroup {
                visible: root.exportKind === "video"
                VRow {
                    label: "编码"
                    VSeg {
                        options: [{ id: "h264", label: "H.264" }, { id: "hevc", label: "HEVC" }]
                        current: veyra.exportHevc ? "hevc" : "h264"
                        onPicked: id => veyra.exportHevc = (id === "hevc")
                    }
                }
                VRow {
                    label: "分辨率"
                    hint: "跟随所选预设，或指定输出尺寸"
                    hintBelow: true
                    VSeg {
                        options: [
                            { id: "-1", label: "预设" },
                            { id: "0", label: "源" },
                            { id: "1", label: "2K" },
                            { id: "2", label: "4K" },
                            { id: "4", label: "5K" },
                            { id: "5", label: "6K" },
                            { id: "6", label: "7K" },
                            { id: "3", label: "8K" }
                        ]
                        current: String(veyra.exportSrTargetIndex)
                        onPicked: id => veyra.exportSrTargetIndex = parseInt(id)
                    }
                }
                VRow {
                    label: "码率"
                    value: veyra.exportBitrateMbps > 0 ? veyra.exportBitrateMbps + " Mbps" : "恒定质量"
                    RowLayout {
                        spacing: 6
                        VSlider {
                            implicitWidth: 90
                            from: 0
                            to: 200
                            value: Math.min(200, veyra.exportBitrateMbps)
                            onMoved: veyra.exportBitrateMbps = Math.round(value)
                        }
                        // Any value 0–2000 Mbps can be typed (0 = 恒定质量).
                        DialogHost.VTextField {
                            id: bitrateField
                            objectName: "export-bitrate-field"
                            implicitWidth: 56
                            text: String(veyra.exportBitrateMbps)
                            onEdited: value => {
                                const v = Math.round(Number(value))
                                if (isFinite(v)) veyra.exportBitrateMbps = Math.max(0, Math.min(2000, v))
                                bitrateField.text = String(veyra.exportBitrateMbps)
                            }
                        }
                    }
                }
                VRow {
                    label: "策略"
                    VSeg {
                        options: [{ id: "0", label: "CBR" }, { id: "1", label: "VBR" }, { id: "2", label: "CQ" }]
                        current: String(veyra.exportRateControl)
                        onPicked: id => veyra.exportRateControl = parseInt(id)
                    }
                }
            }

            VGroup {
                visible: root.exportKind === "video"
                VRow {
                    label: "增强预设"
                    hint: "节点预设待执行器接入"
                    hintBelow: true
                    VSelect {
                        value: veyra.exportPresetName
                        options: veyra.presetChoices
                        onPicked: id => veyra.selectExportPreset(parseInt(id))
                    }
                }
                // The export copies the audio track that is playing, untouched.
                VRow {
                    label: "音轨"
                    visible: veyra.hasSource && !veyra.isCapture
                    hint: veyra.audioTracks.length > 0 ? "原样复制，不重新编码；与播放时选的音轨相同" : (veyra.hasSource ? "这个文件没有音轨" : "")
                    hintBelow: true
                    VSelect {
                        objectName: "export-audio-track"
                        implicitWidth: 170
                        visible: veyra.audioTracks.length > 0
                        readonly property var tracks: veyra.audioTracks.map(t => ({ id: String(t.index), label: t.label }))
                        options: tracks
                        value: (tracks.find(t => Number(t.id) === veyra.selectedAudioTrack) || tracks[0] || { label: "默认" }).label
                        onPicked: id => veyra.selectedAudioTrack = Number(id)
                    }
                }
                VRow {
                    label: "预计大小"
                    hint: veyra.exportSizeEstimate
                    visible: veyra.exportSizeEstimate.length > 0
                }
            }

            // What the export will run with. Frozen when it starts.
            Rectangle {
                visible: root.exportKind === "video"
                Layout.fillWidth: true
                implicitHeight: pinfo.implicitHeight + 16
                radius: 9
                color: Qt.rgba(1, 1, 1, 0.03)
                ColumnLayout {
                    id: pinfo
                    anchors.fill: parent
                    anchors.margins: 8
                    spacing: 3
                    Text {
                        text: (veyra.exportHevc ? "HEVC" : "H.264") + " · "
                              + (["CBR", "VBR", "CQ"][veyra.exportRateControl]) + " · "
                              + (veyra.exportBitrateMbps > 0 ? veyra.exportBitrateMbps + " Mbps" : "自动质量")
                        color: Theme.t2
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fsSmall
                    }
                    Text {
                        Layout.fillWidth: true
                        text: "编码与码率在开始导出时固定；导出期间改设置不会影响正在进行的任务。"
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 11
                        wrapMode: Text.WordWrap
                    }
                }
            }

            // pages-b.js 保存到: a field with the path and a folder icon.
            VRow {
                visible: root.exportKind === "video"
                label: "保存到"
                Rectangle {
                    objectName: "export-target"
                    implicitWidth: 200
                    implicitHeight: Theme.ctlHeight
                    radius: Theme.rCtl
                    color: targetHover.hovered ? Theme.card3 : Theme.card2
                    border.width: 1
                    border.color: Theme.stroke2
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 6
                        Text {
                            Layout.fillWidth: true
                            text: veyra.exportTarget.length > 0 ? veyra.exportTarget : "选择输出位置…"
                            color: veyra.exportTarget.length > 0 ? Theme.t1 : Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                            elide: Text.ElideMiddle
                        }
                        VIcon { name: "folder"; size: 14; color: Theme.t3 }
                    }
                    HoverHandler { id: targetHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: veyra.chooseExportPath() }
                }
            }

            Item { Layout.fillHeight: true }

            // --- progress and the action ---------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

                // pages-b.js .go: "预计 … · 约 …" over the progress bar.
                Text {
                    objectName: "export-estimate"
                    Layout.fillWidth: true
                    visible: root.exportKind === "video" && text.length > 0
                    // Only real figures: a remaining time while exporting, a size when a
                    // bitrate fixes it (constant quality has no size until it runs).
                    readonly property bool sized: veyra.exportBitrateMbps > 0 && veyra.exportSizeEstimate.length > 0
                    text: veyra.exportRunning && veyra.exportEtaSeconds > 0
                          ? "预计还需 " + veyra.formatTime(veyra.exportEtaSeconds) + (sized ? " · " + veyra.exportSizeEstimate : "")
                          : sized ? "预计大小 " + veyra.exportSizeEstimate + " · 耗时开始后估算" : ""
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: veyra.exportStatus
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: 12
                    }
                    Text {
                        text: veyra.exportRunning ? (veyra.exportProgress * 100).toFixed(1) + "%" : ""
                        color: Theme.t1
                        font.family: Theme.fontMono
                        font.pixelSize: 11
                    }
                    Text {
                        visible: veyra.exportRunning && veyra.exportEtaSeconds > 0
                        text: "剩余约 " + veyra.formatTime(veyra.exportEtaSeconds)
                        color: Theme.t3
                        font.family: Theme.fontMono
                        font.pixelSize: 11
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    implicitHeight: 8
                    radius: 9
                    color: Qt.rgba(1, 1, 1, 0.08)
                    Rectangle {
                        height: parent.height
                        radius: 9
                        color: Theme.accent
                        width: parent.width * Math.max(0, Math.min(1, veyra.exportProgress))
                        Behavior on width { NumberAnimation { duration: Theme.d(300) } }
                    }
                }
                // Counts come from the job's own snapshot, not a parallel counter.
                Text {
                    visible: veyra.exportRunning
                    text: "已编码 " + veyra.exportEncoded + " 帧 · 生成 " + veyra.exportGenerated + " 帧"
                    color: Theme.t3
                    font.family: Theme.fontMono
                    font.pixelSize: 11
                }

                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    VButton {
                        objectName: "export-start"
                        Layout.fillWidth: true
                        implicitHeight: 42
                        primary: !veyra.exportRunning
                        enabled: root.exportKind === "video"
                        iconName: veyra.exportRunning ? (veyra.exportPaused ? "upload" : "pause") : "upload"
                        text: veyra.exportRunning ? (veyra.exportPaused ? "继续导出" : "暂停导出") : "开始导出"
                        onClicked: veyra.exportRunning ? veyra.pauseExport(!veyra.exportPaused)
                                                       : veyra.startExport()
                    }
                    VButton {
                        visible: veyra.exportRunning
                        text: "取消"
                        onClicked: veyra.cancelExport()
                    }
                }
            }
        }
    }

    // --- middle: picture, then the trim card ------------------------------
    Rectangle {
        id: vwrap
        anchors.left: parent.left
        anchors.leftMargin: 14 + 250 + 10
        anchors.right: rightCol.left
        anchors.rightMargin: 10
        anchors.top: head.bottom
        anchors.topMargin: 10
        anchors.bottom: trimCard.top
        anchors.bottomMargin: 10
        radius: Theme.rCard
        color: Theme.videoBlack
        border.width: 1
        border.color: Theme.stroke
        clip: true

        Item {
            id: videoArea
            objectName: "videoArea"
            anchors.fill: parent
            // 分屏对比 on the export preview: drag the divider across the picture.
            MouseArea {
                objectName: "export-compare-drag"
                anchors.fill: parent
                enabled: veyra.compareMode === 2
                visible: enabled
                cursorShape: Qt.SplitHCursor
                preventStealing: true
                function drag(mouse) {
                    const content = videoArea.Window.contentItem
                    let host = null
                    for (let i = 0; content && i < content.children.length; ++i)
                        if (content.children[i].objectName === "videoHost") host = content.children[i]
                    const p = videoArea.mapToItem(content, mouse.x, mouse.y)
                    veyra.setCompareSplitAt(host && host.width > 0 ? (p.x - host.x) / host.width : mouse.x / width)
                }
                onPressed: mouse => drag(mouse)
                onPositionChanged: mouse => { if (pressed) drag(mouse) }
            }
            Text {
                anchors.centerIn: parent
                visible: !veyra.hasSource
                text: veyra.statusText
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsH3
            }
            // .exp .compare .lb: which side is which. Each label is cut out of the
            // native video (videoCover) so it shows above the picture.
            Repeater {
                model: veyra.compareMode === 2 && veyra.hasSource ? [{ t: "原画", left: true }, { t: "增强后（预览）", left: false }] : []
                delegate: Rectangle {
                    required property var modelData
                    objectName: "export-compare-label-" + (modelData.left ? "left" : "right")
                    readonly property bool videoCover: true
                    property real coverRadius: 6
                    x: modelData.left ? 10 : videoArea.width - width - 10
                    y: 10
                    width: lb.implicitWidth + 16
                    height: lb.implicitHeight + 8
                    radius: 6
                    color: Qt.rgba(0, 0, 0, 0.6)
                    Text { id: lb; anchors.centerIn: parent; text: modelData.t; color: "#FFFFFF"; font.family: Theme.fontUi; font.pixelSize: 11 }
                }
            }
        }
    }

    Rectangle {
        id: trimCard
        anchors.left: vwrap.left
        anchors.right: vwrap.right
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        height: 92
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke
        // 入点 / 出点 (user request 2026-09-29): play or scrub to a spot, then
        // mark it. One timeline shows the playhead, the kept range and both marks.
        readonly property bool editable: veyra.hasSource && !veyra.exportRunning && veyra.duration > 0
        readonly property real inAt: veyra.exportTrimStart
        readonly property real outAt: veyra.exportTrimEnd > 0 ? veyra.exportTrimEnd : veyra.duration
        function frac(t) { return veyra.duration > 0 ? Math.max(0, Math.min(1, t / veyra.duration)) : 0 }
        function markIn() {
            if (!editable) return
            const t = veyra.position
            if (t >= outAt - 0.05) { veyra.exportTrimEnd = 0 }
            veyra.exportTrimStart = t
        }
        function markOut() {
            if (!editable) return
            const t = veyra.position
            if (t <= inAt + 0.05) { veyra.exportTrimStart = 0 }
            veyra.exportTrimEnd = t >= veyra.duration - 0.05 ? 0 : t
        }
        Shortcut { sequence: "I"; enabled: trimCard.visible && trimCard.editable && root.visible; onActivated: trimCard.markIn() }
        Shortcut { sequence: "O"; enabled: trimCard.visible && trimCard.editable && root.visible; onActivated: trimCard.markOut() }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Item {
                id: timeline
                objectName: "export-timeline"
                Layout.fillWidth: true
                Layout.preferredHeight: 30
                property bool scrubbing: false
                property real scrubFrac: 0
                readonly property real head: scrubbing ? scrubFrac : trimCard.frac(veyra.position)
                // Film strip: frames along the source (seek-preview decoder).
                Row {
                    objectName: "export-filmstrip"
                    anchors.fill: parent
                    visible: veyra.hasSource && veyra.duration > 0 && !veyra.isCapture
                    opacity: 0.55
                    clip: true
                    readonly property int count: Math.max(1, Math.floor(width / 46))
                    Repeater {
                        model: parent.visible ? parent.count : 0
                        delegate: Image {
                            required property int index
                            width: timeline.width / parent.count
                            height: timeline.height
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            sourceSize.height: 48
                            // 2-second buckets, the same keys the seek preview uses.
                            source: "image://veyra-thumb/" + veyra.thumbnailGeneration + "/"
                                    + Math.floor((index + 0.5) / parent.count * veyra.duration / 2) * 2000
                        }
                    }
                }
                Rectangle {
                    id: rail
                    anchors.verticalCenter: parent.verticalCenter
                    width: parent.width; height: 6; radius: 3
                    color: Qt.rgba(1, 1, 1, 0.10)
                }
                // The kept range.
                Rectangle {
                    objectName: "export-range"
                    anchors.verticalCenter: rail.verticalCenter
                    x: trimCard.frac(trimCard.inAt) * timeline.width
                    width: Math.max(2, (trimCard.frac(trimCard.outAt) - trimCard.frac(trimCard.inAt)) * timeline.width)
                    height: 6; radius: 3
                    color: Theme.accent
                    opacity: 0.55
                }
                // In and out marks: brackets that stand above the rail.
                Repeater {
                    model: [{ at: trimCard.inAt, isIn: true }, { at: trimCard.outAt, isIn: false }]
                    delegate: Rectangle {
                        required property var modelData
                        x: trimCard.frac(modelData.at) * timeline.width - (modelData.isIn ? 0 : width)
                        y: 0
                        width: 3; height: timeline.height
                        radius: 1
                        color: Theme.accent
                        Rectangle {
                            y: 0; x: modelData.isIn ? 0 : -5
                            width: 8; height: 3; radius: 1; color: Theme.accent
                        }
                        Rectangle {
                            y: parent.height - 3; x: modelData.isIn ? 0 : -5
                            width: 8; height: 3; radius: 1; color: Theme.accent
                        }
                    }
                }
                // Playhead.
                Rectangle {
                    objectName: "export-playhead"
                    width: 12; height: 12; radius: 6
                    color: "#FFFFFF"
                    anchors.verticalCenter: rail.verticalCenter
                    x: timeline.head * timeline.width - width / 2
                    visible: veyra.duration > 0
                }
                Timer {
                    id: exportScrub
                    interval: 120
                    onTriggered: if (timeline.scrubbing) veyra.seekTo(timeline.scrubFrac * veyra.duration)
                }
                MouseArea {
                    anchors.fill: parent
                    enabled: veyra.duration > 0 && !veyra.isCapture
                    cursorShape: Qt.PointingHandCursor
                    preventStealing: true
                    function f(x) { return Math.max(0, Math.min(1, x / Math.max(1, timeline.width))) }
                    onPressed: mouse => { timeline.scrubFrac = f(mouse.x); timeline.scrubbing = true; exportScrub.start() }
                    onPositionChanged: mouse => { if (timeline.scrubbing) { timeline.scrubFrac = f(mouse.x); if (!exportScrub.running) exportScrub.start() } }
                    onReleased: mouse => { exportScrub.stop(); timeline.scrubbing = false; veyra.seekTo(f(mouse.x) * veyra.duration) }
                    onCanceled: { exportScrub.stop(); timeline.scrubbing = false }
                }
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                VButton { icon: true; ghost: true; iconName: (veyra.running && !veyra.paused) ? "pause" : "play"; onClicked: veyra.togglePlayPause() }
                Text {
                    text: veyra.positionText
                    color: Theme.t2; font.family: Theme.fontMono; font.pixelSize: Theme.fsSmall
                }
                Item { Layout.preferredWidth: 6 }
                VButton { objectName: "export-mark-in"; text: "设为入点  I"; enabled: trimCard.editable; onClicked: trimCard.markIn() }
                VButton { objectName: "export-mark-out"; text: "设为出点  O"; enabled: trimCard.editable; onClicked: trimCard.markOut() }
                VSeg {
                    objectName: "export-compare"
                    options: [{ id: "0", label: "增强" }, { id: "2", label: "分屏对比" }]
                    current: veyra.compareMode === 2 ? "2" : "0"
                    onPicked: id => veyra.compareMode = Number(id)
                }
                VButton {
                    objectName: "export-mark-clear"
                    text: "清除"; ghost: true
                    enabled: trimCard.editable && (veyra.exportTrimStart > 0 || veyra.exportTrimEnd > 0)
                    onClicked: { veyra.exportTrimStart = 0; veyra.exportTrimEnd = 0 }
                }
                Item { Layout.fillWidth: true }
                Text {
                    text: "导出 " + veyra.formatTime(trimCard.inAt) + " — " + veyra.formatTime(trimCard.outAt)
                          + "（" + veyra.formatTime(Math.max(0, trimCard.outAt - trimCard.inAt)) + "）"
                    color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: Theme.fsSmall
                }
            }
        }
    }

    // [data-in] entrance order from pages-b.js PAGES.exp.
    VRise { target: head; d: 0 }
    VRise { target: queueCard; d: 1 }
    VRise { target: vwrap; d: 2 }
    VRise { target: trimCard; d: 3 }
    VRise { target: rightCol; d: 2 }
}
