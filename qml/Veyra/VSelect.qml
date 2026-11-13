import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: sel
    property string value: ""
    readonly property var selectedOption: options.find(o => o.label === value) || ({})
    readonly property color valueColor: selectedOption.danger === true ? Theme.err
                                       : selectedOption.gold === true ? Theme.warn : Theme.t1
    property var options: []      // [{ id, label, danger?, gold? }]
    // .pop h6: the design titles a select's menu with the row label (core.js sel()).
    property string title: ""
    signal picked(string id)

    implicitWidth: 150
    implicitHeight: 30
    radius: 9
    color: hover.hovered ? Qt.rgba(1, 1, 1, 0.09) : Qt.rgba(1, 1, 1, 0.05)
    border.width: 1
    border.color: Theme.stroke
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { gesturePolicy: TapHandler.WithinBounds; onTapped: menu.openAt(sel, "down") }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 10
        Text {
            id: valueLabel
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            text: sel.value
            color: sel.valueColor
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            font.weight: Font.Medium
            elide: Text.ElideRight
        }
        VIcon { name: "down"; size: 14; color: Theme.t3 }
    }

    ToolTip.visible: hover.hovered && valueLabel.truncated
    ToolTip.delay: 500
    ToolTip.text: sel.value

    VMenu {
        id: menu
        // Default to the enclosing VRow's label, as the design's sel(label, ...) does.
        title: {
            if (sel.title.length > 0) return sel.title
            for (let p = sel.parent; p; p = p.parent)
                if (p instanceof VRow) return p.label
            return ""
        }
        items: sel.options.map(o => ({ label: o.label, checked: o.label === sel.value, disabled: o.disabled === true, danger: o.danger === true, gold: o.gold === true, note: o.note || "" }))
        onPicked: (i, o) => sel.picked(sel.options[i].id)
    }
}
