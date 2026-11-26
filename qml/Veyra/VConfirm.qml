// A confirmation in the app's own dialog chrome (DialogHost's DLayer look):
// scrim, #dialog card with radius 16, glyph + title, body, 取消 / 确定.
// Replaces the platform Dialog whose white title bar and OK/Cancel buttons did
// not belong to this design.
import QtQuick
import QtQuick.Layouts

FocusScope {
    id: box
    anchors.fill: parent
    z: 50
    property string title: ""
    property string text: ""
    property string glyph: "info"
    property string acceptText: qsTr("确定")
    property bool acceptEnabled: true
    property string rejectText: qsTr("取消")
    property bool shown: false
    // Wider cards and custom content (the design's 列表 / 节点 comparison) go
    // between the text and the buttons.
    property int cardWidth: 440
    property string acceptIcon: ""
    default property alias extra: extraCol.data
    signal accepted()
    signal rejected()
    function open() { shown = true; box.forceActiveFocus(); rejectBtn.forceActiveFocus() }
    function close() { shown = false }
    visible: scrim.opacity > 0

    Accessible.role: Accessible.Dialog
    Accessible.name: box.title
    Keys.onEscapePressed: event => { event.accepted = true; box.close(); box.rejected() }

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
        property real coverRadius: Theme.rModal
        anchors.centerIn: parent
        width: Math.min(box.cardWidth, box.width - Theme.s32)
        height: col.implicitHeight + Theme.s32
        radius: Theme.rModal
        color: Theme.dialog
        border.width: 1
        border.color: Theme.strokeStrong
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
            anchors.margins: Theme.s16
            spacing: Theme.s12
            RowLayout {
                Layout.fillWidth: true
                spacing: Theme.s12
                Rectangle {
                    implicitWidth: 34; implicitHeight: 34; radius: Theme.rCtl
                    color: Theme.card2
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
                spacing: Theme.s8
                Item { Layout.fillWidth: true }
                VButton {
                    id: rejectBtn
                    objectName: box.objectName + "-reject"
                    text: box.rejectText
                    KeyNavigation.tab: acceptBtn.enabled ? acceptBtn : rejectBtn
                    KeyNavigation.backtab: acceptBtn.enabled ? acceptBtn : rejectBtn
                    onClicked: { box.close(); box.rejected() }
                }
                VButton {
                    id: acceptBtn
                    objectName: box.objectName + "-accept"
                    text: box.acceptText
                    enabled: box.acceptEnabled
                    iconName: box.acceptIcon
                    primary: true
                    KeyNavigation.tab: rejectBtn
                    KeyNavigation.backtab: rejectBtn
                    onClicked: { box.close(); box.accepted() }
                }
            }
        }
    }
}
