import QtQuick
import QtQuick.Controls.Basic
import "Ps5Bindings.js" as Bindings

Column {
    id: diagram
    required property var profile
    required property var rows
    required property string selectedSpec
    signal inputSelected(string key)
    signal inputFocused(Item key)
    spacing: 20

    readonly property int pitch: 46
    readonly property int gap: 4
    readonly property int deckPadding: 12
    readonly property var byKey: {
        const result = {}
        for (const row of rows) {
            if (!result[row.key]) result[row.key] = []
            result[row.key].push(row)
        }
        return result
    }
    readonly property var shortcuts: {
        const result = {}
        for (const [scene, key] of Object.entries(profile.context_hotkeys || {}))
            result[key] = Bindings.situationName(profile, scene)
        return result
    }

    component InputButton: Button {
        id: button
        required property string code
        readonly property var bindings: diagram.byKey[code] || []
        readonly property var binding: bindings.find(r => r.spec === diagram.selectedSpec) || bindings[0]
        readonly property string shortcut: diagram.shortcuts[code] || ""
        readonly property string output: binding ? binding.controls.map(Bindings.controlMark).filter(Boolean).join("+") : ""
        readonly property string gesture: binding && binding.spec.includes(":") ? Bindings.gestureName(diagram.profile, binding.spec) : ""
        readonly property bool selectedTile: Bindings.physical(diagram.selectedSpec) === code
        readonly property color ink: down ? Theme.accentInk : Theme.t1
        objectName: "key-" + code
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        padding: 5
        Accessible.name: [Bindings.inputName(diagram.profile, code), shortcut,
                          ...bindings.map(r => [r.input, r.shortTitle, r.output].filter(Boolean).join(" · "))].filter(Boolean).join("\n")
        Accessible.selected: selectedTile
        ToolTip.visible: enabled && (hovered || visualFocus)
        ToolTip.delay: 450
        ToolTip.text: Accessible.name
        onClicked: diagram.inputSelected(code)
        background: Rectangle {
            radius: 4
            color: button.down ? Theme.accent : button.selectedTile ? Theme.accentSoft
                   : button.hovered || button.binding || button.shortcut ? Theme.card3 : Theme.card
            border.width: button.visualFocus || button.selectedTile ? 2 : 1
            border.color: button.visualFocus ? Theme.t1 : button.selectedTile || button.hovered ? Theme.accent
                          : button.enabled ? Theme.stroke2 : Theme.stroke
        }
    }

    Flickable {
        id: keyboardViewport
        objectName: "keyboard-viewport"
        width: parent.width
        height: deck.height + (deck.width > width ? 14 : 0)
        contentHeight: height
        contentWidth: Math.max(width, deck.width)
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.HorizontalFlick
        ScrollBar.horizontal: ScrollBar {
            policy: deck.width > keyboardViewport.width ? ScrollBar.AlwaysOn : ScrollBar.AlwaysOff
        }
        function reveal(key) {
            const x = key.mapToItem(contentItem, 0, 0).x
            if (x < contentX + 4) contentX = Math.max(0, x - 4)
            else if (x + key.width + 4 > contentX + width)
                contentX = Math.min(contentWidth - width, x + key.width + 4 - width)
            diagram.inputFocused(key)
        }
        Rectangle {
            id: deck
            x: Math.max(0, Math.round((keyboardViewport.width - width) / 2))
            width: 23 * diagram.pitch - diagram.gap + 2 * diagram.deckPadding
            height: 6.5 * diagram.pitch - diagram.gap + 2 * diagram.deckPadding
            radius: 12
            color: Theme.card2
            border.color: Theme.stroke2
            Repeater {
                model: Bindings.keyboard
                InputButton {
                    id: cap
                    required property var modelData
                    code: modelData.key
                    enabled: modelData.bindable
                    x: diagram.deckPadding + Math.round(modelData.x * diagram.pitch)
                    y: diagram.deckPadding + Math.round(modelData.y * diagram.pitch)
                    width: Math.round((modelData.x + modelData.w) * diagram.pitch) - Math.round(modelData.x * diagram.pitch) - diagram.gap
                    height: modelData.h * diagram.pitch - diagram.gap
                    text: Bindings.keycap(code)
                    onVisualFocusChanged: if (visualFocus) keyboardViewport.reveal(this)
                    contentItem: Item {
                        Text {
                            x: 1
                            y: cap.down ? 1 : 0
                            width: parent.width - 1
                            text: cap.text
                            color: cap.enabled ? cap.ink : Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 9
                            font.weight: Font.Medium
                        }
                        Text {
                            anchors.left: parent.left
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            text: cap.shortcut || [cap.output, cap.gesture].filter(Boolean).join(" · ")
                            color: cap.down ? Theme.accentInk : Theme.accent
                            font.family: Theme.fontUi
                            font.pixelSize: 10
                            fontSizeMode: Text.HorizontalFit
                            minimumPixelSize: 8
                            elide: Text.ElideRight
                        }
                    }
                }
            }
        }
    }
    Item {
        id: mouse
        objectName: "mouse-diagram"
        anchors.horizontalCenter: parent.horizontalCenter
        width: 490
        height: 168
        readonly property int calloutWidth: 168
        readonly property int drawingX: 203
        readonly property string selected: Bindings.physical(diagram.selectedSpec)
        property string held: ""
        property string hoveredCode: ""
        readonly property var controls: [
            {key: "MouseLeft", side: 0, slot: 0, px: 26, py: 22},
            {key: "MouseRight", side: 1, slot: 0, px: 61, py: 22},
            {key: "Mouse5", side: 0, slot: 1, px: 7, py: 76},
            {key: "WheelUp", side: 1, slot: 1, px: 44, py: 20},
            {key: "Mouse4", side: 0, slot: 2, px: 7, py: 101},
            {key: "MouseMiddle", side: 1, slot: 2, px: 44, py: 38},
            {key: "MouseMotion", side: 0, slot: 3, px: 42, py: 128},
            {key: "WheelDown", side: 1, slot: 3, px: 44, py: 56}
        ]
        // Rasterize just the drawing at the screen's pixel ratio. The native controls stay separate.
        Image {
            objectName: "mouse-drawing"
            anchors.fill: parent
            cache: false
            retainWhileLoading: true
            sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
            sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
            source: {
                const outline = Qt.tint(Theme.dialog, Theme.stroke2)
                const highlight = Qt.tint(Theme.card3, Theme.accentGlow)
                function fill(key) {
                    if (mouse.held === key) return Theme.accent
                    if (mouse.selected === key || mouse.hoveredCode === key) return highlight
                    return Theme.card3
                }
                const guides = mouse.controls.map(control => {
                    const edge = control.side ? mouse.width - mouse.calloutWidth : mouse.calloutWidth
                    const y = control.slot * 42 + 19
                    const elbow = edge + (control.side ? -10 : 10)
                    const line = mouse.selected === control.key ? Theme.accent : outline
                    return `<path stroke="${line}" d="M${edge} ${y} H${elbow} L${mouse.drawingX + control.px} ${8 + control.py}"/>`
                }).join("")
                const svg = `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 490 168">
                    <g fill="none">${guides}</g>
                    <g transform="translate(${mouse.drawingX} 8)" stroke="${outline}">
                        <path fill="${fill("MouseMotion")}" stroke-width="1.5" d="M44 1 C21 1 9 17 9 44 L9 94 C9 126 21 146 43 146 C68 146 79 125 79 96 L79 46 C79 17 67 1 44 1 Z"/>
                        <path fill="${fill("MouseLeft")}" d="M43 2 C22 2 10 18 10 45 L10 63 Q24 65 43 63 Z"/>
                        <path fill="${fill("MouseRight")}" d="M45 2 C67 2 78 19 78 46 L78 63 Q63 65 45 63 Z"/>
                    </g></svg>`
                return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg)
            }
        }
        Item {
            x: mouse.drawingX; y: 8
            width: 84; height: 148
            Repeater {
                model: mouse.controls
                Item {
                    id: zone
                    required property var modelData
                    readonly property string code: modelData.key
                    readonly property bool wheel: code === "MouseMiddle" || code.startsWith("Wheel")
                    readonly property bool thumb: code === "Mouse4" || code === "Mouse5"
                    readonly property bool palm: code === "MouseMotion"
                    objectName: "mouse-region-" + code
                    width: wheel ? 13 : thumb ? 9 : palm ? 56 : 29
                    height: wheel ? 17 : thumb ? 20 : palm ? 50 : 56
                    x: modelData.px - width / 2
                    y: wheel || thumb ? modelData.py - height / 2 : palm ? 86 : 7
                    Rectangle {
                        anchors.fill: parent
                        visible: zone.wheel || zone.thumb
                        radius: 3
                        color: mouse.held === zone.code || mouse.selected === zone.code || mouse.hoveredCode === zone.code ? Theme.accent : Theme.card
                        border.color: mouse.selected === zone.code ? Theme.accent : Theme.t2
                    }
                    HoverHandler {
                        enabled: !zone.palm || !!diagram.byKey.MouseMotion
                        cursorShape: Qt.PointingHandCursor
                        onHoveredChanged: {
                            if (hovered) mouse.hoveredCode = zone.code
                            else if (mouse.hoveredCode === zone.code) mouse.hoveredCode = ""
                        }
                    }
                    TapHandler {
                        enabled: !zone.palm || !!diagram.byKey.MouseMotion
                        onPressedChanged: mouse.held = pressed ? zone.code : ""
                        onTapped: diagram.inputSelected(zone.code)
                    }
                }
            }
        }
        Repeater {
            model: mouse.controls
            InputButton {
                id: callout
                onVisualFocusChanged: if (visualFocus) diagram.inputFocused(callout)
                required property var modelData
                code: modelData.key
                enabled: code !== "MouseMotion" || !!binding
                width: mouse.calloutWidth; height: 38
                x: modelData.side ? mouse.width - width : 0
                y: modelData.slot * 42
                leftPadding: 8; rightPadding: 8
                text: code === "MouseMotion" ? qsTr("鼠标移动") : Bindings.inputName(diagram.profile, code)
                onDownChanged: mouse.held = down ? code : ""
                onHoveredChanged: {
                    if (hovered) mouse.hoveredCode = code
                    else if (mouse.hoveredCode === code) mouse.hoveredCode = ""
                }
                contentItem: Column {
                    Row {
                        width: parent.width
                        Text {
                            width: parent.width - mouseOutput.width
                            text: callout.text; color: callout.ink; font.family: Theme.fontUi
                            font.pixelSize: 12; font.weight: Font.Medium; elide: Text.ElideRight
                        }
                        Text {
                            id: mouseOutput
                            text: callout.output; color: callout.down ? Theme.accentInk : Theme.accent
                            font.family: Theme.fontUi; font.pixelSize: 10
                        }
                    }
                    Text {
                        width: parent.width
                        text: callout.binding ? callout.binding.shortTitle : "—"
                        color: callout.down ? Theme.accentInk : Theme.t2
                        font.family: Theme.fontUi; font.pixelSize: 10; elide: Text.ElideRight
                    }
                }
            }
        }
    }
}
