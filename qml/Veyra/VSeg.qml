import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: seg
    property var options: []      // [{ id, label }]
    property string current: ""
    signal picked(string id)

    implicitWidth: segRow.implicitWidth + Theme.s8
    implicitHeight: Theme.ctlHeight
    radius: Theme.rCtl
    opacity: enabled ? 1.0 : Theme.disabledOpacity
    activeFocusOnTab: enabled
    color: Theme.card2
    border.width: activeFocus ? 2 : 1
    border.color: activeFocus ? Theme.focusRing : Theme.stroke

    function moveSelection(delta) {
        if (options.length === 0) return
        let idx = currentIndex
        for (let step = 0; step < options.length; ++step) {
            idx = (idx + delta + options.length) % options.length
            if (options[idx].disabled !== true) {
                picked(options[idx].id)
                return
            }
        }
    }
    Keys.onLeftPressed: event => { if (enabled) { moveSelection(-1); event.accepted = true } }
    Keys.onRightPressed: event => { if (enabled) { moveSelection(1); event.accepted = true } }

    Accessible.role: Accessible.PageTabList
    Accessible.focusable: seg.enabled
    Accessible.focused: seg.activeFocus

    readonly property int currentIndex: {
        for (var i = 0; i < options.length; ++i)
            if (options[i].id === current) return i
        return -1
    }
    // itemAt() is not a notifying call; reading count re-runs this once the Repeater
    // has built its buttons (otherwise it stays null from the first evaluation).
    readonly property Item currentItem: (segRepeater.count > 0 && currentIndex >= 0) ? segRepeater.itemAt(currentIndex) : null

    // Read by the motion probe (G0.5): where the indicator is, and where it is going.
    readonly property real indicatorX: indicator.x
    readonly property real targetX: currentItem ? segRow.x + currentItem.x : Theme.s4

    // .seg-ind: one indicator slides under the chosen button (transform and width,
    // .5s --spring), instead of each button lighting its own background.
    Rectangle {
        id: indicator
        visible: seg.currentItem !== null
        x: seg.currentItem ? segRow.x + seg.currentItem.x : Theme.s4
        y: Theme.s4
        width: seg.currentItem ? seg.currentItem.width : 0
        height: 24
        radius: Theme.rCtl
        color: Theme.card3
        border.width: 1
        border.color: Theme.strokeStrong
        Behavior on x { NumberAnimation { duration: Theme.d(500); easing.bezierCurve: Theme.spring } }
        Behavior on width { NumberAnimation { duration: Theme.d(500); easing.bezierCurve: Theme.spring } }
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: Theme.s4; anchors.rightMargin: Theme.s4
            height: 1
            color: Theme.hoverFill
        }
    }

    RowLayout {
        id: segRow
        anchors.centerIn: parent
        spacing: 0
        Repeater {
            id: segRepeater
            model: seg.options
            delegate: Item {
                id: segBtn
                required property var modelData
                enabled: modelData.disabled !== true
                opacity: enabled ? 1 : Theme.disabledOpacity
                implicitWidth: segContent.implicitWidth + Theme.s16 + Theme.s4
                implicitHeight: 24
                Rectangle {
                    anchors.fill: parent
                    radius: Theme.rCtl
                    color: btnTap.pressed ? Theme.pressFill
                         : (btnHover.hovered && seg.current !== segBtn.modelData.id) ? Theme.hoverFill
                         : "transparent"
                }
                // { id, label, icon? }: the design's 列表 / 节点 toggle carries icons.
                Row {
                    id: segContent
                    anchors.centerIn: parent
                    spacing: Theme.s8
                    VIcon {
                        visible: (modelData.icon || "").length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        name: modelData.icon || ""
                        size: 13
                        color: segLabel.color
                    }
                    Text {
                        id: segLabel
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: seg.current === modelData.id ? Theme.t1 : Theme.t2
                        Behavior on color { ColorAnimation { duration: Theme.d(Theme.animNormal) } }
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        font.weight: Font.Medium
                    }
                }
                HoverHandler { id: btnHover; enabled: segBtn.enabled; cursorShape: Qt.PointingHandCursor }
                TapHandler {
                    id: btnTap
                    enabled: segBtn.enabled
                    gesturePolicy: TapHandler.WithinBounds
                    onTapped: {
                        seg.forceActiveFocus()
                        seg.picked(segBtn.modelData.id)
                    }
                }
                Accessible.role: Accessible.PageTab
                Accessible.name: modelData.label
                Accessible.selected: seg.current === modelData.id
                Accessible.selectable: segBtn.enabled
                Accessible.onPressAction: if (segBtn.enabled) seg.picked(modelData.id)
            }
        }
    }
}
