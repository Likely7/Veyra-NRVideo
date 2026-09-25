import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
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

    RowLayout {
        id: segRow
        anchors.centerIn: parent
        spacing: 0
        Repeater {
            model: seg.options
            delegate: Item {
                required property var modelData
                implicitWidth: segLabel.implicitWidth + 20
                implicitHeight: 24
                Rectangle {
                    anchors.fill: parent
                    anchors.margins: 0
                    radius: 7
                    color: seg.current === modelData.id ? Theme.card3 : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.d(Theme.durFast) } }
                }
                Text {
                    id: segLabel
                    anchors.centerIn: parent
                    text: modelData.label
                    color: seg.current === modelData.id ? Theme.t1 : Theme.t2
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
