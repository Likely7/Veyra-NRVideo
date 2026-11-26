import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: tag
    property string text: ""
    property string kind: ""   // "" | "exp" | "acc" | "warn" | "ok"
    property bool elideText: false
    implicitWidth: tagText.implicitWidth + Theme.s16
    implicitHeight: Theme.tagHeight
    radius: Theme.rCtl
    color: kind === "exp" ? Theme.expSoft
         : kind === "acc" ? Theme.accentSoft
         : kind === "warn" ? Theme.warnGlow
         : kind === "ok" ? Theme.okGlow
         : Theme.hoverFill
    Accessible.role: Accessible.StaticText
    Accessible.name: tag.text
    Text {
        id: tagText
        anchors.centerIn: parent
        text: tag.text
        width: tag.elideText ? Math.max(0, Math.min(implicitWidth, tag.width - Theme.s16)) : implicitWidth
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
