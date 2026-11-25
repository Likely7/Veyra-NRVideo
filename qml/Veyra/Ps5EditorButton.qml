import QtQuick
import QtQuick.Controls.Basic

Button {
    id: button
    property bool primary: false
    property bool quiet: false
    property bool leading: false
    horizontalPadding: 12
    verticalPadding: 6
    implicitHeight: Math.max(34, implicitContentHeight + 2 * verticalPadding)
    font.family: Theme.fontUi
    font.pixelSize: Theme.fsBody
    hoverEnabled: true
    contentItem: Text {
        text: button.text
        color: button.primary ? Theme.accentInk : (button.checked ? Theme.accent : Theme.t1)
        font: button.font
        elide: Text.ElideRight
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: button.leading ? Text.AlignLeft : Text.AlignHCenter
    }
    background: Rectangle {
        radius: 7
        color: button.primary ? (button.down ? Qt.darker(Theme.accent, 1.16)
                                             : button.hovered ? Qt.lighter(Theme.accent, 1.09) : Theme.accent)
             : button.down ? Qt.tint(Theme.card3, Theme.accentSoft)
             : button.checked ? (button.hovered ? Qt.tint(Theme.card3, Theme.accentSoft) : Theme.accentSoft)
             : button.hovered ? Theme.card3 : button.quiet ? "transparent" : Theme.card2
        border.color: button.down || button.checked ? Theme.accent
                    : button.quiet || button.primary ? "transparent" : Theme.stroke2
        Rectangle {
            anchors.fill: parent
            anchors.margins: button.primary ? 3 : 0
            radius: button.primary ? 4 : parent.radius
            visible: button.visualFocus
            color: "transparent"
            border.width: 2
            border.color: button.primary ? Theme.accentInk : Theme.accent
        }
    }
    opacity: enabled ? 1 : 0.45
}
