import QtQuick

VButton {
    id: button
    activeFocusOnTab: enabled && visible
    Accessible.role: Accessible.Button
    Accessible.name: text || tip
    Accessible.onPressAction: if (enabled) clicked()
    Keys.onPressed: event => {
        if (event.key !== Qt.Key_Return && event.key !== Qt.Key_Enter && event.key !== Qt.Key_Space) return
        if (!event.isAutoRepeat && button.enabled) button.clicked()
        event.accepted = true
    }
    Rectangle {
        anchors.fill: parent
        radius: button.radius
        color: "transparent"
        border.color: Theme.accent
        border.width: 2
        visible: button.activeFocus
    }
}
