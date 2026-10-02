// A confirmation in the app's own dialog chrome (DialogHost's DLayer look):
// scrim, #dialog card with radius 16, glyph + title, body, 取消 / 确定.
// Replaces the platform Dialog whose white title bar and OK/Cancel buttons did
// not belong to this design.
import QtQuick
import QtQuick.Layouts

Item {
    id: box
    anchors.fill: parent
    z: 50
    property string title: ""
    property string text: ""
    property string glyph: "info"
    property string acceptText: qsTr("确定")
    property string rejectText: qsTr("取消")
    property bool shown: false
    // Wider cards and custom content (the design's 列表 / 节点 comparison) go
    // between the text and the buttons.
    property int cardWidth: 440
    property string acceptIcon: ""
    default property alias extra: extraCol.data
    signal accepted()
    signal rejected()
    function open() { shown = true }
    function close() { shown = false }
    visible: scrim.opacity > 0

    Keys.onEscapePressed: { box.close(); box.rejected() }

    Rectangle {
        id: scrim
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)
        opacity: box.shown ? 1 : 0
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        TapHandler { onTapped: { box.close(); box.rejected() } }
    }

    Rectangle {
        id: card
        // Cut out of the native video window (main.cpp syncVideoCovers).
        objectName: "videoCover"
        property real coverRadius: 16
        anchors.centerIn: parent
        width: Math.min(box.cardWidth, box.width - 32)
        height: col.implicitHeight + 32
        radius: 16
        color: Theme.dialog
        border.width: 1
        border.color: Theme.stroke2
        opacity: box.shown ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        property real motionS: box.shown ? 1 : 0.9
        property real motionDy: box.shown ? 0 : 14
        Behavior on motionS { NumberAnimation { duration: Theme.d(550); easing.bezierCurve: Theme.spring } }
        Behavior on motionDy { NumberAnimation { duration: Theme.d(550); easing.bezierCurve: Theme.spring } }
        transform: [
            Scale { origin.x: card.width / 2; origin.y: card.height / 2; xScale: card.motionS; yScale: card.motionS },
            Translate { y: card.motionDy }
        ]
        // Swallow clicks so they do not reach the scrim.
        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }

        ColumnLayout {
            id: col
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 16
            spacing: 14
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Rectangle {
                    implicitWidth: 34; implicitHeight: 34; radius: 10
                    color: Qt.rgba(1, 1, 1, 0.06)
                    VIcon { anchors.centerIn: parent; name: box.glyph }
                }
                Text {
                    Layout.fillWidth: true
                    text: box.title
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsH2
                    font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                    wrapMode: Text.WordWrap
                }
            }
            Text {
                Layout.fillWidth: true
                visible: box.text.length > 0
                text: box.text
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
                wrapMode: Text.WordWrap
                lineHeight: 1.3
            }
            ColumnLayout {
                id: extraCol
                Layout.fillWidth: true
                spacing: 10
                visible: children.length > 0
            }
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                Item { Layout.fillWidth: true }
                VButton {
                    objectName: box.objectName + "-reject"
                    text: box.rejectText
                    onClicked: { box.close(); box.rejected() }
                }
                VButton {
                    objectName: box.objectName + "-accept"
                    text: box.acceptText
                    iconName: box.acceptIcon
                    primary: true
                    onClicked: { box.close(); box.accepted() }
                }
            }
        }
    }
}
