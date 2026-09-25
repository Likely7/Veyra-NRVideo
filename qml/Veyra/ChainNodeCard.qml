// One stage in the effect chain, used by both list and node mode.
//
// The two pinned stages are visually locked: frame generation is always last and
// RTX Video HDR always sits immediately in front of it, because a stage after
// Video HDR would need HDR-aware handling that only frame generation has. The
// user asked for both pins, so the card shows a lock rather than a drag handle.
import QtQuick
import QtQuick.Layouts

Rectangle {
    id: card
    required property var node
    required property bool selected
    signal toggleRequested(bool enabled)
    signal removeRequested()
    signal selectRequested()

    readonly property bool locked: node.mustBeLast === true

    implicitHeight: 62
    radius: Theme.radiusCard
    color: selected ? Theme.card3 : Theme.card2
    border.width: 1
    border.color: selected ? Theme.accent : Theme.stroke
    Behavior on color { ColorAnimation { duration: Theme.durationFast } }
    Behavior on border.color { ColorAnimation { duration: Theme.durationFast } }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 10

        // Enable switch. A disabled stage stays in the chain: disabling is not
        // the same as removing, and the user asked to keep the chain editable.
        Rectangle {
            width: 34; height: 20; radius: 10
            Layout.alignment: Qt.AlignVCenter
            color: card.node.enabled ? Theme.accent : Theme.card3
            Behavior on color { ColorAnimation { duration: Theme.durationFast } }
            Rectangle {
                width: 14; height: 14; radius: 7
                color: card.node.enabled ? Theme.accentInk : Theme.text3
                anchors.verticalCenter: parent.verticalCenter
                x: card.node.enabled ? parent.width - width - 3 : 3
                Behavior on x { NumberAnimation { duration: Theme.durationNormal; easing.bezierCurve: Theme.spring } }
            }
            HoverHandler { cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: card.toggleRequested(!card.node.enabled) }
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1
            Text {
                text: card.node.label
                color: Theme.text1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fontSizeBody
            }
            Text {
                text: card.locked ? "固定为最后一步" : (card.node.experimental ? "实验" : "")
                visible: text.length > 0
                color: card.locked ? Theme.text3 : Theme.experimental
                font.family: Theme.fontUi
                font.pixelSize: 10
            }
        }

        Text {
            visible: card.locked
            text: "🔒"
            font.pixelSize: 13
            opacity: 0.7
        }

        // Removing frame generation is refused by the engine too; greying it
        // here just avoids offering an action that cannot succeed.
        Text {
            text: "✕"
            visible: !card.locked
            color: removeHover.hovered ? Theme.err : Theme.text3
            font.pixelSize: 14
            HoverHandler { id: removeHover; cursorShape: Qt.PointingHandCursor }
            TapHandler { onTapped: card.removeRequested() }
        }
    }
    HoverHandler { id: cardHover }
    TapHandler { onTapped: card.selectRequested() }
}
