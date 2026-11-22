import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: stopDialog
    required property var backend
    objectName: "ps5-stop-dialog"
    key: "ps5-stop"
    title: qsTr("结束 PlayStation 串流")
    sub: qsTr("请选择断开后的主机状态。")
    glyph: "gamepad"
    dialogWidth: 460
    actions: []
    function end(standby) {
        if (backend.ps5EndStream(standby)) dialogHost.close()
    }
    DialogNote {
        text: backend.ps5.ending ? qsTr("正在结束串流…") : backend.ps5.endMessage || ""
        visible: text.length > 0
    }
    VButton {
        objectName: "ps5-stop-keep-on"
        Layout.fillWidth: true; Layout.topMargin: 8
        text: qsTr("仅断开，主机保持开机")
        enabled: backend.ps5.canEndStream === true && backend.ps5.ending !== true
        onClicked: stopDialog.end(false)
    }
    VButton {
        objectName: "ps5-stop-standby"
        Layout.fillWidth: true; Layout.topMargin: 8
        text: qsTr("让主机待机并断开")
        enabled: backend.ps5.canStandby === true && backend.ps5.ending !== true
        onClicked: stopDialog.end(true)
    }
    DialogNote {
        visible: backend.ps5.canStandby !== true && backend.ps5.ending !== true
        text: qsTr("串流连接建立后才能向主机发送待机请求。")
    }
    RowLayout {
        Layout.fillWidth: true; Layout.topMargin: 12
        Item { Layout.fillWidth: true }
        VButton { objectName: "ps5-stop-cancel"; text: qsTr("继续串流"); ghost: true; onClicked: dialogHost.close() }
    }
}
