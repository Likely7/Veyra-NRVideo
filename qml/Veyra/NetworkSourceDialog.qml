// network dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: networkDialog
    objectName: "network-dialog"
    key: "network"
    glyph: "cast"
    title: qsTr("播放地址")
    sub: qsTr("直接打开 HTTP、HTTPS、RTSP、RTP 或 UDP 视频流")
    dialogWidth: 640
    actions: [{label:qsTr("取消")},{label:qsTr("打开"),primary:true,icon:"play"}]
    onActionTriggered: label => {
        if(label!==qsTr("打开")){dialogHost.close();return}
        if(veyra.openNetworkAddress(networkAddress.text))dialogHost.close()
    }
    DialogTextField {
        id: networkAddress
        objectName: "network-address"
        Layout.fillWidth: true
        placeholder: "https://… / rtsp://… / rtp://… / udp://…"
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("请使用直接媒体地址。RTP 和 UDP 的 MPEG-TS 地址需要包含端口。")
        wrapMode: Text.Wrap
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 12
    }
    VRow {
        label: qsTr("直播时移")
        hint: qsTr("保留最近的直播内容。最多使用 256 MiB 内存，高码率时可回看时间会缩短。")
        VSeg {
            objectName: "network-timeshift"
            options: [{id:"0",label:qsTr("关闭","off")},{id:"60",label:qsTr("1 分钟")},
                      {id:"120",label:qsTr("2 分钟")},{id:"300",label:qsTr("5 分钟")}]
            current: String(veyra.preferences.networkTimeshift || 0)
            onPicked: id => veyra.setPreference("networkTimeshift", Number(id))
        }
    }
}
