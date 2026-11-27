// SPDX-License-Identifier: GPL-3.0-only
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

Item {
    id: key
    property string text: ""
    property string kind: "auto"
    property string size: "m"
    property string visualState: text.length === 0 || text === "—" ? "empty" : "normal"
    property bool interactive: false
    property string accessibleText: text
    signal clicked()
    readonly property int capHeight: size === "s" ? 18 : 24
    readonly property var parts: {
        if (visualState === "recording" || kind !== "auto" && kind !== "combo")
            return [token(text || "—")]
        let rest = text || "—"
        const result = []
        while (/^(Ctrl|Alt|Shift)\+/.test(rest)) {
            const separator = rest.indexOf("+")
            result.push(token(rest.slice(0, separator)))
            result.push({ label: "+", icon: "", separator: true })
            rest = rest.slice(separator + 1)
        }
        result.push(token(rest))
        return result
    }
    readonly property int padding: size === "s" ? 4 : 6
    readonly property real naturalWidth: {
        let result = Math.max(0, parts.length - 1) * caps.spacing
        for (const part of parts) result += partWidth(part)
        return result
    }

    function token(value) {
        const normalized = value.toLocaleLowerCase()
        if (kind === "mouse" && value !== qsTr("鼠标左键") && value !== qsTr("鼠标右键") &&
            value !== qsTr("鼠标中键") && value !== qsTr("鼠标侧键 1") && value !== qsTr("鼠标侧键 2"))
            return { label: value, icon: "mouse" }
        if (kind === "wheel") return { label: value, icon: "wheel" }
        if (value === qsTr("鼠标左键")) return { label: qsTr("左"), icon: "mouse" }
        if (value === qsTr("鼠标右键")) return { label: qsTr("右"), icon: "mouse" }
        if (value === qsTr("鼠标中键")) return { label: qsTr("中键"), icon: "wheel" }
        if (value === qsTr("鼠标侧键 1")) return { label: qsTr("侧1"), icon: "mouse" }
        if (value === qsTr("鼠标侧键 2")) return { label: qsTr("侧2"), icon: "mouse" }
        if (value === qsTr("滚轮向上")) return { label: "↑", icon: "wheel" }
        if (value === qsTr("滚轮向下")) return { label: "↓", icon: "wheel" }
        if (value === qsTr("滚轮向左")) return { label: "←", icon: "wheel" }
        if (value === qsTr("滚轮向右")) return { label: "→", icon: "wheel" }
        if (normalized === "space") return { label: qsTr("空格"), icon: "" }
        if (normalized === "enter" || normalized === "return") return { label: qsTr("回车"), icon: "" }
        if (normalized === "backspace") return { label: qsTr("退格"), icon: "" }
        return { label: value, icon: "" }
    }
    function partWidth(part) {
        if (part.separator) return metrics.advanceWidth("+")
        return Math.max(capHeight, Math.ceil(metrics.advanceWidth(part.label)) + padding * 2 + (part.icon ? 13 : 0))
    }

    implicitWidth: naturalWidth
    implicitHeight: Math.max(interactive ? 28 : capHeight, caps.implicitHeight)
    activeFocusOnTab: interactive && enabled
    Accessible.role: interactive ? Accessible.Button : Accessible.StaticText
    Accessible.name: accessibleText
    Accessible.onPressAction: if (interactive && enabled) clicked()
    Keys.onReturnPressed: if (interactive) clicked()
    Keys.onSpacePressed: if (interactive) clicked()
    HoverHandler { id: hover; enabled: key.interactive; cursorShape: Qt.PointingHandCursor }
    TapHandler { enabled: key.interactive; gesturePolicy: TapHandler.WithinBounds; onTapped: key.clicked() }
    ToolTip.delay: 450
    ToolTip.visible: hover.hovered && accessibleText.length > 0 && width < naturalWidth
    ToolTip.text: accessibleText

    FontMetrics {
        id: metrics
        font.family: Theme.fontUi
        font.pixelSize: key.size === "s" ? Theme.fsEyebrow : Theme.fsSmall
        font.weight: Font.DemiBold
    }
    Flow {
        id: caps
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        spacing: 3
        SequentialAnimation on opacity {
            running: key.visualState === "recording" && key.visible && !Theme.reduced
            loops: Animation.Infinite
            onStopped: caps.opacity = 1
            NumberAnimation { from: 1; to: 0.55; duration: 500; easing.type: Easing.InOutSine }
            NumberAnimation { from: 0.55; to: 1; duration: 500; easing.type: Easing.InOutSine }
        }
        Repeater {
            model: key.parts
            delegate: Item {
                id: piece
                required property var modelData
                readonly property bool separator: modelData.separator === true
                readonly property bool empty: key.visualState === "empty"
                readonly property color ink: key.visualState === "secondary" || separator ? Theme.t2 : Theme.t1
                width: Math.min(caps.width, key.partWidth(modelData))
                height: key.capHeight
                Rectangle {
                    anchors.fill: parent
                    visible: !piece.separator && !piece.empty
                    radius: 5
                    color: Theme.card3
                    border.color: key.visualState === "recording" || key.activeFocus || hover.hovered ? Theme.accent
                        : key.visualState === "conflict" ? Theme.warn : Theme.stroke2
                    border.width: key.visualState === "recording" || key.visualState === "conflict" || key.activeFocus ? 2 : 1
                }
                Shape {
                    anchors.fill: parent
                    visible: piece.empty
                    ShapePath {
                        strokeColor: key.activeFocus || hover.hovered ? Theme.accent : Theme.t3
                        strokeWidth: 1
                        strokeStyle: ShapePath.DashLine
                        dashPattern: [2, 2]
                        fillColor: "transparent"
                        PathRectangle { x: 0.5; y: 0.5; width: Math.max(0, piece.width - 1); height: piece.height - 1; radius: 5 }
                    }
                }
                Row {
                    anchors.centerIn: parent
                    width: Math.min(implicitWidth, Math.max(0, piece.width - (piece.separator ? 0 : key.padding)))
                    spacing: 3
                    Rectangle {
                        visible: piece.modelData.icon.length > 0
                        anchors.verticalCenter: parent.verticalCenter
                        width: 9
                        height: 12
                        radius: 4
                        color: "transparent"
                        border.color: piece.ink
                        Rectangle {
                            width: piece.modelData.icon === "wheel" ? 2 : 1
                            height: 4
                            x: (parent.width - width) / 2
                            y: 2
                            radius: 1
                            color: piece.ink
                        }
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, Math.max(0, piece.width - (piece.separator ? 0 : key.padding * 2) -
                            (piece.modelData.icon.length ? 12 : 0)))
                        elide: Text.ElideRight
                        text: piece.modelData.label
                        color: piece.ink
                        font.family: metrics.font.family
                        font.pixelSize: metrics.font.pixelSize
                        font.weight: metrics.font.weight
                    }
                }
            }
        }
    }
}
