import QtQuick
import QtTest
import Veyra

Item {
    id: root
    width: 420; height: 180
    property real amount: 1.25
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
    TestCase {
        name: "SliderNumberInput"
        when: windowShown
        function init() {
            number.closeEditor(false)
            root.amount = 1.25
            slider.from = 0; slider.to = 5; slider.inputScale = 1; slider.inputDecimals = 2
            slider.enabledControl = true
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
    }
}
