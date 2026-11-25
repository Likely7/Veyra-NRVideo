import QtQuick
import QtQuick.Controls.Basic

ComboBox {
    id: picker
    leftPadding: 12
    rightPadding: 32
    implicitHeight: 34
    implicitWidth: Math.max(118, measure.width + leftPadding + rightPadding)
    font.family: Theme.fontUi
    hoverEnabled: true
    TextMetrics { id: measure; font: picker.font; text: picker.displayText }
    contentItem: Text {
        text: picker.displayText
        font: picker.font
        color: Theme.t1
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
    }
    indicator: VIcon {
        x: picker.width - width - 10
        anchors.verticalCenter: parent.verticalCenter
        name: "down"; size: 14
        color: picker.down ? Theme.accent : Theme.t2
    }
    background: Rectangle {
        radius: Theme.rCtl
        color: picker.down ? Qt.tint(Theme.card3, Theme.accentSoft) : picker.hovered ? Theme.card3 : Theme.card2
        border.width: picker.visualFocus ? 2 : 1
        border.color: picker.visualFocus || picker.down ? Theme.accent : Theme.stroke2
    }
}
