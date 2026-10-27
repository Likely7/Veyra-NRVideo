// Shared dialog presentation; extracted without changing geometry or motion.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    property string placeholder: ""
    property alias text: field.text
    signal edited(string text)
    // Every keystroke. editingFinished alone needs Enter or a focus change: typing a
    // value and clicking 连接 (which takes no focus) or closing the dialog lost it.
    signal typed(string text)
    implicitHeight: 30
    radius: 9
    color: Qt.rgba(1, 1, 1, 0.04)
    border.width: 1
    border.color: field.activeFocus ? Theme.accent : Theme.stroke
    TextInput {
        id: field
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        verticalAlignment: TextInput.AlignVCenter
        color: Theme.t1
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
        selectByMouse: true
        onEditingFinished: parent.edited(text)
        onTextEdited: parent.typed(text)
    }
    Text {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 10
        visible: field.text.length === 0
        text: parent.placeholder
        color: Theme.t3
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
    }
}
