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
    property real fontSize: 0
    signal clicked()

    readonly property real extraWidth: (iconName.length ? 16 + Theme.s8 : 0)
        + (glyph.length ? glyphLabel.implicitWidth + Theme.s8 : 0)
        + (trailingIcon.length ? 12 + Theme.s8 : 0)
    readonly property real textCapWidth: maxTextWidth > 0 ? Math.min(label.implicitWidth, maxTextWidth) : label.implicitWidth
    implicitWidth: icon ? Theme.ctlHeight : (textCapWidth + extraWidth + Theme.s24)
    implicitHeight: Theme.ctlHeight
    Layout.minimumWidth: maxTextWidth > 0 ? Math.min(implicitWidth, Theme.ctlHeight) : implicitWidth
    clip: true
    radius: Theme.rCtl
    color: primary ? (tap.pressed ? Theme.accentPressed : hover.hovered ? Theme.accentHover : Theme.accent)
         : ghost ? (tap.pressed ? Theme.pressFill : hover.hovered ? Theme.hoverFill : "transparent")
         : (tap.pressed ? Theme.card3 : hover.hovered ? Theme.card3 : Theme.card2)
    border.width: activeFocus ? 2 : ((primary || ghost) ? 0 : 1)
    border.color: activeFocus ? Theme.focusRing : (hover.hovered ? Theme.strokeStrong : Theme.stroke)
    Behavior on color { ColorAnimation { duration: Theme.d(Theme.animFast) } }
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    scale: tap.pressed ? 0.96 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.d(Theme.animFast) } }

    activeFocusOnTab: enabled
    Keys.onReturnPressed: event => { if (enabled) { event.accepted = true; clicked() } }
    Keys.onEnterPressed: event => { if (enabled) { event.accepted = true; clicked() } }
    Keys.onSpacePressed: event => { if (enabled) { event.accepted = true; clicked() } }
    Accessible.role: Accessible.Button
    Accessible.name: text.length > 0 ? text : (tip.length > 0 ? tip : (glyph.length > 0 ? glyph : iconName))

    Row {
        id: content
        anchors.centerIn: parent
        spacing: Theme.s8
        VIcon {
            visible: btn.iconName.length > 0
            anchors.verticalCenter: parent.verticalCenter
            name: btn.iconName
            color: label.color
        }
        Text {
            id: glyphLabel
            visible: btn.glyph.length > 0
            anchors.verticalCenter: parent.verticalCenter
            text: btn.glyph
            color: label.color
            font.family: Theme.fontUi
            font.pixelSize: btn.fontSize > 0 ? btn.fontSize : Theme.fsBody
            font.weight: btn.primary ? Font.DemiBold : Font.Medium
            font.variableAxes: btn.primary ? Theme.axesDemiBold : Theme.axesMedium
        }
        Text {
            id: label
            visible: text.length > 0
            readonly property bool constrained: btn.maxTextWidth > 0 || btn.width < btn.implicitWidth
            width: constrained ? Math.max(0, Math.min(implicitWidth, btn.maxTextWidth > 0 ? btn.maxTextWidth : implicitWidth, btn.width - Theme.s24 - btn.extraWidth)) : implicitWidth
            elide: btn.maxTextWidth > 0 ? Text.ElideMiddle : (btn.width < btn.implicitWidth ? Text.ElideRight : Text.ElideNone)
            anchors.verticalCenter: parent.verticalCenter
            text: btn.text
            color: btn.primary ? Theme.accentText : (btn.ghost ? Theme.t2 : Theme.t1)
            font.family: Theme.fontUi
            font.pixelSize: btn.fontSize > 0 ? btn.fontSize : Theme.fsBody
            font.weight: btn.primary ? Font.DemiBold : Font.Medium
            font.variableAxes: btn.primary ? Theme.axesDemiBold : Theme.axesMedium
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
    HoverHandler { id: hover; enabled: btn.enabled; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; enabled: btn.enabled; gesturePolicy: TapHandler.WithinBounds; onTapped: btn.clicked() }
}
