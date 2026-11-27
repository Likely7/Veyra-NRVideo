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
    // Physical controls, followed by the four left and four right stick directions.
    readonly property var locations: [
        [455,224], [500,176], [410,176], [455,128],
        xbox ? [166,232] : [62,176], xbox ? [258,232] : [154,176],
        xbox ? [212,184] : [108,128], xbox ? [212,280] : [108,224],
        [112,74], [448,74], xbox ? [117,166] : [205,242], [355,242],
        xbox ? [333,113] : [370,83], xbox ? [228,113] : [190,83], [280,78],
        [280,xbox ? 64 : 148], [112,25], [448,25],
        xbox ? [117,119] : [205,195], xbox ? [117,213] : [205,289],
        xbox ? [68,166] : [156,242], xbox ? [166,166] : [254,242],
        [355,195], [355,289], [306,242], [404,242]
    ]
    readonly property var shortNames: ({
        [qsTr("鼠标左键")]: "M1", [qsTr("鼠标中键")]: "M3", [qsTr("鼠标右键")]: "M2",
        [qsTr("鼠标侧键 1")]: "M4", [qsTr("鼠标侧键 2")]: "M5",
        [qsTr("滚轮向上")]: "W↑", [qsTr("滚轮向下")]: "W↓",
        [qsTr("滚轮向左")]: "W←", [qsTr("滚轮向右")]: "W→",
        "space": qsTr("空格"), "enter": "↵", "return": "↵", "backspace": "Bksp",
        "lshift": "L⇧", "rshift": "R⇧"
    })
    signal picked(int action)

    implicitWidth: 388
    implicitHeight: content.implicitHeight + 14
    radius: Theme.rCard
    color: Theme.card2
    border.color: Theme.stroke2

    ConsolePadLabels { id: labels; xbox: diagram.xbox }

    function shortBinding(value) {
        const keyStart = value.lastIndexOf("+") + 1
        const key = value.slice(keyStart)
        return value.slice(0, keyStart) + (shortNames[key] ?? shortNames[key.toLowerCase()] ?? key)
    }

    function nameOnBoard(action) {
        if (action >= 18) return ["↑", "↓", "←", "→"][(action - 18) % 4]
        if (action >= 4 && action <= 7) return ["←", "→", "↑", "↓"][action - 4]
        if (action === 12) return "≡"
        if (action === 13) return xbox ? "⧉" : "↗"
        if (action === 14) return qsTr("触摸板")
        return labels.names[action]
    }

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

    component OnPadTarget: Rectangle {
        id: target
        required property int action
        required property var point
        required property real unit
        readonly property bool ps5LearningControl: true
        readonly property bool pressed: labels.pressed(diagram.live, action)
        readonly property bool shoulder: action === 8 || action === 9 || action === 16 || action === 17
        readonly property bool stick: action === 10 || action === 11
        readonly property bool stickDirection: action >= 18
        readonly property bool touchpad: action === 14
        readonly property bool marked: action === diagram.selected || activeFocus
        readonly property string primaryBinding: labels.binding(diagram.input, diagram.driving, action)
        readonly property string alternateBinding: labels.binding(diagram.input, diagram.driving, action, 1)
        readonly property string primaryShort: diagram.shortBinding(primaryBinding)
        readonly property string alternateShort: diagram.shortBinding(alternateBinding)
        readonly property bool hasBoth: primaryBinding !== "—" && alternateBinding !== "—"
        readonly property string pair: primaryShort + "/" + alternateShort
        readonly property bool pairFits: hasBoth && valueMetrics.advanceWidth(pair) <= keyValue.width
        readonly property color labelInk: action < 4
            ? (diagram.xbox ? [Theme.padA, Theme.padB, Theme.padX, Theme.padY]
                            : [Theme.padCross, Theme.padCircle, Theme.padSquare, Theme.padTriangle])[action]
            : Theme.t1

        objectName: "controller-panel-diagram-key-" + action
        enabled: diagram.editable
        activeFocusOnTab: enabled
        x: Math.round(point[0] * unit - width / 2)
        y: Math.round(point[1] * unit - height / 2)
        width: Math.max(shoulder ? 42 : touchpad ? 54 : stickDirection ? 21 : 22,
            Math.round((shoulder ? 103 : touchpad ? 118 : stick ? 52
                : action === 13 || action === 15 ? 53 : action < 4 ? 50 : 44) * unit))
        height: Math.max(21, Math.round((stick ? 52 : touchpad ? 46 : stickDirection ? 39 : 44) * unit))
        radius: action < 4 || stick ? Math.min(width, height) / 2 : touchpad ? 7 : 5
        color: pressed ? Theme.ok : marked || pointer.hovered ? Theme.accentSoft : Theme.card3
        border.color: marked ? Theme.accent : Theme.stroke2
        border.width: marked ? 2 : 1

        Accessible.role: Accessible.Button
        Accessible.name: diagram.keyDescription(action)
        Accessible.description: stick ? (action === 10 ? qsTr("左摇杆") : qsTr("右摇杆")) : ""
        Accessible.onPressAction: if (enabled) diagram.picked(action)
        Keys.onReturnPressed: diagram.picked(action)
        Keys.onSpacePressed: diagram.picked(action)
        HoverHandler { id: pointer; cursorShape: Qt.PointingHandCursor }
        TapHandler { gesturePolicy: TapHandler.WithinBounds; onTapped: diagram.picked(target.action) }
        ToolTip.visible: pointer.hovered
        ToolTip.delay: 450
        ToolTip.text: Accessible.name

        Text {
            anchors.top: parent.top
            anchors.topMargin: 1
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 4
            height: Math.floor((parent.height - 2) / 2)
            text: diagram.nameOnBoard(target.action)
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
            color: target.pressed ? Theme.accentInk : target.labelInk
            font.family: Theme.fontUi
            font.pixelSize: target.action < 4 ? Theme.fsSmall : Theme.fsSmall - 1
            font.weight: Font.DemiBold
        }
        Text {
            id: keyValue
            objectName: "controller-panel-diagram-value-" + target.action
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 1
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width - 2
            height: Math.ceil((parent.height - 2) / 2)
            text: target.pairFits ? target.pair
                : target.primaryBinding === "—" ? target.alternateShort : target.primaryShort
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideLeft
            color: target.pressed ? Theme.accentInk : text === "—" ? Theme.t3 : Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall - 2
        }
        FontMetrics {
            id: valueMetrics
            font: keyValue.font
        }
        Rectangle {
            visible: target.hasBoth && !target.pairFits
            anchors.right: parent.right
            anchors.rightMargin: 3
            anchors.top: parent.top
            anchors.topMargin: 3
            width: 3
            height: 3
            radius: 2
            color: target.pressed ? Theme.accentInk : Theme.accent
        }
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
            ControllerPanelButton {
                readonly property bool ps5LearningControl: true
                readonly property string primaryBinding: labels.binding(diagram.input, diagram.driving, 32)
                readonly property string alternateBinding: labels.binding(diagram.input, diagram.driving, 32, 1)
                objectName: "controller-panel-diagram-key-32"
                enabled: diagram.editable
                text: {
                    const main = diagram.shortBinding(primaryBinding)
                    const backup = diagram.shortBinding(alternateBinding)
                    const shown = main === "—" ? backup : backup === "—" ? main : main + "/" + backup
                    return labels.names[32] + " · " + shown
                }
                maxTextWidth: 130
                tip: diagram.keyDescription(32)
                Accessible.name: tip
                primary: diagram.selected === 32
                onClicked: diagram.picked(32)
                fontSize: Theme.fsSmall
            }
        }

        Item {
            id: art
            objectName: "controller-panel-controller-art"
            Layout.preferredWidth: Math.min(356, Math.max(0, content.width - 28))
            Layout.alignment: Qt.AlignHCenter
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
                model: [10, 11]
                delegate: Rectangle {
                    required property int modelData
                    id: stickBase
                    readonly property var center: diagram.locations[modelData]
                    readonly property real dx: Number((modelData === 10 ? diagram.live?.leftX : diagram.live?.rightX) ?? 0)
                    readonly property real dy: Number((modelData === 10 ? diagram.live?.leftY : diagram.live?.rightY) ?? 0)
                    x: center[0] * art.unit - width / 2
                    y: center[1] * art.unit - height / 2
                    width: 113 * art.unit
                    height: width
                    radius: width / 2
                    color: Theme.card2
                    border.color: Theme.stroke2

                    Rectangle {
                        visible: Math.abs(stickBase.dx) > 0.01 || Math.abs(stickBase.dy) > 0.01
                        width: 5
                        height: width
                        radius: width / 2
                        color: Theme.ok
                        x: (parent.width - width) / 2 + stickBase.dx * parent.width * 0.39
                        y: (parent.height - height) / 2 + stickBase.dy * parent.height * 0.39
                    }
                }
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
    }
}
