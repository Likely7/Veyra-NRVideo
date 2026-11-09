// capture dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    required property var legacyPicker
    id: captureDialog
    objectName: "capture-dialog"
    key: "capture"
    glyph: "video"
    title: qsTr("采集卡")
    sub: qsTr("连接设备后，先关闭增强确认基础画面，再按需开启")
    dialogWidth: 660
    foot: qsTr("关闭其他占用同一采集卡的软件")
    actions: [
        { label: captureDialog.live ? qsTr("断开") : qsTr("取消") },
        { label: qsTr("连接并开始"), primary: true, icon: "play" }
    ]
    onActionTriggered: label => {
        if (label === qsTr("断开")) { veyra.stopPlayback(); return }
        if (label !== qsTr("连接并开始")) { dialogHost.close(); return }
        if (veyra.startCaptureSession()) { dialogHost.close(); dialogHost.startCapture() }
    }
    Connections {
        target: dialogHost
        function onDialogChanged() {
            if (dialogHost.dialog !== "capture") return
            veyra.refreshCaptureDevices()
            // The PS5 手柄控制 host list is the PS5 dialog's saved pairings.
            if (veyra.ps5Control.available === true) veyra.ps5Load()
        }
    }
    function labelOf(list, id, fallback) { const f = list.find(x => String(x.id) === String(id)); return f ? f.label : fallback }
    readonly property bool live: veyra.captureSignalLevel !== "idle"
    readonly property var pc: veyra.ps5Control
    readonly property bool pcOn: pc.available === true && pc.enabled === true
    readonly property string pcTag: {
        switch (pc.state) {
        case "starting": return qsTr("连接中")
        case "connected": return qsTr("已连接")
        case "recovering": return qsTr("重连中")
        case "pin": return qsTr("需要 PIN")
        case "failed": return qsTr("已停止")
        case "nopair": return qsTr("未配对")
        default: return qsTr("未连接")
        }
    }
    readonly property string pcStatus: {
        switch (pc.state) {
        case "starting": return qsTr("正在连接 PS5…")
        case "connected": return pc.keepaliveFps >= 0 ? qsTr("PS5 手柄控制已连接 · 保活画面 %1 fps（收到即丢弃）").arg(pc.keepaliveFps) : qsTr("PS5 手柄控制已连接")
        case "pin": return qsTr("PS5 要求输入登录 PIN")
        case "recovering": case "failed": case "nopair": return pc.message || ""
        default: return qsTr("采集卡连上后自动连接 PS5")
        }
    }

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
                text: captureDialog.live ? qsTr("画面在主窗口播放") : qsTr("连接后在主窗口显示画面")
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
                      : (veyra.captureFormatLabel.length ? veyra.captureFormatLabel : qsTr("选择设备和格式后连接"))
                color: Theme.t2
                font.family: Theme.fontMono
                font.pixelSize: 11
                elide: Text.ElideRight
            }
        }
    }

    DialogSection { text: qsTr("设备") }
    DialogGroup {
        VRow {
            label: qsTr("视频输入设备")
            hint: veyra.captureQueryBusy ? qsTr("正在查询…当前播放继续") : (veyra.captureDevices.length === 0 ? qsTr("未检测到采集设备") : "")
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
                        icon: true; ghost: true; iconName: "refresh"; tip: qsTr("刷新设备")
                        onClicked: veyra.refreshCaptureDevices()
                    }
                    VSpinner { anchors.centerIn: parent; visible: veyra.captureQueryBusy }
                }
            }
        }
        VRow {
            objectName: "capture-legacy-entry"
            label: qsTr("旧版格式选择")
            hint: qsTr("直接查看完整格式列表，包含被常用帧率筛选隐藏的模式")
            VButton {
                objectName: "capture-legacy-open"
                text: qsTr("打开旧版格式列表")
                enabled: !veyra.captureQueryBusy && veyra.captureDeviceId.length > 0
                onClicked: legacyPicker.open()
            }
        }
        VRow {
            label: qsTr("分辨率")
            hint: qsTr("先选择分辨率，再选择该分辨率支持的帧率和像素格式")
            VSelect {
                objectName: "capture-resolution"
                enabled: !veyra.captureQueryBusy
                opacity: enabled ? 1 : 0.5
                implicitWidth: 180
                value: captureDialog.labelOf(veyra.captureResolutionOptions, veyra.captureResolutionId, veyra.captureResolutionOptions.length ? qsTr("请选择分辨率") : "—")
                options: veyra.captureResolutionOptions
                onPicked: id => veyra.captureResolutionId = id
            }
        }
        VRow {
            label: qsTr("自定义分辨率")
            hint: qsTr("仅接受驱动尺寸范围与步进支持的值；连接时核对实际格式")
            RowLayout {
                spacing: 6
                enabled: !veyra.captureQueryBusy && veyra.captureDeviceId.length > 0
                DialogTextField { id: customCaptureResolution; objectName: "capture-custom-resolution"; implicitWidth: 140; text: "1920x1080" }
                VButton { text: qsTr("检查并选择"); onClicked: veyra.captureResolutionId = customCaptureResolution.text }
            }
        }
        VRow {
            label: qsTr("帧率")
            hint: qsTr("列出离散帧率及驱动范围内的候选；设备帧率填 0 时沿用此选择，连接时验证实际值")
            VSelect {
                objectName: "capture-frame-rate"
                enabled: !veyra.captureQueryBusy && veyra.captureResolutionId.length > 0
                opacity: enabled ? 1 : 0.5
                implicitWidth: 280
                value: captureDialog.labelOf(veyra.captureFrameRateOptions, veyra.captureFrameRateId, veyra.captureFrameRateOptions.length ? qsTr("请选择帧率") : "—")
                options: veyra.captureFrameRateOptions
                onPicked: id => veyra.captureFrameRateId = id
            }
        }
        VRow {
            // Some cards list every rate from 1 to 120 fps (logs(18)). The frame-rate menu
            // shows the common rates the card offers; this lists the rest too.
            objectName: "capture-all-rates-row"
            visible: veyra.captureHiddenFrameRates > 0
            label: qsTr("显示全部可用帧率")
            hint: qsTr("默认只列常用帧率：24、25、30、50、60、100、120、144、165、240（含 59.94 这类非整数帧率）。这张卡另外上报了 %1 个帧率").arg(veyra.captureHiddenFrameRates)
            VSwitch {
                objectName: "capture-all-rates"
                enabled: !veyra.captureQueryBusy
                opacity: enabled ? 1 : 0.35
                checked: veyra.preferences.captureShowAllFrameRates === true
                onToggled: checked => veyra.setPreference("captureShowAllFrameRates", checked)
            }
        }
        VRow {
            label: qsTr("像素格式")
            // Never search veyra.captureFormats here: a card can list 15000+ (logs(18)).
            hint: veyra.captureFormatCostHint ? qsTr("该格式比低延迟格式多一道处理环节，高分辨率/高帧率下建议优先低延迟格式") : qsTr("只列出当前分辨率和帧率下的像素格式")
            VSelect {
                objectName: "capture-format"
                enabled: !veyra.captureQueryBusy && veyra.captureResolutionId.length > 0 && veyra.captureFrameRateId.length > 0
                opacity: enabled ? 1 : 0.5
                implicitWidth: 220
                value: captureDialog.labelOf(veyra.capturePixelOptions, veyra.capturePixelId, veyra.captureResolutionId.length ? qsTr("请选择像素格式") : qsTr("请先选择分辨率"))
                options: veyra.capturePixelOptions
                onPicked: id => veyra.capturePixelId = id
            }
        }
        VRow {
            objectName: "capture-driver-settings-row"
            visible: veyra.captureDeviceId.length > 0
            label: qsTr("驱动设置")
            hint: captureDialog.live ? qsTr("先断开采集，再配置驱动")
                  : veyra.captureBlackmagicDevice
                    ? (veyra.preferences.blackmagicNativeCapture === true
                       ? qsTr("请在设备列表选择对应的 HDMI/SDI 输入口；Driver 保留驱动当前输入。支持自动检测的卡建议选择自动，信号未锁定时会明确提示。")
                       : qsTr("Blackmagic 两个条目可能属于同一张卡。DirectShow 黑屏可尝试下方原生采集开关；手动格式须匹配实际信号。"))
                    : qsTr("配置驱动公开的输入与视频属性；其他采集应用占用设备时请先关闭")
            RowLayout {
                spacing: 6
                VButton {
                    objectName: "capture-driver-properties"
                    text: qsTr("设备属性"); ghost: true
                    enabled: !veyra.captureQueryBusy && !captureDialog.live && !veyra.liveOpeningText.length
                    onClicked: veyra.openCaptureDriverSettings(false)
                }
                VButton {
                    objectName: "capture-driver-input"
                    visible: veyra.captureBlackmagicDevice
                    text: qsTr("输入选择"); ghost: true
                    enabled: !veyra.captureQueryBusy && !captureDialog.live && !veyra.liveOpeningText.length
                    onClicked: veyra.openCaptureDriverSettings(true)
                }
            }
        }
        VRow {
            objectName: "capture-blackmagic-native-row"
            label: qsTr("Blackmagic 原生采集")
            hint: qsTr("默认关闭。开启使用 DeckLink 原生接口，关闭使用 DirectShow。需安装 Desktop Video 驱动；切换后重新选择设备和格式并连接，不写入驱动永久配置。")
            VSwitch {
                objectName: "capture-blackmagic-native"
                enabled: !veyra.captureQueryBusy && (veyra.captureBlackmagicDevice || veyra.preferences.blackmagicNativeCapture === true)
                opacity: enabled ? 1 : 0.35
                checked: veyra.preferences.blackmagicNativeCapture === true
                onToggled: checked => veyra.setPreference("blackmagicNativeCapture", checked)
            }
        }
        VRow {
            objectName: "capture-magewell-row"
            label: qsTr("美乐威低延迟模式")
            hint: !veyra.captureMagewellDevice
                  ? qsTr("仅美乐威 Pro Capture（PCIe 内置卡）可用；USB Capture 系列不支持")
                  : (veyra.captureMagewellStatus.length > 0 ? veyra.captureMagewellStatus + " · " : "")
                    + qsTr("采集卡收到 64 行就开始传，整帧更早到达（官方：1080p60 约省 5 ms，4K60 约省 15 ms）。开关后点「连接」重新连接生效")
            VSwitch {
                objectName: "capture-magewell"
                enabled: veyra.captureMagewellDevice
                opacity: enabled ? 1 : 0.35
                checked: veyra.captureMagewellDevice && veyra.preferences.magewellLowLatency === true
                onToggled: checked => veyra.setPreference("magewellLowLatency", checked)
            }
        }
        VRow {
            label: qsTr("设备帧率 FPS")
            hint: qsTr("0 = 沿用所选格式；设备返回其他帧率会报错，不在软件中偷偷丢帧")
            DialogTextField {
                objectName: "capture-fps"
                implicitWidth: 100
                text: String(veyra.captureRequestedFps)
                onEdited: text => veyra.captureRequestedFps = Number(text)
                // Take a valid rate as it is typed. Only a canonical number ("59.94", not
                // "59.") so the field's binding never rewrites what is still being typed.
                onTyped: text => {
                    const n = Number(text.trim())
                    if (text.trim().length > 0 && String(n) === text.trim() && (n === 0 || (n >= 1 && n <= 1000)))
                        veyra.captureRequestedFps = n
                }
            }
        }
    }
    DialogSection { text: qsTr("音频") }
    DialogGroup {
        VRow {
            label: qsTr("音频监听")
            hint: qsTr("只连接明确选中的输入，默认不监听")
            VSelect {
                objectName: "capture-audio"
                implicitWidth: 260
                value: captureDialog.labelOf(veyra.captureAudioInputs, veyra.captureAudioChoice, qsTr("不监听音频"))
                options: veyra.captureAudioInputs
                onPicked: id => veyra.captureAudioChoice = Number(id)
            }
        }
        VRow {
            label: qsTr("虚拟扬声器")
            hint: veyra.captureVirtualAudioBusy ? qsTr("正在处理 VB-CABLE 安装器…")
                : veyra.captureVirtualAudioInstalled ? qsTr("已检测到 VB-CABLE · 用于转接 Windows 侦听")
                : qsTr("可选 · 采集卡直采无声时使用")
            RowLayout {
                spacing: 6
                VButton {
                    objectName: "capture-virtual-audio-setup"
                    text: veyra.captureVirtualAudioInstalled ? qsTr("使用说明") : qsTr("安装…")
                    ghost: true
                    enabled: !veyra.captureVirtualAudioBusy
                    onClicked: virtualAudioGuide.showFor(veyra.captureVirtualAudioInstalled ? 0 : 1)
                }
                VButton {
                    objectName: "capture-virtual-audio-remove"
                    visible: veyra.captureVirtualAudioInstalled
                    text: qsTr("卸载…")
                    ghost: true
                    enabled: !veyra.captureVirtualAudioBusy && !captureDialog.live
                    onClicked: virtualAudioGuide.showFor(2)
                }
            }
        }
        VRow {
            label: qsTr("Dolby / DTS 位流")
            VSeg {
                objectName: "capture-audio-ingress"
                options: [{ id: "0", label: qsTr("自动") }, { id: "1", label: qsTr("强制 PCM") }, { id: "2", label: qsTr("位流优先") }]
                current: String(veyra.captureAudioIngress)
                onPicked: id => veyra.captureAudioIngress = Number(id)
            }
        }
    }
    VConfirm {
        id: virtualAudioGuide
        parent: captureDialog.dialogHost
        objectName: "capture-virtual-audio-confirm"
        cardWidth: 510
        property int operation: 0
        function showFor(value) { operation = value; open() }
        title: operation === 2 ? qsTr("卸载虚拟扬声器")
            : operation === 1 ? qsTr("安装虚拟扬声器") : qsTr("虚拟扬声器使用说明")
        text: operation === 2
            ? qsTr("先在 Windows 中关闭采集卡的“侦听此设备”，或把目标改回原设备。确认后打开 VB-Audio 安装器，选择 Remove Driver 并按提示重启。其他使用 VB-CABLE 的软件也会失去这条音频通路。")
            : qsTr("此功能使用 VB-Audio 的 VB-CABLE（捐赠软件）。首次安装需管理员确认，完成后按安装器提示重启。\n\n1. 在 Windows“录制”里打开采集卡音频的属性，在“侦听”中勾选“侦听此设备”，播放目标选 CABLE Input。")
        acceptText: operation === 2 ? qsTr("确认卸载") : operation === 1 ? qsTr("确认安装") : qsTr("完成")
        onAccepted: { if (operation !== 0) veyra.setupCaptureVirtualAudio(operation === 2) }
        Connections {
            target: captureDialog.dialogHost
            function onDialogChanged() { if (!captureDialog.shown) virtualAudioGuide.close() }
        }
        RowLayout {
            Layout.fillWidth: true
            VButton {
                objectName: "capture-virtual-audio-windows-listen"
                text: qsTr("打开 Windows 录制设置")
                ghost: true
                onClicked: veyra.openCaptureWindowsListen()
            }
            VButton {
                text: qsTr("VB-Audio 官网")
                ghost: true
                onClicked: veyra.openCaptureVirtualAudioVendor()
            }
        }
        Text {
            objectName: "capture-virtual-audio-follow-up"
            visible: virtualAudioGuide.operation !== 2
            Layout.fillWidth: true
            text: qsTr("2. 回到 Veyra，在“音频监听”选“虚拟扬声器回录”，再连接采集。\n3. 在 Veyra 的音频设置中选真实耳机或音箱，可继续使用音画同步。")
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            wrapMode: Text.WordWrap
            lineHeight: 1.3
        }
    }
    // PS5 手柄控制: the capture card stays the picture and the sound; PS5 Remote
    // Play only carries the pad (also from a handheld this picture is re-streamed to).
    DialogSection { visible: captureDialog.pc.available === true; text: qsTr("PS5 手柄控制") }
    DialogGroup {
        visible: captureDialog.pc.available === true
        VRow {
            label: qsTr("用远程游玩控制 PS5")
            hint: qsTr("画面和声音来自采集卡，手柄经 PS5 远程游玩发给主机。把处理后的画面再串流给掌机或别的电脑时，用那边的手柄玩 PS5")
            VSwitch {
                objectName: "capture-ps5-control"
                checked: captureDialog.pcOn
                onToggled: checked => veyra.ps5ControlSet("enabled", checked)
            }
        }
        VRow {
            visible: captureDialog.pcOn
            label: qsTr("PS5 主机")
            hint: veyra.ps5Profiles.length === 0 ? qsTr("还没有配对的 PS5，先在「PS5 串流」里配对一次") : qsTr("和「PS5 串流」共用配对")
            RowLayout {
                spacing: 6
                VSelect {
                    objectName: "capture-ps5-host"
                    visible: veyra.ps5Profiles.length > 0
                    implicitWidth: 170
                    value: { const p = veyra.ps5Profiles.find(x => x.id === veyra.ps5.profile); return p ? p.label : qsTr("请选择") }
                    options: veyra.ps5Profiles
                    onPicked: id => veyra.ps5SelectProfile(id)
                }
                VTag {
                    objectName: "capture-ps5-state"
                    Layout.alignment: Qt.AlignVCenter
                    text: captureDialog.pcTag
                    kind: captureDialog.pc.state === "connected" ? "ok"
                        : captureDialog.pc.state === "failed" || captureDialog.pc.state === "nopair" || captureDialog.pc.state === "pin" ? "warn" : ""
                }
                VButton {
                    objectName: "capture-ps5-restart"
                    visible: captureDialog.pc.state === "failed"
                    text: qsTr("重连"); ghost: true
                    onClicked: veyra.ps5ControlRestart()
                }
                VButton {
                    objectName: "capture-ps5-pair"
                    text: veyra.ps5Profiles.length === 0 ? qsTr("去配对") : qsTr("配对…")
                    ghost: true
                    onClicked: dialogHost.open("ps5")
                }
            }
        }
        VRow {
            visible: captureDialog.pcOn && captureDialog.pc.state === "pin"
            label: qsTr("登录 PIN")
            hint: qsTr("PS5 提示时填写，不是 8 位配对码")
            RowLayout {
                spacing: 6
                DialogTextField { id: controlPin; objectName: "capture-ps5-pin"; implicitWidth: 120 }
                VButton { text: qsTr("提交"); ghost: true; onClicked: { veyra.ps5SendLoginPin(controlPin.text); controlPin.text = "" } }
            }
        }
        VRow {
            visible: captureDialog.pcOn
            label: qsTr("后台也转发手柄")
            hint: qsTr("再串流时建议打开：Veyra 不在前台也继续转发。打开后在这台电脑上用别的程序时，手柄也会操作 PS5")
            VSwitch {
                objectName: "capture-ps5-background"
                checked: captureDialog.pc.background === true
                onToggled: checked => veyra.ps5ControlSet("background", checked)
            }
        }
        VRow {
            visible: captureDialog.pcOn
            label: qsTr("手柄")
            hint: qsTr("谁按键就用谁：本机手柄、串流软件建的虚拟手柄都可以")
            Text {
                objectName: "capture-ps5-pad"
                text: (captureDialog.pc.pad || "").length > 0
                      ? captureDialog.pc.pad + " · " + (captureDialog.pc.gyro === true ? qsTr("有陀螺仪") : qsTr("无陀螺仪"))
                      : captureDialog.pc.state === "off" || captureDialog.pc.state === "nopair" ? "—" : qsTr("未检测到手柄")
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
                elide: Text.ElideRight
                width: Math.min(implicitWidth, 260)
            }
        }
    }
    DialogNote {
        objectName: "capture-ps5-note"
        visible: captureDialog.pcOn
        text: (captureDialog.pcStatus.length > 0 ? captureDialog.pcStatus + "\n" : "")
              + qsTr("PS5 仍会发一路 720p 30fps、约 5 Mbps 的保活画面，本机收到即丢弃；PS5 同一时间只允许一个远程游玩连接。再串流时记得打开上面的「音频监听」。")
    }
    DialogSection { text: qsTr("格式与画面") }
    DialogGroup {
        VRow {
            label: qsTr("输入色彩（SDR / HDR）")
            hint: qsTr("不确定就选「自动」。采集 HDR：采集格式选 P010，这里选「HDR · PQ」（游戏机、显卡的 HDR10 都是 PQ）。「HDR · HLG」用于广播信号。「SDR · 709」是普通画面。变更需重连")
            VSeg {
                objectName: "capture-color-space"
                options: [{ id: "0", label: qsTr("自动") }, { id: "1", label: "HDR · PQ" }, { id: "2", label: "HDR · HLG" }, { id: "3", label: "SDR · 709" }]
                current: String(veyra.captureColorSpace)
                onPicked: id => veyra.captureColorSpace = Number(id)
            }
        }
        VRow {
            label: qsTr("输入范围")
            hint: qsTr("Limited = 16–235，Full = 0–255。画面发灰选「有限」，暗部全黑、亮部过曝选「完全」")
            VSeg {
                objectName: "capture-color-range"
                options: [{ id: "0", label: qsTr("自动") }, { id: "1", label: qsTr("有限 · Limited") }, { id: "2", label: qsTr("完全 · Full") }]
                current: String(veyra.captureColorRange)
                onPicked: id => veyra.captureColorRange = Number(id)
            }
        }
        VRow {
            label: qsTr("设备缓冲")
            hint: qsTr("自动：1080p 及以下 2 帧，更高 3 帧；最小 1 帧延迟最低但可能丢帧")
            VSeg {
                objectName: "capture-buffer-mode"
                options: [{ id: "0", label: qsTr("自动") }, { id: "1", label: qsTr("最小") }, { id: "2", label: qsTr("驱动默认") }]
                current: String(veyra.captureBufferMode)
                onPicked: id => veyra.captureBufferMode = Number(id)
            }
        }
        VRow {
            label: qsTr("转为 SDR 显示")
            hint: qsTr("收到 HDR 也按 SDR 预览，立即生效")
            VSwitch {
                objectName: "capture-force-sdr"
                checked: veyra.captureForceSdr
                onToggled: checked => veyra.captureForceSdr = checked
            }
        }
        VRow {
            label: qsTr("画面上下翻转")
            hint: qsTr("采集画面倒置时开启，立即生效")
            VSwitch {
                objectName: "capture-flip-vertical"
                checked: veyra.captureFlipVertical
                onToggled: checked => veyra.captureFlipVertical = checked
            }
        }
    }
    DialogNote { visible: veyra.captureStatus.length > 0; text: veyra.captureStatus }
}
