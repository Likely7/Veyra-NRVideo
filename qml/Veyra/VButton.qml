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
    // 字号覆盖：默认 0 表示用 Theme.fsBody（所有按钮的常规尺寸）。
    // 窄容器里的短按钮（如 100px 侧栏里的账号区）需要更小的字号才放得下，
    // 但把整个 VButton 的字号改小会影响全应用，所以做成按实例覆盖。
    // Font-size override: 0 means Theme.fsBody, the normal button size. Narrow
    // containers (the 100px rail's account block) need a smaller size for a short
    // label, and changing VButton globally would resize every button, so this is per
    // instance.
    property int fontSize: 0
    signal clicked()

    implicitWidth: icon ? Theme.ctlHeight : (Math.min(label.implicitWidth, maxTextWidth > 0 ? maxTextWidth : label.implicitWidth)
        + (iconName.length ? 25 : 0) + (trailingIcon.length ? 19 : 0) + 24)
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
            width: Math.max(0, Math.min(implicitWidth, btn.maxTextWidth > 0 ? btn.maxTextWidth : implicitWidth,
                btn.width - 24 - (btn.iconName.length ? 25 : 0) - (btn.trailingIcon.length ? 19 : 0)))
            elide: Text.ElideMiddle
            anchors.verticalCenter: parent.verticalCenter
            text: btn.glyph.length > 0 ? btn.glyph + (btn.text.length > 0 ? "  " + btn.text : "") : btn.text
            color: btn.primary ? Theme.accentInk : (btn.ghost ? Theme.t2 : Theme.t1)
            font.family: Theme.fontUi
            font.pixelSize: btn.fontSize > 0 ? btn.fontSize : Theme.fsBody
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
    ToolTip.visible: (tip.length > 0 || label.truncated) && hover.hovered
    ToolTip.delay: 500
    ToolTip.text: tip.length > 0 ? tip : label.text
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; gesturePolicy: TapHandler.WithinBounds; onTapped: btn.clicked() }
}
