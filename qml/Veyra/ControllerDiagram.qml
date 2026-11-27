// SPDX-License-Identifier: GPL-3.0-only
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Shapes

Rectangle {
    id: diagram
    property string platform: "ps5"
    property bool driving: false
    property int selected: -1
    property var input: null
    property var live: ({})
    property bool editable: true
    readonly property bool xbox: platform === "xbox"
    readonly property var leftActions: [16, 8, 18, 10, 4, 13]
    readonly property var rightActions: [17, 9, 3, 1, 2, 0, 22, 11]
    readonly property var footerActions: (xbox ? [12, 15] : [14, 15, 12]).concat(driving ? [33] : [])
    readonly property var coordinates: [
        [434, 153], [466, 125], [402, 125], [434, 97],
        xbox ? [174, 182] : [92, 125], xbox ? [230, 182] : [154, 125],
        xbox ? [202, 155] : [123, 97], xbox ? [202, 209] : [123, 153],
        [121, 59], [439, 59], xbox ? [128, 114] : [204, 174], xbox ? [356, 182] : [356, 174],
        xbox ? [324, 99] : [367, 86], xbox ? [236, 99] : [193, 86], [280, 88], xbox ? [280, 67] : [280, 159],
        [121, 31], [439, 31],
        xbox ? [128, 80] : [204, 138], xbox ? [128, 148] : [204, 210],
        xbox ? [92, 114] : [168, 174], xbox ? [164, 114] : [240, 174],
        xbox ? [356, 145] : [356, 138], xbox ? [356, 220] : [356, 210],
        xbox ? [319, 182] : [320, 174], xbox ? [393, 182] : [392, 174]
    ]
    signal picked(int action)

    objectName: "controller-panel-diagram"
    implicitWidth: 388
    implicitHeight: content.implicitHeight + 16
    radius: Theme.rCard
    color: Theme.card2
    border.color: Theme.stroke2
    clip: true

    ConsolePadLabels { id: labels; xbox: diagram.xbox }

    function selectAt(x, y) {
        let nearest = -1
        let limit = 14 * 14
        for (let action = 0; action < coordinates.length; ++action) {
            if (xbox && action === 14) continue
            const point = coordinates[action]
            const dx = x - point[0] * shellSlot.unit
            const dy = y - point[1] * shellSlot.unit
            const distance = dx * dx + dy * dy
            if (distance < limit) {
                nearest = action
                limit = distance
            }
        }
        if (nearest >= 0) picked(nearest)
    }
    function groupPressed(action) {
        if (action === 4 || action === 18 || action === 22)
            return [0, 1, 2, 3].some(offset => labels.pressed(live, action + offset))
        return labels.pressed(live, action)
    }
    function primaryFor(action) {
        if (action === 22 && input?.tuning?.mouseLook !== false) return qsTr("鼠标")
        if (action === 4 || action === 18 || action === 22) {
            const keys = [0, 1, 2, 3].map(offset => labels.binding(input, driving, action + offset))
                .filter(key => key !== "—")
            return keys.length ? keys.join(" ") : "—"
        }
        return labels.binding(input, driving, action)
    }
    function selectedGroup(action) {
        return (action === 4 || action === 18 || action === 22)
            ? selected >= action && selected < action + 4 : selected === action
    }

    component DiagramTag: Rectangle {
        id: tag
        required property int action
        property bool rightSide: false
        readonly property bool ps5LearningControl: diagram.editable
        readonly property string primary: diagram.primaryFor(action)
        readonly property string alternate: labels.binding(diagram.input, diagram.driving, action, 1)
        readonly property bool showAlternate: alternate !== "—" && alternate !== "" && ![4, 18, 22].includes(action)
        readonly property bool marked: diagram.selectedGroup(action)
        objectName: "controller-panel-diagram-key-" + action
        implicitWidth: Math.min(112, tagGlyph.implicitWidth + primaryCap.implicitWidth
            + (showAlternate ? alternateCap.implicitWidth + caption.spacing : 0) + caption.spacing + 6)
        height: 28
        radius: 6
        color: marked || activeFocus ? Theme.accentSoft : "transparent"
        border.color: Theme.accent
        border.width: activeFocus ? 2 : marked ? 1 : 0
        activeFocusOnTab: diagram.editable
        Accessible.role: Accessible.Button
        Accessible.name: qsTr("%1，主键 %2，备用键 %3").arg(labels.names[action]).arg(primary).arg(alternate)
        Accessible.onPressAction: if (diagram.editable) diagram.picked(action)
        Keys.onReturnPressed: if (diagram.editable) diagram.picked(action)
        Keys.onSpacePressed: if (diagram.editable) diagram.picked(action)
        HoverHandler { id: hover; enabled: diagram.editable; cursorShape: Qt.PointingHandCursor }
        TapHandler { enabled: diagram.editable; onTapped: diagram.picked(tag.action) }
        ToolTip.text: Accessible.name
        ToolTip.visible: hover.hovered
        ToolTip.delay: 400

        Row {
            id: caption
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.leftMargin: 3
            anchors.rightMargin: 3
            layoutDirection: tag.rightSide ? Qt.RightToLeft : Qt.LeftToRight
            spacing: 2
            clip: true
            ConsoleGlyph {
                id: tagGlyph
                anchors.verticalCenter: parent.verticalCenter
                platform: diagram.platform
                action: tag.action
                size: 19
                visualState: diagram.groupPressed(action) ? "pressed" : tag.marked ? "selected" : "normal"
            }
            KeyCap {
                id: primaryCap
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(18, Math.min(implicitWidth, caption.width - tagGlyph.width - caption.spacing -
                    (tag.showAlternate ? 20 : 0)))
                text: tag.primary
                size: "s"
                kind: tag.action === 22 && diagram.input?.tuning?.mouseLook !== false ? "mouse" : "auto"
            }
            KeyCap {
                id: alternateCap
                visible: tag.showAlternate
                anchors.verticalCenter: parent.verticalCenter
                width: Math.min(implicitWidth, 25)
                text: tag.alternate
                size: "s"
                visualState: "secondary"
            }
        }
    }

    Column {
        id: content
        x: 8
        y: 8
        width: Math.max(0, parent.width - 16)
        spacing: 4

        Item {
            id: board
            objectName: "controller-panel-controller-art"
            width: parent.width
            height: Math.max(left.height, right.height, shellSlot.height)
            readonly property int rail: Math.round(Math.min(107, width * 0.29))

            Canvas {
                id: leaders
                anchors.fill: parent
                property int selectedAction: diagram.selected
                property bool xbox: diagram.xbox
                property color idleLine: Theme.stroke2
                property color selectedLine: Theme.accent
                onWidthChanged: requestPaint()
                onHeightChanged: requestPaint()
                onSelectedActionChanged: requestPaint()
                onXboxChanged: requestPaint()
                onIdleLineChanged: requestPaint()
                onSelectedLineChanged: requestPaint()
                onPaint: {
                    const ctx = getContext("2d")
                    ctx.reset()
                    ctx.clearRect(0, 0, width, height)
                    for (const side of [left, right]) {
                        const isRight = side === right
                        const actions = isRight ? diagram.rightActions : diagram.leftActions
                        for (let index = 0; index < actions.length; ++index) {
                            const action = actions[index]
                            const anchor = diagram.coordinates[action]
                            const fromX = isRight ? side.x : side.x + side.width
                            const fromY = side.y + index * (28 + side.spacing) + 14
                            const toX = shellSlot.x + anchor[0] * shellSlot.unit
                            const toY = shellSlot.y + anchor[1] * shellSlot.unit
                            ctx.beginPath()
                            ctx.moveTo(fromX, fromY)
                            ctx.lineTo((fromX + toX) * 0.5, fromY)
                            ctx.lineTo(toX, toY)
                            ctx.strokeStyle = diagram.selectedGroup(action) ? selectedLine : idleLine
                            ctx.lineWidth = 1
                            ctx.stroke()
                        }
                    }
                }
            }
            Column {
                id: left
                anchors.left: parent.left
                anchors.verticalCenter: parent.verticalCenter
                width: board.rail
                spacing: 1
                Repeater {
                    model: diagram.leftActions
                    delegate: DiagramTag {
                        required property int modelData
                        width: left.width
                        action: modelData
                    }
                }
            }
            Item {
                id: shellSlot
                readonly property bool ps5LearningControl: diagram.editable
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.verticalCenter: parent.verticalCenter
                width: Math.max(1, parent.width - 2 * board.rail)
                height: 306 * unit
                readonly property real unit: width / 560
                Shape {
                    width: 560
                    height: 306
                    transformOrigin: Item.TopLeft
                    scale: shellSlot.unit
                    ShapePath {
                        strokeWidth: 3
                        strokeColor: Theme.stroke2
                        fillColor: Theme.card2
                        PathSvg {
                            path: diagram.xbox
                                ? "M280,49 C203,49 123,33 82,68 C50,96 32,209 45,253 C59,296 103,276 123,242 C149,199 190,209 280,211 C370,209 411,199 437,242 C457,276 501,296 515,253 C528,209 510,96 478,68 C437,33 357,49 280,49 Z"
                                : "M280,59 C228,59 215,59 176,56 C145,39 105,42 82,69 C58,91 52,143 41,192 C27,247 44,285 69,279 C94,273 110,239 136,212 C153,195 177,208 198,210 Q280,218 362,210 C383,208 407,195 424,212 C450,239 466,273 491,279 C516,285 533,247 519,192 C508,143 502,91 478,69 C455,42 415,39 384,56 C345,59 332,59 280,59 Z"
                        }
                    }
                }
                Repeater {
                    model: [10, 11]
                    delegate: Rectangle {
                        id: stick
                        required property int modelData
                        readonly property var point: diagram.coordinates[modelData]
                        readonly property real offsetX: Number((modelData === 10 ? diagram.live?.leftX : diagram.live?.rightX) || 0)
                        readonly property real offsetY: Number((modelData === 10 ? diagram.live?.leftY : diagram.live?.rightY) || 0)
                        width: 52 * shellSlot.unit
                        height: width
                        x: point[0] * shellSlot.unit - width / 2
                        y: point[1] * shellSlot.unit - height / 2
                        radius: width / 2
                        color: Theme.card3
                        border.color: Theme.stroke2
                        Rectangle {
                            width: 15 * shellSlot.unit
                            height: width
                            x: (parent.width - width) / 2 + 14 * shellSlot.unit * stick.offsetX
                            y: (parent.height - height) / 2 + 14 * shellSlot.unit * stick.offsetY
                            radius: width / 2
                            color: diagram.groupPressed(stick.modelData) ? Theme.ok : Theme.t2
                        }
                    }
                }
                Repeater {
                    model: diagram.coordinates
                    delegate: ConsoleGlyph {
                        required property int index
                        required property var modelData
                        visible: !(diagram.xbox && index === 14) && index !== 10 && index !== 11
                            && (index < 18 || diagram.selected === index || labels.pressed(diagram.live, index))
                        platform: diagram.platform
                        action: index
                        size: Math.max(10, Math.round(28 * shellSlot.unit))
                        visualState: labels.pressed(diagram.live, index) ? "pressed" : diagram.selected === index ? "selected" : "normal"
                        x: modelData[0] * shellSlot.unit - width / 2
                        y: modelData[1] * shellSlot.unit - height / 2
                    }
                }
                TapHandler {
                    enabled: diagram.editable
                    gesturePolicy: TapHandler.WithinBounds
                    onTapped: point => diagram.selectAt(point.position.x, point.position.y)
                }
                HoverHandler { enabled: diagram.editable; cursorShape: Qt.PointingHandCursor }
            }
            Column {
                id: right
                anchors.right: parent.right
                anchors.verticalCenter: parent.verticalCenter
                width: board.rail
                spacing: 1
                Repeater {
                    model: diagram.rightActions
                    delegate: DiagramTag {
                        required property int modelData
                        width: right.width
                        rightSide: true
                        action: modelData
                    }
                }
            }
        }
        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            spacing: 4
            Repeater {
                model: diagram.footerActions
                delegate: DiagramTag {
                    required property int modelData
                    width: Math.min(82, implicitWidth)
                    action: modelData
                }
            }
        }
    }
}
