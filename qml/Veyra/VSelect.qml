import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: sel
    property string value: ""
    property var options: []      // [{ id, label }]
    signal picked(string id)

    implicitWidth: 150
    implicitHeight: 30
    radius: 9
    color: hover.hovered ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.05)
    border.width: 1
    border.color: Theme.stroke
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { onTapped: menu.popup() }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        Text {
            Layout.fillWidth: true
            text: sel.value
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
        Text { text: "▾"; color: Theme.t3; font.pixelSize: 10 }
    }

    Menu {
        id: menu
        width: Math.max(sel.width, 180)
        Repeater {
            model: sel.options
            delegate: MenuItem {
                required property var modelData
                text: modelData.label
                onTriggered: sel.picked(modelData.id)
            }
        }
    }
}
