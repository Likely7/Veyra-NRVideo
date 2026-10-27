// moonlight dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: mlDialog
    objectName: "moonlight-dialog"
    key: "moonlight"
    glyph: "cast"
    title: qsTr("PC 串流")
    sub: qsTr("串流另一台电脑（Sunshine 主机）· 增强与调色沿用软件自己的处理链")
    dialogWidth: 760
    readonly property var ml: veyra.moonlight
    readonly property var st: ml ? ml.state : ({})
    readonly property var hostList: ml ? ml.hosts : []
    readonly property var appList: ml ? ml.apps : []
    readonly property var cfg: st.settings || ({})
    property int pickedApp: -1
    readonly property int effectiveApp: pickedApp >= 0 ? pickedApp
        : (st.runningApp > 0 ? st.runningApp : (appList.length > 0 ? appList[0].id : -1))
    readonly property bool canStart: !!st.paired && !!st.online && !st.busy && effectiveApp >= 0 && !st.streaming
    actions: st.streaming ? [
        { label: qsTr("关闭") },
        { label: qsTr("断开"), primary: true }
    ] : [
        { label: st.busy ? qsTr("取消操作") : qsTr("关闭") },
        { label: qsTr("开始串流"), primary: true, icon: "play" }
    ]
    onActionTriggered: label => {
        if (label === qsTr("断开")) { veyra.moonlightDisconnect(); dialogHost.close(); return }
        if (label === qsTr("取消操作")) { ml.cancel(); return }
        if (label === qsTr("关闭")) { dialogHost.close(); return }
        if (canStart) ml.connectStream(effectiveApp)
    }
    Connections {
        target: mlDialog
        function onShownChanged() {
            if (!mlDialog.ml) return
            if (mlDialog.shown) { mlDialog.pickedApp = -1; mlDialog.ml.load() }
            else mlDialog.ml.unload()
        }
    }
    Connections {
        target: mlDialog.ml
        function onStarted() { dialogHost.close(); dialogHost.startMoonlight() }
    }
    readonly property var resolutions: [
        { id: "720p", label: "720p · 1280×720" }, { id: "1080p", label: "1080p · 1920×1080" },
        { id: "1440p", label: "1440p · 2560×1440" }, { id: "4k", label: "4K · 3840×2160" },
        { id: "native", label: qsTr("本机屏幕分辨率") }]
    readonly property var rates: [30, 60, 90, 120, 144].map(v => ({ id: String(v), label: v + " fps" }))
    function labelOf(list, id, fallback) { const f = list.find(x => String(x.id) === String(id)); return f ? f.label : fallback }
    function stateText(s) {
        return s === "online" ? qsTr("在线") : s === "unpaired" ? qsTr("未配对") : s === "busy" ? qsTr("在线 · 有游戏在运行")
             : s === "checking" ? qsTr("检查中…") : qsTr("离线")
    }

    DialogSection { text: qsTr("主机") }
    DialogNote {
        visible: mlDialog.hostList.length === 0
        text: qsTr("还没有主机。在要被串流的电脑上安装 Sunshine（官网 app.lizardbyte.dev/Sunshine，软件不自带），")
            + qsTr("两台电脑接在同一个局域网，这里会自动出现；也可以在下面手填它的 IP。首次使用需要配对。")
    }
    Repeater {
        model: mlDialog.hostList
        delegate: Rectangle {
            id: hostRow
            required property var modelData
            Layout.fillWidth: true
            Layout.topMargin: 6
            implicitHeight: 54
            radius: 11
            color: modelData.selected ? Theme.accentSoft : (hostHover.hovered ? Theme.card3 : Theme.card2)
            border.width: 1
            border.color: modelData.selected ? Theme.accent : Theme.stroke
            objectName: "moonlight-host-" + modelData.id
            HoverHandler { id: hostHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: mlDialog.ml.select(hostRow.modelData.id) }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 14
                anchors.rightMargin: 10
                spacing: 12
                VDot {
                    warn: hostRow.modelData.state === "unpaired"
                    off: hostRow.modelData.state === "offline" || hostRow.modelData.state === "checking"
                    Layout.alignment: Qt.AlignVCenter
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: hostRow.modelData.name
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    Text {
                        Layout.fillWidth: true
                        text: hostRow.modelData.address + " · " + mlDialog.stateText(hostRow.modelData.state)
                            + (hostRow.modelData.gpu.length > 0 ? " · " + hostRow.modelData.gpu : "")
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSmall
                        elide: Text.ElideRight
                    }
                }
                VTag { visible: hostRow.modelData.paired; text: qsTr("已配对"); kind: "ok" }
                VButton {
                    visible: !hostRow.modelData.paired && hostRow.modelData.state !== "offline"
                    objectName: "moonlight-pair-" + hostRow.modelData.id
                    text: qsTr("配对")
                    primary: true
                    enabled: !mlDialog.st.busy
                    onClicked: mlDialog.ml.pair(hostRow.modelData.id)
                }
                VButton {
                    text: qsTr("删除")
                    ghost: true
                    enabled: !mlDialog.st.busy
                    onClicked: mlDialog.ml.forget(hostRow.modelData.id)
                }
            }
        }
    }
    DialogGroup {
        VRow {
            label: qsTr("手动添加")
            hint: qsTr("IP 或主机名，端口不是 47989 时写成 192.168.1.20:47989")
            RowLayout {
                spacing: 6
                DialogTextField { id: mlAddress; objectName: "moonlight-address"; implicitWidth: 190; placeholder: "192.168.1.x"; onEdited: text => { if (text.length > 0) { mlDialog.ml.addHost(text); mlAddress.text = "" } } }
                VSpinner { visible: mlDialog.st.busy === true; Layout.alignment: Qt.AlignVCenter }
                VButton { text: qsTr("添加"); ghost: true; enabled: !mlDialog.st.busy && mlAddress.text.length > 0; onClicked: { mlDialog.ml.addHost(mlAddress.text); mlAddress.text = "" } }
                VButton { text: qsTr("刷新"); ghost: true; enabled: !mlDialog.st.busy; onClicked: mlDialog.ml.refresh() }
            }
        }
    }
    // The PIN the user types into the host's Sunshine page.
    Rectangle {
        visible: mlDialog.st.pairing === true
        objectName: "moonlight-pin-box"
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: pinCol.implicitHeight + 28
        radius: 11
        color: Theme.accentSoft
        border.width: 1
        border.color: Theme.accent
        ColumnLayout {
            id: pinCol
            anchors.fill: parent
            anchors.margins: 14
            spacing: 6
            Text {
                Layout.fillWidth: true
                text: qsTr("在主机上打开 Sunshine 网页（https://主机IP:47990），进入「PIN」页，输入下面的数字并提交：")
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
                wrapMode: Text.WordWrap
            }
            Text {
                objectName: "moonlight-pin"
                Layout.alignment: Qt.AlignHCenter
                text: mlDialog.st.pin || ""
                color: Theme.t1
                font.family: Theme.fontMono
                font.pixelSize: 34
                font.letterSpacing: 10
            }
        }
    }

    DialogSection { visible: !!mlDialog.st.paired; text: qsTr("游戏与程序 · ") + (mlDialog.st.hostName || "") }
    DialogNote {
        visible: !!mlDialog.st.selected && !mlDialog.st.paired && !mlDialog.st.pairing
        text: qsTr("这台主机还没有和本机配对。点「配对」，在主机的 Sunshine 网页里输入软件给出的 PIN。")
    }
    DialogNote {
        visible: !!mlDialog.st.paired && !mlDialog.st.online
        text: qsTr("主机现在连不上（离线、休眠，或地址变了）。开机后点「刷新」。")
    }
    Flow {
        visible: !!mlDialog.st.paired && !!mlDialog.st.online
        Layout.fillWidth: true
        spacing: 8
        Repeater {
            model: mlDialog.appList
            delegate: Rectangle {
                id: appCard
                required property var modelData
                readonly property bool picked: mlDialog.effectiveApp === modelData.id
                objectName: "moonlight-app-" + modelData.id
                width: 168
                height: 64
                radius: 11
                color: picked ? Theme.accentSoft : (appHover.hovered ? Theme.card3 : Theme.card2)
                border.width: 1
                border.color: picked ? Theme.accent : Theme.stroke
                HoverHandler { id: appHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: mlDialog.pickedApp = appCard.modelData.id
                    onDoubleTapped: { mlDialog.pickedApp = appCard.modelData.id; if (mlDialog.canStart) mlDialog.ml.connectStream(appCard.modelData.id) }
                }
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 11
                    spacing: 4
                    Text {
                        Layout.fillWidth: true
                        text: appCard.modelData.name
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }
                    RowLayout {
                        spacing: 6
                        VTag { visible: appCard.modelData.running; text: qsTr("运行中"); kind: "acc" }
                        VTag { visible: appCard.modelData.hdr; text: "HDR" }
                    }
                }
            }
        }
    }
    RowLayout {
        visible: !!mlDialog.st.paired && !!mlDialog.st.online && mlDialog.st.runningApp > 0
        Layout.fillWidth: true
        Layout.topMargin: 8
        spacing: 8
        Text {
            Layout.fillWidth: true
            text: qsTr("主机上有游戏在运行。断开串流不会结束它；选它并开始即可继续。")
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
            wrapMode: Text.WordWrap
        }
        VButton { objectName: "moonlight-quit-app"; text: qsTr("退出游戏"); ghost: true; enabled: !mlDialog.st.busy; onClicked: mlDialog.ml.quitApp() }
    }

    DialogSection { text: qsTr("串流设置（只影响串流本身，按主机保存）") }
    DialogGroup {
        VRow {
            label: qsTr("分辨率")
            hint: qsTr("主机按这个尺寸编码；软件的超分与增强在收到之后再做")
            VSelect { objectName: "moonlight-res"; implicitWidth: 190; value: mlDialog.labelOf(mlDialog.resolutions, mlDialog.cfg.res, "1080p · 1920×1080"); options: mlDialog.resolutions; onPicked: id => mlDialog.ml.set("res", id) }
        }
        VRow {
            label: qsTr("帧率")
            VSelect { objectName: "moonlight-fps"; implicitWidth: 130; value: (mlDialog.cfg.fps || 60) + " fps"; options: mlDialog.rates; onPicked: id => mlDialog.ml.set("fps", Number(id)) }
        }
        VRow {
            label: qsTr("码率")
            hint: qsTr("默认 150 Mbps；有线千兆可以拉到 300–500，无线卡顿时调低。主机画面简单时实际码率会低于这里的上限")
            RowLayout {
                spacing: 10
                VSlider {
                    id: bitrateSlider
                    objectName: "moonlight-bitrate"
                    implicitWidth: 170
                    from: 5; to: 500
                    live: false
                    value: mlDialog.cfg.bitrate || 150
                    onMoved: v => mlDialog.ml.set("bitrate", Math.max(5, Math.round(v / 5) * 5))
                }
                VSliderValue {
                    Layout.preferredWidth: 72
                    slider: bitrateSlider
                    horizontalAlignment: Text.AlignLeft
                    text: Math.max(5, Math.round((bitrateSlider.dragging ? bitrateSlider.dragValue : bitrateSlider.value) / 5) * 5) + " Mbps"
                }
            }
        }
        VRow {
            label: qsTr("编码")
            hint: mlDialog.cfg.codec === 3 ? qsTr("AV1 需要主机显卡能编码、本机显卡能解码") : qsTr("自动：优先 HEVC，主机不支持时用 H.264")
            VSeg {
                objectName: "moonlight-codec"
                options: [{ id: "0", label: qsTr("自动") }, { id: "1", label: "H.264" }, { id: "2", label: "HEVC" }, { id: "3", label: "AV1" }]
                current: String(mlDialog.cfg.codec || 0)
                onPicked: id => mlDialog.ml.set("codec", Number(id))
            }
        }
        VRow {
            label: "HDR"
            hint: mlDialog.st.hdrHost ? qsTr("需要主机显示器开启 HDR，且编码选 HEVC 或 AV1；本机需要硬件解码") : qsTr("这台主机没有报告 10 位编码能力")
            VSwitch { objectName: "moonlight-hdr"; enabled: !!mlDialog.st.hdrHost || mlDialog.cfg.hdr === true; checked: mlDialog.cfg.hdr === true; onToggled: checked => mlDialog.ml.set("hdr", checked) }
        }
        VRow {
            label: qsTr("声道")
            VSeg {
                objectName: "moonlight-audio"
                options: [{ id: "2", label: qsTr("立体声") }, { id: "6", label: "5.1" }, { id: "8", label: "7.1" }]
                current: String(mlDialog.cfg.audio || 2)
                onPicked: id => mlDialog.ml.set("audio", Number(id))
            }
        }
        VRow {
            label: qsTr("手柄")
            hint: qsTr("把电脑手柄当作主机上的 Xbox 手柄（1 号）")
            VSwitch { objectName: "moonlight-gamepad"; checked: mlDialog.cfg.gamepad !== false; onToggled: checked => mlDialog.ml.set("gamepad", checked) }
        }
        VRow {
            label: qsTr("自动捕获键盘鼠标")
            hint: qsTr("开始后键鼠交给主机；Ctrl+Alt+Shift+Z 释放，+Q 断开，+S 统计")
            VSwitch { objectName: "moonlight-capture"; checked: mlDialog.cfg.captureInput !== false; onToggled: checked => mlDialog.ml.set("captureInput", checked) }
        }
        VRow {
            label: qsTr("让主机切换到串流分辨率")
            hint: qsTr("关闭时主机保持自己的分辨率，画面由主机缩放")
            VSwitch { objectName: "moonlight-sops"; checked: mlDialog.cfg.sops === true; onToggled: checked => mlDialog.ml.set("sops", checked) }
        }
        VRow {
            label: qsTr("主机同时出声")
            hint: qsTr("默认只在本机出声")
            VSwitch { objectName: "moonlight-hostaudio"; checked: mlDialog.cfg.hostAudio === true; onToggled: checked => mlDialog.ml.set("hostAudio", checked) }
        }
    }
    DialogNote { objectName: "moonlight-status"; visible: (mlDialog.st.status || "").length > 0; text: (mlDialog.st.busy ? qsTr("处理中 · ") : "") + (mlDialog.st.status || "") }
}
