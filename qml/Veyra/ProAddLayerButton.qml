// Professional-view reusable presentation component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes

Rectangle {
    id: addLayer
    property string label: ""
    property string note: ""
    property bool full: false
    signal add()
    Layout.fillWidth: true
    Layout.topMargin: Theme.s4
    implicitHeight: 36
    radius: Theme.rCard
    opacity: full ? Theme.disabledOpacity : 1
    color: addTap.pressed && !full ? Theme.pressFill : addHover.hovered && !full ? Theme.accentSoft : "transparent"
    activeFocusOnTab: !full
    Accessible.role: Accessible.Button
    Accessible.name: addLayer.label
    Accessible.description: addLayer.note
    Keys.onReturnPressed: if (!full) addLayer.add()
    Keys.onSpacePressed: if (!full) addLayer.add()
    readonly property bool softwareRendering: (typeof vySoftwareUi !== "undefined" && vySoftwareUi)
        || GraphicsInfo.api === GraphicsInfo.Software
    readonly property color outline: addLayer.activeFocus ? Theme.focusRing
        : (addHover.hovered && !addLayer.full ? Theme.accent : Theme.strokeStrong)
    Image {
        anchors.fill: parent
        visible: addLayer.softwareRendering
        // Software Shape ignores empty clips. A cached image keeps
        // this dashed path inside the inspector like the icons.
        source: {
            if (!visible || width <= 1 || height <= 1) return ""
            const c = addLayer.outline
            const paint = "rgb(" + Math.round(c.r * 255) + "," + Math.round(c.g * 255) + "," + Math.round(c.b * 255) + ")"
            return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(
                '<svg xmlns="http://www.w3.org/2000/svg" width="' + width + '" height="' + height + '">'
                + '<rect x="0.5" y="0.5" width="' + (width-1) + '" height="' + (height-1)
                + '" rx="' + addLayer.radius + '" fill="none" stroke="' + paint + '" stroke-opacity="' + c.a
                + '" stroke-width="1" stroke-dasharray="5 4"/></svg>')
        }
        sourceSize.width: Math.max(1, Math.ceil(width * Screen.devicePixelRatio))
        sourceSize.height: Math.max(1, Math.ceil(height * Screen.devicePixelRatio))
        cache: true
    }
    Shape {
        visible: !addLayer.softwareRendering
        anchors.fill: parent
        ShapePath {
            strokeWidth: 1
            strokeColor: addLayer.outline
            fillColor: "transparent"
            strokeStyle: ShapePath.DashLine
            dashPattern: [5, 4]
            PathRectangle {
                x: 0; y: 0
                width: addLayer.width
                height: addLayer.height
                radius: addLayer.radius
            }
        }
    }
    Row {
        anchors.centerIn: parent
        spacing: 6
        Text {
            text: addLayer.label
            color: addHover.hovered && !addLayer.full ? Theme.accent : Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            font.weight: Font.Medium; font.variableAxes: Theme.axesMedium
        }
        Text {
            anchors.baseline: parent.children[0].baseline
            text: addLayer.note
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsEyebrow
        }
    }
    HoverHandler { id: addHover; cursorShape: addLayer.full ? Qt.ArrowCursor : Qt.PointingHandCursor }
    TapHandler {
        id: addTap
        enabled: !addLayer.full
        onTapped: addLayer.add()
    }
}
