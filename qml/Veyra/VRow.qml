import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Item {
    id: row
    property string label: ""
    property string hint: ""
    property string value: ""
    default property alias control: slot.data

    // The layout reads Layout.preferredHeight. `height` is deliberately NOT set: a
    // layout-managed item that sets its own height is undefined behaviour, and this
    // row is only ever placed inside a layout.
    implicitHeight: hint.length > 0 ? Theme.rowMinHeight + 12 : Theme.rowMinHeight
    Layout.preferredHeight: implicitHeight
    Layout.fillWidth: true
    RowLayout {
        anchors.fill: parent
        spacing: 12
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 2
            Text { text: row.label; color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody }
            Text {
                visible: row.hint.length > 0
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
}
