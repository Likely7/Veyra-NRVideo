// Source launcher: file actions, responsive device grid, and actual recent sessions.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

VPage {
    id: root
    signal requestPage(string page)
    signal requestOnboarding()
    readonly property Item onboardingEntry: guideEntry
    readonly property alias sourceViewport: homeScroll
    readonly property bool compact: height < 500
    function captureCard() {
        for (let i = 0; i < sourceCards.count; ++i) {
            const slot = sourceCards.itemAt(i)
            if (slot && slot.modelData.act === "capture") return slot.card
        }
        return null
    }
    // The footer link opens 设置 → 赞助作者.
    signal requestSponsor()
    // Main.qml keeps the dusk wordmark hidden while the startup intro (DuskIntro.qml) flies a
    // copy of it in, and aims that copy at this item.
    property bool introHold: false
    readonly property Item wordmarkItem: wordmark

    Flickable {
        id: homeScroll
        objectName: "source-card-scroll"
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: guideRow.top
        anchors.bottomMargin: 8
        contentWidth: width
        contentHeight: Math.max(height, homeContent.y + homeContent.implicitHeight + 24)
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: VScrollBar { }
    }

    ColumnLayout {
        id: homeContent
        parent: homeScroll.contentItem
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(960, Math.max(0, homeScroll.width - 48))
        y: root.compact ? 16 : 32
        spacing: root.compact ? 12 : 20

        RowLayout {
            Layout.fillWidth: true
            spacing: 12
            Image {
                id: wordmark
                objectName: "home-wordmark"
                source: Theme.logoImage
                sourceSize: Qt.size(32, 32)
                Layout.preferredWidth: 32
                Layout.preferredHeight: 32
                fillMode: Image.PreserveAspectFit
                opacity: root.introHold ? 0 : 1
            }
            Text {
                text: "Veyra NR Video"
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsH2
                font.weight: Font.DemiBold
                font.variableAxes: Theme.axesDemiBold
                Layout.fillWidth: true
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            VButton {
                objectName: "home-open-file"
                text: qsTr("打开视频")
                iconName: "folder"
                primary: true
                onClicked: veyra.openFileDialog()
            }
            VButton {
                objectName: "home-playlist"
                text: qsTr("播放列表 · 导入文件夹")
                iconName: "list"
                onClicked: veyra.showPlaylist()
            }
            VButton {
                objectName: "home-network"
                text: qsTr("播放地址…")
                iconName: "cast"
                onClicked: veyra.openNetworkDialog()
            }
        }

        // Keep source names readable by wrapping devices at narrower widths.
        Flow {
            id: srcGrid
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: homeContent.width
            spacing: root.compact ? 8 : 12
            readonly property int columns: width >= 540 ? 3 : 2
            readonly property int cardWidth: Math.max(1, Math.floor((width - (columns - 1) * spacing) / columns))
            readonly property int cardHeight: root.compact ? 64 : 76
            // Subtitles name what is really there: the device of the last capture
            // session and the saved PS5 host; generic wording when there is none.
            // Only the subtitle follows the live state. The cards themselves are a fixed
            // list: when the whole array was one binding, every Xbox / PC state change
            // (polling after sign-in) destroyed and rebuilt all six cards, and a click
            // landing during the rebuild was lost (field report 2026-10-02).
            function cardSub(act) {
                switch (act) {
                case "file": return qsTr("MP4 · MKV · 图片")
                case "capture": return veyra.hasCaptureSession ? veyra.captureSessionSummary.split(" · ")[0] : qsTr("HDMI 采集设备")
                case "ps5": return veyra.remotePlayHost.length > 0 ? qsTr("已保存主机 ") + veyra.remotePlayHost : qsTr("局域网串流")
                case "moonlight": return veyra.moonlight && veyra.moonlight.state.lastLabel ? veyra.moonlight.state.lastLabel : qsTr("Sunshine 主机")
                case "xbox": return veyra.xbox && veyra.xbox.state.lastLabel ? veyra.xbox.state.lastLabel : qsTr("账号登录 · 实验")
                default: return qsTr("窗口或显示器")
                }
            }
            Repeater {
                id: sourceCards
                model: [
                    { glyph: "folder", title: qsTr("打开视频"), act: "file" },
                    { glyph: "video", title: qsTr("采集卡"), act: "capture" },
                    { glyph: "gamepad", title: qsTr("PS5 串流"), act: "ps5" },
                    { glyph: "cast", title: qsTr("PC 串流"), act: "moonlight" },
                    { glyph: "gamepad", title: qsTr("Xbox 串流"), act: "xbox" },
                    { glyph: "monitor", title: qsTr("屏幕捕获"), act: "screen" }
                ]
                // Stable slots preserve the onboarding target and source identity.
                delegate: Item {
                    id: slot
                    required property var modelData
                    required property int index
                    readonly property alias card: srcCard
                    implicitWidth: srcGrid.cardWidth
                    implicitHeight: srcGrid.cardHeight
                  Rectangle {
                    id: srcCard
                    readonly property var modelData: slot.modelData
                    // Content is pinned to the card edges.
                    width: slot.width
                    height: slot.height
                    radius: Theme.rCard
                    color: cardHover.hovered ? Theme.card2 : Theme.card
                    border.width: 1
                    border.color: activeFocus ? Theme.accent : cardHover.hovered ? Theme.stroke2 : Theme.stroke
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: sourceTitle.text + " · " + sourceSubtitle.text
                    Accessible.onPressAction: activate()
                    Keys.onReturnPressed: activate()
                    Keys.onEnterPressed: activate()
                    Keys.onSpacePressed: activate()
                    function activate() {
                        veyra.logUi("ui-source", "home card act=" + modelData.act)
                        switch (modelData.act) {
                        case "file": veyra.openFileDialog(); break
                        case "capture": veyra.openCaptureDialog(); break
                        case "ps5": veyra.openPs5Dialog(); break
                        case "moonlight": veyra.openMoonlightDialog(); break
                        case "xbox": veyra.openXboxDialog(); break
                        case "screen": veyra.openScreenCaptureDialog(); break
                        }
                    }

                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: root.compact ? 8 : 14
                        spacing: 12
                        Rectangle {
                            // .srccard .ico: 34x34, radius 10, translucent plate.
                            implicitWidth: 28
                            implicitHeight: 28
                            radius: Theme.rCtl
                            color: "transparent"
                            VIcon {
                                anchors.centerIn: parent
                                name: modelData.glyph
                            }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 4
                            Text {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                elide: Text.ElideRight
                                id: sourceTitle
                                text: modelData.title
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                                font.weight: Font.Medium
                            }
                            Text {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 0
                                elide: Text.ElideRight
                                id: sourceSubtitle
                                text: srcGrid.cardSub(modelData.act)
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSmall
                            }
                        }
                    }
                    // PS5 手柄控制 is switched on for the capture card.
                    VTag {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: 12
                        visible: modelData.act === "capture" && veyra.ps5Control.enabled === true
                        text: qsTr("PS5 手柄")
                        kind: "acc"
                    }
                    HoverHandler { id: cardHover; cursorShape: Qt.PointingHandCursor }
                    ToolTip.visible: cardHover.hovered && (sourceTitle.truncated || sourceSubtitle.truncated)
                    ToolTip.text: sourceTitle.text + " · " + sourceSubtitle.text
                    TapHandler {
                        id: cardTap
                        onTapped: srcCard.activate()
                    }
                  }
                }
            }
        }

        // Show the recorded session only when a source was used before.
        Rectangle {
            id: resume
            objectName: "home-resume"
            Layout.alignment: Qt.AlignHCenter
            // The last source actually used: capture card, PS5 or screen capture.
            readonly property var last: veyra.lastSource
            visible: last.kind !== undefined
            implicitWidth: srcGrid.width
            implicitHeight: 54
            radius: Theme.rCard
            border.width: 1
            border.color: Theme.stroke
            color: Theme.card
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 10
                spacing: 14
                Rectangle {
                    implicitWidth: 28; implicitHeight: 28; radius: Theme.rCtl
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
                        text: qsTr("继续上次")
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
                    text: qsTr("开始")
                    iconName: "play"
                    primary: true
                    onClicked: veyra.resumeLastSource()
                }
            }
        }

        // .recent: 692 wide, left-aligned, wrapping; "最近" in --v-t3 then 28px chips
        // with a film/image icon. Present only when there are recent files; a chip
        // whose file is gone is dimmed and does nothing (clicking it cannot open it).
        // Custom: a "清空" action sits at the right edge of this block, so it lines up
        // with 继续上次's "开始" button above it, and it wears the same primary button.
        // Anchored Item rather than a layout: the chips' own width is what a layout
        // would use for its implicit size, and the long file names then widened the
        // whole column and pushed the button out of line.
        // It confirms first, because the list is a convenience the user may still want.
        Item {
            id: recentBlock
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: srcGrid.width
            implicitHeight: recent.height
            Layout.preferredHeight: implicitHeight
            visible: veyra.recentFiles.length > 0
            Flow {
                id: recent
                anchors.left: parent.left
                anchors.right: clearRecent.left
                anchors.rightMargin: 12
                anchors.top: parent.top
                spacing: 8
                Text {
                    height: 28
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("最近")
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
                    width: Math.min(recent.width, chipRow.implicitWidth + 22)
                    height: 28
                    radius: Theme.rCtl
                    opacity: modelData.exists ? 1 : 0.45
                    color: chipHover.hovered && modelData.exists ? Qt.rgba(1, 1, 1, 0.08) : Qt.rgba(1, 1, 1, 0.04)
                    border.width: 1
                    border.color: Theme.stroke
                    RowLayout {
                        id: chipRow
                        anchors.fill: parent
                        anchors.leftMargin: 11
                        anchors.rightMargin: 11
                        spacing: 7
                        VIcon {
                            name: chip.isImage ? "image" : "film"
                            size: 14
                            color: chipText.color
                        }
                        Text {
                            id: chipText
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            elide: Text.ElideMiddle
                            text: modelData.label
                            color: chipHover.hovered && modelData.exists ? Theme.t1 : Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                        }
                    }
                    ToolTip.visible: chipHover.hovered && (!modelData.exists || chipText.truncated)
                    ToolTip.text: modelData.exists ? modelData.label : qsTr("文件已不存在")
                    HoverHandler { id: chipHover; cursorShape: modelData.exists ? Qt.PointingHandCursor : Qt.ArrowCursor }
                    TapHandler { onTapped: if (modelData.exists) veyra.openPath(modelData.path) }
                }
            }
            }
            VButton {
                id: clearRecent
                objectName: "home-clear-recent"
                // Same right inset as the resume card's own row (10), so this button's
                // right edge lands exactly on 开始's.
                anchors.right: parent.right
                anchors.rightMargin: 10
                anchors.top: parent.top
                text: qsTr("清空")
                iconName: "trash"
                ghost: true
                tip: qsTr("只清空这个列表，不会删除文件。")
                onClicked: clearRecentConfirm.open()
            }
        }
    }

    Row {
        id: guideRow
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.margins: 16
        spacing: 4
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("第一次使用？")
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }
        VButton {
            id: guideEntry
            objectName: "home-onboarding"
            text: qsTr("查看新手引导")
            activeFocusOnTab: true
            Accessible.role: Accessible.Button
            Accessible.name: text
            Accessible.onPressAction: clicked()
            Keys.onReturnPressed: clicked()
            Keys.onEnterPressed: clicked()
            Keys.onSpacePressed: clicked()
            border.width: activeFocus ? 2 : 1
            border.color: activeFocus ? Theme.accent : Theme.stroke2
            onClicked: {
                forceActiveFocus()
                root.requestOnboarding()
            }
        }
    }

    VButton {
        objectName: "home-sponsor"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 16
        text: qsTr("请作者喝杯咖啡")
        iconName: "coffee"
        ghost: true
        onClicked: {
            veyra.logUi("ui-sponsor", "home link")
            root.requestSponsor()
        }
    }

    // Custom: clearing the home page's recent list. It only drops the history (the
    // files stay on disk), so the question says so, and the accept button repeats
    // the action word from the button that opened it.
    VConfirm {
        id: clearRecentConfirm
        objectName: "home-clear-recent-confirm"
        glyph: "film"
        title: qsTr("清空最近播放记录？")
        text: qsTr("只清空这个列表，不会删除文件。")
        acceptText: qsTr("清空")
        rejectText: qsTr("取消")
        onAccepted: veyra.clearRecentFiles()
    }
}
