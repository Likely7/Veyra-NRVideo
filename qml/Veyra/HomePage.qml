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
    // The coffee line under the title: Main opens 设置 → 赞助作者.
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
        contentHeight: Math.max(height, homeContent.implicitHeight + 24)
        flickableDirection: Flickable.VerticalFlick
        boundsBehavior: Flickable.StopAtBounds
        clip: true
        ScrollBar.vertical: VScrollBar { }
    }

    ColumnLayout {
        id: homeContent
        parent: homeScroll.contentItem
        anchors.horizontalCenter: parent.horizontalCenter
        y: Math.max(root.compact ? Theme.s8 : Theme.s12, Math.round((homeScroll.height - height) / 2))
        spacing: root.compact ? Theme.s12 : Theme.s24

        Item {
            id: mark
            visible: !Theme.dusk
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: logoImg.implicitWidth
            implicitHeight: logoImg.implicitHeight
            Image {
                id: logoImg
                anchors.centerIn: parent
                source: Theme.logoImage
                sourceSize.width: root.compact ? 48 : 112
                fillMode: Image.PreserveAspectFit
            }
        }

        Item {
            id: duskMark
            visible: Theme.dusk
            Layout.alignment: Qt.AlignHCenter
            implicitWidth: wordmark.width
            implicitHeight: wordmark.height
            Image {
                id: wordmark
                objectName: "home-wordmark"
                height: Math.round(root.compact ? Math.min(88, root.height * 0.15)
                                               : Math.max(120, Math.min(180, root.height * 0.22)))
                width: Math.round(height * 1164 / 969)
                source: "wordmark-dusk.png"
                sourceSize: Qt.size(width, height)
                opacity: root.introHold ? 0 : 1
            }
        }

        // .hello: .h1 plus a muted line. The line asks for a coffee; the whole of it
        // opens the sponsor section of 设置.
        ColumnLayout {
            id: hello
            Layout.alignment: Qt.AlignHCenter
            spacing: 6
            Text {
                Layout.alignment: Qt.AlignHCenter
                visible: !Theme.dusk                    // the dusk wordmark says it
                text: "Veyra NR Video"
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsH1
                font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
            }
            Item {
                id: sponsorLine
                objectName: "home-sponsor"
                Layout.alignment: Qt.AlignHCenter
                implicitWidth: sponsorRow.implicitWidth + Theme.s16
                implicitHeight: sponsorRow.implicitHeight + Theme.s8
                activeFocusOnTab: true
                Accessible.role: Accessible.Link
                Accessible.name: qsTr("本软件开源免费，请作者喝杯咖啡")
                Accessible.onPressAction: {
                    veyra.logUi("ui-sponsor", "home link")
                    root.requestSponsor()
                }
                Keys.onReturnPressed: Accessible.onPressAction()
                Keys.onEnterPressed: Accessible.onPressAction()
                Keys.onSpacePressed: Accessible.onPressAction()
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.rCtl
                    color: sponsorTap.pressed ? Theme.pressFill : sponsorHover.hovered ? Theme.hoverFill : "transparent"
                    border.width: sponsorLine.activeFocus ? 1.5 : 0
                    border.color: Theme.focusRing
                }
                Row {
                    id: sponsorRow
                    anchors.centerIn: parent
                    spacing: 5
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("本软件开源免费，如果对你有帮助，你可以 →")
                        color: sponsorHover.hovered ? Theme.t1 : Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    VIcon {
                        anchors.verticalCenter: parent.verticalCenter
                        name: "coffee"
                        size: 14
                        color: Theme.accent
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        text: qsTr("请作者喝杯咖啡")
                        color: sponsorTap.pressed ? Theme.accentPressed : Theme.accent
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        font.weight: Font.Medium
                        font.variableAxes: Theme.axesMedium
                        font.underline: sponsorHover.hovered || sponsorLine.activeFocus
                    }
                }
                HoverHandler { id: sponsorHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    id: sponsorTap
                    onTapped: {
                        veyra.logUi("ui-sponsor", "home link")
                        root.requestSponsor()
                    }
                }
            }
        }

        VButton {
            objectName: "home-playlist"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("播放列表 · 导入文件夹"); iconName: "list"
            onClicked: veyra.showPlaylist()
        }

        Flow {
            id: srcGrid
            Layout.alignment: Qt.AlignHCenter
            Layout.preferredWidth: Math.min(1044, Math.max(320, root.width - Theme.s24 * 2))
            spacing: root.compact ? Theme.s8 : Theme.s12
            readonly property int columns: Math.max(2, Math.min(6, Math.floor((Layout.preferredWidth + spacing) / (152 + spacing))))
            readonly property int cardWidth: Math.max(136, Math.min(176, Math.floor((Layout.preferredWidth - (columns - 1) * spacing) / columns)))
            readonly property int cardHeight: root.compact ? 92 : 124
            function cardSub(act) {
                switch (act) {
                case "file": return qsTr("MP4 · MKV · 图片")
                case "capture": return veyra.hasCaptureSession ? veyra.captureSessionSummary.split(" · ")[0] : qsTr("HDMI 采集设备")
                case "ps5": return veyra.remotePlayHost.length > 0 ? qsTr("已保存主机 ") + veyra.remotePlayHost : qsTr("局域网串流")
                case "moonlight": return veyra.moonlight && veyra.moonlight.state.lastLabel ? veyra.moonlight.state.lastLabel : qsTr("Sunshine 主机")
                case "xbox": return veyra.xbox && veyra.xbox.state.lastLabel ? veyra.xbox.state.lastLabel : qsTr("账号登录 · 测试版")
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
                // The layout owns the slot's position, so the card inside it is free to
                // move its own y for the hover lift (a y set on a layout child is
                // overwritten); the entrance rise goes on the slot.
                delegate: Item {
                    id: slot
                    required property var modelData
                    required property int index
                    readonly property alias card: srcCard
                    implicitWidth: srcGrid.cardWidth
                    implicitHeight: srcGrid.cardHeight
                    VRise { page: root; target: slot; d: 2 + slot.index }
                  Rectangle {
                    id: srcCard
                    readonly property var modelData: slot.modelData
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
                    activeFocusOnTab: true
                    Accessible.role: Accessible.Button
                    Accessible.name: modelData.title + " · " + srcGrid.cardSub(modelData.act)
                    Accessible.onPressAction: activate()
                    Keys.onReturnPressed: activate()
                    Keys.onEnterPressed: activate()
                    Keys.onSpacePressed: activate()
                    width: slot.width
                    height: slot.height
                    radius: Theme.rCard
                    color: cardTap.pressed ? Theme.card3 : cardHover.hovered ? Theme.card2 : Theme.card
                    border.width: activeFocus ? 2 : 1
                    border.color: activeFocus ? Theme.focusRing : cardHover.hovered ? Theme.stroke2 : Theme.stroke
                    y: cardHover.hovered ? -3 : 0
                    Behavior on y { NumberAnimation { duration: Theme.d(Theme.durSlow); easing.bezierCurve: Theme.spring } }
                    scale: cardTap.pressed ? 0.97 : 1.0
                    Behavior on scale { NumberAnimation { duration: Theme.d(Theme.durSlow); easing.bezierCurve: Theme.spring } }

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: root.compact ? Theme.s8 : Theme.s12
                        spacing: 0
                        Rectangle {
                            implicitWidth: 34
                            implicitHeight: 34
                            radius: Theme.rCard
                            color: Theme.hoverFill
                            VIcon {
                                anchors.centerIn: parent
                                name: modelData.glyph
                            }
                        }
                        Item { Layout.fillHeight: true }
                        ColumnLayout {
                            spacing: 3
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
                                font.variableAxes: Theme.axesMedium
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
                    VTag {
                        anchors.top: parent.top
                        anchors.right: parent.right
                        anchors.margins: root.compact ? Theme.s8 : Theme.s12
                        visible: modelData.act === "capture" && veyra.ps5Control.enabled === true && !root.compact
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

        VButton {
            objectName: "home-network"
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("播放地址…")
            iconName: "cast"
            onClicked: veyra.openNetworkDialog()
        }

        // .resume: 692 wide, an accent-tinted gradient plate, shown only when a
        // capture session was actually used before. Nothing invented: the text is
        // the recorded session, and the row is absent when there is none.
        Rectangle {
            id: resume
            objectName: "home-resume"
            Layout.alignment: Qt.AlignHCenter
            readonly property var last: veyra.lastSource
            visible: last.kind !== undefined
            implicitWidth: srcGrid.width
            implicitHeight: 54
            radius: Theme.rCard
            color: resumeHover.hovered ? Theme.card2 : Theme.card
            border.width: 1
            border.color: resumeHover.hovered ? Theme.stroke2 : Theme.stroke
            HoverHandler { id: resumeHover }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: Theme.s12
                anchors.rightMargin: Theme.s12
                spacing: Theme.s12
                Rectangle {
                    implicitWidth: 34; implicitHeight: 34; radius: Theme.rCard
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
                        font.pixelSize: Theme.fsSmall
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
                    font.pixelSize: Theme.fsSmall
                }
                Repeater {
                    model: veyra.recentFiles
                    delegate: Rectangle {
                        id: chip
                        required property var modelData
                        readonly property bool isImage: /\.(png|jpe?g|bmp|webp|tiff?)$/i.test(modelData.path)
                        activeFocusOnTab: modelData.exists
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.exists ? modelData.label : qsTr("%1（文件已不存在）").arg(modelData.label)
                        Accessible.onPressAction: if (modelData.exists) veyra.openPath(modelData.path)
                        Keys.onReturnPressed: if (modelData.exists) veyra.openPath(modelData.path)
                        Keys.onEnterPressed: if (modelData.exists) veyra.openPath(modelData.path)
                        Keys.onSpacePressed: if (modelData.exists) veyra.openPath(modelData.path)
                        width: Math.min(recent.width, chipRow.implicitWidth + 22)
                        height: 28
                        radius: height / 2
                        opacity: modelData.exists ? 1 : Theme.disabledOpacity
                        color: chipTap.pressed && modelData.exists ? Theme.pressFill
                             : chipHover.hovered && modelData.exists ? Theme.hoverFill : Theme.card
                        border.width: activeFocus ? 1.5 : 1
                        border.color: activeFocus ? Theme.focusRing : Theme.stroke
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
                                font.pixelSize: Theme.fsSmall
                            }
                        }
                        ToolTip.visible: chipHover.hovered && (!modelData.exists || chipText.truncated)
                        ToolTip.text: modelData.exists ? modelData.label : qsTr("文件已不存在")
                        HoverHandler { id: chipHover; cursorShape: modelData.exists ? Qt.PointingHandCursor : Qt.ArrowCursor }
                        TapHandler { id: chipTap; onTapped: if (modelData.exists) veyra.openPath(modelData.path) }
                    }
                }
            }
            VButton {
                id: clearRecent
                objectName: "home-clear-recent"
                anchors.right: parent.right
                anchors.rightMargin: Theme.s12
                anchors.top: parent.top
                text: qsTr("清空")
                iconName: "trash"
                primary: true
                tip: qsTr("只清空这个列表，不会删除文件")
                onClicked: clearRecentConfirm.open()
            }
        }
    }

    Row {
        id: guideRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.leftMargin: Theme.s16
        anchors.rightMargin: 64
        anchors.bottomMargin: Theme.s16
        spacing: Theme.s4
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
            border.color: activeFocus ? Theme.focusRing : Theme.stroke2
            onClicked: {
                forceActiveFocus()
                root.requestOnboarding()
            }
        }
    }

    // [data-in] entrance order from pages-a.js (the source cards rise from their delegate, --d 2-5).
    VRise { target: mark; d: 0 }
    VRise { target: hello; d: 1 }
    VRise { target: resume; d: 6 }
    VRise { target: recent; d: 7 }

    // Custom: clearing the home page's recent list. It only drops the history (the
    // files stay on disk), so the question says so, and the accept button repeats
    // the action word from the button that opened it.
    VConfirm {
        id: clearRecentConfirm
        objectName: "home-clear-recent-confirm"
        glyph: "film"
        title: qsTr("清空最近播放记录？")
        text: qsTr("只清空这个列表，不会删除文件")
        acceptText: qsTr("清空")
        rejectText: qsTr("取消")
        onAccepted: veyra.clearRecentFiles()
    }
}
