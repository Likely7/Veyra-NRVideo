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
    // A Lucide name from IconData.js; drawn before the text with the design's 7px gap.
    property string iconName: ""
    // A chevron (or any icon) after the text: the design's dropdown buttons.
    property string trailingIcon: ""
    // Hover tooltip, for icon-only buttons.
    property string tip: ""
    // Long labels (a file name in the source button) elide in the middle.
    property int maxTextWidth: 0
    signal clicked()

    implicitWidth: icon ? Theme.ctlHeight : (content.implicitWidth + 24)
    implicitHeight: Theme.ctlHeight
    radius: Theme.rCtl
    color: primary ? Theme.accent
         : ghost ? (hover.hovered ? Qt.rgba(1, 1, 1, 0.07) : "transparent")
         : (hover.hovered ? Theme.card3 : Theme.card2)
    border.width: (primary || ghost) ? 0 : 1
    border.color: hover.hovered ? Qt.rgba(1, 1, 1, 0.2) : Theme.stroke2
    Behavior on color { ColorAnimation { duration: Theme.d(Theme.durFast) } }
    // .btn:active { transform: scale(.95) }
    // A disabled button reads as such (undo with nothing to undo).
    opacity: enabled ? 1.0 : 0.4
    scale: tap.pressed ? 0.95 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }

    Row {
        id: content
        anchors.centerIn: parent
        spacing: 7
        VIcon {
            visible: btn.iconName.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: btn.iconName
            color: label.color
        }
        Text {
            id: label
            visible: text.length > 0
            width: btn.maxTextWidth > 0 ? Math.min(implicitWidth, btn.maxTextWidth) : implicitWidth
            elide: Text.ElideMiddle
            anchors.verticalCenter: parent.verticalCenter
            text: btn.glyph.length > 0 ? btn.glyph + (btn.text.length > 0 ? "  " + btn.text : "") : btn.text
            color: btn.primary ? Theme.accentInk : (btn.ghost ? Theme.t2 : Theme.t1)
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            font.weight: btn.primary ? Font.DemiBold : Font.Medium
            font.variableAxes: btn.primary ? Theme.axesDemiBold : ({})
        }
        VIcon {
            visible: btn.trailingIcon.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: btn.trailingIcon
            size: 12
            color: Theme.t3
        }
    }
    ToolTip.visible: tip.length > 0 && hover.hovered
    ToolTip.delay: 500
    ToolTip.text: tip
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; gesturePolicy: TapHandler.WithinBounds; onTapped: btn.clicked() }
}
