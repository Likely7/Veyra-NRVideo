// 视频导出: its own simplified mode, as the user asked.
//
// Scope honesty: this page exports what the engine can export today — the
// enhancement chain applied to the current file, encoded by NVENC through the
// existing export job. Output resolution is NOT offered as a control, because
// the engine has no export-size setting yet (it derives the size from the NR
// size policy); showing a resolution picker that changed nothing would be a lie
// about the product. The missing pieces are recorded in the execution doc.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    signal requestPage(string page)

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 28
        spacing: 14

        RowLayout {
            Layout.fillWidth: true
            Text {
                text: "视频导出"
                color: Theme.text1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSizeHeading
                Layout.fillWidth: true
            }
            Rectangle {
                implicitWidth: 68
                implicitHeight: 28
                radius: 14
                color: backHover.hovered ? Theme.card3 : Theme.card2
                Text {
                    anchors.centerIn: parent
                    text: "返回"
                    color: Theme.text2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fontSizeSmall
                }
                HoverHandler { id: backHover; cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: root.requestPage("pro") }
            }
        }

        Rectangle {
            Layout.fillWidth: true
            radius: Theme.radiusCard
            color: Theme.card
            implicitHeight: detail.implicitHeight + 32

            ColumnLayout {
                id: detail
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 16
                spacing: 10

                GridLayout {
                    Layout.fillWidth: true
                    columns: 2
                    columnSpacing: 12
                    rowSpacing: 10

                    Text { text: "源"; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fontSizeSmall }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.hasSource ? veyra.sourceName + "  " + veyra.sourceSummary : "未打开文件"
                        color: Theme.text1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fontSizeBody
                        elide: Text.ElideMiddle
                    }

                    Text { text: "输出"; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fontSizeSmall }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            Layout.fillWidth: true
                            text: veyra.exportTarget.length > 0 ? veyra.exportTarget : "未选择"
                            color: veyra.exportTarget.length > 0 ? Theme.text1 : Theme.text3
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fontSizeBody
                            elide: Text.ElideMiddle
                        }
                        Rectangle {
                            implicitWidth: 62
                            implicitHeight: 26
                            radius: 13
                            color: pickHover.hovered ? Theme.card3 : Theme.card2
                            Text {
                                anchors.centerIn: parent
                                text: "选择…"
                                color: Theme.text2
                                font.family: Theme.fontUi
                                font.pixelSize: 11
                            }
                            HoverHandler { id: pickHover; cursorShape: Qt.PointingHandCursor }
                            TapHandler { onTapped: veyra.chooseExportPath() }
                        }
                    }

                    Text { text: "编码"; color: Theme.text3; font.family: Theme.fontUi; font.pixelSize: Theme.fontSizeSmall }
                    Text {
                        text: "NVENC H.264（D3D12 直接编码，不经 CPU 回读）"
                        color: Theme.text2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }

                Rectangle { Layout.fillWidth: true; implicitHeight: 1; color: Theme.stroke }

                // A short, explicit note about what this page cannot do yet. The
                // project rule is to say so rather than ship a control that does
                // nothing.
                Text {
                    Layout.fillWidth: true
                    text: "导出尺寸目前由 NR 处理尺寸策略决定；自定义输出分辨率尚未实现。"
                    color: Theme.text3
                    font.family: Theme.fontUi
                    font.pixelSize: 11
                    wrapMode: Text.WordWrap
                }

                RowLayout {
                    Layout.topMargin: 4
                    spacing: 10
                    Rectangle {
                        implicitWidth: 108
                        implicitHeight: 34
                        radius: 17
                        color: startHover.hovered ? Qt.lighter(Theme.accent, 1.1) : Theme.accent
                        Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                        Text {
                            anchors.centerIn: parent
                            text: veyra.exportRunning ? "导出中…" : "开始导出"
                            color: Theme.accentInk
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fontSizeBody
                            font.bold: true
                        }
                        HoverHandler { id: startHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: veyra.startExport() }
                    }
                    Rectangle {
                        implicitWidth: 88
                        implicitHeight: 34
                        radius: 17
                        visible: veyra.exportRunning
                        color: cancelHover.hovered ? Theme.card3 : Theme.card2
                        Text {
                            anchors.centerIn: parent
                            text: "取消"
                            color: Theme.text2
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fontSizeBody
                        }
                        HoverHandler { id: cancelHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: veyra.cancelExport() }
                    }
                    Text {
                        text: veyra.exportStatus
                        color: Theme.text2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fontSizeSmall
                    }
                }
            }
        }

        Item { Layout.fillHeight: true }
    }
}
