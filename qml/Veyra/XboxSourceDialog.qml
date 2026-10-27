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
                VButton { visible: xbDialog.st.signedIn; text: qsTr("退出登录"); ghost: true; enabled: !xbDialog.st.busy; onClicked: xbDialog.xb.signOut() }
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
    }
    DialogNote { objectName: "xbox-status"; visible: (xbDialog.st.status || "").length > 0; text: (xbDialog.st.busy ? qsTr("处理中 · ") : "") + (xbDialog.st.status || "") }
}
