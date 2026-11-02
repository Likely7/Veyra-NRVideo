import QtQuick
import QtTest
import Veyra

Item {
    id: root
    width: 420; height: 240
    property real amount: 1.25
    property int shortcuts: 0
    Shortcut { sequence: "Right"; onActivated: root.shortcuts++ }
    VSlider {
        id: slider
        x: 20; y: 30; width: 220
        from: 0; to: 5; value: root.amount
        valueFromModel: true; inputDecimals: 2
        onMoved: value => root.amount = value
    }
    VSliderValue {
        id: number
        x: 265; y: 28; width: 70; height: 24
        slider: slider
        text: root.amount.toFixed(2)
    }
    VButton { id: outside; x: 20; y: 90; width: 120; height: 30; text: "Other control"; onClicked: forceActiveFocus() }
    TextInput { id: nextInput; x: 160; y: 90; width: 140; height: 30; text: "Next input"; selectByMouse: true }
    Item {
        id: pill
        x: 20; y: 150; width: 350; height: 40
        property int windowMoves: 0
        DragHandler {
            target: null
            enabled: !pillSlider.pressed && !pillSlider.editing
            grabPermissions: PointerHandler.CanTakeOverFromHandlersOfDifferentType | PointerHandler.ApprovesTakeOverByAnything
            onActiveChanged: if (active) pill.windowMoves++
        }
        VSlider {
            id: pillSlider
            x: 10; y: 4; width: 82; height: 32
            value: root.amount; valueFromModel: true
            fullKnobHitArea: true
            from: 0; to: 1; inputScale: 100
            onMoved: value => root.amount = value
        }
    }
    TestCase {
        name: "SliderNumberInput"
        when: windowShown
        function init() {
            number.closeEditor(false)
            slider.closeEditor(false)
            root.amount = 1.25
            slider.from = 0; slider.to = 5; slider.inputScale = 1; slider.inputDecimals = 2
            slider.enabledControl = true
            root.shortcuts = 0
            pill.windowMoves = 0
        }
        function type(text) {
            mouseClick(number, 35, 12)
            tryCompare(number, "editing", true)
            const input = findChild(number, "vslider-value-input")
            verify(input !== null && input.activeFocus)
            keyClick(Qt.Key_A, Qt.ControlModifier)
            for (let i = 0; i < text.length; ++i) keyClick(text[i])
        }
        function test_enter_data() {
            return [{tag:"decimal",typed:"2.75",expected:2.75},
                    {tag:"upper-bound",typed:"9",expected:5},
                    {tag:"lower-bound",typed:"-1",expected:0},
                    {tag:"invalid",typed:"abc",expected:1.25}]
        }
        function test_enter(data) {
            type(data.typed); keyClick(Qt.Key_Return)
            compare(number.editing, false)
            compare(root.amount, data.expected)
            // The model binding must survive input so a later engine update is visible.
            root.amount = 3.5; compare(slider.value, 3.5)
        }
        function test_percent() {
            slider.to = 1; slider.inputScale = 100; slider.inputDecimals = 0; root.amount = .4
            type("75"); keyClick(Qt.Key_Return); compare(root.amount, .75)
        }
        function test_escape() {
            type("4"); keyClick(Qt.Key_Escape)
            compare(root.amount, 1.25); compare(number.editing, false)
        }
        function test_focus_loss() {
            type("3.25"); mouseClick(outside, 30, 15)
            tryCompare(number, "editing", false); compare(root.amount, 3.25)
        }
        function test_disabled() {
            slider.enabledControl = false
            mouseClick(number, 35, 12)
            compare(number.editing, false); compare(root.amount, 1.25)
        }
        function test_shortcuts_after_commit_data() {
            return [{tag:"return",key:Qt.Key_Return},{tag:"escape",key:Qt.Key_Escape}]
        }
        function test_shortcuts_after_commit(data) {
            type("2.75")
            keyClick(Qt.Key_Right)
            compare(root.shortcuts, 0)
            keyClick(data.key)
            compare(number.editing, false)
            const input = findChild(number, "vslider-value-input")
            verify(!input.activeFocus && !input.focus)
            keyClick(Qt.Key_Right)
            tryCompare(root, "shortcuts", 1)
        }
        function test_focus_loss_keeps_next_editor() {
            type("3.25")
            mouseClick(nextInput, 25, 12)
            tryCompare(number, "editing", false)
            verify(nextInput.activeFocus)
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick("7")
            compare(nextInput.text, "7")
            compare(root.amount, 3.25)
        }
        function inlineType(text) {
            slider.openEditor()
            const input = findChild(slider, "vslider-editor-input")
            verify(input.activeFocus)
            keyClick(Qt.Key_A, Qt.ControlModifier)
            for (let i = 0; i < text.length; ++i) keyClick(text[i])
        }
        function test_inline_confirm_data() {
            return [{tag:"return",key:Qt.Key_Return}, {tag:"numpad-enter",key:Qt.Key_Enter}]
        }
        function test_inline_confirm(data) {
            slider.to = 1; slider.inputScale = 100; slider.inputDecimals = 0; root.amount = .4
            inlineType("75"); keyClick(data.key)
            compare(slider.editing, false); compare(root.amount, .75)
            const input = findChild(slider, "vslider-editor-input")
            verify(!input.focus && !input.activeFocus)
            keyClick(Qt.Key_Right); tryCompare(root, "shortcuts", 1)
            root.amount = .3; compare(slider.value, .3)
        }
        function test_inline_blank_commits_without_eating_click() {
            inlineType("3.25")
            mouseClick(root, 380, 200)
            tryCompare(slider, "editing", false); compare(root.amount, 3.25)
        }
        function test_inline_next_input_keeps_focus() {
            inlineType("3.25"); mouseClick(nextInput, 25, 12)
            tryCompare(slider, "editing", false); verify(nextInput.activeFocus)
            keyClick(Qt.Key_A, Qt.ControlModifier); keyClick("7")
            compare(nextInput.text, "7"); compare(root.amount, 3.25)
        }
        function test_inline_escape_cancels() {
            inlineType("4"); keyClick(Qt.Key_Escape)
            compare(slider.editing, false); compare(root.amount, 1.25)
            keyClick(Qt.Key_Right); tryCompare(root, "shortcuts", 1)
        }
        function test_pill_volume_drag_keeps_window_still() {
            root.amount = .3
            mousePress(pillSlider, 25, 16)
            for (let i = 1; i <= 10; ++i) mouseMove(pillSlider, 25 + i * 5, 16, 10)
            mouseRelease(pillSlider, 75, 16)
            verify(root.amount > .85, "A narrow volume rail must follow the drag")
            compare(pill.windowMoves, 0)
            root.amount = .2; compare(pillSlider.value, .2)
            mousePress(pill, 200, 20)
            for (let i = 1; i <= 8; ++i) mouseMove(pill, 200 + i * 5, 20, 10)
            mouseRelease(pill, 240, 20)
            compare(pill.windowMoves, 1, "Empty pill space must still move the window")
        }
        function test_pill_volume_drag_from_endpoint_data() {
            return [{tag:"full-volume",value:1,from:82,to:22,lower:.2,upper:.4},
                    {tag:"mute",value:0,from:0,to:60,lower:.6,upper:.85}]
        }
        function test_pill_volume_drag_from_endpoint(data) {
            root.amount = data.value
            mousePress(pillSlider, data.from, 16)
            for (let i = 1; i <= 10; ++i)
                mouseMove(pillSlider, data.from + (data.to-data.from)*i/10, 16, 10)
            mouseRelease(pillSlider, data.to, 16)
            verify(root.amount > data.lower && root.amount < data.upper)
            compare(pill.windowMoves, 0)
        }
        function test_pill_volume_single_move_drag() {
            root.amount = 1
            mousePress(pillSlider, 82, 16)
            mouseMove(pillSlider, 22, 16, 10)
            mouseRelease(pillSlider, 22, 16)
            verify(root.amount > .2 && root.amount < .4,
                   "The threshold-crossing move must not be discarded")
            compare(pill.windowMoves, 0)
        }
    }
}
