import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: pill
    property string key: ""
    property string value: ""
    property real maximumValueWidth: Number.POSITIVE_INFINITY
    // Child items (e.g. a status dot) sit in the row before the text, not at
    // the pill's top-left corner.
    default property alias leading: lead.data
    signal clicked()
    implicitWidth: pillRow.implicitWidth + Theme.s20
    implicitHeight: Theme.ctlHeight
    radius: Theme.rCtl
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    activeFocusOnTab: enabled
    color: tap.pressed ? Theme.pressFill : hover.hovered ? Theme.hoverFill : Theme.card2
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? Theme.focusRing : Theme.strokeStrong
    scale: tap.pressed ? 0.95 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.d(Theme.animFast); easing.bezierCurve: Theme.spring } }
    RowLayout {
        id: pillRow
        anchors.centerIn: parent
        spacing: Theme.s8
        Row { id: lead; visible: children.length > 0; Layout.alignment: Qt.AlignVCenter }
        Text { visible: pill.key.length > 0; text: pill.key; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody }
        Text {
            id: valueLabel
            Layout.maximumWidth: pill.maximumValueWidth
            text: pill.value
            elide: Text.ElideRight
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            font.weight: Font.Medium
        }
    }
    HoverHandler { id: hover; enabled: pill.enabled; cursorShape: Qt.PointingHandCursor }
    ToolTip.visible: hover.hovered && valueLabel.truncated
    ToolTip.text: pill.value
    TapHandler {
        id: tap
        enabled: pill.enabled
        gesturePolicy: TapHandler.WithinBounds
        onTapped: {
            pill.forceActiveFocus()
            pill.clicked()
        }
    }
    Keys.onSpacePressed: event => { if (enabled) { pill.clicked(); event.accepted = true } }
    Keys.onReturnPressed: event => { if (enabled) { pill.clicked(); event.accepted = true } }
    Keys.onEnterPressed: event => { if (enabled) { pill.clicked(); event.accepted = true } }

    Accessible.role: Accessible.Button
    Accessible.name: pill.key.length > 0 ? (pill.key + " " + pill.value) : pill.value
    Accessible.focusable: pill.enabled
    Accessible.focused: pill.activeFocus
    Accessible.pressed: tap.pressed
    Accessible.onPressAction: if (pill.enabled) pill.clicked()
}
