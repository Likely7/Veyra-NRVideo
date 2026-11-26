import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: sw
    property bool checked: false
    signal toggled(bool checked)
    width: Theme.switchWidth
    height: Theme.switchHeight
    radius: 999
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    activeFocusOnTab: enabled
    color: checked
        ? (tap.pressed ? Theme.accentPressed : hover.hovered ? Theme.accentHover : Theme.accent)
        : (tap.pressed ? Theme.pressFill : hover.hovered ? Theme.hoverFill : Theme.card3)
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? Theme.focusRing : (checked ? "transparent" : Theme.strokeStrong)
    Behavior on color { ColorAnimation { duration: Theme.d(Theme.animNormal) } }
    // The dusk palette's sunset accent: the on track is the gradient, faded in
    // and out with the colour.
    VAccentFill {
        anchors.fill: parent
        radius: parent.radius
        visible: Theme.accentGradient && opacity > 0
        opacity: sw.checked ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.d(Theme.animNormal) } }
    }

    Rectangle {
        id: knob
        width: tap.pressed ? 18 : Theme.switchKnob
        height: Theme.switchKnob
        radius: 999
        color: sw.checked ? Theme.accentText : Theme.t1
        anchors.verticalCenter: parent.verticalCenter
        x: sw.checked ? (parent.width - width - 3) : 3
        Behavior on x { NumberAnimation { duration: Theme.d(500); easing.bezierCurve: Theme.spring } }
        Behavior on width { NumberAnimation { duration: Theme.d(Theme.animNormal) } }
    }
    HoverHandler { id: hover; enabled: sw.enabled; cursorShape: Qt.PointingHandCursor }
    // Controlled: owners bind checked to their model and write the new value back in
    // onToggled. Assigning checked here destroyed that binding, so a value changed
    // elsewhere (a preset, another page) no longer moved the switch (GitHub #29).
    TapHandler {
        id: tap
        enabled: sw.enabled
        gesturePolicy: TapHandler.WithinBounds
        onTapped: {
            sw.forceActiveFocus()
            sw.toggled(!sw.checked)
        }
    }
    Keys.onSpacePressed: event => { if (enabled) { sw.toggled(!sw.checked); event.accepted = true } }
    Keys.onReturnPressed: event => { if (enabled) { sw.toggled(!sw.checked); event.accepted = true } }
    Keys.onEnterPressed: event => { if (enabled) { sw.toggled(!sw.checked); event.accepted = true } }

    Accessible.role: Accessible.CheckBox
    Accessible.checkable: true
    Accessible.checked: sw.checked
    Accessible.focusable: sw.enabled
    Accessible.focused: sw.activeFocus
    Accessible.pressed: tap.pressed
    Accessible.onToggleAction: if (sw.enabled) sw.toggled(!sw.checked)
    Accessible.onPressAction: if (sw.enabled) sw.toggled(!sw.checked)
}
