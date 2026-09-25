// 极简模式: the video, and almost nothing else.
//
// Two rules the user set that drive this whole file:
//   * NO black bars around the video. The video host fills the page and the
//     controls sit ON it, fading with the pointer, rather than reserving a band
//     that shows the window background.
//   * "画质" is called 预设, because that is what it is: a preset, not a single
//     quality slider.
//
// The video is a native D3D12 window placed over the `videoHost` item by
// apps/veyra-qml/main.cpp. This page only says where that item goes.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    signal requestPage(string page)

    property bool chromeVisible: true
    Timer {
        id: hideChrome
        interval: 2600
        onTriggered: if (!pointerHover.hovered && !progressHover.hovered) root.chromeVisible = false
    }

    HoverHandler {
        id: pointerHover
        onHoveredChanged: {
            root.chromeVisible = hovered
            if (hovered) hideChrome.restart()
        }
    }

    // The video itself. Square corners, no radius, no margin: rounding or
    // insetting this would crop or letterbox the picture.
    Item {
        id: videoHost
        objectName: "videoHost"
        anchors.fill: parent
    }

    // The picture is letterboxed by the engine into this area, so the page
    // background is only visible while nothing is open.
    Text {
        anchors.centerIn: parent
        visible: !veyra.hasSource
        text: veyra.statusText
        color: Theme.text2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fontSizeTitle
    }

    // --- controls, floating on the picture --------------------------------
    ColumnLayout {
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 8
        opacity: root.chromeVisible ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.durationNormal; easing.bezierCurve: Theme.easeOut } }

        TimingBar {
            Layout.fillWidth: true
            visible: veyra.running
            stages: [
                { label: "调度", ms: veyra.lateMs, color: Theme.warn, measured: veyra.submitFpsKnown },
                { label: "处理", ms: veyra.lateP95Ms, color: Theme.accent, measured: veyra.submitFpsKnown },
                { label: "呈现", ms: 0, color: Theme.ok, measured: false }
            ]
        }

        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 56
            radius: Theme.radiusCard
            color: Qt.rgba(0.07, 0.07, 0.08, 0.82)

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 14
                spacing: 12

                // Play / pause. The spring scale is the one place the interface
                // is allowed to be playful.
                Text {
                    text: veyra.paused || !veyra.running ? "▶" : "❚❚"
                    color: Theme.text1
                    font.pixelSize: 18
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: veyra.togglePlayPause() }
                    scale: playTap.pressed ? 0.85 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.durationNormal; easing.bezierCurve: Theme.spring } }
                    TapHandler { id: playTap }
                }

                Text {
                    text: veyra.positionText
                    color: Theme.text2
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeSmall
                }

                // Seek bar. Dragging seeks on release, not continuously: a seek
                // per pixel would queue rebuilds the engine cannot keep up with.
                Slider {
                    id: seek
                    Layout.fillWidth: true
                    from: 0
                    to: Math.max(1, veyra.duration)
                    value: veyra.position
                    onMoved: if (!pressed) veyra.seekTo(value)
                    HoverHandler { id: progressHover; cursorShape: Qt.PointingHandCursor }
                }

                Text {
                    text: veyra.durationText
                    color: Theme.text2
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fontSizeSmall
                }

                // 预设, not 画质.
                Rectangle {
                    implicitWidth: presetRow.implicitWidth + 18
                    implicitHeight: 30
                    radius: 15
                    color: presetHover.hovered ? Theme.card3 : Theme.card2
                    RowLayout {
                        id: presetRow
                        anchors.centerIn: parent
                        spacing: 6
                        Text { text: "预设"; color: Theme.text1; font.family: Theme.fontUi; font.pixelSize: Theme.fontSizeSmall }
                        Text { text: "▾"; color: Theme.text3; font.pixelSize: 10 }
                    }
                    HoverHandler { id: presetHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.requestPage("settings") }
                }

                Text {
                    text: "⛶"
                    color: Theme.text2
                    font.pixelSize: 16
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.requestPage("pro") }
                }
            }
        }
    }
}
