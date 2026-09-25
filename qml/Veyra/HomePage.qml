// The empty state: what the app shows before anything is open.
//
// Clicking the logo anywhere returns here, so this is the anchor of the whole
// interface. Recent files and the three ways to get a picture in are all here;
// nothing is buried in a menu.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    signal requestPage(string page)

    // The logo sits behind everything, large and faint: the empty state should
    // feel like the product, not like an error page.
    Text {
        anchors.centerIn: parent
        anchors.verticalCenterOffset: -60
        text: "V"
        color: Theme.text1
        opacity: 0.04
        font.family: Theme.fontUi
        font.bold: true
        font.pixelSize: 260
    }

    ColumnLayout {
        anchors.centerIn: parent
        width: Math.min(parent.width - 96, 620)
        spacing: 18

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: "Veyra"
            color: Theme.text1
            font.family: Theme.fontUi
            font.pixelSize: 34
            font.bold: true
            font.letterSpacing: 2
        }
        Text {
            Layout.alignment: Qt.AlignHCenter
            text: veyra.statusText
            color: Theme.text2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fontSizeBody
        }

        // The three sources. Each opens its own dialog; capture and PS5 carry
        // their own settings pages rather than being folded into this one.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: 10
            spacing: 10
            Repeater {
                model: [
                    { label: "打开文件", hint: "视频或图片", action: "file" },
                    { label: "采集卡", hint: "HDMI 输入", action: "capture" },
                    { label: "PS5 串流", hint: "局域网", action: "ps5" },
                    { label: "屏幕捕获", hint: "窗口或显示器", action: "screen" }
                ]
                delegate: Rectangle {
                    required property var modelData
                    implicitWidth: 136
                    implicitHeight: 84
                    radius: Theme.radiusCard
                    color: hover.hovered ? Theme.card2 : Theme.card
                    border.width: 1
                    border.color: hover.hovered ? Theme.accent : Theme.stroke
                    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
                    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 2
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData.label
                            color: Theme.text1
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fontSizeBody
                        }
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: modelData.hint
                            color: Theme.text3
                            font.family: Theme.fontUi
                            font.pixelSize: 10
                        }
                    }
                    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        onTapped: {
                            switch (modelData.action) {
                            case "file": veyra.openFileDialog(); break
                            case "capture": veyra.openCaptureDialog(); break
                            case "ps5": veyra.openPs5Dialog(); break
                            case "screen": veyra.openScreenCaptureDialog(); break
                            }
                        }
                    }
                }
            }
        }

        // Recent files. Shown only when there are some, so a first run is clean.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 14
            spacing: 4
            visible: veyra.recentFiles.length > 0
            Text {
                text: "最近打开"
                color: Theme.text3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSizeSmall
            }
            Repeater {
                model: veyra.recentFiles
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 30
                    radius: 6
                    color: rowHover.hovered ? Theme.card2 : "transparent"
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        Text {
                            Layout.fillWidth: true
                            text: modelData.label
                            color: modelData.exists ? Theme.text1 : Theme.text3
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fontSizeBody
                            elide: Text.ElideMiddle
                        }
                        Text {
                            visible: !modelData.exists
                            text: "已移动"
                            color: Theme.text3
                            font.family: Theme.fontUi
                            font.pixelSize: 10
                        }
                    }
                    HoverHandler { id: rowHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: if (modelData.exists) veyra.openPath(modelData.path) }
                }
            }
        }
    }
}
