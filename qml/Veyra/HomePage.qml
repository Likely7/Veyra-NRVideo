// 首页, rebuilt from the design (pages-a.js PAGES.home + pages.css .home).
//
// The design is: a large logo, a two-line greeting, a 4-up grid of source cards
// (164x124 each), a "continue last capture" row, and a row of recent chips.
// The logo in the dock returns here; there is no other home affordance.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

VPage {
    id: root
    signal requestPage(string page)

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 24

        // .home .mark: 128px wide, with a slow "breathe" glow.
        Image {
            id: mark
            Layout.alignment: Qt.AlignHCenter
            source: "logo.png"
            sourceSize.width: 128
            fillMode: Image.PreserveAspectFit
            opacity: 0.9
            SequentialAnimation on scale {
                running: !Theme.reduced
                loops: Animation.Infinite
                NumberAnimation { to: 1.02; duration: Theme.d(2250); easing.type: Easing.InOutSine }
                NumberAnimation { to: 1.0; duration: Theme.d(2250); easing.type: Easing.InOutSine }
            }
        }

        // .hello: .h1 plus a muted line.
        ColumnLayout {
            id: hello
            Layout.alignment: Qt.AlignHCenter
            spacing: 6
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "今天看点什么？"
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsH1
                font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
            }
            Text {
                Layout.alignment: Qt.AlignHCenter
                text: "选一个片源开始，画质预设和补帧随时在播放栏切换"
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
            }
        }

        // .srcgrid: repeat(4, 164px), gap 12.
        RowLayout {
            Layout.alignment: Qt.AlignHCenter
            spacing: 12
            Repeater {
                model: [
                    { glyph: "folder", title: "打开视频", sub: "MP4 · MKV · 图片", act: "file" },
                    { glyph: "video", title: "采集卡", sub: "HDMI 采集设备", act: "capture" },
                    { glyph: "gamepad", title: "PS5 串流", sub: "局域网串流", act: "ps5" },
                    { glyph: "monitor", title: "屏幕捕获", sub: "窗口或显示器", act: "screen" }
                ]
                delegate: Rectangle {
                    id: srcCard
                    required property var modelData
                    required property int index
                    VRise { page: root; target: srcCard; d: 2 + srcCard.index }
                    // .srccard: 164x124, radius 14, content pinned top and bottom.
                    implicitWidth: 164
                    implicitHeight: 124
                    radius: Theme.rCard
                    color: cardHover.hovered ? Theme.card2 : Theme.card
                    border.width: 1
                    border.color: cardHover.hovered ? Theme.stroke2 : Theme.stroke
                    // .srccard:hover { transform: translateY(-4px) }
                    y: cardHover.hovered ? -4 : 0
                    Behavior on y { NumberAnimation { duration: Theme.d(500); easing.bezierCurve: Theme.spring } }
                    scale: cardTap.pressed ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.d(500); easing.bezierCurve: Theme.spring } }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 0
                        Rectangle {
                            // .srccard .ico: 34x34, radius 10, translucent plate.
                            implicitWidth: 34
                            implicitHeight: 34
                            radius: 10
                            color: Qt.rgba(1, 1, 1, 0.06)
                            VIcon {
                                anchors.centerIn: parent
                                name: modelData.glyph
                            }
                        }
                        Item { Layout.fillHeight: true }
                        ColumnLayout {
                            spacing: 3
                            Text {
                                text: modelData.title
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                                font.weight: Font.Medium
                            }
                            Text {
                                text: modelData.sub
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSmall
                            }
                        }
                    }
                    HoverHandler { id: cardHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler {
                        id: cardTap
                        onTapped: {
                            switch (modelData.act) {
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

        // .resume: 692 wide, an accent-tinted gradient plate, shown only when a
        // capture session was actually used before. Nothing invented: the text is
        // the recorded session, and the row is absent when there is none.
        Rectangle {
            id: resume
            Layout.alignment: Qt.AlignHCenter
            visible: veyra.hasCaptureSession
            implicitWidth: 692
            implicitHeight: 54
            radius: 14
            border.width: 1
            border.color: Theme.stroke
            gradient: Gradient {
                orientation: Gradient.Horizontal
                GradientStop { position: 0.0; color: Theme.accentSoft }
                GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.03) }
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 10
                spacing: 14
                Rectangle {
                    implicitWidth: 34; implicitHeight: 34; radius: 10
                    color: Theme.accentSoft
                    VIcon { anchors.centerIn: parent; name: "gamepad"; color: Theme.accent }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: "继续上次"
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.captureSessionSummary
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
                VButton {
                    text: "开始"
                    primary: true
                    onClicked: veyra.resumeCaptureSession()
                }
            }
        }

        // .recent: a row of chips, present only when there are recent files.
        RowLayout {
            id: recent
            Layout.alignment: Qt.AlignHCenter
            visible: veyra.recentFiles.length > 0
            spacing: 8
            Text {
                text: "最近"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
            }
            Repeater {
                model: veyra.recentFiles
                delegate: Rectangle {
                    required property var modelData
                    implicitWidth: chipText.implicitWidth + 22
                    implicitHeight: 28
                    radius: 99
                    color: chipHover.hovered ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: Theme.stroke
                    Text {
                        id: chipText
                        anchors.centerIn: parent
                        text: modelData.label
                        color: chipHover.hovered ? Theme.t1 : Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: 12
                        elide: Text.ElideMiddle
                    }
                    HoverHandler { id: chipHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: if (modelData.exists) veyra.openPath(modelData.path) }
                }
            }
        }
    }

    // [data-in] entrance order from pages-a.js (the source cards rise from their delegate, --d 2-5).
    VRise { target: mark; d: 0 }
    VRise { target: hello; d: 1 }
    VRise { target: resume; d: 6 }
    VRise { target: recent; d: 7 }
}
