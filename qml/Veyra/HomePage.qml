// 首页, rebuilt from the design (pages-a.js PAGES.home + pages.css .home).
//
// The design is: a large logo, a two-line greeting, a 4-up grid of source cards
// (164x124 each), a "continue last capture" row, and a row of recent chips.
// The logo in the dock returns here; there is no other home affordance.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects

VPage {
    id: root
    signal requestPage(string page)

    ColumnLayout {
        anchors.centerIn: parent
        spacing: 24

        // .home .mark: 128px wide, drop-shadow(0 0 20px rgba(255,255,255,.35)) and the
        // 4.5s "breathe" loop: at 50% the glow is 30px at .55 and the mark scales 1.02.
        // MultiEffect blur is 0..1 of blurMax (32px): 20px = .625, 30px = .94.
        // The layer is padded 40px on every side so the glow has room (a layer on the
        // bare image clips the shadow to the image's own bounds). Negative margins
        // keep the layout the same as a plain 128px image.
        Item {
            id: mark
            Layout.alignment: Qt.AlignHCenter
            Layout.topMargin: -40
            Layout.bottomMargin: -40
            implicitWidth: logoImg.implicitWidth + 80
            implicitHeight: logoImg.implicitHeight + 80
            property real breath: 0
            scale: 1 + 0.02 * breath
            SequentialAnimation on breath {
                running: !Theme.reduced
                loops: Animation.Infinite
                NumberAnimation { to: 1; duration: Theme.d(2250); easing.type: Easing.InOutSine }
                NumberAnimation { to: 0; duration: Theme.d(2250); easing.type: Easing.InOutSine }
            }
            Image {
                id: logoImg
                anchors.centerIn: parent
                source: "logo.png"
                sourceSize.width: 128
                fillMode: Image.PreserveAspectFit
            }
            layer.enabled: true
            layer.effect: MultiEffect {
                shadowEnabled: true
                shadowColor: "#FFFFFF"
                shadowHorizontalOffset: 0
                shadowVerticalOffset: 0
                blurMax: 64
                shadowBlur: 0.625 + 0.3125 * mark.breath
                shadowOpacity: 0.35 + 0.2 * mark.breath
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
                // Subtitles name what is really there: the device of the last capture
                // session and the saved PS5 host; generic wording when there is none.
                model: [
                    { glyph: "folder", title: "打开视频", sub: "MP4 · MKV · 图片", act: "file" },
                    { glyph: "video", title: "采集卡",
                      sub: veyra.hasCaptureSession ? veyra.captureSessionSummary.split(" · ")[0] : "HDMI 采集设备", act: "capture" },
                    { glyph: "gamepad", title: "PS5 串流",
                      sub: veyra.remotePlayHost.length > 0 ? "已保存主机 " + veyra.remotePlayHost : "局域网串流", act: "ps5" },
                    { glyph: "monitor", title: "屏幕捕获", sub: "窗口或显示器", act: "screen" }
                ]
                // The layout owns the slot's position, so the card inside it is free to
                // move its own y for the hover lift (a y set on a layout child is
                // overwritten); the entrance rise goes on the slot.
                delegate: Item {
                    id: slot
                    required property var modelData
                    required property int index
                    implicitWidth: 164
                    implicitHeight: 124
                    VRise { page: root; target: slot; d: 2 + slot.index }
                  Rectangle {
                    id: srcCard
                    readonly property var modelData: slot.modelData
                    // .srccard: 164x124, radius 14, content pinned top and bottom.
                    width: 164
                    height: 124
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
        }

        // .resume: 692 wide, an accent-tinted gradient plate, shown only when a
        // capture session was actually used before. Nothing invented: the text is
        // the recorded session, and the row is absent when there is none.
        Rectangle {
            id: resume
            objectName: "home-resume"
            Layout.alignment: Qt.AlignHCenter
            // The last source actually used: capture card, PS5 or screen capture.
            readonly property var last: veyra.lastSource
            visible: last.kind !== undefined
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
                    VIcon {
                        anchors.centerIn: parent
                        name: resume.last.kind === "screen" ? "monitor" : resume.last.kind === "capture" ? "video" : "gamepad"
                        color: Theme.accent
                    }
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
                        text: resume.last.summary || ""
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }
                }
                VButton {
                    text: "开始"
                    iconName: "play"
                    primary: true
                    onClicked: veyra.resumeLastSource()
                }
            }
        }

        // .recent: 692 wide, left-aligned, wrapping; "最近" in --v-t3 then 28px chips
        // with a film/image icon. Present only when there are recent files; a chip
        // whose file is gone is dimmed and does nothing (clicking it cannot open it).
        Flow {
            id: recent
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: 692
            visible: veyra.recentFiles.length > 0
            spacing: 8
            Text {
                height: 28
                verticalAlignment: Text.AlignVCenter
                text: "最近"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 12
            }
            Repeater {
                model: veyra.recentFiles
                delegate: Rectangle {
                    id: chip
                    required property var modelData
                    readonly property bool isImage: /\.(png|jpe?g|bmp|webp|tiff?)$/i.test(modelData.path)
                    width: chipRow.implicitWidth + 22
                    height: 28
                    radius: 99
                    opacity: modelData.exists ? 1 : 0.45
                    color: chipHover.hovered && modelData.exists ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: Theme.stroke
                    Row {
                        id: chipRow
                        anchors.centerIn: parent
                        spacing: 7
                        VIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: chip.isImage ? "image" : "film"
                            size: 14
                            color: chipText.color
                        }
                        Text {
                            id: chipText
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: chipHover.hovered && modelData.exists ? Theme.t1 : Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                        }
                    }
                    ToolTip.visible: chipHover.hovered && !modelData.exists
                    ToolTip.text: "文件已不存在"
                    HoverHandler { id: chipHover; cursorShape: modelData.exists ? Qt.PointingHandCursor : Qt.ArrowCursor }
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
