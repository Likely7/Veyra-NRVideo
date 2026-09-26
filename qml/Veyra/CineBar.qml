// The cinema control pill (pages-a.js .cine-bar, pages.css .cine-bar), shared by the
// minimal page and the fullscreen control window (G2.5). Only the pill: where it
// sits and how it enters belong to the owner.
//
// .cine-bar: 3 columns (230px | 1fr | 230px), 92px tall, radius 30. The design's
// frosted glass cannot sample a native video window, so the fill is the opaque
// approximation rgba(22,22,26,.86) (D5).
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: bar
    signal requestPage(string page)
    signal requestFullscreen()
    signal requestLock()
    // Set on the fullscreen bar: shows the lock button.
    property bool fullscreen: false
    // The preset menu is open: an owner window that hides the bar on a timer waits.
    readonly property bool menuOpen: presetMenu.visible
    // The pointer is over the pill.
    readonly property bool hovered: barHover.hovered
    HoverHandler { id: barHover }
    implicitHeight: 92
    radius: 30
    color: Qt.rgba(22 / 255, 22 / 255, 26 / 255, 0.86)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.1)

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
                onClicked: presetMenu.openAt(this, "up")
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

    // The preset menu: list presets and node presets in one menu, as designed.
    // pages-a.js presetMenu: list presets, node presets, then the way to manage them.
    VMenu {
        id: presetMenu
        title: "预设"
        readonly property var mk: p => ({ label: p.name, note: p.note, checked: p.name === veyra.currentPresetName,
                                          tag: p.nodeMode ? "节点" : "", preset: p.index })
        items: [{ head: "列表预设" }].concat(veyra.presets.filter(p => !p.nodeMode).map(mk))
            .concat([{ head: "节点预设" }]).concat(veyra.presets.filter(p => p.nodeMode).map(mk))
            .concat([{ sep: true }, { label: "去专业模式管理预设…", icon: "sliders", act: "pro" }])
        onPicked: (i, o) => {
            if (o.act === "pro") bar.requestPage("pro")
            else veyra.applyPresetIndex(o.preset)
        }
    }
}
