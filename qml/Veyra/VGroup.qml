import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// .dgroup — a card holding a stack of label/control rows.
//
// A ColumnLayout, not a Column. VRow has no width of its own: it asks for one through
// Layout.fillWidth, which only a layout honours. A Column ignores Layout.* and skips
// zero-width children entirely, so every row stayed at y=0 with a 0 implicitHeight and
// the rows drew on top of each other (reproduced with qml.exe, 2026-09-26).
//
// The group fills its column by default, as VRow does. Without it a group placed in a
// ColumnLayout got width 0, its inner layout -28, and the rows were not positioned.
Rectangle {
    id: group
    default property alias body: inner2.data
    // For the staggered tab redraw (VRise.Stagger).
    property real motionDy: 0
    transform: Translate { y: group.motionDy }
    Layout.fillWidth: true
    radius: 12
    color: Theme.card2
    border.width: 1
    border.color: Theme.stroke
    // Rows carry their own vertical padding. A note, button or plain layout at
    // either end of the card does not, and sat on its border (导出 音轨 card,
    // field report 2026-10-05), so those ends get the padding a row would have.
    function edgeIsRow(last) {
        const kids = inner2.children
        for (let n = 0; n < kids.length; ++n) {
            const c = kids[last ? kids.length - 1 - n : n]
            if (c instanceof Repeater || !c.visible) continue
            return c instanceof VRow
        }
        return true
    }
    readonly property int topPad: edgeIsRow(false) ? 3 : 10
    readonly property int bottomPad: edgeIsRow(true) ? 3 : 10
    implicitHeight: inner2.height + topPad + bottomPad

    ColumnLayout {
        id: inner2
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        anchors.top: parent.top
        anchors.topMargin: group.topPad
        spacing: 0
    }
}
