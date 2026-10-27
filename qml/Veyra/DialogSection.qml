// Shared dialog presentation; extracted without changing geometry or motion.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    property string text: ""
    Layout.fillWidth: true
    Layout.topMargin: 14
    Layout.bottomMargin: 4
    spacing: 8
    VEyebrow { text: parent.text }
    Rectangle {
        Layout.fillWidth: true
        implicitHeight: 1
        color: Theme.stroke
    }
}
