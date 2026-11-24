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
        property var bindings: Array(26).fill("—")
        property var tuning: ({ horizontal: 1, vertical: 1, mouseLook: true })
        property int learning: -1
        property string feedback: ""
        property bool previewing: false
        property var padState: ({ buttons: 0 })
        function learn(action) { learning = action }
        function clearBinding(action) {
            const list = bindings.slice()
            list[action] = "—"
            bindings = list
            learning = -1
        }
        function applyDefaults() { bindings = ["Space"].concat(Array(25).fill("—")) }
        function setTuning(name, value) {
            const next = Object.assign({}, tuning)
            next[name] = value
            tuning = next
        }
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
            mapper.applyDefaults()
            mapper.releasePs5()
            wait(30)
        }
        function test_click_controller_and_preview() {
            const trigger = findChild(editor, "ps5-bind-17")
            verify(trigger && trigger.visible)
            mouseClick(trigger)
            compare(editor.selected, 17)
            compare(mapper.learning, 17)
            mapper.releasePs5()
            mouseClick(findChild(editor, "ps5-bind-0"))
            compare(mapper.learning, 0)
            mapper.releasePs5()
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
        }
        function test_narrow_geometry() {
            fixture.width = 453
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
