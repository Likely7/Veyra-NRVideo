import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: tag
    property string text: ""
    property string kind: ""   // "" | "exp" | "acc" | "warn" | "ok"
    property bool elideText: false
    implicitWidth: tagText.implicitWidth + 14
    implicitHeight: Theme.tagHeight
    radius: 6
    color: kind === "exp" ? Theme.expSoft
         : kind === "acc" ? Theme.accentSoft
         : kind === "warn" ? Qt.rgba(0.961, 0.784, 0.294, 0.13)
         : kind === "ok" ? Qt.rgba(0.239, 0.863, 0.518, 0.12)
         : Qt.rgba(1, 1, 1, 0.07)
    Text {
        id: tagText
        anchors.centerIn: parent
        text: tag.text
        width: tag.elideText ? Math.max(0, Math.min(implicitWidth, tag.width - 14)) : implicitWidth
        elide: tag.elideText ? Text.ElideRight : Text.ElideNone
        color: tag.kind === "exp" ? Theme.exp
             : tag.kind === "acc" ? Theme.accent
             : tag.kind === "warn" ? Theme.warn
             : tag.kind === "ok" ? Theme.ok
             : Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: 11
        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
    }
}
