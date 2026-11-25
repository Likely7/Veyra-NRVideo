// SPDX-License-Identifier: AGPL-3.0-only
// Adapted 2026-11-25 from the Stickmouse controller layout at cc2e75ff.
// See THIRD_PARTY_NOTICES.md and controller/LICENSE.
import QtQuick
import QtQuick.Controls.Basic
import "Ps5Bindings.js" as Bindings
import "controller/DualSense.js" as DualSense

Item {
    id: diagram
    required property var rows
    required property string selectedControl
    required property string unassigned
    required property real viewportHeight
    signal controlSelected(string control)
    property string hoveredControl: ""
    property string focusedControl: ""

    function trackHover(control, active) {
        if (active) hoveredControl = control
        else if (hoveredControl === control) hoveredControl = ""
    }

    function trackFocus(control, active) {
        if (active) focusedControl = control
        else if (focusedControl === control) focusedControl = ""
    }

    readonly property bool stacked: width < 820
    readonly property var leftControls: ["l2", "l1", "share", "dpad_up", "dpad_left",
                                        "dpad_right", "dpad_down", "left_stick", "l3"]
    readonly property var rightControls: ["r2", "r1", "options", "triangle", "square",
                                         "circle", "cross", "right_stick", "r3"]
    readonly property real gutter: stacked ? 10 : Math.min(48, width * 0.04)
    readonly property real sideWidth: (width - (stacked ? gutter : drawing.width + gutter * 2)) / 2
    readonly property real centerHeight: stacked ? drawing.height + centerGap + Math.max(touch.height, home.height)
                                                : touch.height + drawing.height + home.height + 2 * centerGap
    readonly property real centerGap: stacked ? 6 : 20
    readonly property real topMargin: 8
    implicitHeight: (stacked ? centerHeight + 12 + Math.max(left.height, right.height)
                             : Math.max(centerHeight, left.height, right.height)) + 2 * topMargin

    component Segment: Rectangle {
        required property real fromX
        required property real fromY
        required property real toX
        required property real toY
        x: fromX
        y: fromY - height / 2
        width: Math.hypot(toX - fromX, toY - fromY)
        height: 1
        transformOrigin: Item.Left
        rotation: Math.atan2(toY - fromY, toX - fromX) * 180 / Math.PI
        antialiasing: true
    }

    Image {
        id: drawing
        objectName: "controller-drawing"
        source: DualSense.source(diagram.selectedControl, diagram.hoveredControl, diagram.focusedControl,
                                 Theme.accent, Theme.accentInk)
        retainWhileLoading: true
        cache: false
        width: diagram.stacked ? Math.min(290, diagram.width * 0.50, Math.max(88, diagram.viewportHeight - 72) * 270 / 184)
                               : Math.min(375, diagram.width * 0.32)
        height: width * 184 / 270
        x: (diagram.width - width - (summary.visible ? summary.width + 24 : 0)) / 2
        y: diagram.stacked ? diagram.topMargin : touch.y + touch.height + diagram.centerGap
        sourceSize.width: Math.ceil(width * Screen.devicePixelRatio)
        sourceSize.height: Math.ceil(height * Screen.devicePixelRatio)
        smooth: true
    }

    // The compact viewport shows the selected binding beside its hotspot. The full
    // list below the drawing remains available when imported bindings span many lines.
    Segment {
        visible: summary.visible
        fromX: drawing.x + (summary.port ? summary.port[0] : 0) * drawing.width / 270
        fromY: drawing.y + (summary.port ? summary.port[1] : 0) * drawing.height / 184
        toX: summary.x
        toY: summary.y + summary.height / 2
        color: Theme.accent
    }
    Rectangle {
        id: summary
        objectName: "compact-controller-selection"
        readonly property var port: Bindings.controllerPorts[diagram.selectedControl]
                                    ? Bindings.controllerLeader(diagram.selectedControl, "right") : null
        readonly property var groups: visible ? Bindings.controllerGroups(diagram.rows, diagram.selectedControl) : []
        visible: diagram.stacked && !!port
        x: drawing.x + drawing.width + 24
        y: drawing.y + (drawing.height - height) / 2
        width: Math.max(1, Math.min(240, diagram.width - drawing.width - 24))
        height: Math.min(drawing.height, summaryName.height + summaryBindings.implicitHeight + 24)
        radius: 5
        color: Theme.card2
        border.color: Theme.accent
        Text {
            id: summaryName
            x: 12; y: 8
            width: parent.width - 2 * x
            text: summary.visible ? Bindings.controllerName(diagram.selectedControl) : ""
            color: Theme.accent
            font.family: Theme.fontMono
            font.pixelSize: 12
            font.weight: Font.DemiBold
            elide: Text.ElideRight
        }
        Text {
            id: summaryBindings
            objectName: "compact-control-bindings"
            x: 12; y: summaryName.y + summaryName.height + 4
            width: parent.width - 2 * x
            height: Math.max(0, parent.height - y - 8)
            text: summary.groups.length ? summary.groups.map(g => g.inputs.join(" · ") + "   " + g.action).join("\n")
                                        : diagram.unassigned
            textFormat: Text.PlainText
            wrapMode: Text.Wrap
            elide: Text.ElideRight
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: 13
        }
    }

    component Callout: Button {
        id: callout
        required property string control
        required property string side
        readonly property bool central: side === "center"
        readonly property bool leftAlignedToPad: side === "left"
        readonly property var port: Bindings.controllerLeader(control, side)
        readonly property var groups: Bindings.controllerGroups(diagram.rows, control)
        readonly property bool selected: diagram.selectedControl === control
        objectName: "callout-" + control
        text: Bindings.controllerName(control)
        Accessible.name: [text, groups.length ? groups.map(g => g.inputs.join(", ") + " · " + g.action).join(". ")
                                              : diagram.unassigned].join(" · ")
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        onHoveredChanged: diagram.trackHover(control, hovered)
        onVisualFocusChanged: diagram.trackFocus(control, visualFocus)
        padding: 0
        height: Math.max(30, central ? nameLabel.height + bindings.height + 6
                                     : Math.max(nameLabel.height, bindings.height) + 8)
        onClicked: diagram.controlSelected(control)
        background: Rectangle {
            color: callout.selected ? Theme.accentSoft : callout.hovered ? Theme.card3 : "transparent"
            radius: 5
            border.color: callout.visualFocus ? Theme.accent : "transparent"
        }
        contentItem: Item {
            Text {
                id: nameLabel
                x: callout.central ? (parent.width - width) / 2 : callout.leftAlignedToPad ? parent.width - width - 5 : 5
                y: callout.central ? 2 : (parent.height - height) / 2
                text: callout.text
                color: callout.selected ? Theme.accent : Theme.t1
                font.family: Theme.fontMono
                font.pixelSize: 12
                font.weight: Font.DemiBold
            }
            Column {
                id: bindings
                readonly property int alignment: callout.central ? Text.AlignHCenter :
                                                 callout.leftAlignedToPad ? Text.AlignRight : Text.AlignLeft
                x: callout.central ? 0 : callout.leftAlignedToPad ? 4 : nameLabel.x + nameLabel.width + 8
                y: callout.central ? nameLabel.height + 4 : (parent.height - height) / 2
                width: callout.central ? parent.width : parent.width - nameLabel.width - 17
                spacing: 3
                Text {
                    visible: callout.groups.length === 0
                    width: parent.width
                    text: diagram.unassigned
                    horizontalAlignment: bindings.alignment
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: 12
                }
                Repeater {
                    model: callout.groups
                    Item {
                        id: group
                        required property var modelData
                        width: parent.width
                        readonly property bool multiline: callout.central || keys.width + 6 + action.implicitWidth > width
                        readonly property real groupWidth: multiline ? Math.max(keys.width, action.width) : keys.width + 6 + action.width
                        readonly property real offset: callout.central ? (width - groupWidth) / 2 :
                                                       callout.leftAlignedToPad ? width - groupWidth : 0
                        height: multiline ? keys.height + action.height + 2 : Math.max(keys.height, action.height)
                        Rectangle {
                            id: keys
                            width: Math.min(group.width, keyText.implicitWidth + 10)
                            height: keyText.height + 3
                            x: group.multiline && callout.central ? (parent.width - width) / 2 :
                               group.multiline && callout.leftAlignedToPad ? parent.width - width : group.offset
                            y: group.multiline ? 0 : (parent.height - height) / 2
                            color: Theme.card3
                            border.color: Theme.stroke2
                            radius: 4
                            Text {
                                id: keyText
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, Math.max(1, group.width - 10))
                                text: group.modelData.inputs.join(" · ")
                                wrapMode: Text.Wrap
                                color: Theme.t1
                                font.family: Theme.fontMono
                                font.pixelSize: 12
                                font.weight: Font.DemiBold
                            }
                        }
                        Text {
                            id: action
                            width: Math.min(implicitWidth, group.width)
                            x: group.multiline ? (callout.central ? (parent.width - width) / 2 :
                                                callout.leftAlignedToPad ? parent.width - width : 0)
                                               : keys.x + keys.width + 6
                            y: group.multiline ? keys.height + 2 : (parent.height - height) / 2
                            text: group.modelData.action
                            wrapMode: Text.Wrap
                            horizontalAlignment: bindings.alignment
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                        }
                    }
                }
            }
            Rectangle {
                visible: !callout.central
                anchors.bottom: parent.bottom
                width: parent.width
                height: 1
                color: Theme.stroke
            }
        }

        // Local coordinates follow each callout as bindings wrap or the window resizes.
        Item {
            id: leader
            visible: !diagram.stacked
            readonly property real originX: callout.x + (callout.central ? 0 : callout.parent.x)
            readonly property real originY: callout.y + (callout.central ? 0 : callout.parent.y)
            readonly property real targetX: drawing.x + callout.port[0] * drawing.width / 270 - originX
            readonly property real targetY: drawing.y + callout.port[1] * drawing.height / 184 - originY
            readonly property real startX: callout.central ? targetX : callout.leftAlignedToPad ? callout.width : 0
            readonly property real startY: callout.central ? (callout.control === "touchpad" ? callout.height : 0) : callout.height / 2
            readonly property real elbowX: startX + (targetX - startX) * .36
            readonly property color lineColor: callout.selected ? Theme.accent : callout.groups.length ? Theme.t2 : Theme.t3
            Segment {
                fromX: leader.startX; fromY: leader.startY
                toX: leader.elbowX; toY: leader.startY
                height: callout.selected ? 2 : 1
                color: leader.lineColor
            }
            Segment {
                fromX: leader.elbowX; fromY: leader.startY
                toX: leader.targetX; toY: leader.targetY
                height: callout.selected ? 2 : 1
                color: leader.lineColor
            }
        }
    }

    component Side: Column {
        id: column
        required property string side
        width: diagram.sideWidth
        y: diagram.stacked ? diagram.topMargin + diagram.centerHeight + 12 : (diagram.height - height) / 2
        Repeater {
            model: column.side === "left" ? diagram.leftControls : diagram.rightControls
            Callout {
                required property string modelData
                control: modelData
                side: column.side
                width: column.width
            }
        }
    }
    Side { id: left; side: "left" }
    Side { id: right; side: "right"; x: diagram.width - width }
    Callout {
        id: touch
        control: "touchpad"
        side: "center"
        width: diagram.stacked ? diagram.sideWidth : drawing.width
        x: diagram.stacked ? 0 : drawing.x
        y: diagram.stacked ? drawing.y + drawing.height + diagram.centerGap : (diagram.height - diagram.centerHeight) / 2
    }
    Callout {
        id: home
        control: "ps"
        side: "center"
        width: diagram.stacked ? diagram.sideWidth : drawing.width
        x: diagram.stacked ? diagram.width - width : drawing.x
        y: drawing.y + drawing.height + diagram.centerGap
    }
    Repeater {
        model: Object.keys(Bindings.controllerPorts)
        Button {
            id: hotspot
            required property string modelData
            readonly property var port: Bindings.controllerPorts[modelData]
            readonly property bool selectedTile: diagram.selectedControl === modelData
            objectName: "pad-" + modelData
            x: drawing.x + (port[0] - port[2] / 2) * drawing.width / 270
            y: drawing.y + (port[1] - port[3] / 2) * drawing.height / 184
            width: port[2] * drawing.width / 270
            height: port[3] * drawing.height / 184
            containmentMask: QtObject {
                function contains(point: point): bool {
                    const x = (hotspot.x + point.x - drawing.x) * 270 / drawing.width
                    const y = (hotspot.y + point.y - drawing.y) * 184 / drawing.height
                    return DualSense.contains(hotspot.modelData, hotspot.port, x, y)
                }
            }
            padding: 0
            text: Bindings.controllerName(modelData)
            Accessible.name: text
            focusPolicy: Qt.ClickFocus
            hoverEnabled: true
            onHoveredChanged: diagram.trackHover(modelData, hovered)
            onVisualFocusChanged: diagram.trackFocus(modelData, visualFocus)
            ToolTip.visible: hovered || visualFocus
            ToolTip.text: text
            ToolTip.delay: 350
            contentItem: Item {}
            background: Item {}
            onClicked: diagram.controlSelected(modelData)
        }
    }
}
