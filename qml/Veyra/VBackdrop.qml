import QtQuick

// Flat app chrome, shared by software and GPU scene graphs.
Rectangle {
    property color strokeColor: Theme.stroke
    property bool softwareRendering: typeof vySoftwareUi !== "undefined" && vySoftwareUi
    radius: Theme.rWindow
    color: Theme.bgOuter
    border.width: 1
    border.color: strokeColor
}
