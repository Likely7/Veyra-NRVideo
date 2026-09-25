// 设置: presets, subtitles/audio, and the engine's own diagnostics.
//
// The user asked for a real settings page (the old app buried things in the
// main window), and for presets to be ONE concept with selectable parts rather
// than separate NR presets. So a preset here carries whichever parts the user
// ticks, and applying it leaves the unticked parts alone.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    signal requestPage(string page)

    property int tab: 0

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "设置"
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsH1
                Layout.fillWidth: true
            }
        }

        // Tabs rather than one long scroll: the diagnostics report alone is long
        // enough to bury everything else.
        RowLayout {
            spacing: 6
            Repeater {
                model: ["预设", "字幕与音频", "诊断"]
                delegate: Rectangle {
                    required property int index
                    required property string modelData
                    implicitWidth: tabText.implicitWidth + 24
                    implicitHeight: 30
                    radius: 15
                    color: root.tab === index ? Theme.card3 : (tabHover.hovered ? Theme.card2 : "transparent")
                    Behavior on color { ColorAnimation { duration: Theme.durFast } }
                    Text {
                        id: tabText
                        anchors.centerIn: parent
                        text: modelData
                        color: root.tab === index ? Theme.t1 : Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    HoverHandler { id: tabHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.tab = index }
                }
            }
        }

        // --- presets -------------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.rCard
            color: Theme.card
            visible: root.tab === 0

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                Text {
                    text: "预设"
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsH3
                }
                Text {
                    Layout.fillWidth: true
                    text: "保存时勾选包含哪些部分；应用时只覆盖勾选的部分，其余保持当前设置。"
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                ListView {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: veyra.presets
                    spacing: 4
                    delegate: Rectangle {
                        required property var modelData
                        width: ListView.view.width
                        height: 40
                        radius: 8
                        color: rowHover.hovered ? Theme.card2 : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10
                            Text {
                                Layout.fillWidth: true
                                text: modelData.name
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                                elide: Text.ElideRight
                            }
                            Text {
                                visible: modelData.builtin
                                text: "内置"
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: 10
                            }
                            Text {
                                visible: modelData.nodeMode
                                text: "节点"
                                color: Theme.exp
                                font.family: Theme.fontUi
                                font.pixelSize: 10
                            }
                        }
                        HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: veyra.applyPresetIndex(modelData.index) }
                    }
                }
            }
        }

        // --- subtitles and audio -------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.rCard
            color: Theme.card
            visible: root.tab === 1

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 12

                Text {
                    text: "音频"
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsH3
                }
                RowLayout {
                    spacing: 10
                    Text { text: "音量"; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall }
                    Slider {
                        implicitWidth: 200
                        from: 0; to: 1; value: veyra.volume
                        onMoved: veyra.volume = value
                    }
                    Text {
                        text: Math.round(veyra.volume * 100) + "%"
                        color: Theme.t2
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fsSmall
                    }
                }
                Switch {
                    text: "静音"
                    checked: veyra.muted
                    onToggled: veyra.muted = checked
                }
                RowLayout {
                    spacing: 10
                    Text { text: "音频偏移"; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall }
                    SpinBox {
                        from: -2000
                        to: 2000
                        stepSize: 10
                        value: veyra.audioOffsetMs
                        onValueModified: veyra.audioOffsetMs = value
                    }
                    Text { text: "毫秒"; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.stroke }

                Text {
                    text: "音轨"
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsH3
                }
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
                        implicitHeight: 32
                        radius: 8
                        color: trackHover.hovered ? Theme.card2 : "transparent"
                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 10
                            spacing: 8
                            Rectangle {
                                width: 8; height: 8; radius: 4
                                color: veyra.selectedAudioTrack === modelData.index ? Theme.accent : Theme.t3
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.label
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                            }
                        }
                        HoverHandler { id: trackHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: veyra.selectedAudioTrack = modelData.index }
                    }
                }

                Item { Layout.fillHeight: true }
            }
        }

        // --- diagnostics ---------------------------------------------------
        Rectangle {
            Layout.fillWidth: true
            Layout.fillHeight: true
            radius: Theme.rCard
            color: Theme.card
            visible: root.tab === 2

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 16
                spacing: 10

                Text {
                    text: "诊断"
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsH3
                }
                // Every line comes from the engine's own snapshot. Submit FPS is
                // labelled as submit FPS: there is no display-FPS number here,
                // because we cannot measure one.
                // A Flickable + Text rather than a TextArea: the platform style
                // refuses to let a TextArea repaint its own background, and the
                // report is read-only text, so a plain Text does the job without
                // fighting the style.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: 8
                    color: Theme.card2
                    clip: true
                    Flickable {
                        anchors.fill: parent
                        anchors.margins: 10
                        contentWidth: width
                        contentHeight: reportText.implicitHeight
                        clip: true
                        ScrollBar.vertical: ScrollBar { }
                        Text {
                            id: reportText
                            width: parent.width
                            text: veyra.diagnosticsReport()
                            color: Theme.t2
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsSmall
                            wrapMode: Text.Wrap
                        }
                    }
                }
            }
        }
    }
}
