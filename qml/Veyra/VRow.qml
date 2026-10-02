import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: row
    property string label: ""
    property string hint: ""
    property string value: ""
    // Narrow panels with a wide control (导出 输出设置): the hint runs under the
    // whole row instead of being squeezed beside the control.
    property bool hintBelow: false
    default property alias control: slot.data

    // The layout reads Layout.preferredHeight. `height` is deliberately NOT set: a
    // layout-managed item that sets its own height is undefined behaviour, and this
    // row is only ever placed inside a layout.
    // A hint that wraps (narrow panels) grows the row instead of running into
    // the next one.
    implicitHeight: hintBelow
        ? mainRow.height + (hint.length > 0 ? belowHint.implicitHeight + 6 : 0)
        : Math.max(hint.length > 0 ? Theme.rowMinHeight + 12 : Theme.rowMinHeight,
                   textColumn.implicitHeight + 12, slot.implicitHeight + 8)
    Layout.preferredHeight: implicitHeight
    Layout.fillWidth: true
    RowLayout {
        id: mainRow
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: row.hintBelow ? Math.max(Theme.rowMinHeight, slot.implicitHeight + 8) : parent.height
        spacing: 12
        ColumnLayout {
            id: textColumn
            Layout.fillWidth: true
            spacing: 2
            // fillWidth here too: without a hint the label is the column's only child,
            // and a layout with no filling child cannot grow, which left the control
            // beside the label instead of at the row's right edge.
            // Two lines before eliding: English and Japanese labels run longer than Chinese.
            Text { Layout.fillWidth: true; text: row.label; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody; wrapMode: Text.WordWrap; maximumLineCount: 2; elide: Text.ElideRight }
            Text {
                visible: row.hint.length > 0 && !row.hintBelow
                text: row.hint
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 11
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        Item {
            id: slot
            implicitHeight: childrenRect.height
            Layout.preferredWidth: childrenRect.width
            Layout.preferredHeight: childrenRect.height
        }
        Text {
            visible: row.value.length > 0
            text: row.value
            color: Theme.t1
            font.family: Theme.fontMono
            font.pixelSize: Theme.fsSmall
            Layout.minimumWidth: 40
            horizontalAlignment: Text.AlignRight
        }
    }
    Text {
        id: belowHint
        visible: row.hintBelow && row.hint.length > 0
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: mainRow.bottom
        anchors.topMargin: -4
        text: row.hint
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: 11
        wrapMode: Text.WordWrap
    }
}
