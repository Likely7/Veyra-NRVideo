// SPDX-License-Identifier: GPL-3.0-only
pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

ColumnLayout {
    id: page
    objectName: advanced ? "controller-panel-advanced" : "controller-panel-feel"

    property var input: null
    property bool xbox: false
    property bool driving: false
    property bool advanced: false
    property bool splitAxes: false
    signal methodRequested()

    readonly property var tune: input?.tuning ?? ({})
    readonly property bool physicalAim: tune.physicalAim === true
    readonly property bool mouseLook: tune.mouseLook !== false
    readonly property bool keyboardEnabled: input?.keyboardEnabled === true
    readonly property bool showing: visible && Window.window?.visible === true
    readonly property bool liveInput: input?.previewing === true || input?.ps5Captured === true
        || input?.xboxCaptured === true
    readonly property var pad: showing ? input?.padState ?? ({}) : ({})
    readonly property real fullSpeed: physicalAim
        ? Number(tune.aimFullSpeed ?? 25) / Math.max(.1, Number(tune.aimHorizontal ?? 1))
        : Number(pad.legacyFullSpeed ?? 0)
    readonly property real stickMagnitude: Math.min(1, Math.hypot(pad.rightX ?? 0, pad.rightY ?? 0))
    readonly property int saturation: Math.round(pad.fullStickPct ?? 0)

    spacing: 12

    function requestReset() {
        if (input) dialogs.resetConfirm.open()
    }
    function cancelReset() {
        if (!dialogs.resetConfirm.opened) return false
        dialogs.resetConfirm.close()
        return true
    }

    ConsolePadLabels { id: labels; xbox: page.xbox }

    component Hint: Text {
        Layout.fillWidth: true
        Layout.minimumWidth: 0
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
        wrapMode: Text.Wrap
        color: Theme.t2
    }

    component FeelSwitch: VSwitch {
        id: control
        property string label: ""
        activeFocusOnTab: enabled
        Accessible.role: Accessible.CheckBox
        Accessible.name: label
        Accessible.checked: checked
        Accessible.onPressAction: if (enabled) toggled(!checked)
        border.width: activeFocus ? 2 : 0
        border.color: Theme.t1
        Keys.onPressed: event => {
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)
                    && !event.isAutoRepeat) {
                control.toggled(!control.checked)
                event.accepted = true
            }
        }
    }

    component ToggleRow: VRow {
        id: row
        required property string setting
        property bool initial: false
        enabled: page.input !== null
        FeelSwitch {
            label: row.label
            objectName: "controller-panel-tune-" + row.setting
            checked: (page.tune[row.setting] ?? row.initial) === true
            onToggled: on => page.input?.setTuning(row.setting, on)
        }
    }

    component FeelSection: VSubGroup {
        id: section
        Layout.fillWidth: true
        instantExpansion: true
        activeFocusOnTab: enabled
        Accessible.role: Accessible.Button
        Accessible.name: label
        Accessible.checkable: true
        Accessible.checked: expanded
        Accessible.onPressAction: if (enabled) expanded = !expanded
        border.width: activeFocus ? 1 : 0
        border.color: Theme.accent
        Keys.onPressed: event => {
            if ((event.key === Qt.Key_Return || event.key === Qt.Key_Enter || event.key === Qt.Key_Space)
                    && !event.isAutoRepeat) {
                section.expanded = !section.expanded
                event.accepted = true
            }
        }
    }

    component FeelSelect: Basic.ComboBox {
        id: choice
        property string label: ""
        property string selected: ""
        property string placeholder: ""
        property var options: []
        signal picked(string id)

        implicitWidth: 158
        implicitHeight: Math.max(Theme.ctlHeight, contentItem.implicitHeight + 8)
        textRole: "label"
        valueRole: "id"
        model: options
        currentIndex: options.findIndex(option => option.id === selected)
        displayText: currentIndex < 0 ? placeholder : currentText
        onActivated: index => {
            picked(options[index].id)
            // A one-shot action keeps its prompt. A rejected model change keeps
            // displaying the last saved value.
            currentIndex = Qt.binding(() => options.findIndex(option => option.id === selected))
        }
        Accessible.name: label

        background: Rectangle {
            radius: Theme.rCtl
            color: Theme.card2
            border.color: choice.activeFocus ? Theme.accent : Theme.stroke2
        }
        contentItem: Text {
            leftPadding: 10
            rightPadding: 24
            text: choice.displayText
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.Wrap
        }
        indicator: VIcon {
            x: choice.width - 20
            y: (choice.height - size) / 2
            name: "down"
            size: 14
            color: Theme.t2
        }
        delegate: Basic.ItemDelegate {
            id: option
            required property int index
            required property var modelData
            width: choice.width
            implicitHeight: Math.max(Theme.ctlHeight, contentItem.implicitHeight + 10)
            highlighted: choice.highlightedIndex === index
            background: Rectangle { radius: Theme.rCtl; color: option.highlighted ? Theme.card3 : Theme.card }
            contentItem: Text {
                text: option.modelData.label
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
            }
        }
        popup.background: Rectangle {
            radius: Theme.rCtl
            color: Theme.card
            border.color: Theme.stroke2
        }
    }

    component TuneSlider: VRow {
        id: sliderRow
        required property string setting
        property real minimum: 0
        required property real maximum
        required property real step
        required property real initial
        property real scaleFactor: 1
        property int decimals: 0
        property string unit: ""
        property bool axisControl: false
        property string alsoUpdate: ""
        readonly property real amount: Number(page.tune[setting] ?? initial)

        hintBelow: true
        enabled: page.mouseLook && page.input !== null
        RowLayout {
            spacing: 8
            VSlider {
                id: slider
                objectName: "controller-panel-tune-" + sliderRow.setting
                Layout.preferredWidth: Math.min(132, Math.max(92, page.width * .34))
                Layout.minimumWidth: 92
                from: sliderRow.minimum
                to: sliderRow.maximum
                stepSize: sliderRow.step
                valueFromModel: true
                value: sliderRow.amount
                defaultValue: sliderRow.initial
                resettable: true
                live: false
                enabledControl: sliderRow.enabled
                inputScale: sliderRow.scaleFactor
                inputDecimals: sliderRow.decimals
                Accessible.role: Accessible.Slider
                Accessible.name: sliderRow.label
                onMoved: value => {
                    page.input?.setTuning(sliderRow.setting, value)
                    if (sliderRow.alsoUpdate.length)
                        page.input?.setTuning(sliderRow.alsoUpdate, value)
                }
            }
            VSliderValue {
                Layout.minimumWidth: 48
                text: Number(slider.displayedValue * sliderRow.scaleFactor).toFixed(sliderRow.decimals)
                    + sliderRow.unit
                slider: slider
            }
            ControllerPanelButton {
                objectName: "controller-panel-axis-link"
                visible: sliderRow.axisControl
                icon: true
                iconName: page.splitAxes ? "unlink" : "split"
                tip: page.splitAxes ? qsTr("横纵联动") : qsTr("横纵分开")
                onClicked: page.splitAxes = !page.splitAxes
            }
        }
    }

    component TuneList: Repeater {
        delegate: TuneSlider {
            required property var modelData
            setting: modelData.id
            label: modelData.label
            hint: modelData.hint ?? ""
            minimum: modelData.min ?? 0
            maximum: modelData.max
            step: modelData.step
            initial: modelData.initial
            scaleFactor: modelData.scale ?? 1
            decimals: modelData.decimals ?? 0
            unit: modelData.unit ?? ""
            visible: !modelData.showWhen || page.tune[modelData.showWhen] === true
        }
    }

    component ResponseGraph: Canvas {
        id: graph
        property bool measured: false
        readonly property var settings: page.tune
        readonly property color graphFill: Theme.card2
        readonly property color graphLine: Theme.accent
        Layout.fillWidth: true
        Layout.preferredHeight: 64
        Accessible.role: Accessible.Graphic
        Accessible.name: qsTr("瞄准曲线")

        onSettingsChanged: if (visible) requestPaint()
        onGraphFillChanged: if (visible) requestPaint()
        onGraphLineChanged: if (visible) requestPaint()
        onWidthChanged: if (visible) requestPaint()
        onVisibleChanged: if (visible) requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            ctx.fillStyle = graphFill
            ctx.fillRect(0, 0, width, height)
            ctx.strokeStyle = graphLine
            ctx.lineWidth = 2
            ctx.beginPath()
            const left = 2
            const bottom = height - 2
            const graphWidth = width - 4
            const graphHeight = height - 4
            ctx.moveTo(left, bottom)
            if (measured) {
                const rates = settings.measuredRates ?? []
                if (rates.length !== 9) return
                const steps = [.02, .05, .10, .20, .35, .50, .65, .80, 1.]
                const deadzone = Number(settings.measuredDeadzone ?? 0)
                ctx.lineTo(left + graphWidth * deadzone, bottom)
                for (let index = 0; index < steps.length; ++index) {
                    const position = deadzone + (1 - deadzone) * steps[index]
                    ctx.lineTo(left + graphWidth * position, bottom - graphHeight * rates[index])
                }
            } else {
                for (let index = 1; index <= 100; ++index) {
                    const position = index / 100
                    const input = Math.max(0, position - (settings.inputDeadzone ?? 0))
                    const ramp = Math.min(1, Math.max(0, (input - .10) / .35))
                    const level = Math.min(1, input * (1 + ((settings.turnBoost ?? 1) - 1)
                        * ramp * ramp * (3 - 2 * ramp)))
                    const curve = level < .45 ? .45 * Math.pow(level / .45, settings.curve ?? 1) : level
                    const compensation = settings.deadzone ?? 0
                    const output = input > 0 ? compensation + (1 - compensation) * curve : 0
                    ctx.lineTo(left + graphWidth * position, bottom - graphHeight * output)
                }
            }
            ctx.stroke()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: !page.advanced
        spacing: 12

        ToggleRow {
            label: qsTr("鼠标控右摇杆")
            setting: "mouseLook"
            initial: true
        }

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Hint { text: qsTr("预设"); color: Theme.t1 }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Repeater {
                    model: [
                        {index: 1, label: qsTr("第三人称")},
                        {index: 0, label: qsTr("低死区 FPS")},
                        {index: 2, label: qsTr("线性")}
                    ]
                    delegate: ControllerPanelButton {
                        required property var modelData
                        objectName: "controller-panel-feel-template-" + modelData.index
                        text: modelData.label
                        enabled: page.mouseLook && page.input !== null
                        onClicked: page.input?.useTemplate(modelData.index)
                    }
                }
            }
        }

        VRow {
            label: qsTr("鼠标实际 DPI")
            hint: qsTr("与鼠标驱动的档位填同一个值。想调快慢，请调灵敏度。")
            hintBelow: true
            enabled: page.mouseLook && page.input !== null
            RowLayout {
                spacing: 6
                Basic.SpinBox {
                    id: dpi
                    objectName: "controller-panel-tune-dpi"
                    Layout.preferredWidth: 120
                    implicitHeight: Theme.ctlHeight
                    leftPadding: 26
                    rightPadding: 26
                    topPadding: 0
                    bottomPadding: 0
                    from: 200
                    to: 6400
                    stepSize: 50
                    editable: true
                    value: Math.round(Number(page.tune.dpi ?? 800))
                    Accessible.name: qsTr("鼠标实际 DPI")
                    onValueModified: page.input?.setTuning("dpi", value)
                    background: Rectangle {
                        radius: Theme.rCtl
                        color: Theme.card2
                        border.color: dpi.activeFocus ? Theme.accent : Theme.stroke2
                    }
                    contentItem: TextInput {
                        text: dpi.displayText
                        validator: dpi.validator
                        selectByMouse: true
                        inputMethodHints: Qt.ImhDigitsOnly
                        color: Theme.t1
                        selectedTextColor: Theme.accentInk
                        selectionColor: Theme.accent
                        horizontalAlignment: TextInput.AlignHCenter
                        verticalAlignment: TextInput.AlignVCenter
                        font.family: Theme.fontMono
                        font.pixelSize: Theme.fsBody
                    }
                    down.indicator: VIcon {
                        name: "minus"
                        size: 18
                        x: 6
                        y: (dpi.height - size) / 2
                        color: dpi.down.pressed ? Theme.accent : Theme.t1
                    }
                    up.indicator: VIcon {
                        name: "plus"
                        size: 18
                        x: dpi.width - size - 6
                        y: (dpi.height - size) / 2
                        color: dpi.up.pressed ? Theme.accent : Theme.t1
                    }
                }
                ControllerPanelButton {
                    objectName: "controller-panel-dpi-confirm"
                    visible: page.tune.dpiConfirmed === false
                    text: qsTr("确认")
                    onClicked: page.input?.setTuning("dpiConfirmed", true)
                }
            }
        }

        TuneSlider {
            setting: page.physicalAim ? "aimHorizontal" : "horizontal"
            label: page.splitAxes ? qsTr("横向灵敏度") : qsTr("灵敏度")
            minimum: .1; maximum: 6; step: page.physicalAim ? .05 : .1
            initial: 1; decimals: page.physicalAim ? 2 : 1; unit: "×"
            axisControl: true
            alsoUpdate: page.splitAxes ? "" : (page.physicalAim ? "aimVertical" : "vertical")
        }
        TuneSlider {
            visible: page.splitAxes
            setting: page.physicalAim ? "aimVertical" : "vertical"
            label: qsTr("纵向灵敏度")
            minimum: .1; maximum: 6; step: page.physicalAim ? .05 : .1
            initial: 1; decimals: page.physicalAim ? 2 : 1; unit: "×"
        }
        ToggleRow {
            label: qsTr("反转纵向")
            setting: "invertY"
            enabled: page.mouseLook && page.input !== null
        }

        Rectangle {
            Layout.fillWidth: true
            visible: page.mouseLook && page.fullSpeed > 0
            implicitHeight: readout.implicitHeight + 16
            radius: Theme.rCard
            color: Theme.card2
            border.color: Theme.stroke2
            RowLayout {
                id: readout
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 8
                spacing: 10
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4
                    Hint { text: qsTr("横向约 %1 cm/s 时推满右摇杆").arg(page.fullSpeed.toFixed(1)) }
                    Hint {
                        visible: page.liveInput
                        color: page.stickMagnitude > .98 ? Theme.warn : Theme.t1
                        text: qsTr("手速 %1 cm/s · 右摇杆 %2%")
                            .arg(Number(page.pad.speedCms ?? 0).toFixed(1)).arg(Math.round(100 * page.stickMagnitude))
                    }
                    Hint {
                        visible: page.liveInput && page.pad.timingReady === true
                        text: qsTr("满杆占比 %1%").arg(page.saturation)
                    }
                    Hint {
                        visible: page.liveInput && page.saturation > 30
                        color: Theme.warn
                        text: qsTr("右摇杆经常推满。可降低映射灵敏度，或调高游戏内灵敏度。")
                    }
                }
                Rectangle {
                    id: stick
                    objectName: "controller-panel-right-stick"
                    visible: page.liveInput
                    Layout.preferredWidth: 54
                    Layout.preferredHeight: 54
                    radius: width / 2
                    color: Theme.card3
                    border.color: Theme.stroke2
                    readonly property real xAxis: Math.max(-1, Math.min(1, page.pad.rightX ?? 0))
                    readonly property real yAxis: Math.max(-1, Math.min(1, page.pad.rightY ?? 0))
                    readonly property real travel: width / 2 - 7
                    Rectangle {
                        anchors.centerIn: parent
                        width: parent.width - 10; height: 1
                        color: Theme.stroke2
                    }
                    Rectangle {
                        anchors.centerIn: parent
                        width: 1; height: parent.height - 10
                        color: Theme.stroke2
                    }
                    Rectangle {
                        width: 7; height: 7; radius: width / 2
                        x: (stick.width - width) / 2 + stick.travel * stick.xAxis
                        y: (stick.height - height) / 2 + stick.travel * stick.yAxis
                        color: page.stickMagnitude > .98 ? Theme.warn : Theme.ok
                    }
                }
            }
        }

        FeelSection {
            objectName: "controller-panel-calibration"
            label: qsTr("游戏校准")
            visible: page.physicalAim
            VRow {
                id: deadzoneRow
                label: qsTr("测游戏死区")
                hint: qsTr("选择方向后回到游戏，镜头刚开始移动时按空格保存。")
                hintBelow: true
                enabled: page.keyboardEnabled
                FeelSelect {
                    label: deadzoneRow.label
                    placeholder: qsTr("选择")
                    options: [{id: "1", label: qsTr("横向")}, {id: "2", label: qsTr("纵向")}]
                    onPicked: id => page.input?.armAimTest(Number(id))
                }
            }
            VRow {
                label: qsTr("校准当前游戏")
                hint: qsTr("面向有纹理的静止场景。测试会自动转动镜头，约 20 秒。")
                hintBelow: true
                enabled: page.keyboardEnabled
                ControllerPanelButton { text: qsTr("开始"); onClicked: page.input?.armAimTest(3) }
            }
            Hint { visible: text.length > 0; text: page.input?.aimStatus ?? "" }
            ResponseGraph {
                measured: true
                visible: (page.tune.measuredRates ?? []).length === 9
            }
            ToggleRow {
                visible: Number(page.tune.measuredMax ?? 0) > 0
                label: qsTr("新版甩枪补偿")
                hint: qsTr("推满后最多补足 150 ms 的转动量，停手后镜头可能继续转动。")
                hintBelow: true
                setting: "angleCarry"
            }
        }

        FeelSection {
            objectName: "controller-panel-aim-and-movement"
            label: qsTr("开镜与移动")
            TuneList {
                model: [
                    {id: "adsScale", label: qsTr("开镜灵敏度"), min: .1, max: 3, step: .05, initial: 1, decimals: 2, unit: "×"},
                    {id: "adsDelayMs", label: qsTr("开镜延时"), max: 300, step: 1, initial: 0, unit: " ms"},
                    {id: "walkScale", label: qsTr("慢走"), min: .2, max: .8, step: .01, initial: .45, scale: 100, unit: "%"},
                    {id: "deadzoneShape", label: qsTr("方形死区比例"), max: 1, step: .01, initial: 0, scale: 100, unit: "%"}
                ]
            }
            VRow {
                id: adsRow
                label: qsTr("开镜触发键")
                enabled: page.mouseLook
                FeelSelect {
                    label: adsRow.label
                    options: labels.names.slice(0, 18).map((name, index) => ({id: String(index), label: name}))
                    selected: String(page.tune.adsAction ?? 16)
                    onPicked: id => page.input?.setTuning("adsAction", Number(id))
                }
            }
            VRow {
                id: gyroRow
                visible: !page.xbox
                enabled: page.mouseLook
                label: qsTr("鼠标陀螺仪")
                FeelSelect {
                    label: gyroRow.label
                    options: [{id: "0", label: qsTr("关闭")}, {id: "1", label: qsTr("始终开启")}, {id: "2", label: qsTr("仅开镜")}]
                    selected: String(page.tune.gyro ?? 0)
                    onPicked: id => page.input?.setTuning("gyro", Number(id))
                }
            }
        }

        FeelSection {
            objectName: "controller-panel-driving-feel"
            visible: page.driving
            label: qsTr("驾驶")
            ToggleRow { label: qsTr("分段转向"); setting: "graduatedSteering" }
            TuneList {
                model: [
                    {id: "steeringInitial", label: qsTr("初始转向"), min: .2, max: .6, step: .01,
                        initial: .4, scale: 100, unit: "%", showWhen: "graduatedSteering"},
                    {id: "steeringFullMs", label: qsTr("转向加满时间"), min: 80, max: 400, step: 5,
                        initial: 240, unit: " ms", showWhen: "graduatedSteering"},
                    {id: "steeringReturnMs", label: qsTr("转向回正时间"), min: 30, max: 200, step: 5,
                        initial: 60, unit: " ms", showWhen: "graduatedSteering"},
                    {id: "pedalRiseMs", label: qsTr("油门／刹车上升时间"), max: 300, step: 5, initial: 0, unit: " ms"}
                ]
            }
            ToggleRow { label: qsTr("驾驶时鼠标转向"); setting: "mouseSteering" }
            TuneSlider {
                setting: "mouseSteerGain"
                label: qsTr("鼠标转向灵敏度")
                visible: page.tune.mouseSteering === true
                minimum: .1; maximum: 6; step: .1; initial: 1; decimals: 1; unit: "×"
            }
            ToggleRow {
                label: qsTr("进出载具时切换布局")
                hint: qsTr("按下 %1 对应的键后延时切换。").arg(labels.names[3])
                hintBelow: true
                setting: "autoDriving"
            }
            TuneSlider {
                setting: "autoDriveMs"
                label: qsTr("进出载具切换延时")
                visible: page.tune.autoDriving === true
                minimum: 200; maximum: 3000; step: 50; initial: 1200; unit: " ms"
            }
        }

        FeelSection {
            objectName: "controller-panel-curve"
            label: qsTr("曲线（专家）")
            VRow {
                id: algorithmRow
                label: qsTr("瞄准算法")
                enabled: page.input !== null
                FeelSelect {
                    label: algorithmRow.label
                    options: [
                        {id: "physical", label: qsTr("游戏曲线补偿（推荐）")},
                        {id: "legacy", label: qsTr("旧算法（兼容）")}
                    ]
                    selected: page.physicalAim ? "physical" : "legacy"
                    onPicked: id => page.input?.selectAimAlgorithm(id === "physical")
                }
            }
            TuneList {
                model: page.physicalAim ? [
                    {id: "aimFullSpeed", label: qsTr("满杆手速"), min: .25, max: 150, step: .25, initial: 25, decimals: 1, unit: " cm/s"},
                    {id: "gameDeadzoneX", label: qsTr("游戏横向死区"), max: .5, step: .01, initial: .18, scale: 100, unit: "%"},
                    {id: "gameDeadzoneY", label: qsTr("游戏纵向死区"), max: .5, step: .01, initial: .18, scale: 100, unit: "%"},
                    {id: "gameCurve", label: qsTr("游戏响应曲线"), min: 1, max: 3, step: .05, initial: 2, decimals: 2}
                ] : [
                    {id: "turnSpeed", label: qsTr("满杆速度（800 DPI）"), min: 1000, max: 6000, step: 100, initial: 2400,
                        hint: qsTr("数值越小，越容易快速推满右摇杆。")},
                    {id: "turnBoost", label: qsTr("快速转身倍率"), min: 1, max: 6, step: .1, initial: 1, decimals: 1, unit: "×"},
                    {id: "curve", label: qsTr("瞄准曲线"), min: .3, max: 2.5, step: .05, initial: 1, decimals: 2},
                    {id: "inputDeadzone", label: qsTr("输入死区"), max: .1, step: .005, initial: 0, scale: 100, decimals: 1, unit: "%"},
                    {id: "deadzone", label: qsTr("死区补偿"), max: .3, step: .01, initial: .08, scale: 100, unit: "%"},
                    {id: "smoothing", label: qsTr("平滑时间"), max: 32, step: 1, initial: 0, unit: " ms"}
                ]
            }
            VRow {
                id: fpsRow
                visible: page.physicalAim
                enabled: page.mouseLook
                label: qsTr("游戏帧率")
                FeelSelect {
                    label: fpsRow.label
                    options: [{id: "0", label: qsTr("自动")}, {id: "30", label: "30 FPS"}, {id: "60", label: "60 FPS"}]
                    selected: String(page.tune.aimFps ?? 0)
                    onPicked: id => page.input?.setTuning("aimFps", Number(id))
                }
            }
            ToggleRow {
                visible: !page.physicalAim
                enabled: page.mouseLook && page.input !== null
                label: qsTr("快速转身补偿")
                setting: "carry"
            }
            ResponseGraph { visible: !page.physicalAim }
            Hint {
                visible: !page.physicalAim
                text: qsTr("曲线小于 1 提高低速响应；等于 1 为线性；大于 1 便于微调。")
            }
            Hint {
                visible: page.physicalAim
                text: qsTr("游戏内灵敏度决定转速上限。能调的话，降低游戏死区、关闭加速、选线性曲线。改动后请重新设置 Veyra 中的游戏死区和曲线。")
            }
        }

        ControllerPanelButton {
            objectName: "controller-panel-reset-feel"
            Layout.alignment: Qt.AlignRight
            text: qsTr("重置鼠标手感")
            enabled: page.input !== null
            ghost: true
            onClicked: page.requestReset()
        }
    }

    ColumnLayout {
        Layout.fillWidth: true
        visible: page.advanced
        spacing: 12
        ToggleRow {
            label: qsTr("键鼠控制时降低延迟")
            setting: "consoleLowLatency"
        }
        VRow {
            label: qsTr("测输入延迟")
            enabled: page.keyboardEnabled
            ControllerPanelButton { text: qsTr("开始"); onClicked: page.input?.armAimTest(4) }
        }
        Hint { visible: text.length > 0; text: page.input?.aimStatus ?? "" }
        Hint {
            visible: page.pad.timingReady === true
            text: qsTr("最近 10 秒 · 按键 P50/P95 %1 ms · 鼠标 P50/P95 %2 ms · 发送间隔 P95 %3 ms · 满杆 %4%")
                .arg(page.pad.keyCount ? Number(page.pad.keyP50).toFixed(1) + "/" + Number(page.pad.keyP95).toFixed(1) : "—")
                .arg(page.pad.mouseCount ? Number(page.pad.mouseP50).toFixed(1) + "/" + Number(page.pad.mouseP95).toFixed(1) : "—")
                .arg(Number(page.pad.sendP95 ?? 0).toFixed(1)).arg(page.saturation)
        }
        VRow {
            label: qsTr("同时转发实体手柄")
            ControllerPanelButton { text: qsTr("方式"); trailingIcon: "right"; onClicked: page.methodRequested() }
        }
        VRow {
            id: nativeXboxRow
            visible: page.xbox
            label: qsTr("Xbox 原生键鼠（实验）")
            hint: qsTr("向 Xbox 发送键鼠报告。家用串流是否接受取决于主机和游戏，无响应时关闭此项。")
            hintBelow: true
            enabled: page.input !== null
            FeelSwitch {
                label: nativeXboxRow.label
                checked: page.input?.nativeXbox === true
                onToggled: on => { if (page.input) page.input.nativeXbox = on }
            }
        }
        VRow {
            id: unlockRow
            label: qsTr("释放键鼠的快捷键")
            hint: (page.input?.learning ?? -1) < 0 ? (page.input?.feedback ?? "") : ""
            hintBelow: true
            enabled: page.input !== null
            FeelSelect {
                objectName: "controller-panel-unlock-choice"
                label: unlockRow.label
                options: ["Esc", "Ctrl+Alt+Z", "Alt+Q", "F1", "~"].map((name, index) => ({id: String(index), label: name}))
                selected: String(page.tune.unlock ?? 0)
                onPicked: id => page.input?.setTuning("unlock", Number(id))
            }
        }
    }

    QtObject {
        id: dialogs
        readonly property Popup resetConfirm: Popup {
            objectName: "controller-panel-reset-feel-dialog"
            parent: page.Overlay.overlay
            anchors.centerIn: parent
            width: Math.min(340, Math.max(1, (parent?.width ?? 372) - 32))
            padding: 16
            modal: true
            focus: true
            closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
            onOpened: keepSettings.forceActiveFocus()
            background: Rectangle { radius: Theme.rCard; color: Theme.card; border.color: Theme.stroke2 }
            contentItem: ColumnLayout {
                spacing: 12
                Hint { text: qsTr("恢复当前档案的默认鼠标手感？"); color: Theme.t1; font.pixelSize: Theme.fsBody }
                RowLayout {
                    Layout.fillWidth: true
                    Item { Layout.fillWidth: true }
                    ControllerPanelButton { id: keepSettings; text: qsTr("取消"); onClicked: dialogs.resetConfirm.close() }
                    ControllerPanelButton {
                        objectName: "controller-panel-reset-feel-confirm"
                        text: qsTr("确定")
                        primary: true
                        onClicked: {
                            dialogs.resetConfirm.close()
                            page.input?.resetMouseTuning()
                        }
                    }
                }
            }
        }
    }
}
