import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: btn
    property string text: ""
    property bool primary: false
    property bool ghost: false
    property bool icon: false
    property string glyph: ""
    signal clicked()

    implicitWidth: icon ? Theme.ctlHeight : (label.implicitWidth + 24)
    implicitHeight: Theme.ctlHeight
    radius: Theme.rCtl
    color: primary ? Theme.accent
         : ghost ? (hover.hovered ? Qt.rgba(1, 1, 1, 0.07) : "transparent")
         : (hover.hovered ? Theme.card3 : Theme.card2)
    border.width: (primary || ghost) ? 0 : 1
    border.color: hover.hovered ? Qt.rgba(1, 1, 1, 0.2) : Theme.stroke2
    Behavior on color { ColorAnimation { duration: Theme.d(Theme.durFast) } }
    // .btn:active { transform: scale(.95) }
    scale: tap.pressed ? 0.95 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }

    Text {
        id: label
        anchors.centerIn: parent
        text: btn.glyph.length > 0 ? btn.glyph + (btn.text.length > 0 ? "  " + btn.text : "") : btn.text
        color: btn.primary ? Theme.accentInk : (btn.ghost ? Theme.t2 : Theme.t1)
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
        font.weight: btn.primary ? Font.DemiBold : Font.Medium
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; onTapped: btn.clicked() }
}
