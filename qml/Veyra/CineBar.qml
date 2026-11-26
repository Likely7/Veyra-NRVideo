// The cinema control pill (pages-a.js .cine-bar, pages.css .cine-bar), shared by the
// minimal page and the fullscreen control window (G2.5). Only the pill: where it
// sits and how it enters belong to the owner.
//
// .cine-bar: three columns, 92px tall, radius 30. The design's
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
    signal requestPlaylist()
    // Set on the fullscreen bar: shows the lock button.
    property bool fullscreen: false
    // Set by FullscreenBar so upward menus stop above the playback pill.
    property real menuBottomLimit: -1
    // Any popover needs the owner window's full mask, not just the preset menu.
    readonly property bool menuOpen: presetMenu.visible || ccMenu.visible || audioMenu.visible || loadMenu.visible || playbackRate.menuOpen
    // The pointer is over the pill.
    readonly property bool hovered: barHover.hovered
    readonly property bool compact: width < 820
    readonly property bool narrow: width < 560
    readonly property bool seekPreviewOpen: seekMouse.enabled && (seekMouse.containsMouse || seekArea.scrubbing)
    readonly property bool editing: volumeSlider.editing
    // Keep the control window active until a seek releases its mouse grab, too.
    // Returning focus to the owner during scrubbing cancels the MouseArea press.
    readonly property bool interacting: seekArea.scrubbing || volumeSlider.pressed || editing
    function finishEditing() { volumeSlider.closeEditor(true) }
    HoverHandler { id: barHover }
    // Buttons, the seek rail and the volume slider take their own presses first; only
    // a drag that starts on empty pill reaches this one.
    DragHandler {
        objectName: "cine-move"
        target: null
        enabled: !bar.fullscreen && !bar.interacting
        // Never take a drag over from a control: by default a DragHandler steals the grab
        // from an item once the pointer passes the drag threshold, so scrubbing the seek
        // rail moved the window instead (field report 2026-10-02).
        grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType | PointerHandler.ApprovesTakeOverByAnything
        onActiveChanged: if (active) bar.requestMove()
    }
    implicitHeight: 92
    radius: 30
    color: Theme.shade(18, 20, 24, 0.90)
    border.width: 1
    border.color: Theme.strokeStrong

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: Theme.s16
        anchors.rightMargin: Theme.s16
        spacing: bar.narrow ? Theme.s8 : Theme.s12

        // --- left: artwork, title and playlist -----------------------
        RowLayout {
            visible: !bar.narrow
            Layout.preferredWidth: bar.compact ? 156 : 230
            Layout.minimumWidth: bar.narrow ? 0 : 112
            Layout.maximumWidth: 230
            Layout.fillHeight: true
            spacing: Theme.s8
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
                    font.pixelSize: Theme.fsBody
                    font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                    elide: Text.ElideRight
                }
                Text {
                    Layout.fillWidth: true
                    text: veyra.sourceSummary
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSmall
                    elide: Text.ElideRight
                }
            }
            VButton {
                objectName: "cine-playlist"
                icon: true; iconName: "list"; tip: qsTr("播放列表")
                implicitWidth: 32; implicitHeight: 32
                onClicked: bar.requestPlaylist()
            }
        }

        // --- middle: transport + seek --------------------------------
        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: transport.implicitWidth
            Layout.alignment: Qt.AlignVCenter
            spacing: 4

            RowLayout {
                id: transport
                Layout.alignment: Qt.AlignHCenter
                spacing: bar.compact ? 4 : 6

                // .cbtn: 32px round, muted, brightens on hover.
                component CBtn: Item {
                    id: cbtn
                    property string glyph: ""
                    property string tip: ""
                    property bool highlighted: false
                    property bool enabled: true
                    signal tapped()
                    implicitWidth: 32
                    implicitHeight: 32
                    opacity: enabled ? 1.0 : Theme.disabledOpacity
                    activeFocusOnTab: enabled
                    Accessible.role: Accessible.Button
                    Accessible.name: tip
                    Accessible.onPressAction: if (enabled) tapped()
                    Keys.onReturnPressed: if (enabled) tapped()
                    Keys.onEnterPressed: if (enabled) tapped()
                    Keys.onSpacePressed: if (enabled) tapped()
                    Rectangle {
                        anchors.fill: parent
                        radius: 16
                        color: !cbtn.enabled ? "transparent"
                             : cTap.pressed ? Theme.pressFill
                             : cHover.hovered ? Theme.hoverFill : "transparent"
                        border.width: cbtn.activeFocus ? 1.5 : 0
                        border.color: Theme.focusRing
                    }
                    VIcon {
                        anchors.centerIn: parent
                        name: glyph
                        color: cbtn.highlighted ? Theme.warn : (cbtn.enabled && cHover.hovered) ? Theme.t1 : Theme.t2
                    }
                    scale: cbtn.enabled && cTap.pressed ? 0.86 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                    HoverHandler { id: cHover; enabled: cbtn.enabled; cursorShape: cbtn.enabled ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    ToolTip.visible: cHover.hovered && cbtn.tip.length > 0
                    ToolTip.text: cbtn.tip
                    TapHandler { id: cTap; enabled: cbtn.enabled; gesturePolicy: TapHandler.WithinBounds; onTapped: cbtn.tapped() }
                }

                // 载入 (field request 2026-10-02): the home page's sources without leaving 极简.
                CBtn { objectName: "cine-load"; glyph: "folder"; tip: qsTr("载入片源"); onTapped: loadMenu.openAt(this, "up") }
                CBtn { glyph: "cc"; tip: qsTr("字幕"); onTapped: ccMenu.openAt(this, "up") }
                CBtn { glyph: "back10"; tip: qsTr("后退 10 秒"); enabled: veyra.timelineSeekable; onTapped: veyra.seekBy(-10) }

                // .play: 40px white circle with the play/pause glyph.
                Item {
                    id: playBtn
                    implicitWidth: 40
                    implicitHeight: 40
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: (veyra.running && !veyra.paused) ? qsTr("暂停") : qsTr("播放")
                    Accessible.onPressAction: veyra.togglePlayPause()
                    Keys.onReturnPressed: veyra.togglePlayPause()
                    Keys.onEnterPressed: veyra.togglePlayPause()
                    Keys.onSpacePressed: veyra.togglePlayPause()
                    Rectangle {
                        anchors.fill: parent
                        radius: 20
                        color: Theme.t1
                        border.width: playBtn.activeFocus ? 2 : 0
                        border.color: Theme.focusRing
                    }
                    VIcon {
                        anchors.centerIn: parent
                        name: (veyra.running && !veyra.paused) ? "pausefill" : "playfill"
                        filled: true
                        color: Theme.bg
                    }
                    scale: playTap.pressed ? 0.88 : (playHover.hovered ? 1.06 : 1.0)
                    Behavior on scale { NumberAnimation { duration: Theme.d(450); easing.bezierCurve: Theme.spring } }
                    HoverHandler { id: playHover; cursorShape: Qt.PointingHandCursor }
                    ToolTip.visible: playHover.hovered
                    ToolTip.text: (veyra.running && !veyra.paused) ? qsTr("暂停") : qsTr("播放")
                    TapHandler { id: playTap; gesturePolicy: TapHandler.WithinBounds; onTapped: veyra.togglePlayPause() }
                }

                CBtn { glyph: "fwd10"; tip: qsTr("前进 10 秒"); enabled: veyra.timelineSeekable; onTapped: veyra.seekBy(10) }
                CBtn {
                    objectName: "cine-audio-tracks"
                    glyph: audioMenu.captureSource ? "vol" : "music"
                    tip: audioMenu.buttonLabel
                    highlighted: audioMenu.monitorOff
                    onTapped: audioMenu.openAt(this, "up")
                }
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
                    font.pixelSize: Theme.fsEyebrow
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
                    // Where the last scrub seek was sent, so a release that lands on
                    // the same spot does not interrupt it with a duplicate.
                    property real lastSeekTarget: -1
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
                        // Restarted on every pointer move (see the MouseArea below), so it
                        // fires only once the drag settles. Seeking mid-drag makes two
                        // seeks overlap and they interrupt each other: a network source
                        // re-opens its read on each one, which is what made a drag look
                        // like a freeze. newBV defers the same way (1 s there).
                        interval: 250
                        onTriggered: {
                            if (!seekArea.scrubbing) return
                            const target = veyra.timelineSeconds(seekArea.scrubFrac)
                            seekArea.lastSeekTarget = target
                            veyra.seekTo(target)
                        }
                    }
                    Timer {
                        id: thumbnailDelay
                        interval: 120
                        onTriggered: if (!veyra.liveTimeshift.enabled) seekArea.thumbnailBucket = Math.floor(seekArea.hoverFrac * veyra.duration / 2)
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
                            color: Theme.accent
                        }
                    }
                    Rectangle {
                        width: 11; height: 11; radius: 5.5
                        color: Theme.t1
                        x: seekArea.frac * seekArea.width - width / 2
                        anchors.verticalCenter: parent.verticalCenter
                        scale: seekMouse.containsMouse || seekArea.scrubbing ? 1 : 0
                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                    }
                    // .seek .peek: 144x104 above the rail, opacity .15s, scale .85 -> 1
                    // over .4s --spring from its bottom centre.
                    Rectangle {
                        visible: opacity > 0.01
                        width: veyra.liveTimeshift.available ? 84 : 144
                        height: veyra.liveTimeshift.available ? 26 : 104
                        radius: Theme.rWindow
                        color: Theme.videoBlack
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
                            source: !veyra.liveTimeshift.enabled &&
                                    (seekMouse.containsMouse || seekArea.scrubbing) && seekArea.thumbnailBucket >= 0
                                    ? "image://veyra-thumb/" + veyra.thumbnailGeneration + "/" + (seekArea.thumbnailBucket * 2000)
                                    : ""
                        }
                        Text {
                            anchors.centerIn: previewFrame
                            visible: previewFrame.status === Image.Loading || previewFrame.status === Image.Error
                            text: previewFrame.status === Image.Loading ? qsTr("加载中") : qsTr("预览不可用")
                            color: Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsEyebrow
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            height: 23
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                            text: veyra.liveTimeshift.enabled
                                  ? "−" + veyra.formatTime(veyra.liveTimeshift.end - veyra.timelineSeconds(seekArea.hoverFrac))
                                  : veyra.formatTime(veyra.timelineSeconds(seekArea.hoverFrac))
                            color: Theme.t1
                            font.family: Theme.fontMono
                            font.pixelSize: Theme.fsEyebrow
                            font.weight: Font.Medium; font.variableAxes: Theme.axesMedium
                            Rectangle {
                                anchors.fill: parent
                                z: -1
                                color: Theme.card
                            }
                        }
                    }
                    MouseArea {
                        id: seekMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        preventStealing: true
                        enabled: veyra.timelineSeekable
                        onEnabledChanged: if (!enabled && seekArea.scrubbing) {
                            scrubSeek.stop()
                            seekArea.scrubbing = false
                        }
                        cursorShape: Qt.PointingHandCursor
                        onEntered: thumbnailDelay.restart()
                        onExited: {
                            thumbnailDelay.stop()
                            seekArea.thumbnailBucket = -1
                        }
                        onPressed: mouse => {
                            seekArea.lastSeekTarget = -1
                            seekArea.scrubFrac = seekArea.fractionAt(mouse.x)
                            seekArea.scrubbing = true
                        }
                        onPositionChanged: mouse => {
                            if (seekArea.scrubbing) {
                                seekArea.scrubFrac = seekArea.fractionAt(mouse.x)
                                // restart(), not start(): each move pushes the deadline
                                // back, so the seek waits for the pointer to settle.
                                scrubSeek.restart()
                            }
                            thumbnailDelay.restart()
                        }
                        onReleased: mouse => {
                            const target = veyra.timelineSeconds(seekArea.fractionAt(mouse.x))
                            scrubSeek.stop()
                            seekArea.shownProgress = seekArea.fractionAt(mouse.x)
                            seekArea.scrubbing = false
                            veyra.logUi("ui-seek", "targetSeconds=" + target.toFixed(3))
                            // The timer may have fired on the last move and already
                            // started a seek to (very nearly) this position. A second
                            // request would interrupt that one and cost another full
                            // re-position, so a near-identical target is dropped.
                            if (Math.abs(target - seekArea.lastSeekTarget) > 0.5) {
                                seekArea.lastSeekTarget = target
                                veyra.seekTo(target)
                            }
                        }
                        onCanceled: {
                            scrubSeek.stop()
                            seekArea.scrubbing = false
                        }
                    }
                }
                Text {
                    text: veyra.durationText
                    visible: !veyra.liveTimeshift.available
                    color: Theme.t3
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsEyebrow
                }
                VButton {
                    objectName: "cine-return-live"
                    visible: veyra.liveTimeshift.available
                    ghost: true
                    text: qsTr("回到直播")
                    onClicked: veyra.returnToLive()
                }
            }
        }

        // --- right: preset, volume, picture and bar controls ----------
        RowLayout {
            Layout.fillWidth: false
            Layout.minimumWidth: implicitWidth
            Layout.alignment: Qt.AlignVCenter | Qt.AlignRight
            spacing: 6

            VPill {
                visible: !bar.narrow
                key: ""
                value: veyra.currentPresetName
                maximumValueWidth: bar.compact ? 72 : 96
                onClicked: presetMenu.openAt(this, "up")
                Rectangle {
                    width: 7; height: 7; radius: 3.5
                    color: veyra.failed ? Theme.err : veyra.captureRecovering ? Theme.warn : Theme.ok
                }
            }

            RowLayout {
                visible: !bar.narrow
                Layout.preferredHeight: 32
                spacing: 6
                VIcon { name: "vol"; color: Theme.t2 }
                VSlider {
                    id: volumeSlider
                    objectName: "cine-volume"
                    Layout.preferredWidth: bar.compact ? 48 : 68
                    Layout.fillHeight: true
                    from: 0; to: veyra.maximumVolume; value: veyra.volume; inputScale: 100
                    defaultValue: 1
                    valueFromModel: true
                    fullKnobHitArea: true
                    onMoved: moved => veyra.volume = moved
                }
                VSliderValue {
                    Layout.minimumWidth: 34
                    Layout.preferredWidth: implicitWidth
                    slider: volumeSlider
                    text: Math.round(veyra.volume * 100) + "%"
                    color: Theme.t2; pixelSize: Theme.fsEyebrow
                }
            }

            RotateButton { objectName: "cine-rotate"; visible: !bar.narrow }

            Item {
                id: fullBtn
                implicitWidth: 32; implicitHeight: 32
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: bar.fullscreen ? qsTr("退出全屏") : qsTr("全屏")
                Accessible.onPressAction: bar.requestFullscreen()
                Keys.onReturnPressed: bar.requestFullscreen()
                Keys.onEnterPressed: bar.requestFullscreen()
                Keys.onSpacePressed: bar.requestFullscreen()
                scale: fullTap.pressed ? 0.86 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                Rectangle {
                    anchors.fill: parent
                    radius: 16
                    color: fullTap.pressed ? Theme.pressFill : fullHover.hovered ? Theme.hoverFill : "transparent"
                    border.width: fullBtn.activeFocus ? 1.5 : 0
                    border.color: Theme.focusRing
                }
                VIcon {
                    anchors.centerIn: parent
                    name: "max"
                    color: fullHover.hovered ? Theme.t1 : Theme.t2
                }
                HoverHandler { id: fullHover; cursorShape: Qt.PointingHandCursor }
                ToolTip.visible: fullHover.hovered
                ToolTip.text: bar.fullscreen ? qsTr("退出全屏") : qsTr("全屏")
                TapHandler { id: fullTap; onTapped: bar.requestFullscreen() }
            }
            // Hide the pill (field request 2026-10-01: it covers the game when playing
            // through a capture card or a stream). The dock's eye button brings it back.
            Item {
                id: hideBtn
                objectName: "cine-hide"
                implicitWidth: 32; implicitHeight: 32
                activeFocusOnTab: true
                Accessible.role: Accessible.Button
                Accessible.name: qsTr("隐藏播放条")
                Accessible.onPressAction: bar.requestHide()
                Keys.onReturnPressed: bar.requestHide()
                Keys.onEnterPressed: bar.requestHide()
                Keys.onSpacePressed: bar.requestHide()
                scale: hideTap.pressed ? 0.86 : 1.0
                Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                Rectangle {
                    anchors.fill: parent
                    radius: 16
                    color: hideTap.pressed ? Theme.pressFill : hideHover.hovered ? Theme.hoverFill : "transparent"
                    border.width: hideBtn.activeFocus ? 1.5 : 0
                    border.color: Theme.focusRing
                }
                VIcon { anchors.centerIn: parent; name: "eyeoff"; color: hideHover.hovered ? Theme.t1 : Theme.t2 }
                HoverHandler { id: hideHover; cursorShape: Qt.PointingHandCursor }
                ToolTip.visible: hideHover.hovered
                ToolTip.text: qsTr("隐藏播放条（顶部胶囊里可重新打开）")
                TapHandler { id: hideTap; onTapped: bar.requestHide() }
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
            { label: qsTr("播放地址…"), note: qsTr("HTTP · HTTPS · RTSP"), icon: "cast", act: "network" },
            { label: qsTr("采集卡"), note: qsTr("HDMI 采集设备"), icon: "video", act: "capture" },
            { label: qsTr("PS5 串流"), note: qsTr("局域网串流"), icon: "gamepad", act: "ps5" },
            { label: qsTr("PC 串流"), note: qsTr("Sunshine 主机"), icon: "cast", act: "moonlight" },
            { label: qsTr("Xbox 串流"), note: qsTr("账号登录 · 测试版"), icon: "gamepad", act: "xbox" },
            { label: qsTr("屏幕捕获"), note: qsTr("窗口或显示器"), icon: "monitor", act: "screen" }
        ]
        onPicked: (i, o) => {
            switch (o.act) {
            case "file": veyra.openFileDialog(); break
            case "network": veyra.openNetworkDialog(); break
            case "capture": veyra.openCaptureDialog(); break
            case "ps5": veyra.openPs5Dialog(); break
            case "moonlight": veyra.openMoonlightDialog(); break
            case "xbox": veyra.openXboxDialog(); break
            case "screen": veyra.openScreenCaptureDialog(); break
            }
        }
    }

    PlayerAudioMenu {
        id: audioMenu
        objectName: "cine-audio-menu"
        backend: veyra
        aboveLimit: bar.menuBottomLimit
        onSettingsRequested: bar.requestDialog("audio")
    }
}
