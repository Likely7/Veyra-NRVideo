// SPDX-License-Identifier: GPL-3.0-only
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Window

Rectangle {
    id: diagram
    objectName: "controller-panel-diagram"
    property string platform: "ps5"
    property bool driving: false
    property int selected: -1
    property var input: null
    property var live: ({})
    property bool editable: true
    readonly property bool xbox: platform === "xbox"
    readonly property var locations: [
        [442, 236], [495, 184], [389, 184], [442, 135],
        xbox ? [161, 219] : [64, 183], xbox ? [267, 219] : [172, 183],
        xbox ? [214, 175] : [118, 135], xbox ? [214, 273] : [118, 236],
        [110, 81], [450, 81],
        xbox ? [112, 141] : [228, 242], xbox ? [333, 235] : [333, 242],
        xbox ? [331, 120] : [373, 84], xbox ? [229, 120] : [187, 84],
        [280, 82], xbox ? [280, 61] : [280, 154], [110, 27], [450, 27]
    ]
    readonly property var extraActions: (
        xbox ? [26, 27] : [26, 27, 28, 29, 30, 31]
    ).concat(driving ? [33] : [])
    signal picked(int action)

    implicitWidth: 388
    implicitHeight: content.implicitHeight + 14
    radius: Theme.rCard
    color: Theme.card2
    border.color: Theme.stroke2

    ConsolePadLabels { id: labels; xbox: diagram.xbox }

    function keyDescription(action) {
        return qsTr("%1，主键 %2，备用键 %3").arg(labels.names[action])
            .arg(labels.binding(input, driving, action))
            .arg(labels.binding(input, driving, action, 1))
    }

    function silhouette() {
        const path = xbox
            ? "M280,53 C203,53 119,36 77,71 C44,99 27,228 41,277 C56,316 99,309 121,275 C145,241 176,262 209,276 Q280,290 351,276 C384,262 415,241 439,275 C461,309 504,316 519,277 C533,228 516,99 483,71 C441,36 357,53 280,53 Z"
            : "M280,56 C228,56 218,60 177,54 C145,40 100,43 76,68 C51,95 39,147 31,205 C20,256 39,307 70,304 C99,300 115,260 138,235 C159,214 180,243 207,259 Q280,279 353,259 C380,243 401,214 422,235 C445,260 461,300 490,304 C521,307 540,256 529,205 C521,147 509,95 484,68 C460,43 415,40 383,54 C342,60 332,56 280,56 Z"
        const edge = Theme.t3
        const stroke = "rgb(" + Math.round(edge.r * 255) + "," + Math.round(edge.g * 255) + "," + Math.round(edge.b * 255) + ")"
        const svg = '<svg xmlns="http://www.w3.org/2000/svg" width="560" height="320" viewBox="0 0 560 320">'
            + '<path d="' + path + '" fill="' + Theme.card + '" stroke="' + stroke
            + '" stroke-opacity="' + edge.a + '" stroke-width="3"/></svg>'
        return "data:image/svg+xml;charset=utf-8," + encodeURIComponent(svg)
    }

    component OnPadTarget: Item {
        id: target
        required property int action
        required property var point
        required property real unit
        readonly property bool ps5LearningControl: true
        readonly property bool pressed: labels.pressed(diagram.live, action)
        readonly property bool shoulder: action === 8 || action === 9 || action === 16 || action === 17
        readonly property bool stick: action === 10 || action === 11
        readonly property bool direction: action >= 4 && action <= 7
        readonly property bool touchpad: action === 14
        readonly property bool marked: action === diagram.selected || activeFocus || pointer.hovered
        readonly property color edge: marked ? Theme.accent : pressed ? Theme.ok : Theme.stroke2

        objectName: "controller-panel-diagram-key-" + action
        enabled: diagram.editable
        activeFocusOnTab: enabled
        x: Math.round(point[0] * unit - width / 2)
        y: Math.round(point[1] * unit - height / 2)
        width: shoulder ? Math.max(44, Math.round(100 * unit))
            : touchpad ? Math.max(48, Math.round(124 * unit))
            : Math.max(30, Math.round((stick ? 58 : 46) * unit))
        height: Math.max(30, Math.round((stick ? 58 : touchpad ? 54 : 44) * unit))

        Accessible.role: Accessible.Button
        Accessible.name: diagram.keyDescription(action)
        Accessible.onPressAction: if (enabled) diagram.picked(action)
        Keys.onReturnPressed: diagram.picked(action)
        Keys.onSpacePressed: diagram.picked(action)
        HoverHandler { id: pointer; cursorShape: Qt.PointingHandCursor }
        TapHandler { gesturePolicy: TapHandler.WithinBounds; onTapped: diagram.picked(target.action) }
        ToolTip.visible: pointer.hovered
        ToolTip.delay: 450
        ToolTip.text: Accessible.name

        ConsoleGlyph {
            anchors.fill: parent
            visible: !target.stick && !target.direction && !target.touchpad
            platform: diagram.platform
            action: target.action
            visualState: target.pressed ? "pressed" : target.marked ? "selected" : "normal"
        }
        Rectangle {
            anchors.fill: parent
            visible: target.stick || target.direction || target.touchpad
            radius: target.stick ? height / 2 : 7
            color: target.pressed ? Theme.ok : target.marked ? Theme.accentSoft : Theme.card3
            border.color: target.edge
            border.width: target.marked ? 2 : 1
        }
        Rectangle {
            readonly property bool leftStick: target.action === 10
            readonly property real dx: Number((leftStick ? diagram.live?.leftX : diagram.live?.rightX) ?? 0)
            readonly property real dy: Number((leftStick ? diagram.live?.leftY : diagram.live?.rightY) ?? 0)
            visible: target.stick && (Math.abs(dx) > 0.01 || Math.abs(dy) > 0.01)
            width: 6
            height: width
            x: (parent.width - width) / 2 + Math.max(-1, Math.min(1, dx)) * parent.width * 0.29
            y: (parent.height - height) / 2 + Math.max(-1, Math.min(1, dy)) * parent.height * 0.29
            radius: width / 2
            color: target.pressed ? Theme.accentInk : Theme.accent
        }
        Text {
            anchors.centerIn: parent
            width: Math.max(0, parent.width - 8)
            visible: target.direction || target.touchpad || target.stick
            text: target.direction ? ["←", "→", "↑", "↓"][target.action - 4]
                : target.touchpad ? qsTr("触摸板") : labels.names[target.action]
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: target.pressed ? Theme.accentInk : Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: target.direction ? Theme.fsH3 : Theme.fsSmall
            font.weight: Font.DemiBold
        }
    }

    component ActionTarget: ControllerPanelButton {
        id: button
        required property int action
        readonly property bool ps5LearningControl: true
        objectName: "controller-panel-diagram-key-" + action
        enabled: diagram.editable
        text: labels.names[action]
        tip: diagram.keyDescription(action)
        Accessible.name: tip
        primary: diagram.selected === action
        onClicked: diagram.picked(action)
        fontSize: Theme.fsSmall
    }

    ColumnLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 7
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            Text {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                text: qsTr("点手柄上的按键即可录入主键。")
                wrapMode: Text.Wrap
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
            }
            ActionTarget {
                action: 32
                iconName: "gamepad"
            }
        }

        Item {
            id: art
            objectName: "controller-panel-controller-art"
            Layout.fillWidth: true
            implicitHeight: Math.round(320 * unit)
            readonly property real unit: width / 560
            // A cached image respects the panel's scroll clip in the software renderer.
            Image {
                objectName: "controller-panel-silhouette"
                anchors.fill: parent
                cache: true
                smooth: true
                sourceSize.width: Math.max(1, Math.ceil(width * Screen.devicePixelRatio))
                sourceSize.height: Math.max(1, Math.ceil(height * Screen.devicePixelRatio))
                source: diagram.silhouette()
            }
            Rectangle {
                x: diagram.locations[4][0] * art.unit
                y: diagram.locations[4][1] * art.unit - height / 2
                width: (diagram.locations[5][0] - diagram.locations[4][0]) * art.unit
                height: 24 * art.unit
                color: Theme.card3
                border.color: Theme.stroke2
                radius: height / 2
            }
            Rectangle {
                x: diagram.locations[6][0] * art.unit - width / 2
                y: diagram.locations[6][1] * art.unit
                width: 24 * art.unit
                height: (diagram.locations[7][1] - diagram.locations[6][1]) * art.unit
                color: Theme.card3
                border.color: Theme.stroke2
                radius: width / 2
            }
            Repeater {
                model: diagram.locations.length
                delegate: OnPadTarget {
                    required property int index
                    action: index
                    point: diagram.locations[index]
                    unit: art.unit
                    visible: !diagram.xbox || index !== 14
                }
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            Repeater {
                model: [{ label: qsTr("左摇杆"), first: 18 }, { label: qsTr("右摇杆"), first: 22 }]
                delegate: ColumnLayout {
                    id: axes
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 3
                    Text {
                        Layout.fillWidth: true
                        text: axes.modelData.label
                        wrapMode: Text.Wrap
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSmall
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 3
                        Repeater {
                            model: ["↑", "↓", "←", "→"]
                            delegate: ActionTarget {
                                required property int index
                                required property string modelData
                                action: axes.modelData.first + index
                                text: modelData
                                Layout.fillWidth: true
                                Layout.minimumWidth: 30
                            }
                        }
                    }
                }
            }
        }
        GridLayout {
            Layout.fillWidth: true
            columns: 2
            columnSpacing: 5
            rowSpacing: 3
            Repeater {
                model: diagram.extraActions
                delegate: ActionTarget {
                    required property int modelData
                    action: modelData
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                }
            }
        }
    }
}
