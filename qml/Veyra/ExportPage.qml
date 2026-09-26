// 导出, rebuilt from the design (pages-b.js PAGES.exp + pages.css .exp).
//
// Design grid: 250px | 1fr | 330px, rows auto | 1fr | auto, padding 14. The queue
// card spans rows 2-3 on the left; the video sits in the middle with the trim range
// under it; output settings fill the right column.
//
// Scope, stated on the page rather than hidden: this build has no multi-file queue.
// What is real - preset selection, codec, output size, bitrate and progress - is
// wired to the engine's export job.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

VPage {
    id: root
    signal requestPage(string page)

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
        VButton { text: "添加文件"; onClicked: veyra.openFileDialog() }
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
            VEyebrow { text: "导出内容" }
            Text {
                Layout.fillWidth: true
                text: veyra.hasSource ? veyra.sourceName : "未打开文件"
                color: veyra.hasSource ? Theme.t1 : Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
                elide: Text.ElideMiddle
            }
            Text {
                Layout.fillWidth: true
                text: veyra.hasSource ? veyra.sourceSummary : ""
                color: Theme.t3
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSmall
            }
            Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.stroke }
            // The design's card is a queue of several files. The engine exports one
            // at a time, so no queue is drawn and the card says why.
            Text {
                Layout.fillWidth: true
                text: "本版本一次导出当前打开的一个文件；多文件队列尚未实现。"
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

            VGroup {
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
                    VSeg {
                        options: [
                            { id: "-1", label: "预设" },
                            { id: "0", label: "源" },
                            { id: "1", label: "2K" },
                            { id: "2", label: "4K" },
                            { id: "3", label: "8K" }
                        ]
                        current: String(veyra.exportSrTargetIndex)
                        onPicked: id => veyra.exportSrTargetIndex = parseInt(id)
                    }
                }
                VRow {
                    label: "码率"
                    value: veyra.exportBitrateMbps > 0 ? veyra.exportBitrateMbps + " Mbps" : "恒定质量"
                    VSlider {
                        implicitWidth: 110
                        from: 0
                        to: 200
                        value: veyra.exportBitrateMbps
                        onMoved: veyra.exportBitrateMbps = Math.round(value)
                    }
                }
            }

            VGroup {
                VRow {
                    label: "增强预设"
                    hint: "节点预设待执行器接入"
                    VSelect {
                        value: veyra.exportPresetName
                        options: veyra.presetChoices
                        onPicked: id => veyra.selectExportPreset(parseInt(id))
                    }
                }
            }

            // What the export will run with. Frozen when it starts.
            Rectangle {
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
                              + (veyra.exportBitrateMbps > 0 ? veyra.exportBitrateMbps + " Mbps" : "恒定质量")
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

            VRow {
                label: "保存到"
                VButton {
                    text: veyra.exportTarget.length > 0 ? "重新选择…" : "选择…"
                    onClicked: veyra.chooseExportPath()
                }
            }
            Text {
                Layout.fillWidth: true
                text: veyra.exportTarget.length > 0 ? veyra.exportTarget : "未选择输出位置"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 11
                elide: Text.ElideMiddle
            }

            Item { Layout.fillHeight: true }

            // --- progress and the action ---------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 8

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
                        Layout.fillWidth: true
                        primary: !veyra.exportRunning
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
            Text {
                anchors.centerIn: parent
                visible: !veyra.hasSource
                text: veyra.statusText
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsH3
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
        RowLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 6
            Text {
                text: "导出范围"
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
            }
            Text {
                text: veyra.formatTime(veyra.exportTrimStart) + " — "
                      + veyra.formatTime(veyra.exportTrimEnd > 0 ? veyra.exportTrimEnd : veyra.duration)
                color: Theme.t1
                font.family: Theme.fontMono
                font.pixelSize: Theme.fsSmall
            }
            Item { Layout.fillWidth: true }
            VSlider {
                Layout.fillWidth: true
                from: 0
                to: Math.max(0.001, veyra.duration)
                value: veyra.exportTrimStart
                enabledControl: veyra.hasSource && !veyra.exportRunning && veyra.duration > 0
                onMoved: veyra.exportTrimStart = value
            }
            VSlider {
                Layout.fillWidth: true
                from: 0
                to: Math.max(0.001, veyra.duration)
                value: veyra.exportTrimEnd > 0 ? veyra.exportTrimEnd : veyra.duration
                enabledControl: veyra.hasSource && !veyra.exportRunning && veyra.duration > 0
                onMoved: veyra.exportTrimEnd = value
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
