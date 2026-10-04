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
import QtQuick.Effects

Rectangle {
    id: bar
    signal requestPage(string page)
    // Opens one of the main window's dialogs (字幕设置 / 音频设置).
    signal requestDialog(string key)
    signal requestFullscreen()
    signal requestLock()
    // Windowed 极简: a drag on the pill's empty space moves the window, like a title bar.
    signal requestMove()
    signal requestHide()
    // Set on the fullscreen bar: shows the lock button.
    property bool fullscreen: false
    // Set by FullscreenBar so upward menus stop above the playback pill.
    property real menuBottomLimit: -1
    // Any popover needs the owner window's full mask, not just the preset menu.
    readonly property bool menuOpen: presetMenu.visible || ccMenu.visible || audioMenu.visible || loadMenu.visible || playbackRate.menuOpen
    // The pointer is over the pill.
    readonly property bool hovered: barHover.hovered
    readonly property bool seekPreviewOpen: seekMouse.containsMouse && seekMouse.enabled
    HoverHandler { id: barHover }
    // Buttons, the seek rail and the volume slider take their own presses first; only
    // a drag that starts on empty pill reaches this one.
    DragHandler {
        objectName: "cine-move"
        target: null
        enabled: !bar.fullscreen
        // Never take a drag over from a control: by default a DragHandler steals the grab
        // from an item once the pointer passes the drag threshold, so scrubbing the seek
        // rail moved the window instead (field report 2026-10-02).
        grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType | PointerHandler.ApprovesTakeOverByAnything
        onActiveChanged: if (active) bar.requestMove()
    }
    implicitHeight: 92
    radius: 30
    color: Qt.rgba(22 / 255, 22 / 255, 26 / 255, 0.86)
    border.width: 1
    border.color: Qt.rgba(1, 1, 1, 0.1)

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 16
        spacing: 18

        // --- left: artwork + title (230px) ---------------------------
        RowLayout {
            Layout.preferredWidth: 230; Layout.minimumWidth: 230; Layout.maximumWidth: 230
            Layout.fillHeight: true
            spacing: 10
            // The file's own cover art when it carries one (MP4 covr, MKV image
            // attachment); otherwise the plate stays black - never a stand-in.
            Rectangle {
                id: coverPlate
                implicitWidth: 46; implicitHeight: 46; radius: 12
                color: Theme.videoBlack
                border.width: 1
                border.color: Theme.stroke
                clip: true
                Image {
                    id: coverArt
                    objectName: "cine-cover"
                    anchors.fill: parent
                    anchors.margins: 1
                    // Drawn through the masking effect below, never directly.
                    visible: false
                    fillMode: Image.PreserveAspectCrop
                    asynchronous: true
                    cache: false
                    source: veyra.hasSource && !veyra.isCapture ? "image://veyra-thumb/cover/" + veyra.thumbnailGeneration : ""
                }
                // A sibling MultiEffect, not layer.effect: Qt recreates a layer's effect item on
                // a screen DPI change while it walks the parent's children, and the walk then touched
                // the deleted item (crash moving the window to a 200 % monitor, field 2026-10-01).
                MultiEffect {
                    source: coverArt
                    anchors.fill: coverArt
                    visible: coverArt.status === Image.Ready
                    maskEnabled: true
                    maskSource: coverMask
                }
                Rectangle { id: coverMask; anchors.fill: coverArt; radius: 11; visible: false; layer.enabled: true }
            }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.alignment: Qt.AlignVCenter
                spacing: 2
                Text {
                    Layout.fillWidth: true
                    text: veyra.sourceName.length > 0 ? veyra.sourceName : qsTr("未打开")
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
                    ToolTip.visible: cHover.hovered && cbtn.tip.length > 0
                    ToolTip.text: cbtn.tip
                    TapHandler { id: cTap; gesturePolicy: TapHandler.WithinBounds; onTapped: cbtn.tapped() }
                }

                // 载入 (field request 2026-10-02): the home page's sources without leaving 极简.
                CBtn { objectName: "cine-load"; glyph: "folder"; tip: qsTr("载入片源"); onTapped: loadMenu.openAt(this, "up") }
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
                    TapHandler { id: playTap; gesturePolicy: TapHandler.WithinBounds; onTapped: veyra.togglePlayPause() }
                }

                CBtn { glyph: "fwd10"; onTapped: veyra.seekBy(10) }
                CBtn { glyph: "music"; onTapped: audioMenu.openAt(this, "up") }
                PlaybackRateButton { id: playbackRate; objectName: "cine-playback-rate"; menuBottomLimit: bar.menuBottomLimit }
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
                    objectName: "cine-seek"
                    Layout.fillWidth: true
                    implicitHeight: 14
                    property bool scrubbing: false
                    property real scrubFrac: 0
                    // .seek:hover .thumb { left: 44% } - the thumb tracks the real
                    // progress, not the design's hard-coded 44%.
                    readonly property real frac: scrubbing ? scrubFrac : Math.max(0, Math.min(1, shownProgress))
                    // Twice a second, not every snapshot: the pill is its own window and a rail
                    // that moved every tick made it present ~50 times a second, more often than
                    // a film plays, so OBS game capture could lock onto the pill.
                    property real shownProgress: veyra.progress
                    Timer {
                        interval: 500; repeat: true; running: bar.visible
                        onTriggered: seekArea.shownProgress = veyra.progress
                    }
                    readonly property real hoverFrac: scrubbing ? scrubFrac
                                                      : seekMouse.containsMouse && seekArea.width > 0
                                                        ? Math.max(0, Math.min(1, seekMouse.mouseX / seekArea.width))
                                                        : frac
                    property int thumbnailBucket: -1
                    function fractionAt(x) {
                        return seekArea.width > 0 ? Math.max(0, Math.min(1, x / seekArea.width)) : 0
                    }
                    Timer {
                        id: scrubSeek
                        interval: 120
                        onTriggered: if (seekArea.scrubbing) veyra.seekTo(seekArea.scrubFrac * veyra.duration)
                    }
                    Timer {
                        id: thumbnailDelay
                        interval: 120
                        onTriggered: seekArea.thumbnailBucket = Math.floor(seekArea.hoverFrac * veyra.duration / 2)
                    }
                    Rectangle {
                        id: rail
                        anchors.verticalCenter: parent.verticalCenter
                        width: parent.width
                        height: seekMouse.containsMouse ? 6 : 3
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
                        scale: seekMouse.containsMouse || seekArea.scrubbing ? 1 : 0
                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                    }
                    // .seek .peek: 144x104 above the rail, opacity .15s, scale .85 -> 1
                    // over .4s --spring from its bottom centre.
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
                        opacity: seekMouse.containsMouse || seekArea.scrubbing ? 1 : 0
                        scale: seekMouse.containsMouse || seekArea.scrubbing ? 1 : 0.85
                        transformOrigin: Item.Bottom
                        Behavior on opacity { NumberAnimation { duration: Theme.d(150) } }
                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                        Image {
                            id: previewFrame
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.top: parent.top
                            height: 81
                            asynchronous: true
                            cache: true
                            fillMode: Image.PreserveAspectFit
                            source: (seekMouse.containsMouse || seekArea.scrubbing) && seekArea.thumbnailBucket >= 0
                                    ? "image://veyra-thumb/" + veyra.thumbnailGeneration + "/" + (seekArea.thumbnailBucket * 2000)
                                    : ""
                        }
                        Text {
                            anchors.centerIn: previewFrame
                            visible: previewFrame.status === Image.Loading || previewFrame.status === Image.Error
                            text: previewFrame.status === Image.Loading ? qsTr("加载中") : qsTr("预览不可用")
                            color: Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: 11
                        }
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
                    MouseArea {
                        id: seekMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        preventStealing: true
                        enabled: veyra.duration > 0 && !veyra.isCapture
                        cursorShape: Qt.PointingHandCursor
                        onEntered: thumbnailDelay.restart()
                        onExited: {
                            thumbnailDelay.stop()
                            seekArea.thumbnailBucket = -1
                        }
                        onPressed: mouse => {
                            seekArea.scrubFrac = seekArea.fractionAt(mouse.x)
                            seekArea.scrubbing = true
                        }
                        onPositionChanged: mouse => {
                            if (seekArea.scrubbing) {
                                seekArea.scrubFrac = seekArea.fractionAt(mouse.x)
                                // Scrubbing seeks as it goes, 120 ms apart.
                                if (!scrubSeek.running) scrubSeek.start()
                            }
                            thumbnailDelay.restart()
                        }
                        onReleased: mouse => {
                            const target = seekArea.fractionAt(mouse.x) * veyra.duration
                            scrubSeek.stop()
                            seekArea.shownProgress = seekArea.fractionAt(mouse.x)
                            seekArea.scrubbing = false
                            veyra.logUi("ui-seek", "targetSeconds=" + target.toFixed(3))
                            veyra.seekTo(target)
                        }
                        onCanceled: seekArea.scrubbing = false
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
            Layout.preferredWidth: 250; Layout.minimumWidth: 250; Layout.maximumWidth: 250
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
                implicitWidth: 80
                implicitHeight: 20
                RowLayout {
                    anchors.fill: parent
                    spacing: 6
                    VIcon { name: "vol"; color: Theme.t2 }
                    VSlider {
                        Layout.fillWidth: true
                        from: 0; to: 1; value: veyra.volume; inputScale: 100
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
            // Hide the pill (field request 2026-10-01: it covers the game when playing
            // through a capture card or a stream). The dock's eye button brings it back.
            Item {
                objectName: "cine-hide"
                implicitWidth: 32; implicitHeight: 32
                VIcon { anchors.centerIn: parent; name: "eyeoff"; color: hideHover.hovered ? "#FFFFFF" : Theme.t2 }
                HoverHandler { id: hideHover; cursorShape: Qt.PointingHandCursor }
                ToolTip.visible: hideHover.hovered
                ToolTip.text: qsTr("隐藏播放条（顶部胶囊里可重新打开）")
                TapHandler { onTapped: bar.requestHide() }
            }
        }
    }

    // The preset menu: list presets and node presets in one menu, as designed.
    // pages-a.js presetMenu: list presets, node presets, then the way to manage them.
    VMenu {
        id: presetMenu
        aboveLimit: bar.menuBottomLimit
        title: qsTr("预设")
        readonly property var mk: p => ({ label: p.name, note: p.note, checked: p.name === veyra.currentPresetName,
                                          tag: p.nodeMode ? qsTr("节点") : "", preset: p.index })
        items: [{ head: qsTr("列表预设") }].concat(veyra.presets.filter(p => !p.nodeMode).map(mk))
            .concat([{ head: qsTr("节点预设") }]).concat(veyra.presets.filter(p => p.nodeMode).map(mk))
            .concat([{ sep: true }, { label: qsTr("去专业模式管理预设…"), icon: "sliders", act: "pro" }])
        onPicked: (i, o) => {
            if (o.act === "pro") bar.requestPage("pro")
            else veyra.applyPresetIndex(o.preset)
        }
    }

    // 字幕 (design: app.menu(anchor, '字幕', [...])): the file's own tracks
    // (embedded and same-name external) from the subtitle loader, the primary
    // one checked, then loading a file and the settings dialog.
    VMenu {
        id: ccMenu
        aboveLimit: bar.menuBottomLimit
        title: qsTr("字幕")
        items: [{ label: qsTr("关闭", "off"), checked: veyra.subtitlePrimary < 0, track: -1 }]
            .concat(veyra.subtitleTracks.map(t => ({ label: t.label, note: t.note, track: t.index,
                                                     disabled: !t.usable, checked: t.index === veyra.subtitlePrimary })))
            .concat([{ label: qsTr("加载外部字幕…"), icon: "import", act: "load" },
                     { sep: true },
                     { label: qsTr("字幕设置…"), note: qsTr("字体、字号、描边、位置、延时"), icon: "type", act: "dlg" }])
        onPicked: (i, o) => {
            if (o.act === "load") veyra.loadSubtitleDialog()
            else if (o.act === "dlg") bar.requestDialog("subtitle")
            else if (o.track !== undefined) veyra.subtitlePrimary = o.track
        }
    }

    // 载入: the same six sources as the home page's cards, each opening its own dialog
    // (or the file picker) in the main window; the pill steps aside while one is open.
    VMenu {
        id: loadMenu
        objectName: "cine-load-menu"
        aboveLimit: bar.menuBottomLimit
        title: qsTr("载入片源")
        items: [
            { label: qsTr("打开视频"), note: qsTr("MP4 · MKV · 图片"), icon: "folder", act: "file" },
            { label: qsTr("采集卡"), note: qsTr("HDMI 采集设备"), icon: "video", act: "capture" },
            { label: qsTr("PS5 串流"), note: qsTr("局域网串流"), icon: "gamepad", act: "ps5" },
            { label: qsTr("PC 串流"), note: qsTr("Sunshine 主机"), icon: "cast", act: "moonlight" },
            { label: qsTr("Xbox 串流"), note: qsTr("账号登录 · 实验"), icon: "gamepad", act: "xbox" },
            { label: qsTr("屏幕捕获"), note: qsTr("窗口或显示器"), icon: "monitor", act: "screen" }
        ]
        onPicked: (i, o) => {
            switch (o.act) {
            case "file": veyra.openFileDialog(); break
            case "capture": veyra.openCaptureDialog(); break
            case "ps5": veyra.openPs5Dialog(); break
            case "moonlight": veyra.openMoonlightDialog(); break
            case "xbox": veyra.openXboxDialog(); break
            case "screen": veyra.openScreenCaptureDialog(); break
            }
        }
    }

    // 音轨: this one is real - the bridge exposes the file's audio streams and the
    // selected index, so the list and the check mark both come from the engine.
    VMenu {
        id: audioMenu
        aboveLimit: bar.menuBottomLimit
        title: qsTr("音轨")
        // The bridge's label already carries language · title · codec; only the
        // channel count is separate.
        readonly property var mk: t => ({ label: t.label, index: t.index,
                                          note: t.channels > 0 ? (t.channels + qsTr(" 声道")) : "",
                                          checked: t.index === veyra.selectedAudioTrack })
        items: veyra.audioTracks.length > 0
               ? veyra.audioTracks.map(mk).concat([{ sep: true },
                     { label: qsTr("音频设置…"), note: qsTr("输出设备、音画同步、偏移"), icon: "music", act: "dlg" }])
               : [{ label: qsTr("片源没有音轨或尚未打开"), disabled: true }]
        onPicked: (i, o) => {
            if (o.act === "dlg") return bar.requestDialog("audio")
            if (o.index !== undefined) veyra.selectedAudioTrack = o.index
        }
    }
}
