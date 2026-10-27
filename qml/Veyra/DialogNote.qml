// Shared dialog presentation; extracted without changing geometry or motion.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    property string text: ""
    Layout.fillWidth: true
    Layout.topMargin: 8
    implicitHeight: noteText.implicitHeight + 18
    radius: 9
    color: Qt.rgba(0.961, 0.784, 0.294, 0.06)
    Text {
        id: noteText
        anchors.fill: parent
        anchors.margins: 9
        text: parent.text
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
        wrapMode: Text.WordWrap
    }
}
