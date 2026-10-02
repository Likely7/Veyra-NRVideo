import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts
import "ExportLayout.js" as ExportLayout

VPage {
    id: root
    signal requestPage(string page)
    property string exportKind: "video"
    property int compactTab: 0
    readonly property var geometry: ExportLayout.calculate(width, height, compactTab)
    readonly property bool compact: geometry.compact
    property double draggedId: 0
    property int dropIndex: -1
    property real dragY: 0
    function stateText(state, progress, inspecting) {
        if (state === "ready") return inspecting ? "正在读取轨道…" : "待导出"
        if (state === "queued") return "等待导出"
        if (state === "running") return "导出中 " + (progress * 100).toFixed(0) + "%"
        return ({done: "已完成", failed: "失败 · 点击重试", cancelled: "已取消 · 可重新开始"})[state] || state
    }
    function preview(id) {
        if (compact) compactTab = 1
        Qt.callLater(() => veyra.previewExportItem(id))
    }
    component Note: Text {
        Layout.fillWidth: true
        wrapMode: Text.Wrap
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 11
    }
    component ExportScrollBar: Basic.ScrollBar {
        id: bar
        implicitWidth: 8
        padding: 2
        visible: size < 1
        contentItem: Rectangle { implicitWidth: 4; implicitHeight: 30; radius: 2; color: bar.pressed ? Theme.t2 : Theme.t3; opacity: bar.active || bar.hovered ? 0.9 : 0.45 }
        background: Item {}
    }
    // Export-only bounded menu: even at 720x260 every resolution is reachable.
    component Choice: Basic.ComboBox {
        id: choice
        property var options: []
        property string selected: ""
        signal picked(string id)
        model: options
        textRole: "label"
        valueRole: "id"
        currentIndex: Math.max(0, options.findIndex(o => String(o.id) === selected))
        implicitWidth: 172
        implicitHeight: 30
        onActivated: index => picked(String(options[index].id))
        background: Rectangle { radius: 8; color: Theme.card2; border.color: Theme.stroke2 }
        contentItem: Text { leftPadding: 10; rightPadding: 24; verticalAlignment: Text.AlignVCenter; text: choice.currentText; color: Theme.t1; elide: Text.ElideRight; font.family: Theme.fontUi; font.pixelSize: 12 }
        indicator: VIcon { x: choice.width - 22; y: 8; name: "down"; size: 14; color: Theme.t3 }
        popup: Popup {
            id: choicesPopup
            parent: Overlay.overlay
            width: choice.width
            height: Math.min(list.contentHeight + 8, Math.max(40, root.height - 16), 320)
            padding: 4
            onAboutToShow: {
                const point = choice.mapToItem(parent, 0, choice.height + 4)
                x = Math.max(8, Math.min(point.x, parent.width - width - 8))
                y = Math.max(8, Math.min(point.y, parent.height - height - 8))
            }
            background: Rectangle { radius: 8; color: Theme.card2; border.color: Theme.stroke2 }
            contentItem: ListView {
                id: list
                objectName: "videoCover"
                property real coverRadius: 8
                clip: true
                model: choice.options
                currentIndex: choice.currentIndex
                ScrollBar.vertical: ExportScrollBar {}
                delegate: Basic.ItemDelegate {
                    required property var modelData
                    required property int index
                    width: list.width; height: 32
                    enabled: modelData.disabled !== true
                    highlighted: index === choice.currentIndex
                    background: Rectangle { radius: 5; color: parent.highlighted ? Theme.card3 : "transparent" }
                    contentItem: Text { text: modelData.label; color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: 12; verticalAlignment: Text.AlignVCenter; elide: Text.ElideRight }
                    onClicked: { choice.picked(String(modelData.id)); choicesPopup.close() }
                }
            }
        }
    }
    RowLayout {
        id: head
        x: 12; y: 12; width: parent.width - 24; height: 32
        spacing: 12
        VH2 { text: "导出" }
        Text { visible: !root.compact; text: "先整理文件，再按顺序导出"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 12 }
        Item { Layout.fillWidth: true }
        VSeg {
            objectName: "export-tabs"
            visible: root.compact
            options: [{id: "0", label: "文件"}, {id: "1", label: "预览"}, {id: "2", label: "设置"}]
            current: String(root.compactTab)
            onPicked: id => root.compactTab = Number(id)
        }
    }
    Rectangle {
        id: queueCard
        objectName: "export-queue-card"
        x: root.geometry.left.x; y: root.geometry.left.y
        width: root.geometry.left.width; height: root.geometry.left.height
        visible: !root.compact || root.compactTab === 0
        radius: Theme.rCard; color: Theme.card; border.color: Theme.stroke
        RowLayout {
            id: queueHead
            x: 10; y: 10; width: parent.width - 20; height: 30; spacing: 5
            Text { text: "队列 " + veyra.exportQueueModel.count; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 12 }
            Item { Layout.fillWidth: true }
            VButton { objectName: "export-add-files"; text: "添加"; iconName: "plus"; onClicked: veyra.addExportFilesDialog() }
            VButton { objectName: "export-clear-queue"; ghost: true; text: veyra.exportRunning ? "清空等待" : "清空"; onClicked: veyra.exportQueueModel.clearWaiting() }
        }
        ListView {
            id: queueList
            objectName: "export-queue-list"
            anchors { top: queueHead.bottom; topMargin: 8; left: parent.left; right: parent.right; bottom: parent.bottom; margins: 8 }
            clip: true
            spacing: 5
            model: veyra.exportQueueModel
            ScrollBar.vertical: ExportScrollBar {}
            delegate: Rectangle {
                id: row
                required property int index
                required property double itemId
                required property string name
                required property string input
                required property string output
                required property string state
                required property string note
                required property real progress
                required property real duration
                required property bool current
                required property bool locked
                required property bool inspecting
                objectName: "export-queue-row-" + index
                width: queueList.width; height: 72; radius: 9
                color: current ? Theme.card3 : rowHover.hovered ? Theme.card2 : "transparent"
                border.color: current ? Theme.accent : "transparent"
                HoverHandler { id: rowHover }
                TapHandler { onTapped: root.preview(row.itemId) }
                ToolTip.visible: rowHover.hovered && note.length > 0
                ToolTip.text: note + (output.length ? "\n" + output : "")
                RowLayout {
                    anchors.fill: parent; anchors.margins: 6; spacing: 5
                    Text {
                        text: "≡"; color: row.locked ? Theme.t3 : Theme.t2
                        Layout.preferredWidth: 18; Layout.fillHeight: true
                        verticalAlignment: Text.AlignVCenter; font.pixelSize: 18
                        MouseArea {
                            objectName: "export-drag-" + row.index
                            anchors.fill: parent; enabled: !row.locked; preventStealing: true; cursorShape: Qt.SizeVerCursor
                            onPressed: mouse => { root.draggedId = row.itemId; root.dropIndex = row.index }
                            onPositionChanged: mouse => {
                                if (!pressed) return
                                const point = mapToItem(queueList, mouse.x, mouse.y)
                                root.dragY = point.y
                                const target = queueList.indexAt(1, Math.max(0, Math.min(queueList.contentHeight - 1, point.y + queueList.contentY)))
                                if (target >= 0) root.dropIndex = target
                            }
                            onReleased: {
                                if (root.dropIndex >= 0) veyra.exportQueueModel.moveItem(root.draggedId, root.dropIndex)
                                root.draggedId = 0; root.dropIndex = -1
                            }
                            onCanceled: { root.draggedId = 0; root.dropIndex = -1 }
                        }
                    }
                    Image { source: veyra.fileThumbnailId(row.input, 0); asynchronous: true; fillMode: Image.PreserveAspectFit; Layout.preferredWidth: 44; Layout.preferredHeight: 42 }
                    ColumnLayout {
                        Layout.fillWidth: true; spacing: 3
                        Text { Layout.fillWidth: true; text: row.name; color: Theme.t1; elide: Text.ElideMiddle; font.family: Theme.fontUi; font.pixelSize: 12 }
                        Text { Layout.fillWidth: true; text: root.stateText(row.state, row.progress, row.inspecting); color: row.state === "failed" ? Theme.err : Theme.t3; elide: Text.ElideRight; font.family: Theme.fontUi; font.pixelSize: 10 }
                        Text { Layout.fillWidth: true; text: row.duration > 0 ? veyra.formatTime(row.duration) : ""; color: Theme.t3; font.pixelSize: 10 }
                    }
                    VButton {
                        visible: row.state === "failed"
                        ghost: true; text: "↻"; onClicked: veyra.exportQueueModel.retryItem(row.itemId)
                    }
                    VButton {
                        objectName: "export-remove-" + row.index
                        ghost: true; text: "×"
                        enabled: !row.locked
                        onClicked: veyra.exportQueueModel.removeItem(row.itemId)
                    }
                }
                Rectangle { visible: root.draggedId > 0 && root.dropIndex === row.index; height: 2; width: parent.width; color: Theme.accent }
            }
            Column {
                anchors.centerIn: parent
                width: Math.max(1, parent.width - 24)
                visible: veyra.exportQueueModel.count === 0
                spacing: 8
                Text { width: parent.width; text: root.compact ? "添加或拖入多个视频" : "添加或拖入多个视频\n添加后不会立即导出"; horizontalAlignment: Text.AlignHCenter; color: Theme.t3; wrapMode: Text.Wrap; font.family: Theme.fontUi; font.pixelSize: 12 }
                VButton { anchors.horizontalCenter: parent.horizontalCenter; visible: veyra.hasSource && !veyra.isCapture && queueList.height > 80; text: "添加当前文件"; onClicked: veyra.addCurrentExportFile() }
            }
        }
        Timer {
            interval: 60; repeat: true; running: root.draggedId > 0
            onTriggered: {
                const delta = root.dragY < 25 ? -16 : root.dragY > queueList.height - 25 ? 16 : 0
                queueList.contentY = Math.max(0, Math.min(Math.max(0, queueList.contentHeight - queueList.height), queueList.contentY + delta))
            }
        }
        DropArea {
            anchors.fill: parent
            onEntered: drag => { drag.accepted = drag.hasUrls }
            onDropped: drop => { if (drop.hasUrls) { veyra.addExportFiles(drop.urls); drop.acceptProposedAction() } }
        }
    }
    Rectangle {
        id: rightCol
        objectName: "export-settings-card"
        x: root.geometry.settings.x; y: root.geometry.settings.y
        width: root.geometry.settings.width; height: root.geometry.settings.height
        visible: !root.compact || root.compactTab === 2
        radius: Theme.rCard; color: Theme.card; border.color: Theme.stroke
        Basic.ScrollView {
            id: settingsScroll
            objectName: "export-settings-scroll"
            anchors.fill: parent; anchors.margins: 12
            contentWidth: availableWidth
            clip: true
            ScrollBar.vertical: ExportScrollBar {}
            ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
            ColumnLayout {
                width: settingsScroll.availableWidth
                spacing: 10
                VH2 { text: "输出设置"; font.pixelSize: 14 }
                VRow { label: "类型"; VSeg { options: [{id: "video", label: "视频"}, {id: "image", label: "图片"}]; current: root.exportKind; onPicked: id => root.exportKind = id } }
                VGroup {
                    visible: root.exportKind === "video"
                    VRow { label: "封装"; VSeg { objectName: "export-container"; options: [{id: "0", label: "MP4"}, {id: "1", label: "MKV"}]; current: String(veyra.exportContainer); onPicked: id => veyra.exportContainer = Number(id) } }
                    VRow { label: "视频编码"; VSeg { options: [{id: "0", label: "H.264"}, {id: "1", label: "HEVC"}]; current: veyra.exportHevc ? "1" : "0"; onPicked: id => veyra.exportHevc = id === "1" } }
                    VRow {
                        label: "分辨率"
                        Choice {
                            objectName: "export-resolution"
                            options: [{id: "-1", label: "跟随增强预设"}, {id: "0", label: "源尺寸"}, {id: "1", label: "2K"}, {id: "2", label: "4K"}, {id: "4", label: "5K"}, {id: "5", label: "6K"}, {id: "6", label: "7K"}, {id: "3", label: "8K"}]
                            selected: String(veyra.exportSrTargetIndex)
                            onPicked: id => veyra.exportSrTargetIndex = Number(id)
                        }
                    }
                    VRow { label: "质量策略"; VSeg { options: [{id: "0", label: "CBR"}, {id: "1", label: "VBR"}, {id: "2", label: "CQ"}]; current: String(veyra.exportRateControl); onPicked: id => veyra.exportRateControl = Number(id) } }
                    VRow {
                        visible: veyra.exportRateControl !== 2
                        label: "码率 Mbps"
                        DialogHost.VTextField { objectName: "export-bitrate-field"; implicitWidth: 100; text: String(veyra.exportBitrateMbps); onEdited: value => { const n = Number(value); if (isFinite(n)) veyra.exportBitrateMbps = Math.max(1, Math.min(2000, Math.round(n))) } }
                    }
                    Note { text: veyra.exportRateControl === 2 ? "CQ 使用恒定质量；文件大小随画面复杂度变化。" : "目标码率影响画质与文件大小。" }
                    VRow { label: "增强预设"; Choice { options: veyra.presetChoices; selected: String((veyra.presetChoices.find(o => o.label === veyra.exportPresetName) || {id: "-1"}).id); onPicked: id => veyra.selectExportPreset(Number(id)) } }
                }
                VGroup {
                    visible: root.exportKind === "video"
                    Note { text: veyra.exportQueueModel.selectedId ? "所选文件的音轨与内嵌字幕" : "默认保留全部轨道；点击左侧文件可单独选择。" }
                    VRow {
                        label: "音轨"
                        enabled: veyra.exportSelectionEditable
                        Choice { options: [{id: "1", label: "保留全部"}, {id: "2", label: "选择轨道"}, {id: "3", label: "不保留"}]; selected: String(veyra.exportAudioPolicy); onPicked: id => veyra.setExportTrackPolicy(true, Number(id)) }
                    }
                    VRow {
                        label: "内嵌字幕"
                        enabled: veyra.exportSelectionEditable
                        Choice { options: [{id: "1", label: "保留全部"}, {id: "2", label: "选择轨道"}, {id: "3", label: "不保留"}]; selected: String(veyra.exportSubtitlePolicy); onPicked: id => veyra.setExportTrackPolicy(false, Number(id)) }
                    }
                    Repeater {
                        model: veyra.exportTracks
                        delegate: ColumnLayout {
                            required property var modelData
                            Layout.fillWidth: true; spacing: 1
                            Basic.CheckBox {
                                id: trackCheck
                                Layout.fillWidth: true
                                implicitHeight: 28
                                enabled: veyra.exportSelectionEditable
                                checked: modelData.selected
                                text: modelData.label
                                indicator: Rectangle {
                                    x: trackCheck.leftPadding; y: (trackCheck.height - height) / 2
                                    implicitWidth: 17; implicitHeight: 17; radius: 4
                                    color: trackCheck.checked ? (trackCheck.enabled ? Theme.accent : Theme.t3) : Theme.card2
                                    border.color: trackCheck.checked ? color : Theme.stroke2
                                    Text { anchors.centerIn: parent; text: "✓"; visible: trackCheck.checked; color: Theme.card; font.pixelSize: 12 }
                                }
                                contentItem: Text { text: parent.text; leftPadding: parent.indicator.width + 8; color: Theme.t2; font.pixelSize: 11; elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter }
                                onClicked: veyra.toggleExportTrack(modelData.audio, modelData.index, checked)
                            }
                            Note { text: modelData.action; color: modelData.compatible ? Theme.t3 : Theme.err }
                        }
                    }
                    Note { text: "音轨直接复制，不改变声道数。MKV 可保留 ASS 样式和字体；“保留全部”时当前封装装不下的轨道会跳过并提示，手动选中的轨道装不下会报错。" }
                }
                VGroup {
                    visible: root.exportKind === "image"
                    VButton { Layout.fillWidth: true; text: "导出当前画面"; enabled: veyra.hasSource && !veyra.imageBatchActive; onClicked: veyra.saveFrameDialog() }
                    VButton { Layout.fillWidth: true; text: "批量处理图片…"; enabled: !veyra.imageBatchActive; onClicked: veyra.exportImagesDialog() }
                    Note { text: veyra.imageBatchStatus }
                    VButton { visible: veyra.imageBatchActive; text: "取消图片批次"; onClicked: veyra.cancelImageBatch() }
                }
                Note { visible: root.exportKind === "video"; text: "点击开始时冻结本批设置。" + (veyra.preferences.exportStopsPlayback !== false ? "开始导出时会关闭正在播放的内容，显卡全部留给导出。" : "导出期间可以继续预览其他文件，会和导出抢显卡。") }
                VRow { label: "导出时关闭正在播放的内容"; VSwitch { objectName: "export-stop-playback"; checked: veyra.preferences.exportStopsPlayback !== false; onToggled: veyra.setPreference("exportStopsPlayback", checked) } }
                VButton { objectName: "export-target"; Layout.fillWidth: true; iconName: "folder"; text: "选择保存目录…"; onClicked: veyra.chooseExportPath() }
                Note { text: veyra.exportTarget; wrapMode: Text.WrapAnywhere }
                VRow { label: "完成提示音"; VSwitch { objectName: "export-sound"; checked: veyra.exportCompletionSound; onToggled: veyra.exportCompletionSound = checked } }
            }
        }
    }
    Rectangle {
        objectName: "export-footer"
        x: root.geometry.footer.x; y: root.geometry.footer.y
        width: root.geometry.footer.width; height: root.geometry.footer.height
        radius: Theme.rCard; color: Theme.card; border.color: Theme.stroke
        ColumnLayout {
            anchors.fill: parent; anchors.margins: root.compact ? 6 : 10; spacing: 5
            Text { objectName: "export-status"; Layout.fillWidth: true; visible: !root.compact; text: veyra.exportStatus; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 11; elide: Text.ElideRight; ToolTip.text: text; ToolTip.visible: statusHover.hovered; HoverHandler { id: statusHover } }
            Basic.ProgressBar {
                id: exportProgressBar
                objectName: "export-progress"; Layout.fillWidth: true; Layout.preferredHeight: 5; from: 0; to: 1; value: veyra.exportProgress
                background: Rectangle { implicitHeight: 5; radius: 3; color: Theme.card3 }
                contentItem: Item { Rectangle { width: exportProgressBar.visualPosition * parent.width; height: parent.height; radius: 3; color: Theme.accent } }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 6
                Text { visible: root.compact; Layout.fillWidth: true; text: veyra.exportStatus; color: Theme.t2; elide: Text.ElideRight; font.pixelSize: 11 }
                VButton { objectName: "export-start"; Layout.fillWidth: !root.compact; primary: !veyra.exportRunning; text: veyra.exportRunning ? (veyra.exportPaused ? "继续导出" : "暂停导出") : "开始导出 " + veyra.exportReadyCount + " 项"; enabled: root.exportKind === "video" && (veyra.exportRunning || veyra.exportReadyCount > 0); onClicked: veyra.exportRunning ? veyra.pauseExport(!veyra.exportPaused) : veyra.startExport() }
                VButton { objectName: "export-cancel"; visible: veyra.exportRunning; text: "取消"; onClicked: veyra.cancelExport() }
            }
            // The one remaining-time readout (the old page had two); it holds still while paused.
            Note { objectName: "export-eta"; visible: !root.compact; text: veyra.exportRunning ? "已编码 " + veyra.exportEncoded + " 帧" + (veyra.exportPaused ? " · 已暂停" : veyra.exportEtaSeconds > 0 ? " · 当前文件剩余约 " + veyra.formatTime(veyra.exportEtaSeconds) : "") + " · 等待 " + veyra.exportQueueCount + " 项" : "取消后可重新开始；已完成文件会跳过。" }
        }
    }

    // --- middle: picture, then the trim card ------------------------------
    Rectangle {
        id: vwrap
        objectName: "export-preview-card"
        x: root.geometry.preview.x; y: root.geometry.preview.y
        width: root.geometry.preview.width; height: root.geometry.preview.height
        visible: !root.compact || root.compactTab === 1
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
            // The engine's status line is the player's long diagnostic once a source
            // closes (the export now closes playback); the preview says what it is for.
            Text {
                anchors.centerIn: parent
                width: parent.width - 40
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.WordWrap
                visible: !veyra.hasSource
                text: veyra.exportRunning ? "导出进行中，预览已关闭，显卡留给导出
点左侧文件可重新预览（会和导出抢显卡）" : "点左侧文件预览"
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
        objectName: "export-trim-card"
        x: root.geometry.trim.x; y: root.geometry.trim.y
        width: root.geometry.trim.width; height: root.geometry.trim.height
        visible: !root.compact || root.compactTab === 1
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke
        // 入点 / 出点 (user request 2026-09-29): play or scrub to a spot, then
        // mark it. One timeline shows the playhead, the kept range and both marks.
        readonly property bool editable: veyra.hasSource && veyra.exportSelectionEditable && veyra.duration > 0
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
            anchors.margins: 8
            spacing: 4
            Item {
                id: timeline
                objectName: "export-timeline"
                Layout.fillWidth: true
                Layout.preferredHeight: root.compact ? 18 : 30
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

                VButton { objectName: "export-mark-in"; text: "入点 I"; enabled: trimCard.editable; onClicked: trimCard.markIn() }
                VButton { objectName: "export-mark-out"; text: "出点 O"; enabled: trimCard.editable; onClicked: trimCard.markOut() }
                VSeg {
                    objectName: "export-compare"
                    options: [{ id: "0", label: "增强" }, { id: "2", label: "对比" }]
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
            }
            Text {
                Layout.fillWidth: true
                visible: !root.compact
                text: "导出 " + veyra.formatTime(trimCard.inAt) + " — " + veyra.formatTime(trimCard.outAt)
                color: Theme.t2; font.family: Theme.fontMono; font.pixelSize: 11
                elide: Text.ElideRight
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
