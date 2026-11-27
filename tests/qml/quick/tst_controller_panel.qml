import QtQuick
import QtQuick.Window
import QtTest
import Veyra 1.0

Item {
    id: fixture
    width: 980
    height: 760

    Component {
        id: player
        Window {
            id: playerWindow
            width: 1100
            height: 760
            x: 24
            y: 36
            property alias controllerPanel: panel
            property alias mapper: input
            property alias mockBridge: backend
            property alias reference: quickReference
            property int startCalls: 0
            property bool startedXbox: false

            QtObject {
                id: input
                property bool keyboardEnabled: false
                property bool ps5KeyboardEnabled: false
                property bool xboxKeyboardEnabled: false
                property bool xboxPlatform: false
                property bool ps5Captured: false
                property bool xboxCaptured: false
                property bool previewing: false
                property bool driving: false
                property int learning: -1
                property int learningSlot: 0
                property bool learningDriving: false
                property var learningControls: null
                property var bindingConflict: ({})
                property string feedback: ""
                property string aimStatus: ""
                property string profile: "默认"
                property var profiles: ["默认"]
                property bool xboxOnlyProfile: false
                property string driveKeyLabel: "F6"
                property string quickKeyLabel: "F2"
                property string unlockLabel: "F1"
                property var bindings: {
                    const result = Array(34).fill("—")
                    result[0] = "Space"
                    result[17] = "鼠标左键"
                    result[32] = "V"
                    return result
                }
                property var alternateBindings: Array(34).fill("—")
                property var drivingBindings: {
                    const result = Array(34).fill("—")
                    result[0] = "Space"
                    result[16] = "S"
                    result[17] = "W"
                    result[32] = "V"
                    return result
                }
                property var drivingAlternateBindings: Array(34).fill("—")
                property var actionNotes: []
                property var drivingActionNotes: []
                property var padState: ({buttons: 0, legacyFullSpeed: 1.5})
                property int templateIndex: -1
                property int releaseCalls: 0
                property var tuning: ({horizontal: 1, vertical: 1, turnBoost: 3.6,
                    inputDeadzone: 0, mouseLook: true, dpi: 800, dpiConfirmed: false})
                signal quickReferenceRequested()
                signal quickReferenceScrolled(int delta)

                onKeyboardEnabledChanged: {
                    if (xboxPlatform) xboxKeyboardEnabled = keyboardEnabled
                    else ps5KeyboardEnabled = keyboardEnabled
                }
                function selectPlatform(xbox) {
                    xboxPlatform = xbox
                    keyboardEnabled = xbox ? xboxKeyboardEnabled : ps5KeyboardEnabled
                }
                function selectDriving(on) { driving = on }
                function toggleAction(action) { return false }
                function learnPanelBinding(action, controls, inDriving, slot) {
                    learning = action
                    learningControls = controls
                    learningSlot = slot
                    learningDriving = inDriving
                }
                function learnDriveKey(controls) { learnPanelBinding(34, controls, driving, 0) }
                function learnQuickKey(controls) { learnPanelBinding(35, controls, driving, 0) }
                function cancelLearning() { learning = -1; bindingConflict = ({}) }
                function preview(on) { previewing = on }
                function releasePs5() { cancelLearning(); previewing = false; releaseCalls++ }
                function selectProfile(name, create) {
                    if (create && !profiles.includes(name)) profiles = profiles.concat([name])
                    profile = name
                }
                function clearBinding(action, inDriving, slot) {
                    const name = inDriving
                        ? (slot ? "drivingAlternateBindings" : "drivingBindings")
                        : (slot ? "alternateBindings" : "bindings")
                    const next = input[name].slice()
                    next[action] = "—"
                    input[name] = next
                    cancelLearning()
                }
                function setTuning(key, value) {
                    const next = Object.assign({}, tuning)
                    next[key] = value
                    tuning = next
                }
                function useTemplate(index) { templateIndex = index }
                function resetMouseTuning() {
                    tuning = {dpi: 800, physicalAim: true, aimFullSpeed: 25, mouseLook: tuning.mouseLook}
                }
            }

            QtObject {
                id: backend
                property var ps5Input: input
                property var preferences: ({controllerPanelSplit: true, controllerPanelWidth: 420})
                property var ps5: ({state: "off", busy: false, active: false})
                property var ps5Control: ({available: true, enabled: false, state: "off"})
                property var ps5Profiles: []
                property var ps5LoginPrompt: ({})
                property bool isCapture: true
                property string sourceKind: "capture"
                property var xboxControl: ({available: true, enabled: false, state: "off"})
                property string submittedPin: ""
                property string pinRequestId: ""
                property string pairedCode: ""
                property int ps5RestartCalls: 0
                signal ps5PairSaved()

                property QtObject xbox: QtObject {
                    property var state: ({signedIn: false, selected: "", busy: false})
                    property var consoles: []
                    property int signinCalls: 0
                    function load() {}
                    function signIn() { signinCalls++ }
                    function select(id) { state = Object.assign({}, state, {selected: id}) }
                }

                function ps5Load() {}
                function setPreference(key, value) {
                    const next = Object.assign({}, preferences)
                    next[key] = value
                    preferences = next
                }
                function ps5Set(key, value) {
                    const next = Object.assign({}, ps5)
                    next[key] = value
                    ps5 = next
                }
                function ps5Pair(code) {
                    pairedCode = code
                    ps5 = Object.assign({}, ps5, {busy: true})
                }
                function ps5ControlSet(key, value) {
                    const changes = {}
                    changes[key] = value
                    if (key === "enabled") changes.state = value ? "starting" : "off"
                    ps5Control = Object.assign({}, ps5Control, changes)
                }
                function ps5ControlRestart() {
                    ps5RestartCalls++
                    ps5Control = Object.assign({}, ps5Control, {state: "starting"})
                }
                function ps5SendLoginPin(request, pin) {
                    pinRequestId = request
                    submittedPin = pin
                    return true
                }
                function xboxControlSet(key, value) {
                    const changes = {}
                    changes[key] = value
                    if (key === "enabled") changes.state = value ? "starting" : "off"
                    xboxControl = Object.assign({}, xboxControl, changes)
                }
            }

            ControllerPanelWindow {
                id: panel
                owner: playerWindow
                bridge: backend
                onStartKeyboardRequested: xbox => { playerWindow.startedXbox = xbox; playerWindow.startCalls++ }
            }

            ConsoleKeymapOverlay {
                id: quickReference
                anchors.fill: parent
                input: playerWindow.mapper
                active: true
                onEditRequested: panel.open({platform: xbox ? "xbox" : "ps5",
                    page: "bindings", driving: showDriving})
            }
        }
    }

    TestCase {
        name: "ControllerPanel"
        when: windowShown

        function createPlayer() {
            const owner = createTemporaryObject(player, fixture)
            verify(owner !== null)
            owner.visible = true
            tryCompare(owner, "visible", true)
            return owner
        }

        function open(owner, options) {
            const panel = owner.controllerPanel
            panel.open(options)
            tryCompare(panel, "visible", true)
            checkDock(owner, panel.dockWidth)
            verify(panel.content)
            return panel
        }

        function checkDock(owner, width) {
            const panel = owner.controllerPanel
            tryCompare(panel, "width", width)
            tryCompare(panel, "height", owner.height)
            tryCompare(panel, "x", owner.x + owner.width - width)
            tryCompare(panel, "y", owner.y)
        }

        function find(panel, name) {
            const item = findChild(panel.content, name)
            verify(item !== null, name)
            return item
        }

        function click(panel, name) {
            wait(25)
            const item = find(panel, name)
            tryVerify(() => item.visible && item.enabled && item.width > 0 && item.height > 0, 3000, name)
            const body = find(panel, "controller-panel-body")
            for (let parent = item.parent; parent; parent = parent.parent) {
                if (parent !== body.contentItem) continue
                const y = item.mapToItem(body.contentItem, 0, item.height / 2).y
                if (y < body.contentY || y > body.contentY + body.height)
                    body.contentY = Math.max(0, Math.min(body.contentHeight - body.height, y - body.height / 2))
                break
            }
            wait(25)
            mouseClick(item)
        }

        function test_widthAndCollapse_data() {
            return [{tag: "380", width: 380}, {tag: "420", width: 420}, {tag: "520", width: 520}]
        }

        function test_widthAndCollapse(data) {
            const owner = createPlayer()
            const panel = owner.controllerPanel
            owner.mockBridge.setPreference("controllerPanelWidth", data.width)
            open(owner, {platform: "ps5", page: "connect"})
            checkDock(owner, data.width)
            compare(panel.rightInset, data.width)
            compare(panel.overlay, false)

            owner.width = 820
            tryCompare(panel, "overlay", true)
            compare(panel.rightInset, 0)
            click(panel, "controller-panel-collapse")
            compare(panel.folded, true)
            checkDock(owner, 40)
            owner.x += 31
            owner.y += 18
            checkDock(owner, 40)
            click(panel, "controller-panel-expand")
            compare(panel.folded, false)
            checkDock(owner, data.width)
            owner.width = 1240
            checkDock(owner, data.width)
            tryCompare(panel, "rightInset", data.width)
            panel.dismiss()
            tryCompare(panel, "visible", false)
            tryCompare(panel, "rightInset", 0)
        }

        function test_dockFollowsOwnerAcrossReopen() {
            const owner = createPlayer()
            const panel = open(owner, {platform: "ps5", page: "connect"})
            owner.x += 84
            owner.y += 57
            owner.width = 1260
            owner.height = 705
            owner.mockBridge.setPreference("controllerPanelWidth", 500)
            checkDock(owner, 500)
            compare(panel.rightInset, 500)
            owner.hide()
            tryCompare(panel, "visible", false)
            compare(panel.rightInset, 0)

            owner.x += 53
            owner.y += 21
            owner.width = 1020
            owner.height = 665
            owner.show()
            tryCompare(panel, "visible", true)
            checkDock(owner, 500)
            panel.dismiss()
            tryCompare(panel, "visible", false)
            open(owner, {page: "connect"})
            checkDock(owner, 500)
            compare(panel.rightInset, 500)
        }

        function test_methodAndBindingSlots() {
            const owner = createPlayer()
            const panel = open(owner, {page: "method"})
            const input = owner.mapper
            compare(find(panel, "controller-panel-step-bindings").enabled, false)
            click(panel, "controller-panel-method-keyboard")
            tryCompare(input, "ps5KeyboardEnabled", true)
            compare(panel.page, "method")
            verify(find(panel, "controller-panel-default-binding-hint").visible)
            verify(find(panel, "controller-panel-method-profile").visible)
            compare(panel.bindingsDone, false)
            input.selectProfile("Racing", true)
            verify(!find(panel, "controller-panel-default-binding-hint").visible)
            compare(panel.bindingsDone, true)
            input.selectProfile("默认")
            click(panel, "controller-panel-step-bindings")
            tryCompare(panel, "page", "bindings")
            const editor = find(panel, "controller-panel-bindings")
            editor.listPreferred = true
            compare(editor.binding(0, 0), "Space")
            compare(editor.binding(0, 1), "—")
            click(panel, "controller-panel-list-primary-0")
            compare(editor.selected, 0)
            compare(input.learning, -1)
            click(panel, "controller-panel-edit-slot-0")
            compare(input.learning, 0)
            compare(input.learningSlot, 0)
            compare(input.learningControls, panel.content)
            click(panel, "controller-panel-edit-slot-1")
            compare(input.learning, 0)
            compare(input.learningSlot, 1)
            compare(input.learningDriving, false)
            input.alternateBindings = ["Z"].concat(Array(33).fill("—"))
            input.cancelLearning()
            click(panel, "controller-panel-clear-binding")
            compare(editor.binding(0, 1), "—")
            compare(editor.binding(0, 0), "Space")

            click(panel, "controller-panel-layout-1")
            tryCompare(editor, "driving", true)
            compare(editor.binding(16), "S")
            compare(editor.binding(17), "W")
            click(panel, "controller-panel-list-alternate-0")
            compare(editor.selectedSlot, 1)
            click(panel, "controller-panel-edit-slot-1")
            compare(input.learningDriving, true)
            compare(input.learningSlot, 1)
            panel.selectPage("method")
            tryCompare(input, "learning", -1)
            click(panel, "controller-panel-method-physical")
            compare(input.ps5KeyboardEnabled, false)
            verify(!find(panel, "controller-panel-step-feel").enabled)
        }

        function test_diagramTargets_data() {
            return [
                {tag: "PS5 380", width: 380, xbox: false},
                {tag: "Xbox 380", width: 380, xbox: true},
                {tag: "PS5 520", width: 520, xbox: false},
                {tag: "Xbox 520", width: 520, xbox: true}
            ]
        }

        function test_diagramTargets(data) {
            const owner = createPlayer()
            owner.mockBridge.setPreference("controllerPanelWidth", data.width)
            owner.mapper.ps5KeyboardEnabled = true
            owner.mapper.xboxKeyboardEnabled = true
            const panel = open(owner, {platform: data.xbox ? "xbox" : "ps5", page: "bindings"})
            const editor = find(panel, "controller-panel-bindings")
            tryCompare(editor, "showList", false)
            const diagram = find(panel, "controller-panel-diagram")
            const art = find(panel, "controller-panel-controller-art")
            verify(art.width >= diagram.width - 16, "Silhouette must use the actual panel width")
            tryCompare(find(panel, "controller-panel-silhouette"), "status", Image.Ready)

            const targets = []
            for (let action = 0; action <= 32; ++action) {
                if (data.xbox && (action === 14 || action >= 28 && action <= 31)) continue
                const target = find(panel, "controller-panel-diagram-key-" + action)
                verify(target.visible && target.width >= 28 && target.height >= 28, "Hit area " + action)
                if (action >= 18) continue
                const corner = target.mapToItem(art, 0, 0)
                const rect = Qt.rect(corner.x, corner.y, target.width, target.height)
                verify(rect.x >= 0 && rect.x + rect.width <= art.width &&
                    rect.y >= 0 && rect.y + rect.height <= art.height, "Shielded hit area " + action)
                for (const other of targets) {
                    const overlap = rect.x < other.x + other.width && other.x < rect.x + rect.width &&
                        rect.y < other.y + other.height && other.y < rect.y + rect.height
                    verify(!overlap, "Adjacent buttons need separate hit areas " + action)
                }
                targets.push(rect)
            }
            mouseClick(art, 2, 2)
            compare(owner.mapper.learning, -1)
            for (const action of [16, 9, 17, 0, 6, 10, 11, 19, 25]) {
                click(panel, "controller-panel-diagram-key-" + action)
                compare(editor.selected, action)
                compare(owner.mapper.learning, action)
                compare(owner.mapper.learningSlot, 0)
            }

            const combination = find(panel, "controller-panel-diagram-key-32")
            compare(combination.text, data.xbox ? "LS + RS" : "L3 + R3")
            click(panel, "controller-panel-diagram-key-32")
            compare(editor.selected, 32)
            compare(owner.mapper.learning, 32)
            compare(owner.mapper.learningDriving, false)
            compare(owner.mapper.learningSlot, 0)
            click(panel, "controller-panel-edit-slot-1")
            compare(owner.mapper.learningSlot, 1)
            compare(owner.mapper.learningDriving, false)
            compare(owner.mapper.learning, 32)

            click(panel, "controller-panel-layout-1")
            tryCompare(editor, "driving", true)
            click(panel, "controller-panel-diagram-key-32")
            compare(owner.mapper.learning, 32)
            compare(owner.mapper.learningDriving, true)
            compare(owner.mapper.learningSlot, 0)
            click(panel, "controller-panel-edit-slot-1")
            compare(owner.mapper.learning, 32)
            compare(owner.mapper.learningSlot, 1)
            compare(owner.mapper.learningDriving, true)

            click(panel, "controller-panel-diagram-key-33")
            compare(owner.mapper.learning, 33)
            compare(owner.mapper.learningDriving, true)
            const body = find(panel, "controller-panel-body")
            const slot = find(panel, "controller-panel-edit-slot-1")
            tryVerify(() => {
                const point = slot.mapToItem(body.contentItem, 0, 0)
                return point.y >= body.contentY - 1 && point.y + slot.height <= body.contentY + body.height + 1
            }, 1000, "Alternate binding must be brought into the viewport")

            click(panel, "controller-panel-binding-view-1")
            tryCompare(editor, "showList", true)
            const list = find(panel, "controller-panel-binding-list")
            const alternate = find(panel, "controller-panel-list-alternate-0")
            const end = alternate.mapToItem(list, alternate.width, 0)
            verify(end.x <= list.width + 1)
            click(panel, "controller-panel-binding-view-0")
            tryCompare(editor, "showList", false)
        }

        function test_editorPreviewAndEscape() {
            const owner = createPlayer()
            owner.mapper.keyboardEnabled = true
            const panel = open(owner, {page: "bindings"})
            const editor = find(panel, "controller-panel-bindings")
            editor.listPreferred = true
            click(panel, "controller-panel-list-primary-32")
            compare(editor.selected, 32)
            click(panel, "controller-panel-edit-slot-0")
            compare(owner.mapper.learning, 32)
            const editorScope = panel.content
            editorScope.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            tryCompare(owner.mapper, "learning", -1)
            compare(panel.opened, true)
            click(panel, "controller-panel-profile-add")
            const profile = find(panel, "controller-panel-profile-name")
            profile.text = "Racing"
            click(panel, "controller-panel-profile-save")
            compare(owner.mapper.profile, "Racing")
            click(panel, "controller-panel-preview")
            verify(owner.mapper.previewing)
            owner.mapper.padState = {buttons: 1}
            compare(find(panel, "controller-panel-binding-row-0").pressed, true)
            editorScope.forceActiveFocus()
            keyClick(Qt.Key_Escape)
            tryCompare(owner.mapper, "previewing", false)
            compare(panel.opened, true)
            keyClick(Qt.Key_Escape)
            tryCompare(panel, "opened", false)
            compare(owner.mapper.releaseCalls, 0)
        }

        function test_inlinePairPinAndFailure() {
            const owner = createPlayer()
            const bridge = owner.mockBridge
            const panel = open(owner, {page: "connect", captureOnly: true})
            const connection = panel.content.connectionView
            compare(connection.phase, "nopair")
            compare(find(panel, "controller-panel-primary").enabled, false)
            find(panel, "controller-panel-ps5-account").text = "42"
            find(panel, "controller-panel-ps5-address").text = "192.0.2.1"
            find(panel, "controller-panel-ps5-pair-pin").text = "12345678"
            click(panel, "controller-panel-primary")
            compare(bridge.pairedCode, "12345678")
            compare(bridge.ps5.account, "42")
            compare(bridge.ps5.host, "192.0.2.1")
            bridge.ps5Profiles = [{id: "ps5", label: "PS5 Lab", host: "192.0.2.1"}]
            bridge.ps5 = Object.assign({}, bridge.ps5, {busy: false, profile: "ps5"})
            bridge.ps5PairSaved()
            tryCompare(connection, "phase", "connecting")
            bridge.ps5LoginPrompt = {requestId: "local-pin-request", controlOnly: true}
            tryCompare(connection, "phase", "pin")
            compare(panel.page, "connect")
            const inlinePin = find(panel, "controller-panel-ps5-login-pin")
            verify(inlinePin.visible)
            findChild(inlinePin, "ps5-login-pin-input").text = "0123"
            click(panel, "ps5-login-pin-submit")
            compare(bridge.pinRequestId, "local-pin-request")
            compare(bridge.submittedPin, "0123")
            bridge.ps5LoginPrompt = ({})
            bridge.ps5Control = {available: true, enabled: true, state: "failed", message: "Fixture failure"}
            tryCompare(connection, "phase", "failed")
            compare(find(panel, "controller-panel-status-detail").text, "Fixture failure")
            click(panel, "controller-panel-status-action")
            compare(bridge.ps5RestartCalls, 1)
            bridge.ps5Control = {available: true, enabled: true, state: "connected", pad: "DualSense", gyro: true}
            tryCompare(connection, "phase", "connected")
            tryCompare(panel, "page", "method")
            compare(connection.level, "ok")
            click(panel, "controller-panel-method-keyboard")
            click(panel, "controller-panel-primary")
            compare(owner.startCalls, 1)
            compare(owner.startedXbox, false)
        }

        function test_xboxControllerPath() {
            const owner = createPlayer()
            const bridge = owner.mockBridge
            const panel = open(owner, {platform: "xbox", page: "connect", captureOnly: true})
            const connection = panel.content.connectionView
            compare(panel.xbox, true)
            compare(connection.phase, "signin")
            click(panel, "controller-panel-primary")
            compare(bridge.xbox.signinCalls, 1)
            bridge.xbox.state = {signedIn: true, selected: "", busy: false}
            bridge.xbox.consoles = [{id: "xboxOne", name: "Xbox Lab"}]
            tryCompare(connection, "phase", "host")
            click(panel, "controller-panel-xbox-host-xboxOne")
            tryCompare(connection, "paired", true)
            verify(!bridge.xboxControl.enabled)
            click(panel, "controller-panel-primary")
            tryCompare(connection, "phase", "connecting")
            compare(bridge.xboxControl.host, "xboxOne")
            bridge.xboxControl = {available: true, enabled: true, state: "connected", host: "xboxOne"}
            tryCompare(panel, "page", "method")
            click(panel, "controller-panel-method-keyboard")
            compare(owner.mapper.xboxKeyboardEnabled, true)
            compare(owner.mapper.ps5KeyboardEnabled, false)
            compare(find(panel, "controller-panel-bindings").names[17], "RT")
            click(panel, "controller-panel-primary")
            compare(owner.startCalls, 1)
            compare(owner.startedXbox, true)
            owner.mapper.xboxCaptured = true
            tryCompare(find(panel, "controller-panel-locked"), "visible", true)
        }

        function test_f2EditRestoresDrivingLayout() {
            const owner = createPlayer()
            owner.mapper.keyboardEnabled = true
            owner.mapper.driving = true
            owner.mapper.quickReferenceRequested()
            tryCompare(owner.reference, "referenceOpen", true)
            verify(owner.reference.showDriving)
            const edit = findChild(owner.reference, "controller-panel-reference-edit")
            verify(edit && edit.visible)
            wait(35)
            mouseClick(edit)
            const panel = owner.controllerPanel
            tryCompare(panel, "visible", true)
            compare(panel.page, "bindings")
            verify(panel.editDriving)
            compare(find(panel, "controller-panel-bindings").binding(17), "W")
            compare(owner.reference.referenceOpen, false)
        }

        function test_feelPreservesProfileAndLinkState() {
            const owner = createPlayer()
            owner.mapper.keyboardEnabled = true
            const panel = open(owner, {page: "feel"})
            const input = owner.mapper
            click(panel, "controller-panel-dpi-confirm")
            compare(input.tuning.dpiConfirmed, true)
            click(panel, "controller-panel-feel-template-1")
            compare(input.templateIndex, 1)
            const horizontal = find(panel, "controller-panel-tune-horizontal")
            horizontal.commitValue(2.4)
            fuzzyCompare(input.tuning.horizontal, 2.4, 0.001)
            fuzzyCompare(input.tuning.vertical, 2.4, 0.001)
            click(panel, "controller-panel-axis-link")
            horizontal.commitValue(3.1)
            fuzzyCompare(input.tuning.horizontal, 3.1, 0.001)
            fuzzyCompare(input.tuning.vertical, 2.4, 0.001)
            click(panel, "controller-panel-reset-feel")
            const confirmation = findChild(panel.contentItem, "controller-panel-reset-feel-confirm")
            verify(confirmation && confirmation.visible)
            mouseClick(confirmation)
            tryVerify(() => input.tuning.physicalAim)
            compare(input.tuning.aimFullSpeed, 25)
            compare(input.tuning.dpi, 800)
            compare(input.bindings[0], "Space")
        }
    }
}
