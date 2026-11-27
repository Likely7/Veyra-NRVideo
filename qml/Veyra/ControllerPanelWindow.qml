import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: panel
    objectName: "controller-panel-window"
    required property Window owner
    required property var bridge
    readonly property var input: bridge.ps5Input
    readonly property bool ps5Available: bridge.ps5Control?.available === true
    readonly property bool xboxAvailable: bridge.xboxControl?.available === true
    readonly property bool locked: input?.ps5Captured === true || input?.xboxCaptured === true
    readonly property bool ownerVisible: owner.visible && owner.visibility !== Window.Minimized
    readonly property bool overlay: owner.width < 960
    readonly property int preferredWidth: Math.max(380, Math.min(520, Number(bridge.preferences?.controllerPanelWidth) || 420))
    readonly property int expandedWidth: Math.min(owner.width, preferredWidth)
    readonly property int dockWidth: overlay && folded ? Math.min(40, owner.width) : expandedWidth
    readonly property rect dockGeometry: Qt.rect(owner.x + owner.width - dockWidth, owner.y, dockWidth, owner.height)
    readonly property int rightInset: visible && !overlay ? expandedWidth : 0
    readonly property bool inlinePin: visible && opened && !folded && !xbox && !locked
    property bool opened: false
    property bool initialized: false
    property bool folded: false
    property bool xbox: false
    property bool captureOnly: true
    property string page: "connect"
    property bool editDriving: false
    property var visited: ({})
    property int pendingWidth: preferredWidth

    signal captureSettingsRequested()
    signal captureFocusRequested()
    signal streamStarted()
    signal startKeyboardRequested(bool xbox)

    transientParent: owner
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: Theme.card
    title: qsTr("手柄控制")
    visible: ownerVisible && opened

    readonly property bool keyboard: xbox ? input?.xboxKeyboardEnabled === true : input?.ps5KeyboardEnabled === true
    readonly property string platformKey: xbox ? "xbox" : "ps5"
    readonly property var content: panelLoader.item
    readonly property bool connected: content?.connectionView?.connected === true
    readonly property bool busy: content?.connectionView?.connecting === true
    readonly property bool pinRequested: !xbox && (bridge.ps5LoginPrompt?.requestId || "").length > 0
        && bridge.ps5LoginPrompt?.controlOnly === captureOnly
    readonly property bool bindingsDone: wasVisited("bindings") || content?.methodUsesDefaultProfile === false
    property bool firstVisit: true

    // Resize and move the owned HWND together. Separate width and x updates
    // can expose the old backing-store contents at the new window edge.
    function syncDockGeometry() {
        const target = dockGeometry
        if (x !== target.x || y !== target.y || width !== target.width || height !== target.height)
            setGeometry(target.x, target.y, target.width, target.height)
    }
    function setVisited(step) {
        const next = Object.assign({}, visited)
        next[platformKey + "/" + step] = true
        visited = next
    }
    function wasVisited(step) { return visited[platformKey + "/" + step] === true }
    function firstPage() {
        if (!connected && content?.connectionView?.paired !== true) return "connect"
        if (!wasVisited("method")) return "method"
        return keyboard && !bindingsDone ? "bindings" : "method"
    }
    function selectPage(step) {
        if ((step === "bindings" || step === "feel") && !keyboard) return
        if (page !== step) {
            input?.cancelLearning()
            if (input?.previewing) input.preview(false)
            page = step
            content?.resetScroll()
        }
        if (step === "bindings") setVisited(step)
    }
    function setPlatform(next) {
        if (locked || next === xbox) return
        input?.cancelLearning()
        xbox = next
        input?.selectPlatform(xbox)
        content?.connectionView?.prepare()
        selectPage(firstPage())
    }
    function open(options) {
        options = options || ({})
        const before = platformKey
        const previouslyCaptureOnly = captureOnly
        if (!locked) {
            if (options.platform === "xbox" && xboxAvailable) xbox = true
            else if (options.platform === "ps5" && ps5Available) xbox = false
            else if (!ps5Available && xboxAvailable) xbox = true
            if (options.captureOnly !== undefined) captureOnly = options.captureOnly
            if (options.driving !== undefined) editDriving = options.driving
        }
        if (!ps5Available && !xboxAvailable) return
        folded = false
        const prepare = !initialized || !opened || before !== platformKey || previouslyCaptureOnly !== captureOnly
        initialized = true
        if (prepare && !locked) {
            input?.selectPlatform(xbox)
            content?.connectionView?.prepare()
        }
        if (pinRequested) selectPage("connect")
        else if (options.page) selectPage(options.page)
        else if (firstVisit || before !== platformKey || previouslyCaptureOnly !== captureOnly) selectPage(firstPage())
        firstVisit = false
        syncDockGeometry()
        opened = true
        if (!locked) {
            raise()
            requestActivate()
        }
    }
    function dismiss() {
        if (!opened) return
        input?.cancelLearning()
        if (input?.previewing) input.preview(false)
        content?.closeMenu()
        opened = false
    }
    function toggle(options) {
        if (opened && !folded) dismiss()
        else open(options)
    }
    function collapse() {
        input?.cancelLearning()
        if (input?.previewing) input.preview(false)
        content?.closeMenu()
        folded = true
        syncDockGeometry()
        if (!locked) owner.requestActivate()
    }
    function primaryAction() {
        if (locked) { input?.releasePs5(); return }
        if (connected && keyboard) {
            input?.cancelLearning()
            if (input?.previewing) input.preview(false)
            captureFocusRequested()
            startKeyboardRequested(xbox)
        } else if (!connected) {
            selectPage("connect")
            content?.connectionView?.connectOrPair()
        }
    }

    onLockedChanged: if (locked) {
        xbox = input.xboxCaptured === true
        captureOnly = bridge.sourceKind === "capture"
        content?.closeMenu()
    }
    onPinRequestedChanged: if (opened && pinRequested) {
        folded = false
        selectPage("connect")
    }
    onConnectedChanged: if (opened && connected && page === "connect" && !pinRequested && !wasVisited("method"))
        selectPage("method")
    onKeyboardChanged: if (!keyboard && (page === "bindings" || page === "feel")) selectPage("method")
    onOverlayChanged: if (!overlay) folded = false
    onDockGeometryChanged: if (opened) Qt.callLater(syncDockGeometry)
    onVisibleChanged: {
        if (visible) syncDockGeometry()
        else if (initialized) {
            input?.cancelLearning()
            if (input?.previewing) input.preview(false)
        }
    }
    onClosing: event => { event.accepted = false; dismiss() }

    Shortcut {
        sequence: "Ctrl+Alt+F1"
        context: Qt.WindowShortcut
        enabled: panel.visible && !panel.locked
        onActivated: panel.toggle()
    }

    Loader {
        id: panelLoader
        anchors.fill: parent
        focus: true
        active: panel.initialized
        sourceComponent: FocusScope {
            id: scene
            focus: true
            clip: true
            readonly property alias connectionView: connection
            readonly property alias methodUsesDefaultProfile: method.defaultProfile
            function resetScroll() { body.contentY = 0 }
            function closeMenu() { options.close() }
            function cancelOrClose() {
                if (panel.input?.learning >= 0) { panel.input.cancelLearning(); return }
                if (panel.input?.previewing) { panel.input.preview(false); return }
                if (options.visible) { options.close(); return }
                if (bindings.cancelEditing() || feel.cancelReset()) return
                panel.dismiss()
            }
            Keys.onShortcutOverride: event => {
                if (event.key === Qt.Key_Escape) event.accepted = true
            }
            Keys.onEscapePressed: scene.cancelOrClose()

            Rectangle {
                anchors.fill: parent
                radius: Theme.rWindow
                color: Theme.card
                border.color: Theme.stroke2
                border.width: 1
                visible: panel.folded
                ControllerPanelButton {
                    anchors.top: parent.top
                    anchors.topMargin: 8
                    anchors.horizontalCenter: parent.horizontalCenter
                    objectName: "controller-panel-expand"
                    icon: true
                    iconName: "gamepad"
                    tip: qsTr("展开手柄面板")
                    onClicked: panel.open({platform: panel.platformKey})
                }
                Rectangle {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: 53
                    width: 7; height: 7; radius: 4
                    color: panel.connected ? Theme.ok : panel.busy || panel.pinRequested ? Theme.warn : Theme.t3
                }
            }

            Rectangle {
                id: surface
                anchors.fill: parent
                visible: !panel.folded
                radius: Theme.rWindow
                color: Theme.card
                border.color: Theme.stroke2
                border.width: 1

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 16
                    spacing: 12
                    enabled: !panel.locked && panel.opened

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6
                        VIcon { name: "gamepad"; size: 19; color: Theme.accent }
                        Text {
                            Layout.fillWidth: true
                            text: qsTr("手柄控制")
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsH3
                            font.weight: Font.DemiBold
                            color: Theme.t1
                            elide: Text.ElideRight
                        }
                        ControllerPanelButton {
                            objectName: "controller-panel-platform-ps5"
                            visible: panel.ps5Available
                            text: "PS5"
                            primary: !panel.xbox
                            fontSize: Theme.fsSmall
                            onClicked: panel.setPlatform(false)
                        }
                        ControllerPanelButton {
                            objectName: "controller-panel-platform-xbox"
                            visible: panel.xboxAvailable
                            text: "Xbox"
                            primary: panel.xbox
                            fontSize: Theme.fsSmall
                            onClicked: panel.setPlatform(true)
                        }
                        ControllerPanelButton {
                            id: menuButton
                            objectName: "controller-panel-menu"
                            icon: true
                            iconName: "settings"
                            ghost: true
                            tip: qsTr("更多手柄设置")
                            onClicked: options.openAt(menuButton, "down")
                        }
                        ControllerPanelButton {
                            objectName: "controller-panel-close"
                            icon: true
                            iconName: "x"
                            ghost: true
                            tip: qsTr("关闭窗口")
                            onClicked: panel.dismiss()
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        visible: panel.overlay
                        VTag { text: qsTr("覆盖画面中") }
                        Item { Layout.fillWidth: true }
                        ControllerPanelButton {
                            objectName: "controller-panel-collapse"
                            text: qsTr("收起")
                            ghost: true
                            onClicked: panel.collapse()
                        }
                    }

                    StatusCard {
                        objectName: "controller-panel-status"
                        Layout.fillWidth: true
                        level: connection.level
                        title: connection.title
                        detail: connection.detail
                        action: connection.actionLabel
                        shortcuts: connection.shortcuts
                        onActionRequested: connection.performStatusAction()
                    }

                    StepTabs {
                        objectName: "controller-panel-tabs"
                        Layout.fillWidth: true
                        current: panel.page
                        steps: [
                            {id: "connect", label: qsTr("连接"), done: panel.connected || connection.paired, enabled: true},
                            {id: "method", label: qsTr("方式"), done: panel.wasVisited("method"), enabled: true},
                            {id: "bindings", label: qsTr("键位"), done: panel.bindingsDone,
                                enabled: panel.keyboard, hint: qsTr("键盘鼠标模式下可用")},
                            {id: "feel", label: qsTr("手感"), done: panel.input?.tuning?.dpiConfirmed === true,
                                enabled: panel.keyboard, hint: qsTr("键盘鼠标模式下可用")}
                        ]
                        onPicked: step => panel.selectPage(step)
                    }

                    Flickable {
                        id: body
                        objectName: "controller-panel-body"
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        boundsBehavior: Flickable.StopAtBounds
                        contentWidth: width
                        contentHeight: pages.implicitHeight
                        ScrollBar.vertical: VScrollBar { policy: ScrollBar.AsNeeded }
                        ColumnLayout {
                            id: pages
                            objectName: "controller-panel-pages"
                            // The board height depends on this width. Show the scrollbar
                            // without changing the width or crossing the overflow threshold again.
                            width: Math.max(0, body.width - 10)
                            spacing: 12
                            ControllerConnectionPage {
                                id: connection
                                Layout.fillWidth: true
                                visible: panel.page === "connect"
                                activePage: visible && panel.opened && panel.visible && !panel.folded
                                bridge: panel.bridge
                                input: panel.input
                                xbox: panel.xbox
                                captureOnly: panel.captureOnly
                                onCaptureSettingsRequested: panel.captureSettingsRequested()
                                onStreamStarted: panel.streamStarted()
                            }
                            ControllerMethodPage {
                                id: method
                                Layout.fillWidth: true
                                visible: panel.page === "method"
                                bridge: panel.bridge
                                input: panel.input
                                xbox: panel.xbox
                                captureOnly: panel.captureOnly
                                onMethodPicked: panel.setVisited("method")
                            }
                            ControllerBindingsPage {
                                id: bindings
                                Layout.fillWidth: true
                                visible: panel.page === "bindings"
                                input: panel.input
                                xbox: panel.xbox
                                driving: panel.editDriving
                                learningRoot: scene
                                active: visible && panel.opened && !panel.locked
                                onLayoutPicked: driving => panel.editDriving = driving
                            }
                            ControllerFeelPage {
                                id: feel
                                Layout.fillWidth: true
                                visible: panel.page === "feel" || panel.page === "advanced"
                                input: panel.input
                                xbox: panel.xbox
                                driving: panel.editDriving
                                advanced: panel.page === "advanced"
                                onMethodRequested: panel.selectPage("method")
                            }
                        }
                    }

                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 4
                        ControllerPanelButton {
                            objectName: "controller-panel-primary"
                            Layout.fillWidth: true
                            visible: !panel.connected || panel.keyboard
                            primary: true
                            enabled: panel.connected || connection.primaryEnabled
                            text: panel.connected ? qsTr("开始键鼠控制") : connection.primaryLabel
                            iconName: panel.connected ? "play" : ""
                            onClicked: panel.primaryAction()
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: panel.connected
                            horizontalAlignment: Text.AlignHCenter
                            text: panel.keyboard ? qsTr("也可以直接点击画面") : qsTr("手柄已在转发")
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                VMenu {
                    id: options
                    objectName: "controller-panel-options"
                    items: [
                        {id: "advanced", label: qsTr("高级设置"), icon: "settings"},
                        {id: "reset", label: qsTr("重置本页"), icon: "rotateCcw",
                            disabled: panel.page !== "bindings" && panel.page !== "feel"}
                    ]
                    onPicked: (index, item) => {
                        if (item.id === "advanced") panel.selectPage("advanced")
                        else if (panel.page === "bindings") bindings.resetLayout()
                        else if (panel.page === "feel") feel.requestReset()
                    }
                }

                MouseArea {
                    objectName: "controller-panel-resize"
                    anchors.left: parent.left
                    anchors.top: parent.top
                    anchors.bottom: parent.bottom
                    width: 8
                    hoverEnabled: true
                    enabled: !panel.locked && panel.opened
                    cursorShape: Qt.SizeHorCursor
                    property real startX: 0
                    onPressed: mouse => {
                        startX = mouse.x
                        panel.pendingWidth = panel.preferredWidth
                    }
                    onPositionChanged: mouse => {
                        if (pressed)
                            panel.pendingWidth = Math.round(Math.max(380, Math.min(520, panel.preferredWidth - mouse.x + startX)))
                    }
                    onReleased: if (panel.pendingWidth !== panel.preferredWidth)
                        panel.bridge.setPreference("controllerPanelWidth", panel.pendingWidth)
                    ToolTip.visible: pressed
                    ToolTip.text: qsTr("%1 px").arg(panel.pendingWidth)
                    Rectangle {
                        anchors.left: parent.left
                        anchors.verticalCenter: parent.verticalCenter
                        width: 2
                        height: 36
                        radius: 1
                        color: Theme.accent
                        visible: parent.containsMouse || parent.pressed
                    }
                }

                Rectangle {
                    objectName: "controller-panel-locked"
                    anchors.fill: parent
                    radius: parent.radius
                    color: Theme.shade(0, 0, 0, 0.74)
                    visible: panel.locked
                    ColumnLayout {
                        anchors.centerIn: parent
                        spacing: 8
                        Text {
                            Layout.alignment: Qt.AlignHCenter
                            text: qsTr("键鼠控制中")
                            color: Theme.t1
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsH3
                        }
                        RowLayout {
                            Layout.alignment: Qt.AlignHCenter
                            spacing: 6
                            KeyCap { text: panel.input?.unlockLabel ?? "F1" }
                            Text {
                                text: qsTr("释放键鼠")
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                            }
                        }
                    }
                }
            }
        }
    }
}
