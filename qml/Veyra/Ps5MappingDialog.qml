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
    dialogWidth: 992
    minimumBodyWidth: 0
    actions: [{ label: qsTr("返回") }]
    property var videoControls: typeof veyra !== "undefined" ? veyra : null
    property var input: videoControls?.ps5Input || null
    property int selected: -1
    property bool showList: panel.bodyWidth>0 && panel.bodyWidth<500
    property bool confirmReset: false
    property bool advanced: false
    property bool editDriving: false
    onEditDrivingChanged: { if (panel.input) panel.input.cancelLearning(); panel.confirmReset = false }
    onShownChanged: if (shown) editDriving = input?.driving === true
    Connections {
        target: panel.input
        ignoreUnknownSignals: true
        function onDrivingChanged() { panel.editDriving = panel.input.driving }
    }
    Timer { interval: 3000; running: panel.confirmReset; onTriggered: panel.confirmReset=false }
    readonly property var pad: input ? input.padState : ({})
    readonly property var tune: input ? input.tuning : ({})
    readonly property var names: [
        "×", "○", "□", "△", qsTr("方向键 左"), qsTr("方向键 右"), qsTr("方向键 上"), qsTr("方向键 下"),
        "L1", "R1", "L3", "R3", qsTr("选项键"), qsTr("创建键"), qsTr("触摸板按下"), "PS", "L2", "R2",
        qsTr("左摇杆 上"), qsTr("左摇杆 下"), qsTr("左摇杆 左"), qsTr("左摇杆 右"),
        qsTr("右摇杆 上"), qsTr("右摇杆 下"), qsTr("右摇杆 左"), qsTr("右摇杆 右"), qsTr("慢走"), qsTr("菜单摇杆"), qsTr("触摸板上滑"), qsTr("触摸板下滑"), qsTr("触摸板左滑"), qsTr("触摸板右滑")
    ]

    onActionTriggered: dialogHost.ps5MappingReturn ? dialogHost.open(dialogHost.ps5MappingReturn) : dialogHost.close()
    Connections {
        target: panel
        function onShownChanged() { if (!panel.shown && panel.input) panel.input.releasePs5() }
    }

    function binding(id) {
        const keys = editDriving ? input?.drivingBindings : input?.bindings
        return keys?.[id] ?? "—"
    }
    function shortBinding(id) {
        const value=binding(id)
        if(value===qsTr("鼠标左键"))return "M1"
        if(value===qsTr("鼠标右键"))return "M2"
        if(value===qsTr("鼠标中键"))return "M3"
        if(value==="Backspace")return "Bksp"
        if(value==="Enter")return "↵"
        if(value==="Space")return "␣"
        return value
    }
    function choose(id) {
        if (!input || input.previewing) return
        selected = id
        if (input.learning !== id) input.learn(id, panel, panel.editDriving)
    }
    function active(id) {
        if(id>=26)return false
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

    VSubGroup {
        Layout.fillWidth: true
        label: qsTr("游戏档案") + " · " + (panel.input?.profile || "Default")
        VRow { label: qsTr("游戏档案")
            RowLayout {
                VSelect { title: qsTr("游戏档案"); value: panel.input?.profile || "Default"
                    options: (panel.input?.profiles || []).map(n => ({id:n,label:n}))
                    onPicked: id => panel.input.selectProfile(id,false)
                }
                VButton {
                    objectName: "ps5-gta-v-profile"
                    text: "GTA V"
                    enabled: panel.input !== null && panel.input.profile !== "GTA V"
                    onClicked: panel.input.useGtaVProfile()
                }
            }
        }
        VRow { label: qsTr("新建档案")
            RowLayout {
                TextField { id: profileName; Layout.preferredWidth: 100; maximumLength: 40; selectByMouse: true }
                VButton { text: qsTr("新建"); enabled: profileName.text.trim().length>0
                    onClicked: {panel.input.selectProfile(profileName.text,true);profileName.clear()} }
            }
        }
        VRow { label: qsTr("手感模板")
            VSelect { title: qsTr("手感模板"); value: qsTr("选择")
                options: [{id:"0",label:qsTr("低死区 FPS")},{id:"1",label:qsTr("第三人称")},{id:"2",label:qsTr("线性")}]
                onPicked: id => panel.input.useTemplate(Number(id))
            }
        }
    }
    Text { visible: !panel.input?.keyboardEnabled; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.t3
        text: qsTr("当前使用实体手柄。切到键鼠映射后，点击画面即可接管。") }
    VRow {
        visible: panel.videoControls !== null && (!panel.videoControls.hasSource ||
                  panel.videoControls.sourceKind === "ps5" || panel.videoControls.sourceKind === "capture")
        label: qsTr("跟手优先")
        hint: qsTr("保留画质增强和补帧，缩短增强后的原帧等待。")
        hintBelow: true
        VSwitch {
            objectName: "ps5-responsive-video"
            checked: panel.videoControls?.ps5ResponsePacing === true
            onToggled: on => panel.videoControls.ps5ResponsePacing = on
        }
    }
    VRow {
        label: qsTr("键位布局")
        VSeg {
            objectName: "ps5-mapping-layout"
            readonly property bool ps5LearningControl: true
            current: panel.editDriving ? "driving" : "walking"
            options: [{id:"walking", label:qsTr("步行")}, {id:"driving", label:qsTr("驾驶")}]
            onPicked: id => {
                panel.editDriving = id === "driving"
                if (panel.input?.previewing) panel.input.selectDriving(panel.editDriving)
            }
        }
    }
    VRow {
        label: qsTr("切换步行／驾驶")
        hint: panel.input?.learning === panel.names.length
              ? (panel.input.feedback || qsTr("正在录键"))
              : qsTr("锁定键鼠后即可切换。默认 W（R2）加速、S（L2）刹车／倒车，A/D 转向。")
        hintBelow: true
        RowLayout {
            VButton {
                id: driveKeyButton
                objectName: "ps5-drive-key"
                text: panel.input?.learning === panel.names.length ? qsTr("取消") : (panel.input?.driveKeyLabel ?? "F6")
                primary: panel.input?.learning === panel.names.length
                enabled: panel.input !== null
                onClicked: {
                    if (panel.input.learning === panel.names.length) panel.input.cancelLearning()
                    else panel.input.learnDriveKey(panel)
                }
            }
            VButton {
                objectName: "ps5-input-preview"
                text: panel.input && panel.input.previewing ? qsTr("本地输入预览")+" · "+(panel.input?.unlockLabel||"Esc") : qsTr("本地输入预览")
                primary: true
                enabled: panel.input !== null
                onClicked: {
                    if (panel.input.previewing) panel.input.preview(false)
                    else {
                        panel.input.selectDriving(panel.editDriving)
                        panel.input.preview(true)
                    }
                }
            }
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
            VSeg {
                readonly property bool ps5LearningControl: true
                current: panel.showList?"list":"diagram"
                options: [{id:"diagram",label:qsTr("手柄图示")},{id:"list",label:qsTr("键位列表")}]
                onPicked: id => {
                    panel.input?.cancelLearning()
                    panel.showList = id === "list"
                }
            }
            GridLayout {
                visible: panel.showList; Layout.fillWidth: true; columns: panel.bodyWidth<340?1:2
                Repeater { model: panel.names
                    VButton {
                        required property int index; required property string modelData
                        objectName: "ps5-list-bind-" + index
                        text: modelData+" · "+panel.binding(index); Layout.fillWidth: true
                        primary: panel.selected===index; onClicked: panel.choose(index) }
                }
            }
            Rectangle {
                visible: !panel.showList
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
                        readonly property bool ps5LearningControl: true
                        opacity: panel.input?.previewing && (padKey===10||padKey===11) ? .25 : 1
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
                            text: (keyCap.padKey===14?qsTr("触摸板按下"):keyCap.modelData[5])+"\n"+panel.shortBinding(keyCap.padKey)
                            lineHeight: .88
                            minimumPixelSize: 6; fontSizeMode: Text.Fit
                            height: parent.height-2; verticalAlignment: Text.AlignVCenter
                            font.pixelSize: Math.max(8, (keyCap.padKey < 4 ? 17 : 12) * board.unit)
                            font.bold: true
                            color: keyCap.pressed ? "#15242a" : keyCap.marked ? Theme.accentInk : "#f1f4ff"
                        }
                        ToolTip.visible: hover.hovered && !panel.input?.previewing
                        ToolTip.text: Accessible.name
                        ToolTip.delay: 200
                        HoverHandler { id: hover; cursorShape: Qt.PointingHandCursor }
                        TapHandler {
                            gesturePolicy: TapHandler.WithinBounds
                            onTapped: panel.choose(keyCap.padKey)
                        }
                    }
                }
            }

            VRow {
                visible: [1,10,16,27].includes(panel.selected) && !(panel.editDriving && panel.selected === 16)
                label: qsTr("切换模式")
                VSwitch { checked: { const b=panel.input?.bindings;return panel.input?.toggleAction?.(panel.selected) || false }
                    onToggled: on => panel.input.setToggleAction(panel.selected,on) }
            }
            VRow {
                label: panel.selected < 0 ? "" : ((panel.input?.learning === panel.selected)
                    ? qsTr("正在录键") + " · " + panel.names[panel.selected] : panel.names[panel.selected])
                hint: panel.input?.feedback || (panel.input?.learning === panel.selected && panel.selected >= 0
                    ? qsTr("按下键盘或鼠标键。点击另一键位可直接切换。")
                    : qsTr("点击任意手柄按键开始改键。"))
                hintBelow: true
                RowLayout {
                    Text {
                        visible: panel.selected >= 0
                        text: panel.binding(panel.selected)
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        Layout.maximumWidth: 115
                        elide: Text.ElideRight
                    }
                    VButton {
                        objectName: "ps5-cancel-binding"
                        visible: panel.selected >= 0 && panel.input?.learning === panel.selected
                        text: qsTr("取消")
                        onClicked: panel.input.cancelLearning()
                    }
                    VButton {
                        objectName: "ps5-clear-binding"
                        visible: panel.selected >= 0
                        text: qsTr("清除")
                        ghost: true
                        enabled: panel.input !== null
                        onClicked: panel.input.clearBinding(panel.selected, panel.editDriving)
                    }
                }
            }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.preferredWidth: panel.bodyWidth >= 810 ? 355 : panel.bodyWidth
            Layout.alignment: Qt.AlignTop
            spacing: 6
            VRow {
                label: qsTr("鼠标控右摇杆")
                VSwitch {
                    checked: panel.tune.mouseLook !== false
                    onToggled: on => panel.input?.setTuning("mouseLook", on)
                }
            }
            VSeg { current: panel.advanced?"advanced":"basic"
                options: [{id:"basic",label:qsTr("基础")},{id:"advanced",label:qsTr("进阶")}]
                onPicked: id => panel.advanced=id==="advanced"
            }
            Repeater {
                model: [
                    {id:"dpi",title:qsTr("鼠标实际 DPI"),min:200,max:6400,step:1,scale:1,initial:800,decimals:0,unit:""},
                    {id:"adsScale",title:qsTr("开镜灵敏度"),min:.1,max:3,step:.05,scale:1,initial:1,decimals:2,unit:"×"},
                    {id:"adsDelayMs",title:qsTr("开镜延时"),min:0,max:300,step:1,scale:1,initial:0,decimals:0,unit:" ms"},
                    {id:"walkScale",title:qsTr("慢走"),min:.2,max:.8,step:.01,scale:100,initial:.45,decimals:0,unit:"%"},
                    {id:"deadzoneShape",title:qsTr("方形死区比例"),min:0,max:1,step:.01,scale:100,initial:0,decimals:0,unit:"%"},
                    {id:"horizontal", title:qsTr("横向灵敏度"), min:0.1, max:6, step:0.1, scale:1, initial:1, decimals:1, unit:"×"},
                    {id:"vertical", title:qsTr("纵向灵敏度"), min:0.1, max:6, step:0.1, scale:1, initial:1, decimals:1, unit:"×"},
                    {id:"turnSpeed", title:qsTr("满杆速度（800 DPI）"), min:1000, max:6000, step:100, scale:1, initial:2400, decimals:0, unit:""},
                    {id:"turnBoost", title:qsTr("快速转身倍率"), min:1, max:6, step:0.1, scale:1, initial:1, decimals:1, unit:"×"},
                    {id:"curve", title:qsTr("瞄准曲线"), min:0.3, max:2.5, step:0.05, scale:1, initial:1, decimals:2, unit:""},
                    {id:"inputDeadzone", title:qsTr("输入死区"), min:0, max:0.1, step:0.005, scale:100, initial:0, decimals:1, unit:"%"},
                    {id:"deadzone", title:qsTr("死区补偿"), min:0, max:0.3, step:0.01, scale:100, initial:.08, decimals:0, unit:"%"},
                    {id:"smoothing", title:qsTr("平滑时间"), min:0, max:32, step:1, scale:1, initial:0, decimals:0, unit:" ms"}
                ]
                delegate: VRow {
                    required property var modelData
                    enabled: panel.tune.mouseLook!==false
                    opacity: enabled?1:.45
                    visible: panel.advanced===["curve","turnBoost","turnSpeed","smoothing","inputDeadzone","walkScale","adsDelayMs","deadzoneShape"].includes(modelData.id)
                    label: modelData.title
                    hint: modelData.id === "dpi" ? qsTr("与鼠标驱动的档位填同一个值。想调快慢，请调灵敏度。") :
                          modelData.id === "turnSpeed" ? qsTr("数值越小，越容易快速推满右摇杆。") : ""
                    hintBelow: true
                    RowLayout {
                        VSlider {
                            objectName: "ps5-tune-" + modelData.id
                            Layout.preferredWidth: panel.bodyWidth<500?100:137
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
                enabled: panel.tune.mouseLook!==false; visible: !panel.advanced
                label: qsTr("反转纵向")
                VSwitch { checked: panel.tune.invertY === true; onToggled: on => panel.input?.setTuning("invertY", on) }
            }
            VRow { visible: panel.advanced; label: qsTr("解锁快捷键")
                VSelect { title: qsTr("解锁快捷键"); value: panel.input?.unlockLabel || "Esc"
                    options: ["Esc","Ctrl+Alt+Z","Alt+Q","F1","~"].map((n,i)=>({id:String(i),label:n}))
                    onPicked: id=>panel.input.setTuning("unlock",Number(id)) }
            }
            VRow {visible: panel.advanced; enabled: panel.tune.mouseLook!==false; label: qsTr("开镜触发键")
                VSelect {title: qsTr("开镜触发键"); options: panel.names.slice(0,18).map((n,i)=>({id:String(i),label:n}))
                    value: panel.names[Number(panel.tune.adsAction??16)]; onPicked: id=>panel.input.setTuning("adsAction",Number(id))}
            }
            VRow {visible: panel.advanced; enabled: panel.tune.mouseLook!==false; label: qsTr("鼠标陀螺仪")
                VSelect {title: qsTr("鼠标陀螺仪"); options: [{id:"0",label:qsTr("关闭")},{id:"1",label:qsTr("始终开启")},{id:"2",label:qsTr("仅开镜")}]
                    value: options[Number(panel.tune.gyro||0)].label; onPicked: id=>panel.input.setTuning("gyro",Number(id))}
            }
            VRow {visible: panel.advanced; enabled: panel.tune.mouseLook!==false; label: qsTr("快速转身补偿")
                VSwitch {checked: panel.tune.carry===true; onToggled: on=>panel.input.setTuning("carry",on)}
            }
            Canvas {
                Layout.fillWidth: true; implicitHeight: 54; visible: panel.advanced
                property var settings: panel.tune
                onSettingsChanged: requestPaint()
                onWidthChanged: requestPaint()
                onPaint: {
                    const c=getContext("2d");c.reset();c.fillStyle="#181f2d";c.fillRect(0,0,width,height)
                    c.strokeStyle="#92b7ff";c.lineWidth=2;c.beginPath()
                    for(let i=0;i<=100;i++){
                        const u=Math.max(0,i/100-(settings.inputDeadzone||0)), k=Math.min(1,Math.max(0,(u-.10)/.35))
                        const b=u*(1+((settings.turnBoost||1)-1)*k*k*(3-2*k)), d=settings.deadzone||0
                        const level=Math.min(1,b), aim=.45
                        const shaped=level<aim ? aim*Math.pow(level/aim,settings.curve||1) : level
                        const y=height-2-(height-4)*(u>0?d+(1-d)*shaped:0)
                        if(i===0)c.moveTo(0,y);else c.lineTo(width*i/100,y)
                    }
                    c.stroke()
                }
            }
            Text { visible: panel.advanced; Layout.fillWidth: true; wrapMode: Text.Wrap; color: Theme.t3
                text: qsTr("曲线小于 1 提高低速响应；等于 1 为线性；大于 1 便于微调。") }
            Canvas {
                visible: !!panel.input?.previewing
                Layout.alignment: Qt.AlignHCenter; implicitWidth: 76; implicitHeight: 76
                property real xAxis: panel.pad.rightX||0
                property real yAxis: panel.pad.rightY||0
                property real dz: panel.tune.deadzone||0
                onXAxisChanged: requestPaint()
                onYAxisChanged: requestPaint()
                onDzChanged: requestPaint()
                onPaint: {
                    const c=getContext("2d"),r=width/2-3,m=width/2;c.reset()
                    c.strokeStyle="#91a5c5";c.lineWidth=1
                    c.beginPath();c.arc(m,m,r,0,2*Math.PI);c.stroke()
                    c.beginPath();c.moveTo(m-r,m);c.lineTo(m+r,m);c.moveTo(m,m-r);c.lineTo(m,m+r);c.stroke()
                    for(let a=0;a<2*Math.PI;a+=Math.PI/8){c.beginPath();c.arc(m,m,r*dz,a,a+Math.PI/14);c.stroke()}
                    c.fillStyle=Math.hypot(xAxis,yAxis)>.98?"#ffad65":"#73dfcc"
                    c.beginPath();c.arc(m+r*xAxis,m+r*yAxis,3.5,0,2*Math.PI);c.fill()
                }
            }
            Text {visible: !!panel.input?.previewing; color: Math.hypot(panel.pad.rightX||0,panel.pad.rightY||0)>.98?"#ffad65":Theme.t1
                text: Math.round(panel.pad.speed||0)+" counts/s   X "+Math.round(100*(panel.pad.rightX||0))+"%   Y "+Math.round(100*(panel.pad.rightY||0))+"%   ADS "+(panel.pad.ads?"●":"○") }
            VButton {
                objectName: "ps5-reset-mouse"
                Layout.alignment: Qt.AlignRight
                text: qsTr("重置鼠标手感")
                ghost: true
                enabled: panel.input !== null
                onClicked: panel.input.resetMouseTuning()
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        VButton {
            objectName: "ps5-reset-bindings"
            text: panel.confirmReset?qsTr("确认重置键位"):qsTr("重置键位")
            ghost: true
            enabled: panel.input !== null
            onClicked: {if(panel.confirmReset)panel.input.applyDefaults(panel.editDriving);panel.confirmReset=!panel.confirmReset}
        }
        Item { Layout.fillWidth: true }
    }
}
