import QtQuick
import QtQuick.Controls

Rectangle {
    id: banner
    // The native video host subtracts visible videoCover items from its region.
    objectName: "videoCover"
    property real coverRadius: Theme.rCard
    property string version: ""
    property bool shown: false
    signal openRequested()
    width: 500
    height: Math.max(54, message.implicitHeight + Theme.s24)
    radius: Theme.rCard
    color: bannerMouse.pressed ? Theme.card3 : bannerMouse.containsMouse ? Theme.card2 : Theme.card
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? Theme.focusRing : Theme.ok
    visible: shown
    activeFocusOnTab: shown
    Accessible.role: Accessible.Alert
    Accessible.name: message.text
    Accessible.onPressAction: { banner.openRequested(); banner.dismiss() }
    Keys.onReturnPressed: { banner.openRequested(); banner.dismiss() }
    Keys.onEnterPressed: { banner.openRequested(); banner.dismiss() }
    Keys.onSpacePressed: { banner.openRequested(); banner.dismiss() }
    Keys.onEscapePressed: banner.dismiss()

    function show(value) { version = value; shown = true; dismissTimer.restart() }
    function dismiss() { shown = false; dismissTimer.stop() }
    // One-shot only: idle UI and playback render rates are unchanged.
    Timer { id: dismissTimer; interval: 8000; onTriggered: banner.shown = false }

    Rectangle {
        anchors.fill: parent
        radius: parent.radius
        color: Theme.okGlow
        opacity: bannerMouse.containsMouse ? 0.9 : 0.65
    }

    MouseArea {
        id: bannerMouse
        anchors.fill: parent
        hoverEnabled: true
        cursorShape: Qt.PointingHandCursor
        onClicked: { banner.openRequested(); banner.dismiss() }
    }
    Text {
        id: message
        anchors.left: parent.left
        anchors.leftMargin: Theme.s16
        anchors.right: closeButton.left
        anchors.rightMargin: Theme.s8
        anchors.verticalCenter: parent.verticalCenter
        text: qsTr("发现新版本 %1，点击查看更新").arg(banner.version)
        color: Theme.t1
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
        wrapMode: Text.WordWrap
        textFormat: Text.PlainText
    }
    VButton {
        id: closeButton
        objectName: "update-dismiss"
        anchors.right: parent.right
        anchors.rightMargin: Theme.s8
        anchors.verticalCenter: parent.verticalCenter
        icon: true
        iconName: "x"
        ghost: true
        tip: qsTr("关闭")
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("关闭更新提示")
        onClicked: banner.dismiss()
    }
}
