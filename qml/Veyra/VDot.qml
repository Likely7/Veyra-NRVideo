import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
Rectangle {
    property bool warn: false
    property bool err: false
    property bool off: false
    width: 7; height: 7; radius: 3.5
    color: err ? Theme.err : warn ? Theme.warn : off ? Theme.t3 : Theme.ok
    Rectangle {
        anchors.centerIn: parent
        width: 13; height: 13; radius: 6.5
        z: -1
        color: "transparent"
        border.width: 3
        border.color: err ? Theme.errGlow : warn ? Theme.warnGlow : off ? "transparent" : Theme.okGlow
    }
    // .dot.warn { animation: pulse 1.2s infinite }
    SequentialAnimation on opacity {
        running: warn
        loops: Animation.Infinite
        NumberAnimation { to: 0.35; duration: 600 }
        NumberAnimation { to: 1.0; duration: 600 }
    }
}
