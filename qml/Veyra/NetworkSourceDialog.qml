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
    sub: qsTr("直接打开 HTTP、HTTPS 或 RTSP 视频流，例如 IPTV 或摄像机地址")
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
        placeholder: "https://… / rtsp://…"
    }
    Text {
        Layout.fillWidth: true
        text: qsTr("需要直接媒体地址，普通网页或需要网页登录的页面不能直接播放。直播流通常无法拖动进度。")
        wrapMode: Text.Wrap
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 12
    }
}
