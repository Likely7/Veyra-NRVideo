// 极简模式, rebuilt from the design (pages-a.js PAGES.min + pages.css .min/.cine-bar).
//
// The design's whole point here: the window snaps to the film's aspect ratio (no
// letterbox bars), and a large rounded control pill straddles the bottom edge of
// the picture - half on the picture, half below it. The pill is a 3-column grid
// (230px | 1fr | 230px) at min(860px, 100% - 48px), 92px tall, radius 30.
//
// The window resize itself is Main.qml's job (it owns the window); this page only
// reports the film aspect and draws the bar.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    signal requestPage(string page)

    // Where the picture ends and the pill straddles. Main.qml sets the window
    // height to pictureHeight + 46, so the bar's top edge is pictureHeight - 46.
    // The picture is the window minus the control bar's full height: the bar
    // sits below the picture rather than straddling it, because a native video
    // window always draws above the QML scene.
    readonly property real pictureHeight: parent ? parent.height - 92 : 0

    // --- the picture ------------------------------------------------------
    // .min .stage: full width, the picture height, radius 8 (the window radius),
    // pure black behind. The native video window is placed here by main.cpp; the
    // radius applies to the black plate, never to the video, because rounding the
    // picture crops it.
    Rectangle {
        id: stage
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: root.pictureHeight
        radius: Theme.rWindow
        color: Theme.videoBlack
        clip: true

        Text {
            anchors.centerIn: parent
            visible: !veyra.hasSource
            text: "选择一个片源开始"
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsH3
        }
    }

    // --- the control pill -------------------------------------------------
    // .cine-bar: min(860px, 100% - 48px), 92px, radius 30, top = pictureHeight-46.
    Rectangle {
        id: cineBar
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(860, root.width - 48)
        height: 92
        y: root.pictureHeight
        radius: 30
        color: Qt.rgba(22 / 255, 22 / 255, 26 / 255, 0.86)
        border.width: 1
        border.color: Qt.rgba(1, 1, 1, 0.1)
        visible: veyra.hasSource

        // barIn: opacity 0, translate 30px, scale .94 -> settled
        opacity: 0
        SequentialAnimation on opacity {
            running: cineBar.visible
            PauseAnimation { duration: Theme.d(120) }
            NumberAnimation { to: 1.0; duration: Theme.d(800); easing.bezierCurve: Theme.spring }
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 14
            anchors.rightMargin: 18
            spacing: 18

            // --- left: artwork + title (230px) ---------------------------
            RowLayout {
                Layout.preferredWidth: 230
                Layout.fillHeight: true
                spacing: 10
                Rectangle {
                    implicitWidth: 46; implicitHeight: 46; radius: 12
                    color: Theme.videoBlack
                    border.width: 1
                    border.color: Theme.stroke
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.alignment: Qt.AlignVCenter
                    spacing: 2
                    Text {
                        Layout.fillWidth: true
                        text: veyra.sourceName.length > 0 ? veyra.sourceName : "未打开"
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: 13
                        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: veyra.sourceSummary
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 11
                        elide: Text.ElideRight
                    }
                }
            }

            // --- middle: transport + seek --------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 4

                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 10

                    // .cbtn: 32px round, muted, brightens on hover.
                    component CBtn: Item {
                        id: cbtn
                        property string glyph: ""
                        property string tip: ""
                        // The signal has to be declared: an inline component does not
                        // inherit the TapHandler's signal, so callers cannot assign
                        // onTapped without it.
                        signal tapped()
                        implicitWidth: 32
                        implicitHeight: 32
                        Rectangle {
                            anchors.fill: parent
                            radius: 16
                            color: cHover.hovered ? Qt.rgba(1, 1, 1, 0.08) : "transparent"
                        }
                        VIcon {
                            anchors.centerIn: parent
                            name: glyph
                            color: cHover.hovered ? "#FFFFFF" : Theme.t2
                        }
                        scale: cTap.pressed ? 0.86 : 1.0
                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                        HoverHandler { id: cHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { id: cTap; onTapped: cbtn.tapped() }
                    }

                    CBtn { glyph: "cc"; onTapped: {} }
                    CBtn { glyph: "back10"; onTapped: veyra.seekBy(-10) }

                    // .play: 40px white circle with the play/pause glyph.
                    Item {
                        implicitWidth: 40
                        implicitHeight: 40
                        Rectangle {
                            anchors.fill: parent
                            radius: 20
                            color: "#FFFFFF"
                        }
                        VIcon {
                            anchors.centerIn: parent
                            name: (veyra.running && !veyra.paused) ? "pausefill" : "playfill"
                            filled: true
                            color: "#0A0A0C"
                        }
                        scale: playTap.pressed ? 0.88 : (playHover.hovered ? 1.06 : 1.0)
                        Behavior on scale { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } }
                        HoverHandler { id: playHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { id: playTap; onTapped: veyra.togglePlayPause() }
                    }

                    CBtn { glyph: "fwd10"; onTapped: veyra.seekBy(10) }
                    CBtn { glyph: "music"; onTapped: {} }
                }

                // .seekrow: mono times either side of the rail.
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 10
                    Text {
                        text: veyra.positionText
                        color: Theme.t3
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                    }
                    Item {
                        Layout.fillWidth: true
                        implicitHeight: 14
                        Rectangle {
                            id: rail
                            anchors.verticalCenter: parent.verticalCenter
                            width: parent.width
                            height: seekHover.hovered ? 6 : 3
                            radius: 9
                            color: Qt.rgba(1, 1, 1, 0.14)
                            Behavior on height { NumberAnimation { duration: Theme.d(300); easing.bezierCurve: Theme.spring } }
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, veyra.progress))
                                height: parent.height
                                radius: 9
                                color: "#FFFFFF"
                            }
                        }
                        HoverHandler { id: seekHover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            onTapped: point => {
                                const f = Math.max(0, Math.min(1, point.position.x / width))
                                veyra.seekTo(f * veyra.duration)
                            }
                        }
                    }
                    Text {
                        text: veyra.durationText
                        color: Theme.t3
                        font.family: Theme.fontMono
                        font.pixelSize: 10
                    }
                }
            }

            // --- right: preset, volume, fullscreen (230px) ---------------
            RowLayout {
                Layout.preferredWidth: 230
                Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
                spacing: 6

                // .pill with a status dot: the dot reports the engine's state, and
                // the label is the preset actually in use.
                VPill {
                    key: ""
                    value: veyra.currentPresetName
                    onClicked: presetMenu.popup()
                    Rectangle {
                        width: 7; height: 7; radius: 3.5
                        color: veyra.failed ? Theme.err : veyra.captureRecovering ? Theme.warn : Theme.ok
                    }
                }

                Item {
                    implicitWidth: 92
                    implicitHeight: 20
                    RowLayout {
                        anchors.fill: parent
                        spacing: 6
                        VIcon { name: "vol"; color: Theme.t2 }
                        VSlider {
                            Layout.fillWidth: true
                            from: 0; to: 1; value: veyra.volume
                            onMoved: veyra.volume = value
                        }
                    }
                }

                Item {
                    implicitWidth: 32; implicitHeight: 32
                    VIcon {
                        anchors.centerIn: parent
                        name: "max"
                        color: Theme.t2
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.requestPage("pro") }
                }
            }
        }
    }

    // The preset menu: list presets and node presets in one menu, as designed.
    Menu {
        id: presetMenu
        width: 260
        Repeater {
            model: veyra.presets
            delegate: MenuItem {
                required property var modelData
                text: modelData.name + (modelData.builtin ? "  （内置）" : "")
                         + (modelData.nodeMode ? "  · 节点" : "")
                onTriggered: veyra.applyPresetIndex(modelData.index)
            }
        }
    }

    // Report the film aspect upward so the window can snap to it.
    onPictureHeightChanged: if (veyra.sourceAspect > 0.2) root.requestAspect(veyra.sourceAspect)
    signal requestAspect(real aspect)
}
