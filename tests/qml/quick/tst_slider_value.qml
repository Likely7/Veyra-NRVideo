import QtQuick
import QtTest
import Veyra

Item {
    id: root
    width: 420; height: 180
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
    TestCase {
        name: "SliderNumberInput"
        when: windowShown
        function init() {
            number.closeEditor(false)
            root.amount = 1.25
            slider.from = 0; slider.to = 5; slider.inputScale = 1; slider.inputDecimals = 2
            slider.enabledControl = true
            root.shortcuts = 0
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
        function test_percent_data() {
            return [{tag:"normal-volume",typed:"75",expected:.75},
                    {tag:"unity",typed:"100",expected:1},
                    {tag:"double-volume",typed:"200",expected:2},
                    {tag:"triple-volume",typed:"300",expected:3},
                    {tag:"volume-upper-bound",typed:"999",expected:3},
                    {tag:"volume-lower-bound",typed:"-10",expected:0},
                    {tag:"invalid-volume",typed:"abc",expected:.4}]
        }
        function test_percent(data) {
            slider.to = 3; slider.inputScale = 100; slider.inputDecimals = 0; root.amount = .4
            type(data.typed); keyClick(Qt.Key_Return); compare(root.amount, data.expected)
            // Input must not break the binding when another page changes volume.
            root.amount = 2.5; compare(slider.value, 2.5)
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
    }
}
