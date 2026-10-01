// The five dialogs, rebuilt from the design's dialogs.js.
//
// Each mirrors the design's structure: a header with a colour-plated icon and a
// subtitle, a scrolling body of sections (a heading with a hairline, then a group
// of label/control rows), and a footer with the actions.
//
// Scope: the dialog edits only what the engine actually carries. Device and screen
// target lists come from the source's own enumeration; force-SDR and flip are real
// settings. The format list, audio monitoring, bitstream passthrough, subtitle
// style and remote-play detail are not wired to anything yet, and each says so on
// the page instead of being drawn as a control that would do nothing.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: host
    anchors.fill: parent
    z: 100

    // Which dialog is showing: "" | capture | ps5 | moonlight | xbox | screen | subtitle | audio
    property string dialog: ""

    function open(key) {
        if (key === "save") saveDialog.resetForOpen()
        if (key === "manage") {
            manageDialog.mode = veyra.nodeMode === 1 ? "node" : "list"
            manageDialog.feedback = ""
        }
        host.dialog = key
    }
    function close() { host.dialog = "" }

    signal startCapture()
    signal startPs5()
    signal startMoonlight()
    signal startXbox()
    signal startScreen()

    // Scrim (M16): the design dims the page behind the dialog, opacity .2s linear.
    // The native video window cannot be dimmed from QML; only the dialog panel is cut
    // out of it (videoCover below), so the picture beside the dialog stays undimmed.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)
        opacity: host.dialog !== "" ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        TapHandler { onTapped: host.close() }
    }
    // For the motion probe: the open dialog's scale.
    property real motionScale: 1

    // --- shared chrome ----------------------------------------------------
    component DLayer: Rectangle {
        id: dlg
        property string title: ""
        property string sub: ""
        property string glyph: ""
        property int dialogWidth: 640
        property var actions: []
        // dialogs.js `foot`: a quiet note at the left of the button row.
        property string foot: ""
        default property alias body: bodyCol.data
        signal actionTriggered(string label)

        // Which dialog key shows this layer.
        property string key: ""
        readonly property bool shown: key.length > 0 && host.dialog === key
        // M17: .dlg scale .9 -> 1 and translate 0 14px -> 0 over .55s --spring inside the
        // scrim's .2s fade; closing runs the same back while the scrim fades out.
        property real motionS: 0.9
        property real motionDy: 14
        onShownChanged: { if (shown) { motionS = 0.9; motionDy = 14; enterAnim.restart() } else exitAnim.restart() }
        onMotionSChanged: if (shown) host.motionScale = motionS
        ParallelAnimation {
            id: enterAnim
            NumberAnimation { target: dlg; property: "motionS"; to: 1; duration: Theme.d(550); easing.bezierCurve: Theme.spring }
            NumberAnimation { target: dlg; property: "motionDy"; to: 0; duration: Theme.d(550); easing.bezierCurve: Theme.spring }
        }
        ParallelAnimation {
            id: exitAnim
            NumberAnimation { target: dlg; property: "motionS"; to: 0.9; duration: Theme.d(550); easing.bezierCurve: Theme.spring }
            NumberAnimation { target: dlg; property: "motionDy"; to: 14; duration: Theme.d(550); easing.bezierCurve: Theme.spring }
        }
        opacity: shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        transform: [
            Scale { origin.x: dlg.width / 2; origin.y: dlg.height / 2; xScale: dlg.motionS; yScale: dlg.motionS },
            Translate { y: dlg.motionDy }
        ]
        // Cut out of the native video window (main.cpp syncVideoCovers). The flag,
        // not the objectName: each dialog below names itself for tests.
        objectName: "videoCover"
        readonly property bool videoCover: true
        property real coverRadius: 16

        anchors.centerIn: parent
        width: dialogWidth
        // Never taller than the window: the body scrolls instead of the panel
        // being cut off at the top and bottom of a small window.
        // Header and footer each carry 16px margins top and bottom (64 in all); the
        // old +40 left every body 24px short, so short dialogs hid their last row.
        implicitHeight: Math.min(720, host.height - 24, header.height + bodyScroll.contentHeight + footer.height + 64)
        height: implicitHeight
        // Swallow presses on the panel itself so they never reach the scrim
        // below (a passive tap on a control used to close the whole dialog).
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; z: -1 }
        radius: 16
        color: Theme.dialog
        border.width: 1
        border.color: Theme.stroke2

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                id: header
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 12
                Rectangle {
                    implicitWidth: 34; implicitHeight: 34; radius: 10
                    color: Qt.rgba(1, 1, 1, 0.06)
                    VIcon { anchors.centerIn: parent; name: dlg.glyph }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        // Fill: without a subtitle this is the column's only child, and
                        // its natural width would cap the column and pull the close
                        // button in beside the title.
                        Layout.fillWidth: true
                        elide: Text.ElideRight
                        text: dlg.title
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsH2
                        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: dlg.sub.length > 0
                        text: dlg.sub
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                VButton { icon: true; iconName: "x"; ghost: true; onClicked: host.close() }
            }

            Flickable {
                id: bodyScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                contentHeight: bodyCol.implicitHeight
                clip: true
                ScrollBar.vertical: VScrollBar { }
                ColumnLayout {
                    id: bodyCol
                    // Keep the groups clear of the overlaid scroll bar.
                    width: parent.width - (bodyScroll.contentHeight > bodyScroll.height ? 12 : 0)
                    spacing: 0
                }
            }

            RowLayout {
                id: footer
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 8
                Text {
                    objectName: "dialog-foot"
                    Layout.fillWidth: true
                    visible: dlg.foot.length > 0
                    text: dlg.foot
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
                Item { Layout.fillWidth: true; visible: dlg.foot.length === 0 }
                Repeater {
                    model: dlg.actions
                    delegate: VButton {
                        required property var modelData
                        text: modelData.label
                        iconName: modelData.icon || ""
                        primary: modelData.primary === true
                        onClicked: dlg.actionTriggered(modelData.label)
                    }
                }
            }
        }
    }

    // --- 采集卡 -----------------------------------------------------------
    // The 1.4.4 capture panel, in the new design: device details and formats are
    // queried asynchronously, only an explicitly chosen audio input is opened,
    // and the choice is remembered for "继续上次采集".
    DLayer {
        id: captureDialog
        objectName: "capture-dialog"
        key: "capture"
        glyph: "video"
        title: "采集卡"
        sub: "连接设备后，先关闭增强确认基础画面，再按需开启"
        dialogWidth: 660
        foot: "关闭其他占用同一采集卡的软件"
        actions: [
            { label: "取消" },
            { label: "连接并开始", primary: true, icon: "play" }
        ]
        onActionTriggered: label => {
            if (label !== "连接并开始") { host.close(); return }
            if (veyra.startCaptureSession()) { host.close(); host.startCapture() }
        }
        Connections {
            target: host
            function onDialogChanged() { if (host.dialog === "capture") veyra.refreshCaptureDevices() }
        }
        function labelOf(list, id, fallback) { const f = list.find(x => String(x.id) === String(id)); return f ? f.label : fallback }
        readonly property bool live: veyra.captureSignalLevel !== "idle"

        // dialogs.js .cap-prev: the picture and a signal line under it. The
        // picture is the session's own poster still when the engine made one;
        // there is no periodic frame grab here, because that would sit on the
        // live capture path. The status line is live.
        Rectangle {
            objectName: "capture-preview"
            Layout.fillWidth: true
            Layout.topMargin: 4
            implicitHeight: 180 + 34
            radius: 10
            color: Theme.videoBlack
            border.width: 1
            border.color: Theme.stroke
            clip: true
            Image {
                id: capturePoster
                objectName: "capture-preview-image"
                anchors.left: parent.left; anchors.right: parent.right; anchors.top: parent.top
                anchors.margins: 1
                height: 180
                fillMode: Image.PreserveAspectCrop
                source: captureDialog.live ? veyra.capturePreviewUrl : ""
                visible: status === Image.Ready
            }
            Column {
                anchors.horizontalCenter: parent.horizontalCenter
                y: 90 - height / 2
                spacing: 8
                visible: !capturePoster.visible
                VIcon { anchors.horizontalCenter: parent.horizontalCenter; name: "video"; size: 22; color: Theme.t3 }
                Text {
                    text: captureDialog.live ? "画面在主窗口播放" : "连接后在主窗口显示画面"
                    color: Theme.t3
                    font.family: Theme.fontUi
                    font.pixelSize: 12
                }
            }
            RowLayout {
                anchors.left: parent.left; anchors.right: parent.right; anchors.bottom: parent.bottom
                height: 34
                anchors.leftMargin: 10; anchors.rightMargin: 10
                spacing: 8
                VTag {
                    objectName: "capture-signal"
                    text: veyra.captureSignalText
                    kind: veyra.captureSignalLevel === "ok" ? "ok" : veyra.captureSignalLevel === "warn" ? "warn" : ""
                }
                Text {
                    Layout.fillWidth: true
                    text: captureDialog.live
                          ? veyra.sourceSummary.replace("x", "×") + " · " + veyra.captureFps.toFixed(2) + " fps"
                            + (veyra.sourceFormatText.split(" · ").length > 2 ? " · " + veyra.sourceFormatText.split(" · ")[1] : "")
                          : captureDialog.labelOf(veyra.captureFormats, veyra.captureFormatKey, "选择设备和格式后连接")
                    color: Theme.t2
                    font.family: Theme.fontMono
                    font.pixelSize: 11
                    elide: Text.ElideRight
                }
            }
        }

        DSection { text: "设备" }
        DGroup {
            VRow {
                label: "视频输入设备"
                hint: veyra.captureQueryBusy ? "正在查询…当前播放继续" : (veyra.captureDevices.length === 0 ? "未检测到采集设备" : "")
                RowLayout {
                    spacing: 6
                    VSelect {
                        objectName: "capture-device"
                        implicitWidth: 220
                        value: veyra.captureDeviceLabel
                        options: veyra.captureDevices
                        onPicked: id => veyra.captureDeviceId = id
                    }
                    // M38: the refresh icon becomes a spinner while the query runs.
                    Item {
                        implicitWidth: Theme.ctlHeight; implicitHeight: Theme.ctlHeight
                        VButton {
                            objectName: "capture-refresh"
                            anchors.fill: parent
                            visible: !veyra.captureQueryBusy
                            icon: true; ghost: true; iconName: "refresh"; tip: "刷新设备"
                            onClicked: veyra.refreshCaptureDevices()
                        }
                        VSpinner { anchors.centerIn: parent; visible: veyra.captureQueryBusy }
                    }
                }
            }
            VRow {
                label: "设备实际支持的格式"
                hint: { const f = veyra.captureFormats.find(x => x.id === veyra.captureFormatKey); return f && f.costHint ? "该格式比低延迟格式多一道处理环节，高分辨率/高帧率下建议优先低延迟格式" : "" }
                VSelect {
                    objectName: "capture-format"
                    implicitWidth: 260
                    value: captureDialog.labelOf(veyra.captureFormats, veyra.captureFormatKey, veyra.captureFormats.length ? "请选择格式" : "—")
                    options: veyra.captureFormats
                    onPicked: id => veyra.captureFormatKey = id
                }
            }
            VRow {
                label: "设备帧率 FPS"
                hint: "0 = 沿用所选格式；设备返回其他帧率会报错，不在软件中偷偷丢帧"
                VTextField {
                    objectName: "capture-fps"
                    implicitWidth: 100
                    text: String(veyra.captureRequestedFps)
                    onEdited: text => veyra.captureRequestedFps = Number(text)
                }
            }
        }
        DSection { text: "音频" }
        DGroup {
            VRow {
                label: "音频监听"
                hint: "只连接明确选中的输入，默认不监听"
                VSelect {
                    objectName: "capture-audio"
                    implicitWidth: 260
                    value: captureDialog.labelOf(veyra.captureAudioInputs, veyra.captureAudioChoice, "不监听音频")
                    options: veyra.captureAudioInputs
                    onPicked: id => veyra.captureAudioChoice = Number(id)
                }
            }
            VRow {
                label: "Dolby / DTS 位流"
                VSeg {
                    options: [{ id: "0", label: "自动" }, { id: "1", label: "强制 PCM" }, { id: "2", label: "位流优先" }]
                    current: String(veyra.captureAudioIngress)
                    onPicked: id => veyra.captureAudioIngress = Number(id)
                }
            }
        }
        DSection { text: "格式与画面" }
        DGroup {
            VRow {
                label: "输入色彩（SDR / HDR）"
                hint: "不确定就选「自动」。采集 HDR：采集格式选 P010，这里选「HDR · PQ」（游戏机、显卡的 HDR10 都是 PQ）。「HDR · HLG」用于广播信号。「SDR · 709」是普通画面。变更需重连"
                VSeg {
                    options: [{ id: "0", label: "自动" }, { id: "1", label: "HDR · PQ" }, { id: "2", label: "HDR · HLG" }, { id: "3", label: "SDR · 709" }]
                    current: String(veyra.captureColorSpace)
                    onPicked: id => veyra.captureColorSpace = Number(id)
                }
            }
            VRow {
                label: "输入范围"
                hint: "Limited = 16–235，Full = 0–255。画面发灰选「有限」，暗部全黑、亮部过曝选「完全」"
                VSeg {
                    options: [{ id: "0", label: "自动" }, { id: "1", label: "有限 · Limited" }, { id: "2", label: "完全 · Full" }]
                    current: String(veyra.captureColorRange)
                    onPicked: id => veyra.captureColorRange = Number(id)
                }
            }
            VRow {
                label: "设备缓冲"
                hint: "自动：1080p 及以下 2 帧，更高 3 帧；最小 1 帧延迟最低但可能丢帧"
                VSeg {
                    options: [{ id: "0", label: "自动" }, { id: "1", label: "最小" }, { id: "2", label: "驱动默认" }]
                    current: String(veyra.captureBufferMode)
                    onPicked: id => veyra.captureBufferMode = Number(id)
                }
            }
            VRow {
                label: "转为 SDR 显示"
                hint: "收到 HDR 也按 SDR 预览，立即生效"
                VSwitch {
                    checked: veyra.captureForceSdr
                    onToggled: veyra.captureForceSdr = checked
                }
            }
            VRow {
                label: "画面上下翻转"
                hint: "采集画面倒置时开启，立即生效"
                VSwitch {
                    checked: veyra.captureFlipVertical
                    onToggled: veyra.captureFlipVertical = checked
                }
            }
        }
        DNote { visible: veyra.captureStatus.length > 0; text: veyra.captureStatus }
    }

    // --- PS5 串流 ---------------------------------------------------------
    // The 1.4.4 Remote Play panel: saved pairings are encrypted in the user's
    // %LOCALAPPDATA%\Veyra\remoteplay and shared with 1.4.4.
    DLayer {
        id: ps5Dialog
        objectName: "ps5-dialog"
        key: "ps5"
        glyph: "gamepad"
        title: "PS5 串流"
        sub: "局域网 Remote Play · 凭据加密保存在本机"
        dialogWidth: 680
        actions: [
            { label: veyra.ps5.active ? "断开" : "取消" },
            { label: veyra.ps5.active ? "应用设置并重连" : "连接", primary: true }
        ]
        onActionTriggered: label => {
            if (label === "断开") { veyra.ps5Cancel(); return }
            if (label === "取消") { if (veyra.ps5.busy) veyra.ps5Cancel(); else host.close(); return }
            if (veyra.ps5Connect()) { host.close(); host.startPs5() }
        }
        Connections {
            target: host
            function onDialogChanged() { if (host.dialog === "ps5") veyra.ps5Load() }
        }
        readonly property var qualities: [{ id: "0", label: "720p · 30 fps" }, { id: "1", label: "720p · 60 fps" }, { id: "2", label: "1080p · 30 fps" }, { id: "3", label: "1080p · 60 fps" }]
        readonly property var bitrates: [5, 10, 15, 20, 30, 50, 80, 100].map((m, i) => ({ id: String(i), label: m + " Mbps" }))

        DSection { text: "主机" }
        DGroup {
            VRow {
                visible: veyra.ps5Profiles.length > 0
                label: "已保存主机"
                VSelect {
                    objectName: "ps5-profile"
                    implicitWidth: 220
                    value: { const p = veyra.ps5Profiles.find(x => x.id === veyra.ps5.profile); return p ? p.label : "请选择" }
                    options: veyra.ps5Profiles
                    onPicked: id => veyra.ps5SelectProfile(id)
                }
            }
            VRow {
                label: "主机地址"
                hint: "PS5 设置 → 网络 → 连接状态；找不到时可手填 IP"
                RowLayout {
                    spacing: 6
                    VTextField { objectName: "ps5-host"; implicitWidth: 170; placeholder: "192.168.1.x"; text: veyra.ps5.host || ""; onEdited: text => veyra.ps5Set("host", text) }
                    VSpinner { visible: veyra.ps5.busy === true; Layout.alignment: Qt.AlignVCenter }
                    VButton { text: "查找"; ghost: true; enabled: !veyra.ps5.busy; onClicked: veyra.ps5Scan() }
                    VButton { text: "唤醒"; ghost: true; enabled: !veyra.ps5.busy && veyra.ps5.profile.length > 0; onClicked: veyra.ps5Wake() }
                }
            }
        }
        DSection { text: "配对（首次）" }
        DGroup {
            VRow {
                label: "PSN Account ID"
                hint: "账号数字 ID 或 8 字节 Base64，不是昵称；登录 PSN 后自动填入"
                VTextField { implicitWidth: 240; text: veyra.ps5.account || ""; onEdited: text => veyra.ps5Set("account", text) }
            }
            VRow {
                label: "8 位配对码"
                hint: "PS5 设置 → 系统 → 远程游玩 → 关联设备"
                RowLayout {
                    spacing: 6
                    VTextField { id: pairPin; implicitWidth: 120; placeholder: "••••••••" }
                    VButton { text: "配对并保存"; enabled: !veyra.ps5.busy; onClicked: { veyra.ps5Pair(pairPin.text); pairPin.text = "" } }
                }
            }
            VRow {
                label: "删除配对"
                hint: "只删除本机保存的这台主机；以后需要重新配对"
                VButton { text: "删除"; ghost: true; enabled: !veyra.ps5.busy && veyra.ps5.profile.length > 0; onClicked: veyra.ps5Forget() }
            }
        }
        DSection { text: "PSN 账号" }
        DGroup {
            VRow {
                label: veyra.ps5.psnReady ? "已登录" : "未登录"
                hint: "在 Sony 网页登录后复制回调地址再提交；软件不接触密码"
                RowLayout {
                    spacing: 6
                    VButton { text: "登录 PSN"; ghost: true; enabled: !veyra.ps5.busy; onClicked: veyra.ps5PsnLogin() }
                    VButton { text: "提交登录结果"; ghost: true; enabled: !veyra.ps5.busy; onClicked: veyra.ps5PsnComplete() }
                    VButton { text: "退出"; ghost: true; enabled: !veyra.ps5.busy && veyra.ps5.psnReady; onClicked: veyra.ps5PsnForget() }
                }
            }
        }
        DSection { text: "串流（重连生效）" }
        DGroup {
            VRow {
                label: "分辨率与帧率"
                VSelect { implicitWidth: 170; value: ps5Dialog.qualities[veyra.ps5.quality || 0].label; options: ps5Dialog.qualities; onPicked: id => veyra.ps5Set("quality", Number(id)) }
            }
            VRow {
                label: "编码"
                hint: "HDR 需要 PS5 实际输出 HDR 且 Windows HDR 开启"
                VSeg {
                    options: [{ id: "0", label: "H.264" }, { id: "1", label: "H.265" }, { id: "2", label: "H.265 HDR" }]
                    current: String(veyra.ps5.codec || 0)
                    onPicked: id => veyra.ps5Set("codec", Number(id))
                }
            }
            VRow {
                label: "请求码率"
                hint: "发给 PS5 的带宽请求，主机不保证达到"
                VSelect { implicitWidth: 130; value: ps5Dialog.bitrates[veyra.ps5.bitrate || 0].label; options: ps5Dialog.bitrates; onPicked: id => veyra.ps5Set("bitrate", Number(id)) }
            }
            VRow {
                label: "解码"
                VSeg {
                    options: [{ id: "0", label: "自动" }, { id: "1", label: "CPU 软件" }, { id: "2", label: "D3D12VA 硬件" }]
                    current: String(veyra.ps5.decode || 0)
                    onPicked: id => veyra.ps5Set("decode", Number(id))
                }
            }
            VRow {
                label: "采样"
                hint: "精细：按位置还原色度，放大用双三次；不是 AI 超分"
                VSeg {
                    options: [{ id: "0", label: "兼容" }, { id: "1", label: "精细" }]
                    current: String(veyra.ps5.sampling === undefined ? 1 : veyra.ps5.sampling)
                    onPicked: id => veyra.ps5Set("sampling", Number(id))
                }
            }
            VRow {
                label: "仅观看"
                hint: "不向 PS5 转发电脑手柄输入"
                VSwitch { checked: veyra.ps5.viewOnly === true; onToggled: veyra.ps5Set("viewOnly", checked) }
            }
            VRow {
                label: "陀螺仪"
                hint: veyra.ps5.controller ? (veyra.ps5.gyro ? "手柄支持陀螺仪" : "手柄没有陀螺仪") : "连接串流及电脑手柄后可校准"
                VButton { text: veyra.ps5.calibrating ? "校准中…" : "校准"; ghost: true; onClicked: veyra.ps5Calibrate() }
            }
        }
        DSection { text: "登录 PIN" }
        DGroup {
            VRow {
                label: "PS5 提示时填写"
                hint: "不是 8 位配对码"
                RowLayout {
                    spacing: 6
                    VTextField { id: loginPin; implicitWidth: 120 }
                    VButton { text: "提交"; ghost: true; onClicked: { veyra.ps5SendLoginPin(loginPin.text); loginPin.text = "" } }
                }
            }
        }
        DNote { visible: (veyra.ps5.status || "").length > 0; text: (veyra.ps5.busy ? "处理中 · " : "") + (veyra.ps5.status || "") }
    }

    // --- PC 串流（Sunshine / GameStream） ----------------------------------
    // Hosts, pairing, the host's apps and the stream-core settings. The effect chain is not here: once
    // connected, the picture goes through the software's own pages like any other source.
    DLayer {
        id: mlDialog
        objectName: "moonlight-dialog"
        key: "moonlight"
        glyph: "cast"
        title: "PC 串流"
        sub: "串流另一台电脑（Sunshine 主机）· 增强与调色沿用软件自己的处理链"
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
            { label: "关闭" },
            { label: "断开", primary: true }
        ] : [
            { label: st.busy ? "取消操作" : "关闭" },
            { label: "开始串流", primary: true, icon: "play" }
        ]
        onActionTriggered: label => {
            if (label === "断开") { veyra.moonlightDisconnect(); host.close(); return }
            if (label === "取消操作") { ml.cancel(); return }
            if (label === "关闭") { host.close(); return }
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
            function onStarted() { host.close(); host.startMoonlight() }
        }
        readonly property var resolutions: [
            { id: "720p", label: "720p · 1280×720" }, { id: "1080p", label: "1080p · 1920×1080" },
            { id: "1440p", label: "1440p · 2560×1440" }, { id: "4k", label: "4K · 3840×2160" },
            { id: "native", label: "本机屏幕分辨率" }]
        readonly property var rates: [30, 60, 90, 120, 144].map(v => ({ id: String(v), label: v + " fps" }))
        function labelOf(list, id, fallback) { const f = list.find(x => String(x.id) === String(id)); return f ? f.label : fallback }
        function stateText(s) {
            return s === "online" ? "在线" : s === "unpaired" ? "未配对" : s === "busy" ? "在线 · 有游戏在运行"
                 : s === "checking" ? "检查中…" : "离线"
        }

        DSection { text: "主机" }
        DNote {
            visible: mlDialog.hostList.length === 0
            text: "还没有主机。在要被串流的电脑上安装 Sunshine（官网 app.lizardbyte.dev/Sunshine，软件不自带），"
                + "两台电脑接在同一个局域网，这里会自动出现；也可以在下面手填它的 IP。首次使用需要配对。"
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
                    VTag { visible: hostRow.modelData.paired; text: "已配对"; kind: "ok" }
                    VButton {
                        visible: !hostRow.modelData.paired && hostRow.modelData.state !== "offline"
                        objectName: "moonlight-pair-" + hostRow.modelData.id
                        text: "配对"
                        primary: true
                        enabled: !mlDialog.st.busy
                        onClicked: mlDialog.ml.pair(hostRow.modelData.id)
                    }
                    VButton {
                        text: "删除"
                        ghost: true
                        enabled: !mlDialog.st.busy
                        onClicked: mlDialog.ml.forget(hostRow.modelData.id)
                    }
                }
            }
        }
        DGroup {
            VRow {
                label: "手动添加"
                hint: "IP 或主机名，端口不是 47989 时写成 192.168.1.20:47989"
                RowLayout {
                    spacing: 6
                    VTextField { id: mlAddress; objectName: "moonlight-address"; implicitWidth: 190; placeholder: "192.168.1.x"; onEdited: text => { if (text.length > 0) { mlDialog.ml.addHost(text); mlAddress.text = "" } } }
                    VSpinner { visible: mlDialog.st.busy === true; Layout.alignment: Qt.AlignVCenter }
                    VButton { text: "添加"; ghost: true; enabled: !mlDialog.st.busy && mlAddress.text.length > 0; onClicked: { mlDialog.ml.addHost(mlAddress.text); mlAddress.text = "" } }
                    VButton { text: "刷新"; ghost: true; enabled: !mlDialog.st.busy; onClicked: mlDialog.ml.refresh() }
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
                    text: "在主机上打开 Sunshine 网页（https://主机IP:47990），进入「PIN」页，输入下面的数字并提交："
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

        DSection { visible: !!mlDialog.st.paired; text: "游戏与程序 · " + (mlDialog.st.hostName || "") }
        DNote {
            visible: !!mlDialog.st.selected && !mlDialog.st.paired && !mlDialog.st.pairing
            text: "这台主机还没有和本机配对。点「配对」，在主机的 Sunshine 网页里输入软件给出的 PIN。"
        }
        DNote {
            visible: !!mlDialog.st.paired && !mlDialog.st.online
            text: "主机现在连不上（离线、休眠，或地址变了）。开机后点「刷新」。"
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
                            VTag { visible: appCard.modelData.running; text: "运行中"; kind: "acc" }
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
                text: "主机上有游戏在运行。断开串流不会结束它；选它并开始即可继续。"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
                wrapMode: Text.WordWrap
            }
            VButton { objectName: "moonlight-quit-app"; text: "退出游戏"; ghost: true; enabled: !mlDialog.st.busy; onClicked: mlDialog.ml.quitApp() }
        }

        DSection { text: "串流设置（只影响串流本身，按主机保存）" }
        DGroup {
            VRow {
                label: "分辨率"
                hint: "主机按这个尺寸编码；软件的超分与增强在收到之后再做"
                VSelect { objectName: "moonlight-res"; implicitWidth: 190; value: mlDialog.labelOf(mlDialog.resolutions, mlDialog.cfg.res, "1080p · 1920×1080"); options: mlDialog.resolutions; onPicked: id => mlDialog.ml.set("res", id) }
            }
            VRow {
                label: "帧率"
                VSelect { objectName: "moonlight-fps"; implicitWidth: 130; value: (mlDialog.cfg.fps || 60) + " fps"; options: mlDialog.rates; onPicked: id => mlDialog.ml.set("fps", Number(id)) }
            }
            VRow {
                label: "码率"
                hint: "默认 150 Mbps；有线千兆可以拉到 300–500，无线卡顿时调低。主机画面简单时实际码率会低于这里的上限"
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
                    Text {
                        Layout.preferredWidth: 72
                        text: Math.max(5, Math.round((bitrateSlider.dragging ? bitrateSlider.dragValue : bitrateSlider.value) / 5) * 5) + " Mbps"
                        color: Theme.t1
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fsSmall
                    }
                }
            }
            VRow {
                label: "编码"
                hint: mlDialog.cfg.codec === 3 ? "AV1 需要主机显卡能编码、本机显卡能解码" : "自动：优先 HEVC，主机不支持时用 H.264"
                VSeg {
                    objectName: "moonlight-codec"
                    options: [{ id: "0", label: "自动" }, { id: "1", label: "H.264" }, { id: "2", label: "HEVC" }, { id: "3", label: "AV1" }]
                    current: String(mlDialog.cfg.codec || 0)
                    onPicked: id => mlDialog.ml.set("codec", Number(id))
                }
            }
            VRow {
                label: "HDR"
                hint: mlDialog.st.hdrHost ? "需要主机显示器开启 HDR，且编码选 HEVC 或 AV1；本机需要硬件解码" : "这台主机没有报告 10 位编码能力"
                VSwitch { objectName: "moonlight-hdr"; enabled: !!mlDialog.st.hdrHost || mlDialog.cfg.hdr === true; checked: mlDialog.cfg.hdr === true; onToggled: mlDialog.ml.set("hdr", checked) }
            }
            VRow {
                label: "声道"
                VSeg {
                    objectName: "moonlight-audio"
                    options: [{ id: "2", label: "立体声" }, { id: "6", label: "5.1" }, { id: "8", label: "7.1" }]
                    current: String(mlDialog.cfg.audio || 2)
                    onPicked: id => mlDialog.ml.set("audio", Number(id))
                }
            }
            VRow {
                label: "手柄"
                hint: "把电脑手柄当作主机上的 Xbox 手柄（1 号）"
                VSwitch { objectName: "moonlight-gamepad"; checked: mlDialog.cfg.gamepad !== false; onToggled: mlDialog.ml.set("gamepad", checked) }
            }
            VRow {
                label: "自动捕获键盘鼠标"
                hint: "开始后键鼠交给主机；Ctrl+Alt+Shift+Z 释放，+Q 断开，+S 统计"
                VSwitch { objectName: "moonlight-capture"; checked: mlDialog.cfg.captureInput !== false; onToggled: mlDialog.ml.set("captureInput", checked) }
            }
            VRow {
                label: "让主机切换到串流分辨率"
                hint: "关闭时主机保持自己的分辨率，画面由主机缩放"
                VSwitch { objectName: "moonlight-sops"; checked: mlDialog.cfg.sops === true; onToggled: mlDialog.ml.set("sops", checked) }
            }
            VRow {
                label: "主机同时出声"
                hint: "默认只在本机出声"
                VSwitch { objectName: "moonlight-hostaudio"; checked: mlDialog.cfg.hostAudio === true; onToggled: mlDialog.ml.set("hostAudio", checked) }
            }
        }
        DNote { objectName: "moonlight-status"; visible: (mlDialog.st.status || "").length > 0; text: (mlDialog.st.busy ? "处理中 · " : "") + (mlDialog.st.status || "") }
    }

    // --- Xbox 串流（非官方） ---------------------------------------------------
    // Sign in with the Xbox (Microsoft) account on Microsoft's own page via a device code, pick a console,
    // start. The software never sees the password; only a refresh token is kept, encrypted for this user.
    DLayer {
        id: xbDialog
        objectName: "xbox-dialog"
        key: "xbox"
        glyph: "gamepad"
        title: "Xbox 串流"
        sub: "串流你自己的 Xbox 主机 · 非官方实验功能 · 增强与调色沿用软件自己的处理链"
        dialogWidth: 680
        readonly property var xb: veyra.xbox
        readonly property var st: xb ? xb.state : ({})
        readonly property var list: xb ? xb.consoles : []
        actions: st.streaming ? [
            { label: "关闭" },
            { label: "断开", primary: true }
        ] : !st.signedIn ? [
            { label: st.busy ? "取消操作" : "关闭" }
        ] : [
            { label: st.busy ? "取消操作" : "关闭" },
            { label: "开始串流", primary: true, icon: "play" }
        ]
        onActionTriggered: label => {
            if (label === "断开") { veyra.xboxDisconnect(); host.close(); return }
            if (label === "取消操作") { xb.cancel(); return }
            if (label === "关闭") { host.close(); return }
            if (st.signedIn && !st.busy && (st.selected || "").length > 0) xb.connectStream()
        }
        Connections {
            target: xbDialog
            function onShownChanged() { if (xbDialog.shown && xbDialog.xb) xbDialog.xb.load() }
        }
        Connections {
            target: xbDialog.xb
            function onStarted() { host.close(); host.startXbox() }
        }

        DNote {
            text: "非官方：使用与 Greenlight 等开源客户端相同的方式连接微软的串流服务，不隶属于微软，微软随时可能改动导致失效。仅限你自己的账号和主机。"
        }
        DSection { text: "账号" }
        DGroup {
            VRow {
                label: xbDialog.st.signedIn ? "已登录" : "未登录"
                hint: xbDialog.st.signedIn ? "登录信息加密保存在本机；退出会删除它" : "用你的 Xbox（微软）账号在微软的页面登录，软件看不到密码"
                RowLayout {
                    spacing: 6
                    VSpinner { visible: xbDialog.st.busy === true; Layout.alignment: Qt.AlignVCenter }
                    VButton { objectName: "xbox-signin"; visible: !xbDialog.st.signedIn; text: "登录"; primary: true; enabled: !xbDialog.st.busy; onClicked: xbDialog.xb.signIn() }
                    VButton { visible: xbDialog.st.signedIn; text: "退出登录"; ghost: true; enabled: !xbDialog.st.busy; onClicked: xbDialog.xb.signOut() }
                }
            }
        }
        // The device code, large, with the page to type it into.
        Rectangle {
            visible: (xbDialog.st.code || "").length > 0
            objectName: "xbox-code-box"
            Layout.fillWidth: true
            Layout.topMargin: 8
            implicitHeight: codeCol.implicitHeight + 28
            radius: 11
            color: Theme.accentSoft
            border.width: 1
            border.color: Theme.accent
            ColumnLayout {
                id: codeCol
                anchors.fill: parent
                anchors.margins: 14
                spacing: 8
                Text {
                    Layout.fillWidth: true
                    text: "在手机或浏览器打开 " + (xbDialog.st.verificationUri || "https://www.microsoft.com/link") + "，输入下面的登录码："
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSmall
                    wrapMode: Text.WordWrap
                }
                Text {
                    objectName: "xbox-code"
                    Layout.alignment: Qt.AlignHCenter
                    text: xbDialog.st.code || ""
                    color: Theme.t1
                    font.family: Theme.fontMono
                    font.pixelSize: 30
                    font.letterSpacing: 4
                }
                VButton { Layout.alignment: Qt.AlignHCenter; text: "在浏览器打开登录页"; ghost: true; onClicked: xbDialog.xb.openSignInPage() }
            }
        }

        DSection { visible: !!xbDialog.st.signedIn; text: "主机" }
        Repeater {
            model: xbDialog.st.signedIn ? xbDialog.list : []
            delegate: Rectangle {
                id: consoleRow
                required property var modelData
                readonly property bool picked: xbDialog.st.selected === modelData.id
                objectName: "xbox-console-" + modelData.id
                Layout.fillWidth: true
                Layout.topMargin: 6
                implicitHeight: 54
                radius: 11
                color: picked ? Theme.accentSoft : (consoleHover.hovered ? Theme.card3 : Theme.card2)
                border.width: 1
                border.color: picked ? Theme.accent : Theme.stroke
                HoverHandler { id: consoleHover; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    onTapped: xbDialog.xb.select(consoleRow.modelData.id)
                    onDoubleTapped: { xbDialog.xb.select(consoleRow.modelData.id); if (!xbDialog.st.busy) xbDialog.xb.connectStream() }
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 14
                    anchors.rightMargin: 12
                    spacing: 12
                    VDot { off: consoleRow.modelData.power === "关机"; warn: consoleRow.modelData.power === "正在更新"; Layout.alignment: Qt.AlignVCenter }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 1
                        Text { Layout.fillWidth: true; text: consoleRow.modelData.name; color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody; font.weight: Font.Medium; elide: Text.ElideRight }
                        Text { Layout.fillWidth: true; text: consoleRow.modelData.type + " · " + consoleRow.modelData.power; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall; elide: Text.ElideRight }
                    }
                }
            }
        }
        DGroup {
            visible: !!xbDialog.st.signedIn
            VRow {
                label: "主机列表"
                hint: "主机要用同一个账号登录，并在 设置 → 设备和连接 → 远程功能 里启用远程功能"
                VButton { text: "刷新"; ghost: true; enabled: !xbDialog.st.busy; onClicked: xbDialog.xb.refresh() }
            }
            VRow {
                label: "手柄"
                hint: "把电脑手柄当作主机上的手柄"
                VSwitch { objectName: "xbox-gamepad"; checked: xbDialog.st.gamepad !== false; onToggled: xbDialog.xb.set("gamepad", checked) }
            }
        }
        DNote { objectName: "xbox-status"; visible: (xbDialog.st.status || "").length > 0; text: (xbDialog.st.busy ? "处理中 · " : "") + (xbDialog.st.status || "") }
    }

    // --- 屏幕捕获 ---------------------------------------------------------
    DLayer {
        id: screenDialog
        objectName: "screen-dialog"
        key: "screen"
        glyph: "monitor"
        title: "屏幕捕获"
        sub: "把一个窗口或整块显示器作为片源"
        dialogWidth: 700
        actions: [
            { label: "取消" },
            { label: "开始捕获", primary: true }
        ]
        onActionTriggered: label => {
            if (label !== "开始捕获") { host.close(); return }
            if (veyra.startScreenCapture()) { host.close(); host.startScreen() }
        }
        Connections {
            target: host
            function onDialogChanged() { if (host.dialog === "screen") veyra.refreshCaptureTargets() }
        }
        readonly property var o: veyra.screenOptions

        DSection { text: "目标" }
        DGroup {
            VRow {
                label: "类型"
                VSeg {
                    objectName: "screen-kind"
                    options: [{ id: "0", label: "窗口" }, { id: "1", label: "显示器" }]
                    current: String(screenDialog.o.kind)
                    onPicked: id => veyra.setScreenOption("kind", Number(id))
                }
            }
            VRow {
                label: "捕获目标"
                hint: veyra.screenTargets.length === 0 ? "未找到可捕获的窗口或显示器" : ""
                RowLayout {
                    spacing: 6
                    VSelect {
                        objectName: "screen-target"
                        implicitWidth: 300
                        value: veyra.screenTargetLabel
                        options: veyra.screenTargets
                        onPicked: id => veyra.screenTargetId = id
                    }
                    VButton { text: "刷新"; ghost: true; onClicked: veyra.refreshCaptureTargets() }
                }
            }
        }
        DSection { text: "采集" }
        DGroup {
            VRow {
                label: "采集方式"
                hint: screenDialog.o.kind === 1 ? "DXGI 为显示器兼容方式，无鼠标指针" : "窗口只能用 Windows Graphics Capture"
                VSeg {
                    enabled: screenDialog.o.kind === 1
                    opacity: enabled ? 1 : 0.5
                    options: [{ id: "0", label: "WGC" }, { id: "1", label: "DXGI" }]
                    current: String(screenDialog.o.method)
                    onPicked: id => veyra.setScreenOption("method", Number(id))
                }
            }
            VRow {
                label: "帧率上限"
                VSeg {
                    options: [{ id: "0", label: "跟随" }, { id: "1", label: "30" }, { id: "2", label: "60" }, { id: "3", label: "120" }, { id: "4", label: "144" }, { id: "5", label: "240" }]
                    current: String(screenDialog.o.fps)
                    onPicked: id => veyra.setScreenOption("fps", Number(id))
                }
            }
            VRow {
                label: "显示鼠标指针"
                VSwitch {
                    enabled: screenDialog.o.method === 0
                    checked: screenDialog.o.cursor === true
                    onToggled: veyra.setScreenOption("cursor", checked)
                }
            }
            VRow {
                label: "裁剪（像素）"
                hint: "左 / 上 / 右 / 下，按源像素"
                RowLayout {
                    spacing: 4
                    Repeater {
                        model: ["left", "top", "right", "bottom"]
                        delegate: VTextField {
                            required property string modelData
                            implicitWidth: 58
                            text: String(screenDialog.o[modelData])
                            onEdited: text => veyra.setScreenOption(modelData, Number(text))
                        }
                    }
                }
            }
            VRow {
                label: "画面"
                VSeg {
                    options: [{ id: "fit", label: "适应窗口" }, { id: "fill", label: "填满窗口" }]
                    current: screenDialog.o.fill ? "fill" : "fit"
                    onPicked: id => veyra.setScreenOption("fill", id === "fill")
                }
            }
        }
    }

    // --- 字幕设置 ---------------------------------------------------------
    DLayer {
        id: subtitleDialog
        objectName: "subtitle-dialog"
        key: "subtitle"
        glyph: "type"
        title: "字幕设置"
        sub: "实时预览，设置对所有文件生效"
        dialogWidth: 620
        actions: [{ label: "恢复默认" }, { label: "完成", primary: true }]
        onActionTriggered: label => {
            if (label !== "恢复默认") { host.close(); return }
            // The look only: tracks and the timing offset belong to the file.
            const defaults = { subtitleEnabled: true, subtitleSize: 22, subtitleFont: 0, subtitleOutline: 2,
                               subtitleBackground: false, subtitleMargin: 0, subtitleFit: false, subtitleLines: 2 }
            for (const key in defaults) veyra.setPreference(key, defaults[key])
            veyra.logUi("ui-subtitle", "style reset to defaults")
        }

        readonly property var prefs: veyra.preferences
        function pref(key, fallback) { return prefs[key] !== undefined ? prefs[key] : fallback }
        readonly property var trackOptions: [{ id: "-1", label: "关闭" }]
            .concat(veyra.subtitleTracks.map(t => ({ id: String(t.index), label: t.label + " · " + t.note, disabled: !t.usable })))
        function trackLabel(index) {
            const t = veyra.subtitleTracks.find(x => x.index === index)
            return t ? t.label : "关闭"
        }

        // dialogs.js .sub-prev: a sample line over the picture, drawn with the
        // current style so every change shows at once.
        Rectangle {
            id: subPreview
            objectName: "subtitle-preview"
            Layout.fillWidth: true
            Layout.topMargin: 4
            implicitHeight: 180
            radius: 10
            border.width: 1
            border.color: Theme.stroke
            clip: true
            gradient: Gradient {
                GradientStop { position: 0; color: "#1B2230" }
                GradientStop { position: 1; color: "#0B0C10" }
            }
            readonly property real scaleFactor: height / 540
            Image {
                anchors.fill: parent
                anchors.margins: 1
                fillMode: Image.PreserveAspectCrop
                asynchronous: true
                visible: status === Image.Ready
                source: subtitleDialog.shown && veyra.hasSource && !veyra.isCapture && veyra.duration > 0
                        ? "image://veyra-thumb/" + veyra.thumbnailGeneration + "/" + Math.floor(veyra.position / 2) * 2000 : ""
            }
            Column {
                objectName: "subtitle-preview-lines"
                visible: subtitleDialog.pref("subtitleEnabled", true)
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 12 + subtitleDialog.pref("subtitleMargin", 0) * subPreview.scaleFactor
                spacing: 2
                Repeater {
                    model: veyra.subtitleText.length > 0 ? veyra.subtitleText.split("\n").slice(0, 2)
                                                         : ["这是字幕预览，", "改动会实时显示在这里。"]
                    delegate: Rectangle {
                        required property string modelData
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: line.implicitWidth + 16
                        height: line.implicitHeight + 2
                        radius: 4
                        color: subtitleDialog.pref("subtitleBackground", false) ? Qt.rgba(0, 0, 0, 0.6) : "transparent"
                        Text {
                            id: line
                            anchors.centerIn: parent
                            text: modelData
                            color: "#FFFFFF"
                            readonly property var families: ["", "SimHei", "SimSun", "DengXian", "Arial", "Segoe UI"]
                            font.family: families[subtitleDialog.pref("subtitleFont", 0)] || Theme.fontUi
                            font.pixelSize: Math.max(12, subtitleDialog.pref("subtitleSize", 22) * 0.8)
                            font.weight: Font.DemiBold
                            style: subtitleDialog.pref("subtitleOutline", 2) > 0 ? Text.Outline : Text.Normal
                            styleColor: Qt.rgba(0, 0, 0, subtitleDialog.pref("subtitleOutline", 2) >= 3 ? 1 : 0.85)
                        }
                    }
                }
            }
        }

        DSection { text: "字幕轨" }
        DGroup {
            VRow {
                label: "主字幕"
                hint: veyra.subtitleTracks.length === 0 ? (veyra.hasSource ? "当前片源没有字幕" : "打开视频后显示可用字幕") : ""
                VSelect {
                    objectName: "subtitle-primary"
                    implicitWidth: 220
                    value: subtitleDialog.trackLabel(veyra.subtitlePrimary)
                    options: subtitleDialog.trackOptions
                    onPicked: id => veyra.subtitlePrimary = Number(id)
                }
            }
            VRow {
                label: "副字幕（双语）"
                hint: "显示在主字幕上方"
                VSelect {
                    objectName: "subtitle-secondary"
                    implicitWidth: 220
                    value: subtitleDialog.trackLabel(veyra.subtitleSecondary)
                    options: subtitleDialog.trackOptions
                    onPicked: id => veyra.subtitleSecondary = Number(id)
                }
            }
            VRow {
                label: "外挂字幕"
                hint: "SRT / ASS / SSA / WebVTT"
                VButton { objectName: "subtitle-load"; text: "加载文件…"; onClicked: veyra.loadSubtitleDialog() }
            }
            VRow {
                label: "默认显示副字幕"
                hint: "打开新文件时自动选第二条可用字幕"
                VSwitch {
                    checked: subtitleDialog.pref("subtitleSecondLanguage", false)
                    onToggled: veyra.setPreference("subtitleSecondLanguage", checked)
                }
            }
        }
        DSection { text: "时间" }
        DGroup {
            VRow {
                label: "主字幕延时"
                hint: "Z / X 微调 50 ms，Shift 为 1 s；正值 = 字幕推后"
                // .stepper: − value +, 50 ms a step; the value can be typed.
                RowLayout {
                    objectName: "subtitle-delay-stepper"
                    spacing: 4
                    VButton { objectName: "subtitle-delay-minus"; icon: true; iconName: "minus"; implicitWidth: 28; implicitHeight: 28; onClicked: veyra.nudgeSubtitle(-50) }
                    VTextField {
                        objectName: "subtitle-delay-value"
                        implicitWidth: 86
                        text: veyra.subtitleOffsetMs + " ms"
                        onEdited: text => { const v = parseInt(text); if (isFinite(v)) veyra.subtitleOffsetMs = v }
                    }
                    VButton { objectName: "subtitle-delay-plus"; icon: true; iconName: "plus"; implicitWidth: 28; implicitHeight: 28; onClicked: veyra.nudgeSubtitle(50) }
                }
            }
            VRow {
                label: "按音轨自动对齐"
                hint: veyra.subtitleStatus.length > 0 ? veyra.subtitleStatus : "分析前 30 分钟的人声与字幕时间"
                VButton {
                    text: veyra.subtitleAligning ? "分析中…" : "自动对齐"
                    enabled: !veyra.subtitleAligning && veyra.subtitlePrimary >= 0
                    onClicked: veyra.autoAlignSubtitle()
                }
            }
        }
        DSection { text: "样式" }
        DGroup {
            VRow {
                label: "显示字幕"
                hint: "快捷键 B"
                VSwitch {
                    objectName: "subtitle-enabled"
                    checked: subtitleDialog.pref("subtitleEnabled", true)
                    onToggled: veyra.setPreference("subtitleEnabled", checked)
                }
            }
            VRow {
                label: "字号"
                value: subtitleDialog.pref("subtitleSize", 22) + " px"
                VSlider {
                    objectName: "subtitle-size"
                    implicitWidth: 170
                    from: 16; to: 56; value: subtitleDialog.pref("subtitleSize", 22)
                    onMoved: veyra.setPreference("subtitleSize", Math.round(value))
                }
            }
            VRow {
                label: "字体"
                VSelect {
                    objectName: "subtitle-font"
                    implicitWidth: 170
                    readonly property var names: ["字幕原字体", "黑体", "宋体", "等线", "Arial", "Segoe UI"]
                    value: names[subtitleDialog.pref("subtitleFont", 0)]
                    options: names.map((n, i) => ({ id: String(i), label: n }))
                    onPicked: id => veyra.setPreference("subtitleFont", Number(id))
                }
            }
            VRow {
                label: "描边"
                VSeg {
                    objectName: "subtitle-outline"
                    options: [{ id: "0", label: "无" }, { id: "1", label: "细" }, { id: "2", label: "标准" }, { id: "3", label: "粗" }]
                    current: String(subtitleDialog.pref("subtitleOutline", 2))
                    onPicked: id => veyra.setPreference("subtitleOutline", Number(id))
                }
            }
            VRow {
                label: "背景条"
                VSwitch {
                    checked: subtitleDialog.pref("subtitleBackground", false)
                    onToggled: veyra.setPreference("subtitleBackground", checked)
                }
            }
            VRow {
                label: "底部距离"
                value: subtitleDialog.pref("subtitleMargin", 0) + " px"
                VSlider {
                    objectName: "subtitle-margin"
                    implicitWidth: 170
                    from: 0; to: 240; value: subtitleDialog.pref("subtitleMargin", 0)
                    onMoved: veyra.setPreference("subtitleMargin", Math.round(value))
                }
            }
            VRow {
                label: "长句自动缩小"
                hint: "默认保持字号；开启后超过目标行数才缩小"
                VSwitch {
                    checked: subtitleDialog.pref("subtitleFit", false)
                    onToggled: veyra.setPreference("subtitleFit", checked)
                }
            }
            VRow {
                visible: subtitleDialog.pref("subtitleFit", false)
                label: "目标行数"
                value: String(subtitleDialog.pref("subtitleLines", 2))
                VSlider {
                    implicitWidth: 170
                    from: 1; to: 8; value: subtitleDialog.pref("subtitleLines", 2)
                    onMoved: veyra.setPreference("subtitleLines", Math.round(value))
                }
            }
        }
    }

    // --- 音频设置 ---------------------------------------------------------
    DLayer {
        key: "audio"
        glyph: "music"
        title: "音频设置"
        dialogWidth: 560
        actions: [{ label: "完成", primary: true }]
        onActionTriggered: host.close()

        DSection { text: "音轨" }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                visible: veyra.audioTracks.length === 0
                text: "当前源没有可选音轨。"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
            }
            Repeater {
                model: veyra.audioTracks
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 52
                    radius: 12
                    color: Theme.card2
                    border.width: 1
                    border.color: veyra.selectedAudioTrack === modelData.index ? Theme.accent : Theme.stroke
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12
                        // .trk .bars: the playing track's bars move (M39).
                        VEqBars {
                            objectName: "audio-track-bars-" + modelData.index
                            active: veyra.selectedAudioTrack === modelData.index && veyra.running && !veyra.paused
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: modelData.label
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                            }
                            Text {
                                text: modelData.channels > 0 ? modelData.channels + " 声道" : ""
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: 11
                            }
                        }
                        VIcon {
                            visible: veyra.selectedAudioTrack === modelData.index
                            name: "check"
                            color: Theme.accent
                        }
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: veyra.selectedAudioTrack = modelData.index }
                }
            }
        }
        DSection { text: "输出" }
        DGroup {
            VRow {
                label: "音量"
                value: Math.round(veyra.volume * 100) + "%"
                VSlider {
                    implicitWidth: 170
                    from: 0; to: 1; value: veyra.volume
                    onMoved: veyra.volume = value
                }
            }
            VRow {
                label: "静音"
                VSwitch {
                    checked: veyra.muted
                    onToggled: veyra.muted = checked
                }
            }
        }
        DSection { text: "音画同步" }
        DGroup {
            VRow {
                label: "声音同步"
                hint: veyra.audioSyncMode === 0 ? (veyra.audioSyncLive ? "软件估算补偿 " + veyra.audioCompensationMs.toFixed(0) + " ms" : "自动估算 · 采集卡 / PS5 实时输入时生效")
                      : veyra.audioSyncMode === 1 ? "手动偏移 · 实时输入时生效" : "关闭补偿"
                VSeg {
                    options: [{ id: "0", label: "自动估算" }, { id: "1", label: "手动" }, { id: "2", label: "关闭" }]
                    current: String(veyra.audioSyncMode)
                    onPicked: id => veyra.audioSyncMode = Number(id)
                }
            }
            VRow {
                label: "手动偏移"
                hint: "负值 = 声音提前"
                value: veyra.audioOffsetMs + " ms"
                VSlider {
                    implicitWidth: 170
                    center: true
                    from: -250; to: 250; value: veyra.audioOffsetMs; enabledControl: veyra.audioSyncMode === 1
                    onMoved: veyra.audioOffsetMs = Math.round(value / 10) * 10
                }
            }
            VRow {
                label: "当前偏差"
                hint: "软件内测得的音画偏差，不是扬声器实测；只在采集卡 / PS5 实时输入时有"
                value: veyra.audioSkewKnown ? (veyra.audioSkewMs >= 0 ? "+" : "") + veyra.audioSkewMs.toFixed(0) + " ms" : "未测"
            }
        }
        DSection { text: "输出设备" }
        DGroup {
            VRow {
                label: "输出设备"
                hint: veyra.audioOutputStatus
                VSelect {
                    objectName: "audio-device"
                    implicitWidth: 240
                    readonly property string chosen: veyra.preferences.audioDevice || ""
                    value: { const d = veyra.audioDevices.find(x => x.id === chosen); return d ? d.label : "跟随系统默认" }
                    options: veyra.audioDevices
                    onPicked: id => veyra.setPreference("audioDevice", id)
                }
            }
            VRow {
                label: "立体声下混"
                hint: "多声道片源混成双声道再输出（耳机）；关闭时按设备声道布局映射"
                VSwitch {
                    objectName: "audio-stereo"
                    checked: veyra.preferences.audioForceStereo === true
                    onToggled: veyra.setPreference("audioForceStereo", checked)
                }
            }
        }
        // The list is read when the dialog opens: devices come and go.
        Connections {
            target: host
            function onDialogChanged() { if (host.dialog === "audio") veyra.refreshAudioDevices() }
        }
    }

    // --- 另存为预设 -------------------------------------------------------
    // The design lists every part that will be saved before asking for a name, so
    // the user can see what they are about to store.
    DLayer {
        id: saveDialog
        objectName: "preset-save-dialog"
        key: "save"
        glyph: "plus"
        readonly property string kindLabel: veyra.nodeMode === 1 ? "节点" : "列表"
        title: "另存为" + kindLabel + "预设"
        sub: kindLabel + "预设和" + (veyra.nodeMode === 1 ? "列表" : "节点") + "预设分开保存，互不影响；应用时只覆盖勾选的部分"
        dialogWidth: 580
        // presets.js currentItems: what the chain holds right now, one row per
        // node, with the parameters that matter.
        function nodeSummary(n) {
            switch (n.type) {
            case "sr": return veyra.srTargetLabel + (veyra.videoSrQuality > 0 ? " · 质量 " + veyra.videoSrQuality : "")
            case "nr": { const l = veyra.nrLayers.find(x => x.index === n.index); return l ? "强度 " + l.intensity.toFixed(2) : "NR" }
            case "video-hdr": return "峰值 " + (veyra.videoHdrParams.peakNits || 1000) + " nits"
            case "protection": return veyra.protectionRegions.length + " / 4 个区域"
            case "color": return "调色参数"
            case "frame-generation": return veyra.fgMultiplier + "X · " + (veyra.fgBackendName === "xess" ? "Intel XeSS" : "DLSS")
            }
            return ""
        }
        function nodeLabel(n) {
            if (n.type !== "nr") return n.label
            let k = 0
            for (const m of veyra.chain) { if (m.type === "nr") ++k; if (m.index === n.index) break }
            return "NR 层 " + k
        }
        readonly property bool nameTaken: presetNameField.text.trim().length > 0 &&
            veyra.presets.some(p => p.name === presetNameField.text.trim())
        property var chosen: ({})
        property var parts: []
        function resetForOpen() {
            const current = veyra.presetSaveParts
            const selected = {}
            for (const part of current) selected[part.id] = part.meaningful === true
            parts = current
            chosen = selected
            presetNameField.text = ""
            saveNote.text = ""
            saveAsDefault.checked = false
        }
        actions: [
            { label: "取消" },
            { label: "保存预设", primary: true, icon: "check" }
        ]
        onActionTriggered: label => {
            if (label !== "保存预设") { host.close(); return }
            const name = presetNameField.text.trim()
            if (name.length === 0) { saveNote.text = "预设需要一个名字"; return }
            let mask = 0
            const ids = ["chain", "color", "fg", "audio"]
            for (const part of parts) {
                const bit = ids.indexOf(part.id)
                if (bit >= 0 && chosen[part.id] === true) mask |= (1 << bit)
            }
            if (mask === 0) { saveNote.text = "至少选择一项内容"; return }
            if (veyra.savePresetAs(name, mask, veyra.nodeMode === 1)) {
                if (saveAsDefault.checked) {
                    const saved = veyra.presets.find(p => p.name === name && p.nodeMode === (veyra.nodeMode === 1))
                    if (saved) veyra.setDefaultPreset(saved.index)
                }
                host.close()
            }
            else saveNote.text = "保存失败：名称可能已存在"
        }

        // .namebox: the name first, with a live note under it.
        ColumnLayout {
            Layout.fillWidth: true
            Layout.topMargin: 4
            spacing: 6
            Text { text: "预设名称"; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 12 }
            VTextField {
                id: presetNameField
                objectName: "preset-name"
                Layout.fillWidth: true
                placeholder: "例如：夜间游戏"
            }
            Text {
                objectName: "preset-name-hint"
                Layout.fillWidth: true
                text: saveDialog.nameTaken ? "已有同名预设，换一个名称才能保存"
                      : presetNameField.text.trim().length > 0 ? "可以保存"
                      : "名称会显示在极简模式和导出页的预设菜单里"
                color: saveDialog.nameTaken ? Theme.warn : Theme.ok
                font.family: Theme.fontUi
                font.pixelSize: 11
            }
        }
        DSection { text: "将保存的内容 · 当前正在使用的" + saveDialog.kindLabel + "设置" }
        // .savelist: every node, a green dot when it runs; the list dims when
        // the effect chain itself is not part of the preset.
        ColumnLayout {
            objectName: "preset-save-list"
            Layout.fillWidth: true
            spacing: 4
            opacity: saveDialog.chosen["chain"] === true ? 1 : 0.45
            Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
            Repeater {
                model: saveDialog.shown ? veyra.chain : []
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 34
                    radius: 8
                    color: Qt.rgba(1, 1, 1, 0.03)
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 10
                        spacing: 10
                        Rectangle {
                            implicitWidth: 7; implicitHeight: 7; radius: 3.5
                            color: modelData.enabled ? Theme.ok : Qt.rgba(1, 1, 1, 0.25)
                        }
                        Text {
                            Layout.preferredWidth: 110
                            text: saveDialog.nodeLabel(modelData)
                            color: modelData.enabled ? Theme.t1 : Theme.t3
                            font.family: Theme.fontUi; font.pixelSize: 12
                            font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                            elide: Text.ElideRight
                        }
                        Text {
                            Layout.fillWidth: true
                            text: saveDialog.nodeSummary(modelData)
                            color: Theme.t3
                            font.family: Theme.fontUi; font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                        VTag { visible: !modelData.enabled; text: "已关闭" }
                    }
                }
            }
        }
        DSection { text: "选项" }
        DGroup {
            Repeater {
                model: saveDialog.shown ? saveDialog.parts : []
                delegate: VRow {
                    required property var modelData
                    label: "包含" + modelData.label
                    hint: modelData.summary
                    VSwitch {
                        objectName: "preset-part-" + modelData.id
                        // A part at its default is still offered, but starts off: a
                        // preset that stored nothing would be a surprise.
                        checked: saveDialog.chosen[modelData.id] === true
                        onToggled: checked => {
                            const c = Object.assign({}, saveDialog.chosen)
                            c[modelData.id] = checked
                            saveDialog.chosen = c
                        }
                    }
                }
            }
            VRow {
                label: "保存后设为启动默认"
                hint: "下次启动软件时自动应用这个预设"
                VSwitch { id: saveAsDefault; objectName: "preset-save-default"; checked: false }
            }
        }
        Text {
            id: saveNote
            objectName: "preset-save-error"
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: ""
            color: Theme.warn
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
            wrapMode: Text.WordWrap
        }
    }

    // --- 管理预设 ---------------------------------------------------------
    DLayer {
        id: manageDialog
        key: "manage"
        glyph: "settings"
        title: "管理预设"
        sub: "导出页和极简模式都从这里的预设中选择"
        dialogWidth: 620
        property string mode: "list"
        property string feedback: ""
        property bool feedbackError: false
        function runAction(action, index, name) {
            let ok = false
            if (action === "rename") ok = veyra.renamePreset(index, name)
            else if (action === "duplicate") ok = veyra.duplicatePreset(index)
            else if (action === "delete") ok = veyra.deletePreset(index)
            else if (action === "default") ok = veyra.setDefaultPreset(index)
            feedbackError = !ok
            if (action === "rename")
                feedback = ok ? "预设已改名" : "改名失败：名称不能为空、超过48字或与已有预设重复"
            else if (action === "duplicate")
                feedback = ok ? "预设已复制" : "复制失败：预设数量可能已达上限"
            else if (action === "delete")
                feedback = ok ? "预设已删除" : "删除预设失败"
            else if (action === "default")
                feedback = ok ? (index < 0 ? "已取消启动默认预设" : "已设为启动默认预设") : "启动默认预设保存失败"
            return ok
        }
        actions: [{ label: "完成", primary: true }]
        onActionTriggered: host.close()

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 8
            // presets.js manage: the mode toggle, then 导入 / 导出 on the right.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                VSeg {
                    objectName: "preset-manage-mode"
                    options: [{ id: "list", label: "列表预设" }, { id: "node", label: "节点预设" }]
                    current: manageDialog.mode
                    onPicked: mode => { manageDialog.mode = mode; manageDialog.feedback = "" }
                }
                Item { Layout.fillWidth: true }
                VButton { objectName: "preset-import"; ghost: true; iconName: "import"; text: "导入"; onClicked: veyra.importPresetDialog() }
                VButton {
                    id: presetExportBtn
                    objectName: "preset-export"
                    ghost: true; iconName: "upload"; text: "导出"
                    enabled: presetRows.count > 0
                    onClicked: presetExportMenu.openAt(presetExportBtn, "down")
                }
            }
            Text {
                visible: presetRows.count === 0
                text: "这个模式还没有预设。"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
            }
            Repeater {
                id: presetRows
                model: manageDialog.shown
                       ? veyra.presets.filter(p => p.nodeMode === (manageDialog.mode === "node")) : []
                delegate: Rectangle {
                    id: presetRow
                    required property var modelData
                    objectName: "preset-row-" + modelData.index
                    Layout.fillWidth: true
                    implicitHeight: 54
                    radius: 12
                    color: Theme.card2
                    border.width: 1
                    border.color: Theme.stroke
                    readonly property bool isDefault: veyra.defaultPresetIndex === modelData.index
                    // .prow.bye (M41): shrink and fade over .25s, then delete.
                    property real motionS: 1
                    scale: motionS
                    ParallelAnimation {
                        id: rowBye
                        NumberAnimation { target: presetRow; property: "motionS"; to: 0.9; duration: Theme.d(250); easing.bezierCurve: Theme.easeOut }
                        NumberAnimation { target: presetRow; property: "opacity"; to: 0; duration: Theme.d(250); easing.bezierCurve: Theme.easeOut }
                        onFinished: manageDialog.runAction("delete", presetRow.modelData.index)
                    }
                    function remove() {
                        if (Theme.d(250) <= 0) manageDialog.runAction("delete", modelData.index)
                        else rowBye.restart()
                    }
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 8
                        // .pico
                        Rectangle {
                            implicitWidth: 30; implicitHeight: 30; radius: 9
                            color: Qt.rgba(1, 1, 1, 0.05)
                            VIcon { anchors.centerIn: parent; name: modelData.nodeMode ? "nodes" : "layers"; size: 14; color: Theme.t2 }
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            VTextField {
                                id: presetNameEdit
                                objectName: "preset-rename-" + modelData.index
                                Layout.fillWidth: true
                                enabled: !modelData.builtin
                                text: modelData.name
                                onEdited: name => {
                                    const trimmed = name.trim()
                                    if (trimmed === modelData.name) return
                                    if (!manageDialog.runAction("rename", modelData.index, trimmed)) {
                                        presetNameEdit.text = modelData.name
                                    }
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.note.length > 0 ? modelData.note
                                      : (modelData.nodeMode ? "节点预设" : "列表预设")
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                        VTag { objectName: "preset-default-tag-" + modelData.index; visible: presetRow.isDefault; kind: "acc"; text: "启动默认" }
                        VButton {
                            objectName: "preset-default-" + modelData.index
                            icon: true; ghost: true; iconName: "home"
                            tip: presetRow.isDefault ? "取消启动默认" : "设为启动默认"
                            onClicked: manageDialog.runAction("default", presetRow.isDefault ? -1 : modelData.index)
                        }
                        VButton {
                            objectName: "preset-duplicate-" + modelData.index
                            icon: true; ghost: true; iconName: "dup"
                            tip: "复制"
                            onClicked: manageDialog.runAction("duplicate", modelData.index)
                        }
                        VButton {
                            objectName: "preset-delete-" + modelData.index
                            icon: true; ghost: true; iconName: "trash"
                            tip: "删除"
                            enabled: !modelData.builtin
                            onClicked: presetRow.remove()
                        }
                    }
                }
            }
            VMenu {
                id: presetExportMenu
                objectName: "preset-export-menu"
                title: "导出预设"
                items: veyra.presets.filter(p => p.nodeMode === (manageDialog.mode === "node"))
                    .map(p => ({ label: p.name, note: p.note, icon: "upload", preset: p.index }))
                onPicked: (i, o) => veyra.exportPresetDialog(o.preset)
            }
            Text {
                objectName: "preset-manage-feedback"
                Layout.fillWidth: true
                visible: manageDialog.feedback.length > 0
                text: manageDialog.feedback
                color: manageDialog.feedbackError ? Theme.warn : Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
                wrapMode: Text.WordWrap
            }
        }
    }

    // --- small building blocks the design's dialogs use --------------------
    component DSection: RowLayout {
        property string text: ""
        Layout.fillWidth: true
        Layout.topMargin: 14
        Layout.bottomMargin: 4
        spacing: 8
        VEyebrow { text: parent.text }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.stroke
        }
    }

    component DGroup: VGroup {
        Layout.fillWidth: true
    }

    component DNote: Rectangle {
        property string text: ""
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: noteText.implicitHeight + 18
        radius: 9
        color: Qt.rgba(0.961, 0.784, 0.294, 0.06)
        Text {
            id: noteText
            anchors.fill: parent
            anchors.margins: 9
            text: parent.text
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
            wrapMode: Text.WordWrap
        }
    }

    component VTextField: Rectangle {
        property string placeholder: ""
        property alias text: field.text
        signal edited(string text)
        implicitHeight: 30
        radius: 9
        color: Qt.rgba(1, 1, 1, 0.04)
        border.width: 1
        border.color: field.activeFocus ? Theme.accent : Theme.stroke
        TextInput {
            id: field
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            selectByMouse: true
            onEditingFinished: parent.edited(text)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 10
            visible: field.text.length === 0
            text: parent.placeholder
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }
    }
}
