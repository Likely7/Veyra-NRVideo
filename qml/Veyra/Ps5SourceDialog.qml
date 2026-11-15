// ps5 dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: ps5Dialog
    objectName: "ps5-dialog"
    key: "ps5"
    glyph: "gamepad"
    title: qsTr("PS5 串流")
    sub: qsTr("局域网 Remote Play · 凭据加密保存在本机")
    dialogWidth: 680
    actions: [
        { label: veyra.ps5.active ? qsTr("断开") : qsTr("取消") },
        { label: veyra.ps5.active ? qsTr("应用设置并重连") : qsTr("连接"), primary: true }
    ]
    onActionTriggered: label => {
        if (label === qsTr("断开")) { veyra.ps5Cancel(); return }
        if (label === qsTr("取消")) { if (veyra.ps5.busy) veyra.ps5Cancel(); else dialogHost.close(); return }
        if (veyra.ps5Connect()) { dialogHost.close(); dialogHost.startPs5() }
    }
    Connections {
        target: dialogHost
        function onDialogChanged() { if (dialogHost.dialog === "ps5") veyra.ps5Load() }
    }
    readonly property var qualities: [{ id: "0", label: "720p · 30 fps" }, { id: "1", label: "720p · 60 fps" }, { id: "2", label: "1080p · 30 fps" }, { id: "3", label: "1080p · 60 fps" }]
    readonly property var bitrates: [5, 10, 15, 20, 30, 50, 80, 100].map((m, i) => ({ id: String(i), label: m + " Mbps" }))

    DialogSection { text: qsTr("主机") }
    DialogGroup {
        VRow {
            visible: veyra.ps5Profiles.length > 0
            label: qsTr("已保存主机")
            VSelect {
                objectName: "ps5-profile"
                implicitWidth: 220
                value: { const p = veyra.ps5Profiles.find(x => x.id === veyra.ps5.profile); return p ? p.label : qsTr("请选择") }
                options: veyra.ps5Profiles
                onPicked: id => veyra.ps5SelectProfile(id)
            }
        }
        VRow {
            label: qsTr("主机地址")
            hint: qsTr("PS5 设置 → 网络 → 连接状态；找不到时可手填 IP")
            RowLayout {
                spacing: 6
                DialogTextField { objectName: "ps5-host"; implicitWidth: 170; placeholder: "192.168.1.x"; text: veyra.ps5.host || ""; onEdited: text => veyra.ps5Set("host", text) }
                VSpinner { visible: veyra.ps5.busy === true; Layout.alignment: Qt.AlignVCenter }
                VButton { text: qsTr("查找"); ghost: true; enabled: !veyra.ps5.busy; onClicked: veyra.ps5Scan() }
                VButton { text: qsTr("唤醒"); ghost: true; enabled: !veyra.ps5.busy && veyra.ps5.profile.length > 0; onClicked: veyra.ps5Wake() }
            }
        }
    }
    DialogSection { text: qsTr("配对（首次）") }
    DialogGroup {
        VRow {
            label: "PSN Account ID"
            hint: qsTr("点击获取 Account ID，在网页查询后复制到这里。支持数字 ID 或 8 字节 Base64，不是 PSN 昵称。")
            hintBelow: true
            RowLayout {
                spacing: 6
                DialogTextField { objectName: "ps5-account-id"; implicitWidth: 240; text: veyra.ps5.account || ""; onEdited: text => veyra.ps5Set("account", text) }
                VButton { objectName: "ps5-account-lookup"; text: qsTr("获取 Account ID"); ghost: true; onClicked: veyra.ps5OpenAccountChecker() }
            }
        }
        VRow {
            label: qsTr("8 位配对码")
            hint: qsTr("PS5 设置 → 系统 → 远程游玩 → 关联设备")
            RowLayout {
                spacing: 6
                DialogTextField { id: pairPin; implicitWidth: 120; placeholder: "••••••••" }
                VButton { text: qsTr("配对并保存"); enabled: !veyra.ps5.busy; onClicked: { veyra.ps5Pair(pairPin.text); pairPin.text = "" } }
            }
        }
        VRow {
            label: qsTr("删除配对")
            hint: qsTr("只删除本机保存的这台主机；以后需要重新配对")
            VButton { text: qsTr("删除"); ghost: true; enabled: !veyra.ps5.busy && veyra.ps5.profile.length > 0; onClicked: veyra.ps5Forget() }
        }
    }
    DialogSection { text: qsTr("串流（重连生效）") }
    DialogGroup {
        VRow {
            label: qsTr("分辨率与帧率")
            VSelect { implicitWidth: 170; value: ps5Dialog.qualities[veyra.ps5.quality || 0].label; options: ps5Dialog.qualities; onPicked: id => veyra.ps5Set("quality", Number(id)) }
        }
        VRow {
            label: qsTr("编码")
            hint: qsTr("HDR 需要 PS5 实际输出 HDR 且 Windows HDR 开启")
            VSeg {
                options: [{ id: "0", label: "H.264" }, { id: "1", label: "H.265" }, { id: "2", label: "H.265 HDR" }]
                current: String(veyra.ps5.codec || 0)
                onPicked: id => veyra.ps5Set("codec", Number(id))
            }
        }
        VRow {
            label: qsTr("请求码率")
            hint: qsTr("发给 PS5 的带宽请求，主机不保证达到")
            VSelect { implicitWidth: 130; value: ps5Dialog.bitrates[veyra.ps5.bitrate || 0].label; options: ps5Dialog.bitrates; onPicked: id => veyra.ps5Set("bitrate", Number(id)) }
        }
        VRow {
            label: qsTr("解码")
            VSeg {
                options: [{ id: "0", label: qsTr("自动") }, { id: "1", label: qsTr("CPU 软件") }, { id: "2", label: qsTr("D3D12VA 硬件") }]
                current: String(veyra.ps5.decode || 0)
                onPicked: id => veyra.ps5Set("decode", Number(id))
            }
        }
        VRow {
            label: qsTr("采样")
            hint: qsTr("精细：按位置还原色度，放大用双三次；不是 AI 超分")
            VSeg {
                options: [{ id: "0", label: qsTr("兼容") }, { id: "1", label: qsTr("精细") }]
                current: String(veyra.ps5.sampling === undefined ? 1 : veyra.ps5.sampling)
                onPicked: id => veyra.ps5Set("sampling", Number(id))
            }
        }
        VRow {
            label: qsTr("仅观看")
            hint: qsTr("不向 PS5 转发电脑手柄输入")
            VSwitch { checked: veyra.ps5.viewOnly === true; onToggled: checked => veyra.ps5Set("viewOnly", checked) }
        }
        // The mirror of 仅观看: only the pad, the picture from a capture card.
        VRow {
            visible: veyra.ps5Control.available === true
            label: qsTr("画面用采集卡")
            hint: veyra.ps5Control.enabled === true
                  ? qsTr("已在「采集卡」里打开 PS5 手柄控制：连接采集卡时只用远程游玩传手柄")
                  : qsTr("PS5 的 HDMI 接了采集卡时，可以只用远程游玩传手柄、画面用采集卡原画（可 4K/HDR），也方便再串流给掌机")
            VButton { objectName: "ps5-open-capture"; text: qsTr("采集卡设置"); ghost: true; onClicked: dialogHost.open("capture") }
        }
        VRow {
            label: qsTr("陀螺仪")
            hint: veyra.ps5.controller ? (veyra.ps5.gyro ? qsTr("手柄支持陀螺仪") : qsTr("手柄没有陀螺仪")) : qsTr("连接串流及电脑手柄后可校准")
            VButton { text: veyra.ps5.calibrating ? qsTr("校准中…") : qsTr("校准"); ghost: true; onClicked: veyra.ps5Calibrate() }
        }
    }
    DialogSection { text: qsTr("登录 PIN") }
    DialogGroup {
        VRow {
            label: qsTr("PS5 提示时填写")
            hint: qsTr("不是 8 位配对码")
            RowLayout {
                spacing: 6
                DialogTextField { id: loginPin; implicitWidth: 120 }
                VButton { text: qsTr("提交"); ghost: true; onClicked: { veyra.ps5SendLoginPin(loginPin.text); loginPin.text = "" } }
            }
        }
    }
    DialogNote { visible: (veyra.ps5.status || "").length > 0; text: (veyra.ps5.busy ? qsTr("处理中 · ") : "") + (veyra.ps5.status || "") }
}
