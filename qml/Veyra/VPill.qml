import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    id: pill
    property string key: ""
    property string value: ""
    // Child items (e.g. a status dot) sit in the row before the text, not at
    // the pill's top-left corner.
    default property alias leading: lead.data
    signal clicked()
    implicitWidth: pillRow.implicitWidth + 21
    implicitHeight: Theme.ctlHeight
    radius: 999
    color: hover.hovered ? Qt.rgba(1, 1, 1, 0.10) : Qt.rgba(1, 1, 1, 0.05)
    border.width: 1
    border.color: Theme.stroke2
    scale: tap.pressed ? 0.95 : 1.0
    Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
    RowLayout {
        id: pillRow
        anchors.centerIn: parent
        spacing: 7
        Row { id: lead; visible: children.length > 0; Layout.alignment: Qt.AlignVCenter }
        Text { visible: pill.key.length > 0; text: pill.key; color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody }
        Text { text: pill.value; color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody; font.weight: Font.Medium }
    }
    HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
    TapHandler { id: tap; gesturePolicy: TapHandler.WithinBounds; onTapped: pill.clicked() }
}
