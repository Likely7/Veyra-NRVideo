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
    // Set by FullscreenBar so upward menus stop above the playback pill.
    property real menuBottomLimit: -1
    // Any popover needs the owner window's full mask, not just the preset menu.
    readonly property bool menuOpen: presetMenu.visible || ccMenu.visible || audioMenu.visible
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

                CBtn { glyph: "cc"; onTapped: ccMenu.openAt(this, "up") }
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
                CBtn { glyph: "music"; onTapped: audioMenu.openAt(this, "up") }
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
                    id: seekArea
                    Layout.fillWidth: true
                    implicitHeight: 14
                    // .seek:hover .thumb { left: 44% } - the thumb tracks the real
                    // progress, not the design's hard-coded 44%.
                    readonly property real frac: Math.max(0, Math.min(1, veyra.progress))
                    readonly property real hoverFrac: seekHover.hovered && width > 0
                                                      ? Math.max(0, Math.min(1, seekHover.point.position.x / width))
                                                      : frac
                    Rectangle {
                        id: rail
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: seekHover.hovered ? 6 : 3
                        radius: 9
                        color: Qt.rgba(1, 1, 1, 0.14)
                        Behavior on height { NumberAnimation { duration: Theme.d(300); easing.bezierCurve: Theme.spring } }
                        Rectangle {
                            width: parent.width * seekArea.frac
                            height: parent.height
                            radius: 9
                            color: "#FFFFFF"
                        }
                    }
                    // .seek .thumb: 11px dot, scale 0 -> 1 over .4s --spring on hover.
                    Rectangle {
                        width: 11; height: 11; radius: 5.5
                        color: "#FFFFFF"
                        x: seekArea.frac * seekArea.width - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        scale: seekHover.hovered ? 1 : 0
                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                    }
                    // .seek .peek: 144x104 above the rail, opacity .15s, scale .85 -> 1
                    // over .4s --spring from its bottom centre. The design shows a frame
                    // of the target position; the engine keeps no frame the UI can read
                    // (only save-to-file), so the scene area is the design's own black
                    // plate and the time is real. Recorded as 尚未接入 in WORKLOG (B1).
                    Rectangle {
                        visible: opacity > 0.01
                        width: 144
                        height: 104
                        radius: 8
                        color: "#000000"
                        border.width: 1
                        border.color: Theme.stroke2
                        x: Math.max(0, Math.min(parent.width - width,
                                                seekArea.hoverFrac * seekArea.width - width / 2))
                        y: -height - 18
                        opacity: seekHover.hovered ? 1 : 0
                        scale: seekHover.hovered ? 1 : 0.85
                        transformOrigin: Item.Bottom
                        Behavior on opacity { NumberAnimation { duration: Theme.d(150) } }
                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 23
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: veyra.formatTime(seekArea.hoverFrac * veyra.duration)
                            color: Theme.t1
                            font.family: Theme.fontMono
                            font.pixelSize: 11
                            font.weight: Font.Medium; font.variableAxes: Theme.axesMedium
                            Rectangle {
                                anchors.fill: parent
                                z: -1
                                color: "#111111"
                            }
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
                // The design's "全屏" button (title="全屏"). It used to call an
                // undefined root.requestPage("pro").
                TapHandler { onTapped: bar.requestFullscreen() }
            }
        }
    }

    // The preset menu: list presets and node presets in one menu, as designed.
    // pages-a.js presetMenu: list presets, node presets, then the way to manage them.
    VMenu {
        id: presetMenu
        aboveLimit: bar.menuBottomLimit
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

    // 字幕 (design: app.menu(anchor, '字幕', [...])): the design lists embedded
    // tracks plus a settings dialog. The engine has no subtitle stream at all -
    // no track list, no renderer - so every entry here is the design's own text
    // with nothing behind it. Shown disabled rather than silently doing nothing.
    VMenu {
        id: ccMenu
        aboveLimit: bar.menuBottomLimit
        title: "字幕"
        items: [
            { label: "关闭", disabled: true },
            { label: "简体中文 · 内嵌 ASS", disabled: true },
            { label: "English · 内嵌 SRT", disabled: true },
            { label: "加载外部字幕…", icon: "import", disabled: true },
            { sep: true },
            { label: "字幕设置…", note: "字体、字号、描边、位置、延时（尚未接入）", icon: "type", disabled: true }
        ]
        onPicked: veyra.logUi("ui-cine", "subtitle menu picked index=" + i + " (no subtitle stream in the engine)")
    }

    // 音轨: this one is real - the bridge exposes the file's audio streams and the
    // selected index, so the list and the check mark both come from the engine.
    VMenu {
        id: audioMenu
        aboveLimit: bar.menuBottomLimit
        title: "音轨"
        // The bridge's label already carries language · title · codec; only the
        // channel count is separate.
        readonly property var mk: t => ({ label: t.label, index: t.index,
                                          note: t.channels > 0 ? (t.channels + " 声道") : "",
                                          checked: t.index === veyra.selectedAudioTrack })
        items: veyra.audioTracks.length > 0
               ? veyra.audioTracks.map(mk).concat([{ sep: true },
                     { label: "音频设置…", note: "输出设备、音画同步、偏移", icon: "music", act: "dlg" }])
               : [{ label: "片源没有音轨或尚未打开", disabled: true }]
        onPicked: (i, o) => {
            if (o.act === "dlg") return bar.requestPage("set")
            if (o.index !== undefined) veyra.selectedAudioTrack = o.index
        }
    }
}
