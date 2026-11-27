// Font Awesome Free solid icons. Preserve app aliases and control geometry.
import QtQuick
import QtQuick.Window
import "FontAwesomeIconData.js" as FAIconData

Item {
    id: icon
    property string name
    property color color: Theme.t1
    property real size: 16
    // Compatibility properties: Font Awesome solid paths use fills, not strokes.
    property real strokeWidth: 1.7
    property bool filled: false
    readonly property var artwork: FAIconData.icons[name]
    readonly property bool known: artwork !== undefined
    implicitWidth: size
    implicitHeight: size

    function softwareSvg() {
        if (!known) return ""
        const paint = "rgb(" + Math.round(color.r * 255) + "," + Math.round(color.g * 255) + "," + Math.round(color.b * 255) + ")"
        const svg = '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 '
            + artwork.width + ' ' + artwork.height + '"><path fill="' + paint
            + '" fill-opacity="' + color.a + '" d="' + artwork.path + '"/></svg>'
        return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg)
    }

    // Qt's SVG image path works with both software and GPU renderers and respects
    // clipping. Each icon keeps its native aspect ratio within the same square slot.
    Image {
        objectName: "software-icon-image"
        anchors.fill: parent
        visible: icon.known
        source: icon.softwareSvg()
        fillMode: Image.PreserveAspectFit
        sourceSize.width: Math.max(1, Math.ceil(icon.size * Screen.devicePixelRatio))
        sourceSize.height: Math.max(1, Math.ceil(icon.size * Screen.devicePixelRatio))
        cache: true
        smooth: true
    }
    Text {
        anchors.centerIn: parent
        visible: icon.name === "back10" || icon.name === "fwd10"
        text: "10"
        color: icon.color
        font.family: Theme.fontUi
        font.pixelSize: Math.max(6, icon.size * 0.38)
        font.weight: Font.Bold
    }
}
