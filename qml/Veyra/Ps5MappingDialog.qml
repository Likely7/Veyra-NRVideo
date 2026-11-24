// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: panel
    objectName: "ps5-mapping"
    key: "ps5-mapping"
    title: qsTr("PS5 键鼠映射")
    glyph: "gamepad"
    dialogWidth: 918
    minimumBodyWidth: 0
    actions: [{ label: qsTr("返回") }]
    property var input: typeof veyra !== "undefined" ? (veyra.ps5Input || null) : null
    property int selected: 0
    readonly property var pad: input ? input.padState : ({})
    readonly property var tune: input ? input.tuning : ({})
    readonly property var names: [
        "×", "○", "□", "△", qsTr("方向键 左"), qsTr("方向键 右"), qsTr("方向键 上"), qsTr("方向键 下"),
        "L1", "R1", "L3", "R3", qsTr("选项键"), qsTr("创建键"), qsTr("触摸板按下"), "PS", "L2", "R2",
        qsTr("左摇杆 上"), qsTr("左摇杆 下"), qsTr("左摇杆 左"), qsTr("左摇杆 右"),
        qsTr("右摇杆 上"), qsTr("右摇杆 下"), qsTr("右摇杆 左"), qsTr("右摇杆 右")
    ]

    onActionTriggered: dialogHost.open(dialogHost.ps5MappingReturn || "ps5")
    Connections {
        target: panel
        function onShownChanged() { if (!panel.shown && panel.input) panel.input.releasePs5() }
    }

    function binding(id) { return input && input.bindings[id] !== undefined ? input.bindings[id] : "—" }
    function choose(id) {
        selected = id
        if (input) input.learn(id)
    }
    function active(id) {
        if (id < 16) return (Number(pad.buttons || 0) & (1 << id)) !== 0
        if (id === 16 || id === 17) return Number(id === 16 ? pad.l2 : pad.r2) > 0
        const value = id < 22 ? ((id < 20) ? pad.leftY : pad.leftX) : ((id < 24) ? pad.rightY : pad.rightX)
        return Number(value || 0) * ((id === 18 || id === 20 || id === 22 || id === 24) ? -1 : 1) > 0.3
    }

    VRow {
        label: qsTr("控制方式")
        VSeg {
            objectName: "ps5-input-mode"
            enabled: panel.input !== null
            current: panel.input && panel.input.keyboardEnabled ? "keys" : "pad"
            options: [{ id: "pad", label: qsTr("实体手柄") }, { id: "keys", label: qsTr("键鼠映射") }]
            onPicked: id => { if (panel.input) panel.input.keyboardEnabled = id === "keys" }
        }
    }

    GridLayout {
        columns: panel.bodyWidth >= 810 ? 2 : 1
        columnSpacing: 14
        rowSpacing: 10
        Layout.fillWidth: true

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: 550
            spacing: 8
            Rectangle {
                id: board
                objectName: "ps5-controller-board"
                Layout.fillWidth: true
                implicitHeight: width * 306 / 560
                radius: 14
                color: Theme.card2
                border.color: Theme.stroke2
                readonly property real unit: width / 560
                Canvas {
                    anchors.fill: parent
                    onWidthChanged: requestPaint()
                    onHeightChanged: requestPaint()
                    onPaint: {
                        const ctx = getContext("2d")
                        ctx.reset()
                        ctx.clearRect(0, 0, width, height)
                        ctx.scale(board.unit, board.unit)
                        ctx.beginPath()
                        ctx.moveTo(280, 59)
                        ctx.bezierCurveTo(228, 59, 215, 59, 176, 56)
                        ctx.bezierCurveTo(145, 39, 105, 42, 82, 69)
                        ctx.bezierCurveTo(58, 91, 52, 143, 41, 192)
                        ctx.bezierCurveTo(27, 247, 44, 285, 69, 279)
                        ctx.bezierCurveTo(94, 273, 110, 239, 136, 212)
                        ctx.bezierCurveTo(153, 195, 177, 208, 198, 210)
                        ctx.quadraticCurveTo(280, 218, 362, 210)
                        ctx.bezierCurveTo(383, 208, 407, 195, 424, 212)
                        ctx.bezierCurveTo(450, 239, 466, 273, 491, 279)
                        ctx.bezierCurveTo(516, 285, 533, 247, 519, 192)
                        ctx.bezierCurveTo(508, 143, 502, 91, 478, 69)
                        ctx.bezierCurveTo(455, 42, 415, 39, 384, 56)
                        ctx.bezierCurveTo(345, 59, 332, 59, 280, 59)
                        ctx.closePath()
                        const shell = ctx.createLinearGradient(0, 45, 0, 282)
                        shell.addColorStop(0, "#e5e9f5")
                        shell.addColorStop(1, "#939eb9")
                        ctx.fillStyle = shell
                        ctx.fill()
                        ctx.lineWidth = 2
                        ctx.strokeStyle = "#f2f5ff"
                        ctx.stroke()
                        ctx.beginPath()
                        ctx.moveTo(210, 63)
                        ctx.quadraticCurveTo(219, 114, 177, 177)
                        ctx.lineTo(151, 204)
                        ctx.quadraticCurveTo(175, 201, 198, 212)
                        ctx.quadraticCurveTo(280, 223, 362, 212)
                        ctx.quadraticCurveTo(386, 201, 409, 204)
                        ctx.lineTo(383, 177)
                        ctx.quadraticCurveTo(341, 114, 350, 63)
                        ctx.closePath()
                        ctx.fillStyle = "#252c42"
                        ctx.fill()
                        ctx.strokeStyle = "#879bf0"
                        ctx.lineWidth = 1.6
                        ctx.stroke()
                    }
                }

                Repeater {
                    model: [{x:204, y:174, side:"left"}, {x:356, y:174, side:"right"}]
                    delegate: Rectangle {
                        required property var modelData
                        readonly property real dx: panel.pad[modelData.side + "X"] || 0
                        readonly property real dy: panel.pad[modelData.side + "Y"] || 0
                        x: (modelData.x - 26) * board.unit
                        y: (modelData.y - 26) * board.unit
                        width: 52 * board.unit
                        height: width
                        radius: width / 2
                        color: "#151c2e"
                        border.color: "#879bf0"
                        Rectangle {
                            x: (parent.width - width) / 2 + parent.dx * 14 * board.unit
                            y: (parent.height - height) / 2 + parent.dy * 14 * board.unit
                            width: 14 * board.unit
                            height: width
                            radius: width / 2
                            color: "#98b9ff"
                        }
                    }
                }

                Repeater {
                    model: [
                        [16,121,31,62,23,"L2"],[17,439,31,62,23,"R2"],
                        [8,121,60,60,23,"L1"],[9,439,60,60,23,"R1"],
                        [13,193,86,19,23,"↗"],[12,367,86,19,23,"≡"],[14,280,88,132,46,""],
                        [15,280,159,35,26,"PS"],
                        [6,123,97,28,26,"▲"],[4,92,125,28,26,"◀"],[5,154,125,28,26,"▶"],[7,123,153,28,26,"▼"],
                        [3,434,97,30,27,"△"],[2,402,125,30,27,"□"],[1,466,125,30,27,"○"],[0,434,153,30,27,"×"],
                        [10,204,174,26,23,"L3"],[11,356,174,26,23,"R3"],
                        [18,204,137,21,18,"↑"],[19,204,212,21,18,"↓"],[20,167,174,21,18,"←"],[21,241,174,21,18,"→"],
                        [22,356,137,21,18,"↑"],[23,356,212,21,18,"↓"],[24,319,174,21,18,"←"],[25,393,174,21,18,"→"]
                    ]
                    delegate: Rectangle {
                        id: keyCap
                        required property var modelData
                        readonly property int padKey: modelData[0]
                        readonly property bool marked: panel.selected === padKey
                        readonly property bool pressed: panel.active(padKey)
                        objectName: "ps5-bind-" + padKey
                        x: (modelData[1] - modelData[3] / 2) * board.unit
                        y: (modelData[2] - modelData[4] / 2) * board.unit
                        width: modelData[3] * board.unit
                        height: modelData[4] * board.unit
                        radius: padKey === 14 ? 8 : height * 0.44
                        color: pressed ? "#6ce3c7" : marked ? Theme.accent : (hover.hovered || activeFocus ? "#404f78" : "#252e47")
                        border.color: marked ? "#ffffff" : "#aab8d7"
                        border.width: marked || activeFocus ? 2 : 1
                        activeFocusOnTab: true
                        Accessible.role: Accessible.Button
                        Accessible.name: panel.names[padKey] + " " + panel.binding(padKey)
                        Accessible.onPressAction: panel.choose(padKey)
                        Keys.onReturnPressed: panel.choose(padKey)
                        Keys.onSpacePressed: panel.choose(padKey)
                        Text {
                            anchors.centerIn: parent
                            width: parent.width - 4
                            horizontalAlignment: Text.AlignHCenter
                            elide: Text.ElideRight
                            text: keyCap.padKey === 14 ? panel.binding(14) : keyCap.modelData[5]
                            font.pixelSize: Math.max(8, (keyCap.padKey < 4 ? 17 : 12) * board.unit)
                            font.bold: true
                            color: keyCap.pressed ? "#15242a" : keyCap.marked ? Theme.accentInk : "#f1f4ff"
                        }
                        ToolTip.visible: hover.hovered && !panel.input?.previewing
                        ToolTip.text: Accessible.name
                        ToolTip.delay: 200
                        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                        TapHandler { onTapped: panel.choose(keyCap.padKey) }
                    }
                }
            }

            VRow {
                label: panel.names[panel.selected]
                hint: panel.input && panel.input.learning >= 0 ? qsTr("按键、组合键或鼠标键 · Esc 取消") :
                      (panel.input && panel.input.feedback ? panel.input.feedback : qsTr("点击手柄上的按键开始绑定"))
                hintBelow: true
                RowLayout {
                    Text {
                        text: panel.binding(panel.selected)
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        Layout.maximumWidth: 115
                        elide: Text.ElideRight
                    }
                    VButton {
                        objectName: "ps5-bind-selected"
                        text: qsTr("改键")
                        enabled: panel.input !== null
                        onClicked: panel.choose(panel.selected)
                    }
                    VButton {
                        objectName: "ps5-clear-binding"
                        text: qsTr("清除")
                        ghost: true
                        enabled: panel.input !== null
                        onClicked: panel.input.clearBinding(panel.selected)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: panel.bodyWidth >= 810 ? 270 : panel.bodyWidth
            Layout.alignment: Qt.AlignTop
            spacing: 6
            VRow {
                label: qsTr("鼠标控右摇杆")
                VSwitch {
                    checked: panel.tune.mouseLook !== false
                    onToggled: on => panel.input?.setTuning("mouseLook", on)
                }
            }
            Repeater {
                model: [
                    {id:"horizontal", title:qsTr("横向灵敏度"), min:0.1, max:6, step:0.1, scale:1, initial:1, decimals:1, unit:"×"},
                    {id:"vertical", title:qsTr("纵向灵敏度"), min:0.1, max:6, step:0.1, scale:1, initial:1, decimals:1, unit:"×"},
                    {id:"deadzone", title:qsTr("死区补偿"), min:0, max:0.3, step:0.01, scale:100, initial:0.08, decimals:0, unit:"%"},
                    {id:"curve", title:qsTr("响应曲线"), min:0.5, max:2.5, step:0.05, scale:1, initial:1, decimals:2, unit:""},
                    {id:"smoothing", title:qsTr("平滑时间"), min:0, max:32, step:1, scale:1, initial:0, decimals:0, unit:" ms"}
                ]
                delegate: VRow {
                    required property var modelData
                    label: modelData.title
                    RowLayout {
                        VSlider {
                            objectName: "ps5-tune-" + modelData.id
                            Layout.preferredWidth: 91
                            Layout.minimumWidth: 50
                            from: modelData.min
                            to: modelData.max
                            stepSize: modelData.step
                            valueFromModel: true
                            value: Number(panel.tune[modelData.id] ?? modelData.initial)
                            defaultValue: modelData.initial
                            resettable: true
                            live: false
                            inputScale: modelData.scale
                            onMoved: amount => panel.input?.setTuning(modelData.id, amount)
                        }
                        Text {
                            Layout.preferredWidth: 48
                            horizontalAlignment: Text.AlignRight
                            text: (Number(panel.tune[modelData.id] ?? modelData.initial) * modelData.scale).toFixed(modelData.decimals) + modelData.unit
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSmall
                        }
                    }
                }
            }
            VRow {
                label: qsTr("反转纵向")
                VSwitch { checked: panel.tune.invertY === true; onToggled: on => panel.input?.setTuning("invertY", on) }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        VButton {
            objectName: "ps5-input-preview"
            text: panel.input && panel.input.previewing ? qsTr("预览中 · Esc 结束") : qsTr("本地输入预览")
            primary: true
            enabled: panel.input !== null
            onClicked: panel.input.preview(!panel.input.previewing)
        }
        VButton {
            objectName: "ps5-reset-bindings"
            text: qsTr("恢复默认")
            ghost: true
            enabled: panel.input !== null
            onClicked: panel.input.applyDefaults()
        }
        Item { Layout.fillWidth: true }
    }
}
