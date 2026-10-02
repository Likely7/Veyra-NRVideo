import QtQuick
import QtQuick.Controls

Rectangle {
    id: control
    implicitWidth: 44
    implicitHeight: 28
    radius: 8
    visible: veyra.hasSource && !veyra.isCapture && veyra.duration > 0
    enabled: veyra.running
    color: hover.hovered ? Qt.rgba(1, 1, 1, 0.10) : "transparent"
    readonly property string rateLabel: Number(veyra.playbackRate || 1).toString() + "×"
    Text {
        anchors.centerIn: parent
        text: control.rateLabel
        color: Theme.t1
        font.family: Theme.fontMono
        font.pixelSize: 12
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: rateMenu.openAt(control, "up") }
    ToolTip.visible: hover.hovered
    ToolTip.text: qsTr("播放速度")
    VMenu {
        id: rateMenu
        objectName: control.objectName + "-menu"
        title: qsTr("播放速度")
        items: [1, 1.5, 2, 3].map(r => ({ label: r + "×", rate: r, checked: Number(veyra.playbackRate || 1) === r }))
        onPicked: (i, o) => veyra.playbackRate = o.rate
    }
}
