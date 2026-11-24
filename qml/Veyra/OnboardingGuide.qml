import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Popup {
    id: guide
    objectName: "onboarding-guide"
    required property var targetFor
    signal stepRequested(var step)

    width: parent ? parent.width : 0
    height: parent ? parent.height : 0
    padding: 0
    modal: true
    dim: false
    focus: true
    closePolicy: Popup.NoAutoClose
    popupType: Popup.Item
    background: null

    property int stepIndex: -1
    readonly property var step: steps[stepIndex]
    property rect spotlight: Qt.rect(0, 0, 0, 0)
    readonly property var steps: [
        { target: "capture", title: qsTr("从采集卡开始"),
          detail: qsTr("在首页打开「采集卡」，进入 HDMI 画面的采集设置。") },
        { target: "device", title: qsTr("选择你接入的采集卡"),
          detail: qsTr("在「视频输入设备」里选你实际连接的采集卡。选定后会读取它支持的采集档位。") },
        { target: "resolution", title: qsTr("选择采集分辨率"),
          detail: qsTr("从这张采集卡列出的分辨率里选你要用的那一档。帧率列表会随这项选择更新。") },
        { target: "capture-rate", title: qsTr("采集帧率优先选 60 FPS"),
          detail: qsTr("如果设备提供 60 FPS，优先选它。59.94 FPS 也是常见的近 60 档。游戏本身跑 30 FPS，也可以按 60 FPS 采集。") },
        { target: "format", title: qsTr("选择采集格式"),
          detail: qsTr("「像素格式」会列出当前分辨率和帧率下可以用的选项。这几种常见格式的区别如下。") },
        { target: "input-color", title: qsTr("设置输入色彩"),
          detail: qsTr("主机正在输出 HDR10 时，选 P010，再把这里设为「HDR · PQ」。NV12、YUY2 和 RGB24 走「SDR · 709」。修改后重新连接才生效。") },
        { target: "connect", title: qsTr("连接并开始采集"),
          detail: qsTr("选好设备和格式后，在这里连接。先确认预览有画面、采集帧率符合预期，再看专业模式里的增强选项。") },
        { tab: "quality", target: "sr", title: qsTr("超分辨率在这里"),
          detail: qsTr("需要改变输出尺寸时，在「超分辨率」里选择算法和目标尺寸。") },
        { tab: "quality", target: "nr", title: qsTr("DLSS 5 神经渲染（NR）"),
          detail: qsTr("在「NR 画面增强」里添加一层。Veyra 对已采集的画面使用实验性神经渲染，增强光照、材质和视觉细节。") },
        { tab: "quality", target: "hdr", title: qsTr("SDR 转 HDR 在这里"),
          detail: qsTr("想把 SDR 画面转换为 HDR，可以在支持的显卡和 HDR 显示器上试用 RTX Video HDR。") },
        { tab: "fg", target: "fg", title: qsTr("补帧在这里设置"),
          detail: qsTr("切到「补帧」，在这里选算法、倍率和档位。倍率决定每组原始画面之间生成多少帧。") },
        { tab: "fg", target: "dedup", title: qsTr("打开自动去重"),
          detail: qsTr("在「光流 · 运动估算」打开「去除重复帧」，下拉选「自动去除」。软件比较画面内容，跳过重复帧，让补帧用不同画面生成中间帧。游戏帧率波动时也按画面内容判断。") },
        { tab: "quality", target: "timing", title: qsTr("最后看帧率和耗时"),
          detail: qsTr("对照输入帧率、输出帧率和 GPU 耗时，看看设置能否稳定运行。全屏按 Home 可以看详细耗时。") }
    ]

    function start() {
        if (visible) return
        open()
        goTo(0)
    }
    function goTo(index) {
        if (index < 0) return
        if (index >= steps.length) { close(); return }
        if (index === 0 && previousButton.activeFocus) nextButton.forceActiveFocus()
        stepIndex = index
        copyScroll.cancelFlick()
        copyScroll.contentY = copyScroll.originY
        spotlight = Qt.rect(0, 0, 0, 0)
        stepRequested(step)
        Qt.callLater(updateSpotlight)
    }
    onOpened: nextButton.forceActiveFocus()
    onClosed: stepIndex = -1

    function intersect(a, b) {
        const left = Math.max(a.x, b.x)
        const top = Math.max(a.y, b.y)
        const right = Math.min(a.x + a.width, b.x + b.width)
        const bottom = Math.min(a.y + a.height, b.y + b.height)
        return Qt.rect(left, top, Math.max(0, right - left), Math.max(0, bottom - top))
    }

    function updateSpotlight() {
        if (!visible || !step) return
        const anchor = targetFor(step.target)
        const item = anchor ? anchor.item : null
        if (!item || !item.visible || item.width <= 0 || item.height <= 0) {
            if (spotlight.width > 0) spotlight = Qt.rect(0, 0, 0, 0)
            return
        }

        const viewport = anchor.viewport
        if (viewport && viewport.height > 0) {
            const row = item.mapToItem(viewport, 0, 0, item.width, item.height)
            const margin = Math.min(8, Math.max(0, (viewport.height - row.height) / 2))
            if (row.y < margin - 0.5 || row.y + row.height > viewport.height - margin + 0.5) {
                const offset = row.y - Math.max(0, (viewport.height - row.height) / 2)
                const maxY = viewport.originY + Math.max(0, viewport.contentHeight - viewport.height)
                const desiredY = Math.max(viewport.originY, Math.min(maxY, viewport.contentY + offset))
                if (Math.abs(viewport.contentY - desiredY) > 0.5) viewport.contentY = desiredY
            }
        }

        const bounds = item.mapToItem(pane, -4, -4, item.width + 8, item.height + 8)
        let rect = intersect(bounds, Qt.rect(0, 0, width, height))
        for (let ancestor = item.parent; ancestor && rect.width > 0 && rect.height > 0; ancestor = ancestor.parent) {
            if (ancestor.clip)
                rect = intersect(rect, ancestor.mapToItem(pane, 0, 0, ancestor.width, ancestor.height))
        }
        if (rect.x !== spotlight.x || rect.y !== spotlight.y ||
            rect.width !== spotlight.width || rect.height !== spotlight.height)
            spotlight = rect
    }

    // The walkthrough also follows late layouts, animated accordion bodies,
    // and rows that move as an existing capture source reports new properties.
    Timer { interval: 16; running: guide.visible; repeat: true; onTriggered: guide.updateSpotlight() }

    function tipPlacement() {
        const edge = 10
        const gap = 12
        const idealWidth = Math.min(340, Math.max(1, width - 2 * edge))
        const wholeHeight = Math.max(1, height - 2 * edge)
        const hole = spotlight
        if (hole.width <= 0 || hole.height <= 0)
            return { x: (width - idealWidth) / 2, centerY: height / 2, width: idealWidth, limit: wholeHeight }

        const roomLeft = hole.x - edge - gap
        const roomRight = width - hole.x - hole.width - edge - gap
        const sideWidth = Math.max(roomLeft, roomRight)
        if (sideWidth >= Math.min(270, idealWidth)) {
            const panelWidth = Math.min(idealWidth, sideWidth)
            const x = roomRight >= roomLeft ? hole.x + hole.width + gap : hole.x - gap - panelWidth
            return { x: x, centerY: hole.y + hole.height / 2, width: panelWidth, limit: wholeHeight }
        }

        const stackedWidth = Math.min(440, width - 2 * edge)
        const x = Math.max(edge, Math.min(width - edge - stackedWidth, hole.x + (hole.width - stackedWidth) / 2))
        const above = hole.y - edge - gap
        const below = height - hole.y - hole.height - edge - gap
        if (Math.max(above, below) < 49)
            return { x: x, centerY: height / 2, width: idealWidth, limit: wholeHeight }
        if (below >= above)
            return { x: x, top: hole.y + hole.height + gap, width: stackedWidth, limit: below }
        return { x: x, bottom: hole.y - gap, width: stackedWidth, limit: above }
    }
    readonly property var placement: tipPlacement()

    component GuideButton: VButton {
        activeFocusOnTab: enabled
        Accessible.role: Accessible.Button
        Accessible.name: text
        Accessible.onPressAction: if (enabled) clicked()
        Keys.onReturnPressed: clicked()
        Keys.onEnterPressed: clicked()
        Keys.onSpacePressed: clicked()
        Rectangle {
            anchors.fill: parent
            anchors.margins: -2
            radius: parent.radius + 2
            color: "transparent"
            border.color: Theme.t1
            visible: parent.activeFocus
        }
    }
    component CopyText: Text {
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
        textFormat: Text.PlainText
        wrapMode: Text.Wrap
    }
    component FrameRow: Row {
        id: frames
        property bool deduplicated: false
        spacing: 4
        Repeater {
            model: frames.deduplicated ? ["A", "B", "C"] : ["A", "A", "B", "B", "C", "C"]
            delegate: Rectangle {
                required property string modelData
                required property int index
                readonly property bool repeated: !frames.deduplicated && index % 2 === 1
                width: 22
                height: 22
                radius: 4
                color: Theme.card3
                opacity: repeated ? 0.5 : 1
                Text {
                    anchors.centerIn: parent
                    text: modelData
                    color: Theme.accent
                    font.family: Theme.fontMono
                    font.pixelSize: 12
                    font.strikeout: parent.repeated
                }
            }
        }
    }

    contentItem: FocusScope {
        id: pane
        readonly property bool videoCover: true
        Keys.onShortcutOverride: event => event.accepted = true
        Keys.onPressed: event => {
            if (event.key === Qt.Key_Escape) guide.close()
            else if (event.modifiers === Qt.NoModifier && event.key === Qt.Key_Right) guide.goTo(guide.stepIndex + 1)
            else if (event.modifiers === Qt.NoModifier && event.key === Qt.Key_Left) guide.goTo(guide.stepIndex - 1)
            else return
            event.accepted = true
        }

        readonly property color shade: Qt.rgba(0, 0, 0, 0.72)
        Rectangle {
            width: parent.width
            height: hotspot.visible ? hotspot.y : parent.height
            color: pane.shade
        }
        Rectangle {
            y: hotspot.y + hotspot.height
            width: parent.width
            height: hotspot.visible ? Math.max(0, parent.height - y) : 0
            color: pane.shade
        }
        Rectangle {
            y: hotspot.y
            width: hotspot.visible ? hotspot.x : 0
            height: hotspot.height
            color: pane.shade
        }
        Rectangle {
            x: hotspot.x + hotspot.width
            y: hotspot.y
            width: hotspot.visible ? Math.max(0, parent.width - x) : 0
            height: hotspot.height
            color: pane.shade
        }
        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.AllButtons
            preventStealing: true
            onWheel: event => event.accepted = true
        }
        MouseArea {
            id: hotspot
            objectName: "onboarding-spotlight"
            x: guide.spotlight.x
            y: guide.spotlight.y
            width: guide.spotlight.width
            height: guide.spotlight.height
            visible: width > 0 && height > 0
            cursorShape: Qt.PointingHandCursor
            preventStealing: true
            Accessible.role: Accessible.Button
            Accessible.name: nextButton.text
            Accessible.onPressAction: guide.goTo(guide.stepIndex + 1)
            onClicked: guide.goTo(guide.stepIndex + 1)
            Rectangle {
                anchors.fill: parent
                color: "transparent"
                radius: 7
                border.width: 1
                border.color: Theme.accent
            }
            Rectangle {
                objectName: "onboarding-pulse"
                anchors.fill: parent
                anchors.margins: -2
                color: "transparent"
                radius: 9
                border.width: 2
                border.color: Theme.accent
                opacity: Theme.reduced ? 0.72 : 0.34
                SequentialAnimation on opacity {
                    running: guide.visible && hotspot.visible && !Theme.reduced
                    loops: Animation.Infinite
                    NumberAnimation { from: 0.34; to: 0.92; duration: 925; easing.type: Easing.InOutSine }
                    NumberAnimation { from: 0.92; to: 0.34; duration: 925; easing.type: Easing.InOutSine }
                }
            }
        }

        Rectangle {
            id: tip
            objectName: "onboarding-tip"
            x: guide.placement.x
            y: guide.placement.top !== undefined ? guide.placement.top
               : guide.placement.bottom !== undefined ? guide.placement.bottom - height
               : Math.max(10, Math.min(pane.height - height - 10, guide.placement.centerY - height / 2))
            width: guide.placement.width
            readonly property bool compact: guide.placement.limit < 130
            height: Math.min(guide.placement.limit, card.implicitHeight + 2 * card.anchors.topMargin)
            radius: 12
            color: Theme.dialog
            border.width: 1
            border.color: Theme.stroke2

            MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons }
            GridLayout {
                id: card
                columns: 4
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 12
                anchors.topMargin: tip.compact ? 4 : 7
                anchors.bottomMargin: anchors.topMargin
                rowSpacing: tip.compact ? 3 : 5
                columnSpacing: 5

                Text {
                    Layout.row: 0
                    Layout.column: 0
                    Layout.columnSpan: tip.compact ? 1 : 3
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    elide: Text.ElideRight
                    text: qsTr("第 %1 / %2 步").arg(guide.stepIndex + 1).arg(guide.steps.length)
                    color: Theme.accent
                    font.family: Theme.fontUi
                    font.pixelSize: 12
                }
                GuideButton {
                    id: closeButton
                    objectName: "onboarding-close"
                    Layout.row: 0
                    Layout.column: 3
                    Layout.alignment: Qt.AlignRight
                    Layout.preferredWidth: tip.compact ? 24 : implicitWidth
                    Layout.preferredHeight: tip.compact ? 22 : implicitHeight
                    icon: true
                    iconName: "x"
                    ghost: true
                    tip: qsTr("退出引导")
                    Accessible.name: closeButton.tip
                    onClicked: guide.close()
                }

                Flickable {
                    id: copyScroll
                    objectName: "onboarding-copy"
                    Layout.row: 1
                    Layout.column: 0
                    Layout.columnSpan: 4
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    Layout.minimumHeight: 0
                    Layout.preferredHeight: copyColumn.implicitHeight
                    contentWidth: width
                    contentHeight: copyColumn.implicitHeight
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: contentHeight > height
                    ScrollBar.vertical: VScrollBar {}

                    WheelHandler {
                        target: null
                        enabled: copyScroll.interactive
                        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                        onWheel: event => {
                            const delta = event.pixelDelta.y || event.angleDelta.y / 3
                            if (delta === 0) return
                            copyScroll.cancelFlick()
                            const first = copyScroll.originY
                            const last = first + copyScroll.contentHeight - copyScroll.height
                            copyScroll.contentY = Math.max(first, Math.min(last, copyScroll.contentY - delta))
                            event.accepted = true
                        }
                    }

                    Column {
                        id: copyColumn
                        width: Math.max(0, parent.width - 10)
                        spacing: 6
                        CopyText {
                            objectName: "onboarding-title"
                            width: parent.width
                            text: guide.step ? guide.step.title : ""
                            font.pixelSize: Theme.fsH2
                            font.weight: Font.DemiBold
                            font.variableAxes: Theme.axesDemiBold
                            color: Theme.t1
                            Accessible.role: Accessible.Heading
                        }
                        CopyText {
                            width: parent.width
                            text: guide.step ? guide.step.detail : ""
                        }
                        Column {
                            visible: guide.step?.target === "format"
                            width: parent.width
                            spacing: 4
                            Repeater {
                                model: [
                                    { name: "NV12", note: qsTr("8 位 4:2:0 · SDR，所需带宽较低。") },
                                    { name: "YUY2", note: qsTr("8 位 4:2:2 · SDR，保留更多纵向色彩细节。") },
                                    { name: "RGB24", note: qsTr("8 位 RGB · SDR，每像素有完整的 RGB 分量，带宽较高。") },
                                    { name: "P010", note: qsTr("10 位 4:2:0 · 这四种里，采集原生 HDR10 要选 P010。") }
                                ]
                                delegate: RowLayout {
                                    required property var modelData
                                    width: parent.width
                                    spacing: 6
                                    CopyText {
                                        Layout.preferredWidth: 46
                                        text: modelData.name
                                        color: Theme.accent
                                        font.weight: Font.DemiBold
                                        font.variableAxes: Theme.axesDemiBold
                                    }
                                    CopyText { Layout.fillWidth: true; text: modelData.note }
                                }
                            }
                        }
                        Column {
                            visible: guide.step?.target === "dedup"
                            width: parent.width
                            spacing: 4
                            CopyText { text: qsTr("30 FPS 游戏示例"); color: Theme.t3 }
                            CopyText { text: qsTr("60 FPS 采集"); width: parent.width }
                            FrameRow {}
                            CopyText { text: qsTr("自动去除后"); width: parent.width }
                            FrameRow { deduplicated: true }
                        }
                    }
                }

                GuideButton {
                    id: previousButton
                    objectName: "onboarding-previous"
                    Layout.row: tip.compact ? 0 : 2
                    Layout.column: tip.compact ? 1 : 0
                    Layout.maximumWidth: (tip.width - 56) / 2
                    Layout.preferredHeight: tip.compact ? 24 : implicitHeight
                    text: qsTr("上一步")
                    enabled: guide.stepIndex > 0
                    onClicked: guide.goTo(guide.stepIndex - 1)
                }
                GuideButton {
                    id: nextButton
                    objectName: "onboarding-next"
                    Layout.row: tip.compact ? 0 : 2
                    Layout.column: tip.compact ? 2 : 3
                    Layout.alignment: Qt.AlignRight
                    Layout.maximumWidth: (tip.width - 56) / 2
                    Layout.preferredHeight: tip.compact ? 24 : implicitHeight
                    text: guide.stepIndex === guide.steps.length - 1 ? qsTr("完成") : qsTr("下一步")
                    primary: true
                    onClicked: guide.goTo(guide.stepIndex + 1)
                }
            }
        }
    }
}
