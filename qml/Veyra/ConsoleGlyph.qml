// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Shapes

Item {
    id: glyph
    property string platform: "ps5"
    property int action: 0
    property int size: 20
    property string visualState: "normal"
    readonly property bool xbox: platform === "xbox"
    readonly property bool face: action >= 0 && action < 4
    readonly property bool dpad: action >= 4 && action < 8
    readonly property bool shoulder: action === 8 || action === 9
    readonly property bool trigger: action === 16 || action === 17
    readonly property bool touch: action === 14 || (action >= 28 && action <= 31)
    readonly property bool compactLabel: action === 15 || action >= 26 || shoulder || trigger
    readonly property color faceInk: !face ? Theme.t1 : (xbox
        ? [Theme.padA, Theme.padB, Theme.padX, Theme.padY][action]
        : [Theme.padCross, Theme.padCircle, Theme.padSquare, Theme.padTriangle][action])
    readonly property color ink: visualState === "pressed" ? Theme.accentInk : faceInk
    readonly property color line: visualState === "selected" ? Theme.accent
        : visualState === "conflict" ? Theme.warn
        : visualState === "pressed" ? Theme.ok
        : face ? faceInk : Theme.stroke2
    readonly property string glyphText: {
        if (face) return xbox ? ["A", "B", "X", "Y"][action] : ["×", "○", "□", "△"][action]
        if (shoulder) return action === 8 ? (xbox ? "LB" : "L1") : (xbox ? "RB" : "R1")
        if (trigger) return action === 16 ? (xbox ? "LT" : "L2") : (xbox ? "RT" : "R2")
        if (action === 10) return xbox ? "LS" : "L3"
        if (action === 11) return xbox ? "RS" : "R3"
        if (action === 12) return xbox ? "☰" : "≡"
        if (action === 13) return xbox ? "▢" : "↗"
        if (action === 15) return xbox ? "Xbox" : "PS"
        if (action >= 18 && action <= 25) return (action < 22 ? "L" : "R") + ["↑", "↓", "←", "→"][(action - 18) % 4]
        if (action === 26) return "L"
        if (action === 27) return "L↔"
        if (action >= 28 && action <= 31) return ["↑", "↓", "←", "→"][action - 28]
        if (action === 32) return "L+R"
        if (action === 33) return "½"
        return ""
    }

    implicitWidth: size + (shoulder || trigger || action === 32 || (xbox && action === 15) ? 6 : 0)
    implicitHeight: size
    width: implicitWidth
    height: implicitHeight

    Rectangle {
        id: background
        anchors.fill: parent
        radius: glyph.shoulder || glyph.trigger || glyph.touch ? Math.max(3, height * 0.26) : height / 2
        bottomLeftRadius: glyph.trigger ? height / 2 : radius
        bottomRightRadius: glyph.trigger ? height / 2 : radius
        color: glyph.visualState === "pressed" ? Theme.ok : Theme.card3
        border.color: glyph.line
        border.width: glyph.visualState === "unbound" ? 0
            : glyph.visualState === "selected" || glyph.visualState === "conflict" ? 2 : 1
    }
    Shape {
        anchors.fill: parent
        visible: glyph.visualState === "unbound"
        ShapePath {
            strokeColor: Theme.t3
            strokeWidth: 1
            strokeStyle: ShapePath.DashLine
            dashPattern: [2, 2]
            fillColor: "transparent"
            PathRectangle { x: 0.5; y: 0.5; width: glyph.width - 1; height: glyph.height - 1; radius: background.radius }
        }
    }
    Item {
        anchors.centerIn: parent
        width: parent.height * 0.62
        height: width
        visible: glyph.dpad
        Rectangle {
            anchors.verticalCenter: parent.verticalCenter
            width: parent.width
            height: parent.height / 3
            radius: 1
            color: Qt.alpha(glyph.ink, 0.4)
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width / 3
            height: parent.height
            radius: 1
            color: Qt.alpha(glyph.ink, 0.4)
        }
        Rectangle {
            width: parent.width / 3
            height: width
            x: glyph.action === 4 ? 0 : glyph.action === 5 ? width * 2 : width
            y: glyph.action === 6 ? 0 : glyph.action === 7 ? height * 2 : height
            radius: 1
            color: glyph.ink
        }
    }
    Text {
        anchors.centerIn: parent
        visible: text.length > 0
        text: glyph.glyphText
        color: glyph.ink
        font.family: Theme.fontUi
        font.pixelSize: Math.round(glyph.height * (glyph.compactLabel ? 0.48 : 0.61))
        font.weight: Font.DemiBold
    }
}
