import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Effects
Rectangle {
    id: seg
    property var options: []      // [{ id, label }]
    property string current: ""
    signal picked(string id)

    implicitWidth: segRow.implicitWidth + 6
    implicitHeight: 30
    radius: Theme.rCtl
    color: Qt.rgba(1, 1, 1, 0.05)
    border.width: 1
    border.color: Theme.stroke

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
    readonly property real targetX: currentItem ? segRow.x + currentItem.x : 3

    // .seg-ind: one indicator slides under the chosen button (transform and width,
    // .5s --spring), instead of each button lighting its own background.
    Rectangle {
        id: indicator
        visible: seg.currentItem !== null
        x: seg.currentItem ? segRow.x + seg.currentItem.x : 3
        y: 3
        width: seg.currentItem ? seg.currentItem.width : 0
        height: 24
        radius: 7
        color: Theme.card3
        Behavior on x { NumberAnimation { duration: Theme.d(500); easing.bezierCurve: Theme.spring } }
        Behavior on width { NumberAnimation { duration: Theme.d(500); easing.bezierCurve: Theme.spring } }
        // box-shadow 0 1px 0 rgba(255,255,255,.06) inset: the lit top edge.
        Rectangle {
            anchors.left: parent.left; anchors.right: parent.right
            anchors.leftMargin: 4; anchors.rightMargin: 4
            height: 1
            color: Qt.rgba(1, 1, 1, 0.06)
        }
    }

    // box-shadow 0 4px 10px rgba(0,0,0,.35): card3 alone is the same grey as the seg
    // background, so the shadow is what makes the indicator visible (G1.5).
    MultiEffect {
        source: indicator
        anchors.fill: indicator
        visible: indicator.visible
        shadowEnabled: true
        shadowColor: Qt.rgba(0, 0, 0, 0.35)
        shadowVerticalOffset: 4
        shadowHorizontalOffset: 0
        blurMax: 16
        shadowBlur: 0.625
    }

    RowLayout {
        id: segRow
        anchors.centerIn: parent
        spacing: 0
        Repeater {
            id: segRepeater
            model: seg.options
            delegate: Item {
                required property var modelData
                implicitWidth: segLabel.implicitWidth + 20
                implicitHeight: 24
                Text {
                    id: segLabel
                    anchors.centerIn: parent
                    text: modelData.label
                    color: seg.current === modelData.id ? Theme.t1 : Theme.t2
                    Behavior on color { ColorAnimation { duration: Theme.d(200) } }
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsBody
                    font.weight: Font.Medium
                }
                HoverHandler { cursorShape: Qt.PointingHandCursor }
                TapHandler { onTapped: seg.picked(modelData.id) }
            }
        }
    }
}
