import QtQuick
VButton {
    required property var backend
    signal requestStop()
    objectName: "ps5-end-stream"
    visible: !!backend.ps5 && (backend.ps5.canEndStream === true || backend.ps5.ending === true)
    enabled: !!backend.ps5 && backend.ps5.ending !== true
    icon: true
    ghost: true
    implicitWidth: 32
    implicitHeight: 32
    iconName: "unlink"
    tip: backend.ps5 && backend.ps5.ending === true ? qsTr("正在结束…") : qsTr("结束串流")
    Accessible.role: Accessible.Button
    Accessible.name: tip
    onClicked: requestStop()
}
