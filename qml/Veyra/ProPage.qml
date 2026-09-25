// 专业模式: the video on the left, the chain and its parameters on the right.
//
// This is list mode. Per the user's decision it does NOT allow reordering — only
// adding layers and toggling them — because a dragged chain is how beginners
// break the pipeline. Free arrangement lives in node mode, which is a separate
// page rather than a toggle inside this one.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    signal requestPage(string page)

    property int selectedEffect: -1

    RowLayout {
        anchors.fill: parent
        spacing: 0

        // --- video + transport ------------------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 0

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                // The native video window is placed over this item by main.cpp.
                // Only the visible page may own it, or two pages would fight for
                // the same HWND.
                Item {
                    id: videoHost
                    objectName: "videoHost"
                    anchors.fill: parent
                    visible: root.visible
                }
                Text {
                    anchors.centerIn: parent
                    visible: !veyra.hasSource
                    text: veyra.statusText
                    color: Theme.text2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fontSizeTitle
                }
            }

            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 44
                color: Qt.rgba(0.07, 0.07, 0.08, 0.9)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 14
                    spacing: 10
                    Text {
                        text: veyra.paused || !veyra.running ? "▶" : "❚❚"
                        color: Theme.text1
                        font.pixelSize: 16
                        HoverHandler { cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: veyra.togglePlayPause() }
                    }
                    Text {
                        text: veyra.positionText
                        color: Theme.text2
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeSmall
                    }
                    Slider {
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, veyra.duration)
                        value: veyra.position
                        onMoved: if (!pressed) veyra.seekTo(value)
                    }
                    Text {
                        text: veyra.durationText
                        color: Theme.text2
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }

        // --- effect panel --------------------------------------------------
        Rectangle {
            Layout.preferredWidth: 360
            Layout.fillHeight: true
            color: Theme.card

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 14
                spacing: 12

                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "效果链"
                        color: Theme.text1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fontSizeTitle
                        Layout.fillWidth: true
                    }
                    // Node mode is a different page, and the button says so.
                    Rectangle {
                        implicitWidth: 72
                        implicitHeight: 26
                        radius: 13
                        color: nodeHover.hovered ? Theme.card3 : Theme.card2
                        Text {
                            anchors.centerIn: parent
                            text: "节点模式"
                            color: Theme.text2
                            font.family: Theme.fontUi
                            font.pixelSize: 11
                        }
                        HoverHandler { id: nodeHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: root.requestPage("node") }
                    }
                }

                ChainList {
                    id: chainList
                    Layout.fillWidth: true
                    onSelectedChanged: root.selectedEffect = selected
                }

                // Adding a stage. The engine refuses anything that would break
                // the pipeline order and explains why under the list.
                Text {
                    text: "添加效果"
                    color: Theme.text3
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fontSizeSmall
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: 6
                    Repeater {
                        model: veyra.effectCatalog
                        delegate: Rectangle {
                            required property var modelData
                            width: chipText.implicitWidth + 20
                            height: 28
                            radius: 14
                            color: chipHover.hovered ? Theme.card3 : Theme.card2
                            border.width: 1
                            border.color: modelData.mustBeLast ? Theme.stroke2 : "transparent"
                            Text {
                                id: chipText
                                anchors.centerIn: parent
                                text: "+ " + modelData.label
                                color: modelData.experimental ? Theme.experimental : Theme.text2
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fontSizeSmall
                            }
                            HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: chainList.addEffect(modelData.id) }
                        }
                    }
                }

                // Parameters of the selected stage. These are the NR controls;
                // the panel is only shown when a stage is selected so the user is
                // never shown controls that do not apply to what is selected.
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    radius: Theme.radiusCard
                    color: Theme.card2
                    visible: root.selectedEffect >= 0
                    clip: true

                    Flickable {
                        anchors.fill: parent
                        anchors.margins: 12
                        contentHeight: params.implicitHeight
                        clip: true
                        ColumnLayout {
                            id: params
                            width: parent.width
                            spacing: 8

                            Text {
                                text: "参数"
                                color: Theme.text2
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fontSizeSmall
                            }
                            GridLayout {
                                Layout.fillWidth: true
                                columns: 2
                                columnSpacing: 10
                                rowSpacing: 4

                                Text { text: "强度"; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fontSizeSmall }
                                Slider {
                                    Layout.fillWidth: true
                                    from: 0; to: 2; value: veyra.nrIntensity
                                    onMoved: veyra.nrIntensity = value
                                }
                                Text { text: "色调"; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fontSizeSmall }
                                Slider {
                                    Layout.fillWidth: true
                                    from: 0; to: 2; value: veyra.nrTone
                                    onMoved: veyra.nrTone = value
                                }
                                Text { text: "结构"; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fontSizeSmall }
                                Slider {
                                    Layout.fillWidth: true
                                    from: 0; to: 2; value: veyra.nrStructure
                                    onMoved: veyra.nrStructure = value
                                }
                                Text { text: "肤色"; color: Theme.text2; font.family: Theme.fontUi; font.pixelSize: Theme.fontSizeSmall }
                                Slider {
                                    Layout.fillWidth: true
                                    from: -1; to: 1; value: veyra.nrSkin
                                    onMoved: veyra.nrSkin = value
                                }
                            }

                            Text {
                                Layout.topMargin: 6
                                text: "时间域防闪烁"
                                color: Theme.text2
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fontSizeSmall
                            }
                            Switch {
                                text: "在 NR 后稳定残差（默认关闭）"
                                checked: veyra.nrTemporal
                                onToggled: veyra.nrTemporal = checked
                            }
                        }
                    }
                }
            }
        }
    }
}
