// xbox dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: xbDialog
    objectName: "xbox-dialog"
    key: "xbox"
    glyph: "gamepad"
    title: qsTr("Xbox 串流")
    sub: qsTr("串流你自己的 Xbox 主机 · 非官方实验功能 · 增强与调色沿用软件自己的处理链")
    dialogWidth: 680
    readonly property var xb: veyra.xbox
    readonly property var st: xb ? xb.state : ({})
    readonly property var list: xb ? xb.consoles : []
    actions: st.streaming ? [
        { label: qsTr("关闭") },
        { label: qsTr("断开"), primary: true }
    ] : !st.signedIn ? [
        { label: st.busy ? qsTr("取消操作") : qsTr("关闭") }
    ] : [
        { label: st.busy ? qsTr("取消操作") : qsTr("关闭") },
        { label: qsTr("开始串流"), primary: true, icon: "play" }
    ]
    onActionTriggered: label => {
        if (label === qsTr("断开")) { veyra.xboxDisconnect(); dialogHost.close(); return }
        if (label === qsTr("取消操作")) { xb.cancel(); return }
        if (label === qsTr("关闭")) { dialogHost.close(); return }
        if (st.signedIn && !st.busy && (st.selected || "").length > 0) xb.connectStream()
    }
    Connections {
        target: xbDialog
        function onShownChanged() { if (xbDialog.shown && xbDialog.xb) xbDialog.xb.load() }
    }
    Connections {
        target: xbDialog.xb
        function onStarted() { dialogHost.close(); dialogHost.startXbox() }
    }

    DialogNote {
        text: qsTr("非官方：使用与 Greenlight 等开源客户端相同的方式连接微软的串流服务，不隶属于微软，微软随时可能改动导致失效。仅限你自己的账号和主机。")
    }
    DialogSection { text: qsTr("账号") }
    DialogGroup {
        VRow {
            label: xbDialog.st.signedIn ? qsTr("已登录") : qsTr("未登录")
            hint: xbDialog.st.signedIn ? qsTr("登录信息加密保存在本机；退出会删除它") : qsTr("用你的 Xbox（微软）账号在微软的页面登录，软件看不到密码")
            RowLayout {
                spacing: 6
                VSpinner { visible: xbDialog.st.busy === true; Layout.alignment: Qt.AlignVCenter }
                VButton { objectName: "xbox-signin"; visible: !xbDialog.st.signedIn; text: qsTr("登录"); primary: true; enabled: !xbDialog.st.busy; onClicked: xbDialog.xb.signIn() }
                VButton { visible: !!xbDialog.st.signedIn; text: qsTr("退出登录"); ghost: true; enabled: !xbDialog.st.busy; onClicked: xbDialog.xb.signOut() }
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
                text: qsTr("在手机或浏览器打开 ") + (xbDialog.st.verificationUri || "https://www.microsoft.com/link") + qsTr("，输入下面的登录码：")
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
            VButton { Layout.alignment: Qt.AlignHCenter; text: qsTr("在浏览器打开登录页"); ghost: true; onClicked: xbDialog.xb.openSignInPage() }
        }
    }

    DialogSection { visible: !!xbDialog.st.signedIn; text: qsTr("主机") }
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
                VDot { off: consoleRow.modelData.power === qsTr("关机"); warn: consoleRow.modelData.power === qsTr("正在更新"); Layout.alignment: Qt.AlignVCenter }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 1
                    Text { Layout.fillWidth: true; text: consoleRow.modelData.name; color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody; font.weight: Font.Medium; elide: Text.ElideRight }
                    Text { Layout.fillWidth: true; text: consoleRow.modelData.type + " · " + consoleRow.modelData.power; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: Theme.fsSmall; elide: Text.ElideRight }
                }
            }
        }
    }
    DialogGroup {
        visible: !!xbDialog.st.signedIn
        VRow {
            label: qsTr("主机列表")
            hint: qsTr("主机要用同一个账号登录，并在 设置 → 设备和连接 → 远程功能 里启用远程功能")
            VButton { text: qsTr("刷新"); ghost: true; enabled: !xbDialog.st.busy; onClicked: xbDialog.xb.refresh() }
        }
        VRow {
            label: qsTr("手柄")
            hint: qsTr("把电脑手柄当作主机上的手柄")
            VSwitch { objectName: "xbox-gamepad"; checked: xbDialog.st.gamepad !== false; onToggled: checked => xbDialog.xb.set("gamepad", checked) }
        }
        VRow {
            label: qsTr("软件视频解码")
            hint: qsTr("兼容选项：画面出现持续花屏时尝试开启，断开后重新连接生效。可能增加 CPU 占用和延迟；超分和补帧仍使用 GPU。")
            VSwitch { objectName: "xbox-software-decode"; checked: xbDialog.st.softwareDecode === true; onToggled: checked => xbDialog.xb.set("softwareDecode", checked) }
        }
        VRow {
            label: qsTr("硬件解码接口")
            hint: qsTr("自动 = D3D12VA。Intel 核显花屏时选 D3D11VA（DXVA），仍用核显硬解、不占 CPU。断开后重新连接生效。")
            hintBelow: true
            VSelect {
                objectName: "xbox-decode-backend"
                implicitWidth: 170
                title: qsTr("硬件解码接口")
                options: [{id:"0",label:qsTr("自动 (D3D12VA)")},{id:"1",label:qsTr("D3D11VA (DXVA)")}]
                value: xbDialog.st.decodeBackend === 1 ? qsTr("D3D11VA (DXVA)") : qsTr("自动 (D3D12VA)")
                onPicked: id => xbDialog.xb.set("decodeBackend", Number(id))
            }
        }
        VRow {
            label: qsTr("传输引擎")
            hint: qsTr("V1 原版：原生 libdatachannel 收流，延迟略低。V2 增强版：与 XStreaming 相同的 Chromium WebRTC 收流（WebView2），抗丢包/抖动更强、复杂网络更稳。断开后重新连接生效。")
            hintBelow: true
            VSelect {
                objectName: "xbox-transport-engine"
                implicitWidth: 170
                title: qsTr("传输引擎")
                options: [{id:"1",label:qsTr("V1 原版")},{id:"2",label:qsTr("V2 增强版")}]
                value: xbDialog.st.transportEngine === 1 ? qsTr("V1 原版") : qsTr("V2 增强版")
                onPicked: id => xbDialog.xb.set("transportEngine", Number(id))
            }
        }
        VRow {
            visible: true
            label: qsTr("码率上限 (Mbps)")
            hint: qsTr("0 = 由主机决定。建议 15–30；过高可能被主机钳制。")
            DialogTextField {
                objectName: "xbox-bitrate"
                implicitWidth: 110
                placeholder: "0"
                text: String(Math.round((xbDialog.st.videoBitrateKbps || 0) / 1000))
                onEdited: text => xbDialog.xb.set("videoBitrateKbps", Math.max(0, Math.min(40, parseInt(text) || 0)) * 1000)
            }
        }
        VRow {
            visible: true
            label: qsTr("自定义 TURN（可选）")
            hint: qsTr("格式 turn:主机:端口；复杂 NAT 下的兜底中继。用户名/密码可留空。")
            DialogTextField {
                objectName: "xbox-turn-url"
                implicitWidth: 190
                placeholder: "turn:host:3478"
                text: xbDialog.st.turnUrl || ""
                onEdited: text => xbDialog.xb.set("turnUrl", text)
            }
        }
        VRow {
            visible: (xbDialog.st.turnUrl || "").length > 0
            label: qsTr("TURN 用户名")
            DialogTextField {
                objectName: "xbox-turn-user"
                implicitWidth: 150
                text: xbDialog.st.turnUser || ""
                onEdited: text => xbDialog.xb.set("turnUser", text)
            }
        }
        VRow {
            visible: (xbDialog.st.turnUrl || "").length > 0
            label: qsTr("TURN 密码")
            DialogTextField {
                objectName: "xbox-turn-pass"
                implicitWidth: 150
                text: xbDialog.st.turnPass || ""
                onEdited: text => xbDialog.xb.set("turnPass", text)
            }
        }
    }
    DialogNote { objectName: "xbox-status"; visible: (xbDialog.st.status || "").length > 0; text: (xbDialog.st.busy ? qsTr("处理中 · ") : "") + (xbDialog.st.status || "") }
}
