// Shared dialog presentation; extracted without changing geometry or motion.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    property string text: ""
    Layout.fillWidth: true
    Layout.topMargin: Theme.s4
    implicitHeight: noteText.implicitHeight + Theme.s24
    radius: Theme.rCard
    color: Theme.warnGlow
    border.width: 1
    border.color: Theme.stroke
    Text {
        id: noteText
        anchors.fill: parent
        anchors.margins: Theme.s12
        text: parent.text
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
        wrapMode: Text.WordWrap
    }
}
