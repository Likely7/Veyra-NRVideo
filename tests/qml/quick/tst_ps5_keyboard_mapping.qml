import QtQuick
import QtTest
import Veyra

Item {
    id: fixture
    width: 980
    height: 760

    Item {
        id: host
        anchors.fill: parent
        property string dialog: ""
        property string ps5MappingReturn: "capture"
        property real motionScale: 1
        function open(key) { dialog = key }
    }
    QtObject {
        id: mapper
        property bool keyboardEnabled: false
        property var bindings: Array(33).fill("—")
        property var alternateBindings: Array(33).fill("—")
        property var drivingBindings: Array(33).fill("—")
        property var drivingAlternateBindings: Array(33).fill("—")
        property int learningSlot: 0
        property bool learningDriving: false
        property string driveKeyLabel: "F6"
        property bool driving: false
        function selectDriving(on) { driving = on }
        function learnDriveKey(button) { learning = 33 }
        property var tuning: ({ horizontal: 1, vertical: 1, mouseLook: true })
        property int learning: -1
        property string feedback: ""
        property bool previewing: false
        property var padState: ({ buttons: 0 })
        function learn(action, button, drivingLayout, slot) {
            learning = action
            learningSlot = slot
            learningDriving = drivingLayout
        }
        function clearBinding(action, drivingLayout, slot) {
            const propertyName = slot === 1
                ? (drivingLayout ? "drivingAlternateBindings" : "alternateBindings")
                : (drivingLayout ? "drivingBindings" : "bindings")
            const list = mapper[propertyName].slice()
            list[action] = "—"
            mapper[propertyName] = list
            learning = -1
        }
        function applyDefaults(drivingLayout) {
            const keys = Array(33).fill("—")
            keys[0] = "Space"
            keys[16] = drivingLayout ? "S" : "鼠标右键"
            keys[17] = drivingLayout ? "W" : "鼠标左键"
            keys[9] = "Q"
            keys[32] = "V"
            if (drivingLayout) {
                drivingBindings = keys
                drivingAlternateBindings = Array(33).fill("—")
            } else {
                bindings = keys
                alternateBindings = Array(33).fill("—")
            }
        }
        function resetMouseTuning() {
            tuning = { horizontal: 1, vertical: 1, turnBoost: 3.6, inputDeadzone: 0, mouseLook: tuning.mouseLook }
        }
        function setTuning(name, value) {
            const next = Object.assign({}, tuning)
            next[name] = value
            tuning = next
        }
        property string profile: "默认"
        property var profiles: ["默认"]
        function selectProfile(name, create) {
            if (create) profiles = profiles.concat([name])
            profile = name
        }
        property string unlockLabel:"Esc"
        function toggleAction(id){return false}
        function cancelLearning(){learning=-1}
        function preview(on) { previewing = on }
        function releasePs5() { learning = -1; previewing = false }
    }
    Ps5MappingDialog {
        id: editor
        dialogHost: host
        instantTransition: true
        input: mapper
    }
    TestCase {
        name: "Ps5KeyboardMappingEditor"
        when: windowShown
        function init() {
            fixture.width = 980
            host.dialog = "ps5-mapping"
            mapper.driving=false
            mapper.profile = "默认"
            mapper.profiles = ["默认"]
            editor.selected = -1
            editor.selectedSlot = 0
            const profileGroup = findChild(editor, "ps5-profile-group")
            profileGroup.instantExpansion = true
            profileGroup.expanded = false
            editor.bodyViewport.contentY = 0
            editor.showList = Qt.binding(() => editor.bodyWidth > 0 && editor.bodyWidth < 500)
            editor.editDriving=false
            editor.advanced=false
            mapper.applyDefaults(false)
            mapper.applyDefaults(true)
            mapper.resetMouseTuning()
            mapper.releasePs5()
            mapper.padState = {buttons: 0}
            wait(30)
        }
        function test_click_controller_and_preview() {
            const trigger = findChild(editor, "ps5-bind-17")
            verify(trigger && trigger.visible)
            mouseClick(trigger)
            compare(editor.selected, 17)
            compare(mapper.learning,17)
            mouseClick(findChild(editor, "ps5-bind-0"))
            compare(mapper.learning,0)
            const cancel = findChild(editor,"ps5-cancel-binding")
            verify(cancel.visible)
            mouseClick(cancel)
            compare(mapper.learning,-1)
            mouseClick(findChild(editor, "ps5-clear-binding"))
            compare(editor.binding(0), "—")
            mouseClick(findChild(editor, "ps5-input-preview"))
            verify(mapper.previewing)
            mapper.padState = {buttons: 1}
            verify(findChild(editor, "ps5-bind-0").pressed)
            host.dialog = "capture"
            verify(!mapper.previewing)
        }
        function test_tuning_updates_model() {
            const slider = findChild(editor, "ps5-tune-horizontal")
            verify(slider && slider.visible)
            slider.commitValue(3.2)
            compare(mapper.tuning.horizontal, 3.2)
            compare(slider.value, 3.2)
            mapper.setTuning("horizontal", 2.1)
            compare(slider.value, 2.1)
            editor.advanced=true
            const boost = findChild(editor, "ps5-tune-turnBoost")
            const filter = findChild(editor, "ps5-tune-inputDeadzone")
            compare(boost.value, 3.6)
            boost.commitValue(4.4)
            filter.commitValue(0.035)
            fuzzyCompare(mapper.tuning.turnBoost, 4.4, 0.001)
            fuzzyCompare(mapper.tuning.inputDeadzone, 0.035, 0.0001)
            const key = mapper.bindings[0]
            mapper.setTuning("mouseLook", false)
            mouseClick(findChild(editor, "ps5-reset-mouse"))
            compare(boost.value, 3.6)
            compare(filter.value, 0)
            compare(mapper.tuning.mouseLook, false)
            compare(mapper.bindings[0], key)
            verify(editor.shown)
        }
        function test_driving_layout_and_preview() {
            const layout = findChild(editor, "ps5-mapping-layout")
            verify(layout.visible)
            layout.picked("driving")
            compare(editor.binding(17), "W")
            compare(editor.binding(16), "S")
            compare(editor.binding(9), "Q")
            const key = findChild(editor, "ps5-drive-key")
            key.clicked()
            compare(mapper.learning, 33)
            key.clicked()
            compare(mapper.learning, -1)
            findChild(editor, "ps5-input-preview").clicked()
            verify(mapper.previewing)
            verify(mapper.driving)
            mapper.selectDriving(false)
            verify(!editor.editDriving)
            compare(editor.binding(0), "Space")
        }
        function test_list_slots_learn_and_clear_independently() {
            editor.showList = true
            const primary = findChild(editor, "ps5-list-bind-0-0")
            const alternate = findChild(editor, "ps5-list-bind-0-1")
            verify(primary && alternate && primary.visible && alternate.visible)
            compare(primary.text, "Space")
            compare(alternate.text, "—")
            mouseClick(primary)
            compare(mapper.learning, 0)
            compare(mapper.learningSlot, 0)
            mouseClick(alternate)
            compare(mapper.learning, 0)
            compare(mapper.learningSlot, 1)
            compare(mapper.learningDriving, false)
            verify(alternate.primary)
            verify(!primary.primary)
            mapper.alternateBindings = ["Z"].concat(Array(32).fill("—"))
            compare(alternate.text, "Z")
            findChild(editor, "ps5-clear-binding").clicked()
            compare(alternate.text, "—")
            compare(primary.text, "Space")

            findChild(editor, "ps5-mapping-layout").picked("driving")
            mouseClick(alternate)
            compare(mapper.learningSlot, 1)
            compare(mapper.learningDriving, true)
        }
        function test_combo_row_is_reachable() {
            editor.showList = true
            const row = findChild(editor, "ps5-bind-row-32")
            const combo = findChild(row, "ps5-list-bind-32-0")
            compare(row.modelData, "L3 + R3")
            compare(combo.text, "V")
            const view = editor.bodyViewport
            wait(30)
            const center = combo.mapToItem(view.contentItem, combo.width / 2, combo.height / 2)
            view.contentY = Math.min(view.contentHeight - view.height, center.y - view.height / 2)
            wait(30)
            mouseClick(combo)
            compare(mapper.learning, 32)
            compare(mapper.learningSlot, 0)
        }
        function test_profile_field_creates_named_profile() {
            compare(mapper.profiles, ["默认"])
            findChild(editor, "ps5-profile-group").expanded = true
            wait(30)
            const field = findChild(editor, "ps5-profile-name")
            const create = findChild(editor, "ps5-create-profile")
            verify(field && field.visible)
            verify(!create.enabled)
            field.text = "新布局"
            verify(create.enabled)
            mouseClick(create)
            compare(mapper.profile, "新布局")
            compare(field.text, "")
        }
        function test_narrow_geometry() {
            fixture.width = 453
            wait(30)
            verify(editor.showList)
            const list = findChild(editor, "ps5-binding-list")
            const second = findChild(editor, "ps5-list-bind-0-1")
            const end = second.mapToItem(list, second.width, second.height)
            verify(list.width <= editor.bodyWidth + 1)
            verify(end.x <= list.width + 1)
            editor.showList=false
            wait(30)
            const board = findChild(editor, "ps5-controller-board")
            const trigger = findChild(editor, "ps5-bind-17")
            const spot = trigger.mapToItem(board, 0, 0)
            verify(board.width <= editor.bodyWidth + 1)
            verify(spot.x >= 0 && spot.x + trigger.width < board.width)
            verify(findChild(editor, "ps5-input-preview").x < editor.bodyWidth)
        }
    }
}
