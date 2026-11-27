import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

ColumnLayout {
    id: page

    objectName: "controller-panel-connection-page"
    property var bridge: null
    property var input: null
    property bool xbox: false
    property bool captureOnly: true
    property bool activePage: visible
    property bool pairFormExpanded: false
    property bool pairPending: false
    property bool pairCaptureOnly: true
    property string forgetHost: ""
    signal captureSettingsRequested()
    signal streamStarted()

    readonly property var ps5: bridge?.ps5 || ({})
    readonly property var ps5Hosts: bridge?.ps5Profiles || []
    readonly property var ps5Control: bridge?.ps5Control || ({})
    readonly property var xboxControl: bridge?.xboxControl || ({})
    readonly property var xboxModel: bridge?.xbox || null
    readonly property var xboxState: xboxModel?.state || ({})
    readonly property var xboxHosts: xboxModel?.consoles || []
    readonly property var control: xbox ? xboxControl : ps5Control
    readonly property var loginPrompt: bridge?.ps5LoginPrompt || ({})
    readonly property bool captureReady: bridge?.isCapture === true
        && (!bridge.sourceKind || bridge.sourceKind === "capture")
    readonly property bool enabledForCapture: control.enabled === true
    readonly property bool keyboard: xbox
        ? (input?.xboxKeyboardEnabled ?? input?.keyboardEnabled) === true
        : (input?.ps5KeyboardEnabled ?? input?.keyboardEnabled) === true
    readonly property bool captured: xbox ? input?.xboxCaptured === true : input?.ps5Captured === true
    readonly property string platformName: xbox ? "Xbox" : "PS5"
    readonly property string xboxHostId: (captureOnly ? xboxControl.host : "") || xboxState.selected || ""
    readonly property bool paired: xbox ? xboxState.signedIn === true && xboxHostId.length > 0 : ps5Hosts.length > 0
    readonly property string hostName: xbox
        ? (xboxHosts.find(host => host.id === xboxHostId)?.name || xboxState.lastLabel || platformName)
        : (ps5Hosts.find(host => host.id === ps5.profile)?.label || ps5.host || platformName)
    readonly property string nativeState: captureOnly ? control.state || "off"
        : xbox ? xboxState.stream?.state || (xboxState.streaming ? "starting" : "off")
        : ps5.state || (bridge?.ps5VideoActive ? "connected" : ps5.active ? "starting" : "off")
    readonly property string occupation: {
        const xboxPad = ["starting", "retry", "connected"].includes(xboxControl.state)
        const ps5Pad = ["starting", "recovering", "pin", "connected"].includes(ps5Control.state)
        const ps5Stream = ps5.active || ps5.state === "starting"
        if (xbox && (ps5Pad || ps5Stream || xboxControl.state === "ps5"))
            return ps5Pad || xboxControl.state === "ps5" ? "ps5-pad" : "ps5-stream"
        if (!xbox && (xboxPad || xboxState.streaming)) return xboxPad ? "xbox-pad" : "xbox-stream"
        if (captureOnly && xbox && (xboxState.streaming || nativeState === "full")) return "xbox-stream"
        if (captureOnly && !xbox && ps5Stream) return "ps5-stream"
        if (!captureOnly && xbox && xboxPad) return "xbox-pad"
        if (!captureOnly && !xbox && ps5Pad) return "ps5-pad"
        return ""
    }
    readonly property bool connected: nativeState === "connected" || nativeState === "streaming"
    readonly property bool connecting: (xbox ? xboxState.busy : ps5.busy) === true
        || ["starting", "connecting", "retry", "recovering"].includes(nativeState)
    readonly property bool pinRequested: !xbox && !!loginPrompt.requestId
        && (loginPrompt.controlOnly === true) === captureOnly
    readonly property bool showPairForm: !xbox && (!paired || pairFormExpanded)
    readonly property bool pairReady: accountField.text.trim().length > 0
        && addressField.text.trim().length > 0 && /^[0-9]{8}$/.test(pairPin.text)
    readonly property string padName: (captureOnly ? control.pad : xbox ? xboxControl.pad : ps5.pad) || ""
    readonly property bool hasGyro: (captureOnly ? control.gyro : ps5.gyro) === true
    readonly property string liveLayout: input?.driving ? qsTr("驾驶") : qsTr("步行")
    readonly property string profileLabel: !input?.profile || input.profile === "默认" ? qsTr("默认") : input.profile
    readonly property string phase: {
        if (captured) return "captured"
        if (occupation) return "occupied"
        if (pinRequested || nativeState === "pin") return "pin"
        if (connected) return "connected"
        if (connecting) return "connecting"
        if (nativeState === "failed" || nativeState === "ended") return "failed"
        if (xbox && !xboxState.signedIn) return "signin"
        if (!paired || nativeState === "nopair") return xbox ? "host" : "nopair"
        if (captureOnly && !captureReady) return "capture"
        return captureOnly && !enabledForCapture ? "disabled" : "idle"
    }
    readonly property string level: phase === "connected" || phase === "captured" ? "ok"
        : phase === "failed" ? "err" : ["capture", "signin", "nopair", "host", "pin", "occupied"].includes(phase) ? "warn" : "idle"
    readonly property string title: {
        switch (phase) {
        case "captured": return qsTr("键鼠控制中 · %1").arg(liveLayout)
        case "occupied": return qsTr("另一个串流正在使用主机")
        case "pin": return qsTr("PS5 要求输入登录 PIN")
        case "connected":
            return keyboard ? qsTr("已连接 %1 · 键鼠映射").arg(hostName)
                            : qsTr("已连接 %1 · 实体手柄").arg(hostName)
        case "connecting": return qsTr("正在连接 %1…").arg(hostName)
        case "failed": return qsTr("连接失败")
        case "capture": return qsTr("采集卡未连接")
        case "signin": return qsTr("未登录")
        case "host": return qsTr("选择主机")
        case "nopair": return qsTr("还没有配对的 PS5")
        case "disabled": return qsTr("手柄控制未开启")
        default: return qsTr("等待连接")
        }
    }
    readonly property string detail: {
        switch (phase) {
        case "occupied": return occupation.startsWith("xbox") ? qsTr("Xbox 串流正在运行") : qsTr("PS5 串流正在运行")
        case "pin": return qsTr("在下方填写 PS5 的登录 PIN，不是 8 位配对码。")
        case "connected":
            if (keyboard) return qsTr("档案：%1 · 布局：%2").arg(profileLabel).arg(liveLayout)
            if (!padName) return qsTr("未检测到手柄")
            return qsTr("正在转发：%1").arg(xbox ? padName
                : qsTr("%1 · %2").arg(padName).arg(hasGyro ? qsTr("有陀螺仪") : qsTr("无陀螺仪")))
        case "connecting": return operationStatus || qsTr("主机在待机时会先唤醒，约需 10–30 秒。")
        case "failed": return operationStatus || qsTr("请检查主机和网络后重连。")
        case "capture": return qsTr("画面来自采集卡，先连接采集卡再开始游戏。")
        case "signin": return qsTr("登录 Xbox（微软）账号后选择主机。")
        case "host": return qsTr("从下面的列表中选择要控制的 Xbox。")
        case "nopair": return qsTr("在「连接」里配对一次，以后可以直接连接。")
        case "disabled": return qsTr("开启后，操作经远程游玩发给主机，画面仍来自采集卡。")
        default: return ""
        }
    }
    readonly property string operationStatus: xbox
        ? (captureOnly ? xboxControl.message : xboxState.stream?.message) || xboxState.status || ""
        : (captureOnly ? ps5Control.message : "") || ps5.status || ""
    readonly property var shortcuts: !captured ? [] : [
        { key: input?.unlockLabel || "F1", label: qsTr("释放键鼠") },
        { key: input?.quickKeyLabel || "F2", label: qsTr("键位速查") },
        { key: input?.driveKeyLabel || "F6", label: qsTr("切换步行/驾驶") }
    ]
    readonly property string actionLabel: {
        switch (phase) {
        case "occupied": return qsTr("断开它")
        case "connecting": return qsTr("取消")
        case "capture": return qsTr("打开采集卡")
        case "failed": return qsTr("重连")
        default: return ""
        }
    }
    readonly property string primaryLabel: {
        if (connecting) return qsTr("连接中…")
        if (phase === "pin") return qsTr("等待登录 PIN")
        if (xbox && !xboxState.signedIn) return qsTr("登录")
        if (xbox && !paired) return qsTr("选择主机")
        if (showPairForm) return qsTr("配对并连接")
        if (captureOnly && enabledForCapture && !captureReady) return qsTr("打开采集卡")
        return qsTr("连接 %1").arg(platformName)
    }
    readonly property bool primaryEnabled: !!bridge && !connecting && !occupation && phase !== "pin"
        && (!xbox || !!xboxModel) && (!xbox || !xboxState.signedIn || paired) && (!showPairForm || pairReady)

    spacing: 12

    function prepare() {
        if (xbox) {
            if (!xboxState.busy && typeof xboxModel?.load === "function") xboxModel.load()
        } else if (!ps5.busy && !ps5.active && ps5.state !== "starting" && typeof bridge?.ps5Load === "function") {
            bridge.ps5Load()
        }
    }

    function connectOrPair() {
        if (!primaryEnabled || connected || captured) return
        if (xbox && !xboxState.signedIn) {
            xboxModel?.signIn()
        } else if (showPairForm) {
            pairPending = true
            pairCaptureOnly = captureOnly
            bridge.ps5Set("account", accountField.text.trim())
            bridge.ps5Set("host", addressField.text.trim())
            bridge.ps5Pair(pairPin.text)
            pairPin.clear()
            if (!ps5.busy) pairPending = false
        } else if (captureOnly && enabledForCapture && !captureReady) {
            captureSettingsRequested()
        } else {
            connectSelected()
        }
    }

    function connectSelected() {
        if (!bridge || occupation) return
        if (captureOnly) {
            if (xbox) {
                if (!xboxHostId) return
                if (xboxControl.host !== xboxHostId || nativeState === "failed") {
                    bridge.xboxControlSet("host", xboxHostId)
                }
                if (!enabledForCapture) bridge.xboxControlSet("enabled", true)
            } else if (enabledForCapture && nativeState === "failed") {
                bridge.ps5ControlRestart()
            } else if (!enabledForCapture) {
                bridge.ps5ControlSet("enabled", true)
            }
        } else if (xbox ? xboxModel?.connectStream() : bridge.ps5Connect()) {
            streamStarted()
        }
    }

    function disconnectOrCancel() {
        if (!bridge) return
        if (xbox ? xboxState.busy : ps5.busy) {
            if (xbox) xboxModel?.cancel()
            else bridge.ps5Cancel()
        } else if (captureOnly) {
            setCaptureOption("enabled", false)
        } else if (xbox) {
            bridge.xboxDisconnect()
        } else {
            bridge.ps5Cancel()
        }
    }

    function performStatusAction() {
        if (phase === "capture") captureSettingsRequested()
        else if (phase === "connecting") disconnectOrCancel()
        else if (phase === "failed") connectSelected()
        else if (phase === "occupied") {
            switch (occupation) {
            case "ps5-pad": bridge.ps5ControlSet("enabled", false); break
            case "ps5-stream": bridge.ps5Cancel(); break
            case "xbox-pad": bridge.xboxControlSet("enabled", false); break
            case "xbox-stream": bridge.xboxDisconnect(); break
            }
        }
    }

    function setCaptureOption(key, on) {
        if (xbox) bridge?.xboxControlSet(key, on)
        else bridge?.ps5ControlSet(key, on)
    }

    function pickXboxHost(id) {
        xboxModel?.select(id)
        if (captureOnly && xboxControl.host !== id) bridge?.xboxControlSet("host", id)
    }

    function scrollToPair() {
        pairFormExpanded = !pairFormExpanded
        if (pairFormExpanded) Qt.callLater(() => accountField.forceActiveFocus())
    }

    Connections {
        target: page.bridge
        ignoreUnknownSignals: true
        function onPs5Changed() {
            if (!page.ps5.busy && page.pairPending) Qt.callLater(() => {
                if (!page.ps5.busy) page.pairPending = false
            })
        }
        function onPs5PairSaved() {
            if (!page.pairPending) return
            page.pairPending = false
            page.pairFormExpanded = false
            if (!page.xbox && page.captureOnly === page.pairCaptureOnly) page.connectSelected()
        }
    }

    onActivePageChanged: {
        if (activePage && pinRequested) Qt.callLater(loginPin.focusEditor)
    }
    onPinRequestedChanged: {
        if (pinRequested && activePage) Qt.callLater(loginPin.focusEditor)
    }

    component PanelToggle: VSwitch {
        property string label: ""
        activeFocusOnTab: visible && enabled
        border.width: activeFocus ? 1 : 0
        border.color: Theme.accent
        Accessible.role: Accessible.CheckBox
        Accessible.name: label
        Accessible.checked: checked
        Accessible.onToggleAction: toggled(!checked)
        Keys.onReturnPressed: toggled(!checked)
        Keys.onSpacePressed: toggled(!checked)
    }

    component PanelText: Text {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }

    component PanelField: Basic.TextField {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        implicitHeight: Theme.ctlHeight
        color: Theme.t1
        placeholderTextColor: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
        focusPolicy: Qt.StrongFocus
        selectByMouse: true
        background: Rectangle {
            radius: Theme.rCtl
            color: Theme.card3
            border.width: 1
            border.color: parent.activeFocus ? Theme.accent : Theme.stroke2
        }
    }

    component HostChoice: AbstractButton {
        id: hostChoice
        property string subtitle: ""
        property bool selected: false
        property bool online: false

        Layout.fillWidth: true
        Layout.minimumWidth: 0
        padding: 10
        implicitHeight: Math.max(Theme.rowMinHeight, contentItem.implicitHeight + topPadding + bottomPadding)
        focusPolicy: Qt.StrongFocus
        hoverEnabled: true
        Accessible.name: text
        Accessible.description: subtitle
        Keys.onReturnPressed: clicked()
        background: Rectangle {
            radius: Theme.rCtl
            color: hostChoice.selected ? Theme.accentSoft : hostChoice.hovered ? Theme.card3 : Theme.card2
            border.width: hostChoice.activeFocus ? 2 : 1
            border.color: hostChoice.selected || hostChoice.activeFocus ? Theme.accent : Theme.stroke2
        }
        contentItem: RowLayout {
            spacing: 8
            VDot { off: !hostChoice.online }
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 2
                PanelText {
                    text: hostChoice.text
                    color: Theme.t1
                    font.pixelSize: Theme.fsBody
                    font.weight: Font.DemiBold
                }
                PanelText {
                    visible: text.length > 0
                    text: hostChoice.subtitle
                }
            }
            VIcon {
                visible: hostChoice.selected
                name: "check"
                color: Theme.accent
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: Theme.rowMinHeight
        visible: page.captureOnly
        spacing: 10
        PanelText {
            text: page.xbox ? qsTr("用远程游玩控制 Xbox") : qsTr("用远程游玩控制 PS5")
            color: Theme.t1
            font.pixelSize: Theme.fsBody
        }
        PanelToggle {
            objectName: "controller-panel-connection-enabled"
            label: page.xbox ? qsTr("用远程游玩控制 Xbox") : qsTr("用远程游玩控制 PS5")
            checked: page.enabledForCapture
            enabled: !page.occupation
            onToggled: on => page.setCaptureOption("enabled", on)
        }
    }

    PanelText {
        visible: page.captureOnly
        text: qsTr("画面和声音来自采集卡，操作经远程游玩发给主机。")
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: !page.xbox
        spacing: 8

        Repeater {
            model: page.ps5Hosts
            delegate: ColumnLayout {
                id: ps5Host
                required property var modelData
                readonly property bool picked: page.ps5.profile === modelData.id
                Layout.fillWidth: true
                spacing: 6

                HostChoice {
                    objectName: "controller-panel-ps5-host-" + ps5Host.modelData.id
                    text: ps5Host.modelData.name || "PS5"
                    subtitle: ps5Host.modelData.host || ps5Host.modelData.label || ""
                    selected: ps5Host.picked
                    online: selected && page.connected
                    enabled: !page.ps5.busy
                    onClicked: {
                        if (ps5Host.picked) return
                        page.forgetHost = ""
                        page.pairFormExpanded = false
                        page.bridge.ps5SelectProfile(ps5Host.modelData.id)
                    }
                }

                RowLayout {
                    Layout.fillWidth: true
                    visible: ps5Host.picked
                    spacing: 8
                    Item { Layout.fillWidth: true }
                    ControllerPanelButton {
                        objectName: "controller-panel-ps5-wake"
                        text: qsTr("唤醒")
                        enabled: !page.ps5.busy
                        ghost: true
                        onClicked: page.bridge.ps5Wake()
                    }
                    ControllerPanelButton {
                        objectName: "controller-panel-ps5-forget"
                        text: page.forgetHost === ps5Host.modelData.id ? qsTr("确认删除") : qsTr("删除配对")
                        enabled: !page.ps5.busy
                        onClicked: {
                            if (page.forgetHost === ps5Host.modelData.id) {
                                page.forgetHost = ""
                                page.bridge.ps5Forget()
                            } else {
                                page.forgetHost = ps5Host.modelData.id
                            }
                        }
                    }
                    ControllerPanelButton {
                        visible: page.forgetHost === ps5Host.modelData.id
                        text: qsTr("取消")
                        ghost: true
                        onClicked: page.forgetHost = ""
                    }
                }
            }
        }

        ControllerPanelButton {
            objectName: "controller-panel-ps5-add"
            visible: page.paired
            text: page.pairFormExpanded ? qsTr("收起配对") : qsTr("配对新主机")
            onClicked: page.scrollToPair()
        }

        Rectangle {
            objectName: "controller-panel-ps5-pair-form"
            Layout.fillWidth: true
            visible: page.showPairForm
            implicitHeight: pairing.implicitHeight + 24
            radius: Theme.rCard
            color: Theme.card2
            border.width: 1
            border.color: Theme.stroke2

            ColumnLayout {
                id: pairing
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 8

                PanelText { text: qsTr("配对新主机"); color: Theme.t1; font.pixelSize: Theme.fsH3 }
                PanelText { text: qsTr("%1 · %2").arg("1").arg("Account ID") }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    PanelField {
                        id: accountField
                        objectName: "controller-panel-ps5-account"
                        placeholderText: "Account ID"
                        Accessible.name: "Account ID"
                        enabled: !page.ps5.busy
                    }
                    ControllerPanelButton {
                        objectName: "controller-panel-ps5-account-help"
                        text: qsTr("获取 Account ID")
                        maxTextWidth: 106
                        onClicked: page.bridge?.ps5OpenAccountChecker()
                    }
                }
                PanelText {
                    text: qsTr("使用要配对的 PS5 用户对应的 PSN 账号查询，在这里填入数字 ID 或 8 字节 Base64。")
                }

                PanelText { text: qsTr("%1 · %2").arg("2").arg(qsTr("主机地址")) }
                RowLayout {
                    Layout.fillWidth: true
                    spacing: 8
                    PanelField {
                        id: addressField
                        objectName: "controller-panel-ps5-address"
                        placeholderText: "192.168.1.x"
                        Accessible.name: qsTr("主机地址")
                        enabled: !page.ps5.busy
                    }
                    ControllerPanelButton {
                        objectName: "controller-panel-ps5-scan"
                        text: qsTr("查找")
                        enabled: !page.ps5.busy
                        onClicked: page.bridge?.ps5Scan()
                    }
                }
                PanelText { text: qsTr("PS5 设置 → 网络 → 连接状态；找不到时可手填 IP") }
                Flow {
                    Layout.fillWidth: true
                    spacing: 5
                    Repeater {
                        model: page.ps5.discoveredHosts || []
                        delegate: ControllerPanelButton {
                            required property string modelData
                            text: modelData
                            onClicked: addressField.text = modelData
                        }
                    }
                }

                PanelText { text: qsTr("%1 · %2").arg("3").arg(qsTr("8 位配对码")) }
                PanelField {
                    id: pairPin
                    objectName: "controller-panel-ps5-pair-pin"
                    placeholderText: "••••••••"
                    Accessible.name: qsTr("8 位配对码")
                    enabled: !page.ps5.busy
                    echoMode: TextInput.Password
                    passwordMaskDelay: 0
                    maximumLength: 8
                    inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                    validator: RegularExpressionValidator { regularExpression: /^[0-9]{0,8}$/ }
                    onAccepted: page.connectOrPair()
                }
                PanelText { text: qsTr("PS5 设置 → 系统 → 远程游玩 → 关联设备") }
                PanelText {
                    visible: text.length > 0
                    text: page.ps5.status || ""
                    color: Theme.warn
                }
            }
        }

        PanelText {
            visible: page.pinRequested
            text: qsTr("登录 PIN")
            color: Theme.t1
            font.pixelSize: Theme.fsBody
        }
        Ps5LoginPinField {
            id: loginPin
            objectName: "controller-panel-ps5-login-pin"
            visible: page.pinRequested
            backend: page.bridge
            prompt: page.pinRequested ? page.loginPrompt : ({})
            active: page.activePage && page.pinRequested
        }
        PanelText {
            visible: page.pinRequested
            text: loginPin.feedback || qsTr("PS5 提示时填写，不是 8 位配对码")
            color: loginPin.feedback ? Theme.warn : Theme.t2
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.xbox
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.minimumHeight: Theme.rowMinHeight
            PanelText {
                text: page.xboxState.signedIn ? qsTr("已登录") : qsTr("未登录")
                color: Theme.t1
                font.pixelSize: Theme.fsBody
            }
            ControllerPanelButton {
                objectName: "controller-panel-xbox-account-action"
                visible: page.xboxState.signedIn === true
                text: qsTr("退出登录")
                enabled: !!page.xboxModel && !page.xboxState.busy
                ghost: true
                onClicked: page.xboxModel.signOut()
            }
        }

        Rectangle {
            Layout.fillWidth: true
            visible: !!page.xboxState.code
            implicitHeight: codeBox.implicitHeight + 24
            radius: Theme.rCard
            color: Theme.accentSoft
            border.width: 1
            border.color: Theme.accent

            ColumnLayout {
                id: codeBox
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 6

                PanelText {
                    text: qsTr("打开 %1，输入下面的登录码。").arg(page.xboxState.verificationUri || "https://www.microsoft.com/link")
                }
                PanelText {
                    objectName: "controller-panel-xbox-code"
                    text: page.xboxState.code || ""
                    color: Theme.t1
                    font.family: Theme.fontMono
                    font.pixelSize: Theme.fsH3
                }
                ControllerPanelButton {
                    text: qsTr("在浏览器打开登录页")
                    onClicked: page.xboxModel?.openSignInPage()
                }
            }
        }

        Repeater {
            model: page.xboxState.signedIn ? page.xboxHosts : []
            delegate: HostChoice {
                required property var modelData
                objectName: "controller-panel-xbox-host-" + modelData.id
                text: modelData.name || "Xbox"
                subtitle: qsTr("%1 · %2").arg(modelData.type || "").arg(modelData.power || "")
                selected: page.xboxHostId === modelData.id
                online: selected && page.connected
                enabled: !page.xboxState.busy
                onClicked: page.pickXboxHost(modelData.id)
            }
        }
        RowLayout {
            Layout.fillWidth: true
            visible: page.xboxState.signedIn === true
            PanelText { text: qsTr("主机要用同一个账号登录，并在 设置 → 设备和连接 → 远程功能 里启用远程功能") }
            ControllerPanelButton {
                objectName: "controller-panel-xbox-refresh"
                text: qsTr("刷新")
                enabled: !page.xboxState.busy
                onClicked: page.xboxModel?.refresh()
            }
        }
        PanelText {
            visible: text.length > 0
            text: page.xboxState.status || ""
            color: page.phase === "failed" ? Theme.warn : Theme.t2
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: Theme.rowMinHeight
        visible: page.captureOnly && page.enabledForCapture
        spacing: 10

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4
            PanelText {
                text: qsTr("后台也转发手柄")
                color: Theme.t1
                font.pixelSize: Theme.fsBody
            }
            PanelText { text: qsTr("离开 Veyra 后，实体手柄仍会控制主机。") }
        }
        PanelToggle {
            objectName: "controller-panel-connection-background"
            label: qsTr("后台也转发手柄")
            checked: page.control.background === true
            onToggled: on => page.setCaptureOption("background", on)
        }
    }

    VAccordion {
        id: keepalive
        Layout.fillWidth: true
        visible: page.captureOnly && page.enabledForCapture
        enabledSwitch: false
        compactHeader: true
        title: qsTr("为什么还在收 720p 画面？")
        glyph: "gamepad"
        activeFocusOnTab: visible
        Accessible.role: Accessible.Button
        Accessible.name: title
        Accessible.onPressAction: open = !open
        Keys.onReturnPressed: open = !open
        Keys.onSpacePressed: open = !open

        PanelText {
            text: page.xbox ? qsTr("Xbox 的 720p 保活画面收到即丢弃，采集卡负责实际画面和声音。")
                 : qsTr("PS5 仍会发一路 720p 30fps、约 5 Mbps 的保活画面，本机收到即丢弃；PS5 同一时间只允许一个远程游玩连接。再串流时记得选择上面的「音频输入」。")
        }
    }

    ControllerPanelButton {
        objectName: "controller-panel-connection-disconnect"
        Layout.alignment: Qt.AlignRight
        visible: page.connected
        text: qsTr("断开")
        onClicked: page.disconnectOrCancel()
    }
}
