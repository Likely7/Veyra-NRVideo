import QtQuick
import QtTest
import Veyra

// UI-only model: validates shared slider bindings, not audio hardware or the C++ bridge.
Item {
    id: root
    width: 420; height: 250
    property real gain: 1
    readonly property real maximum: 3
    Repeater {
        id: controls
        model: 5
        delegate: VSlider {
            required property int index
            x: 20; y: 20 + index * 35; width: 280
            from: 0; to: root.maximum; value: root.gain
            valueFromModel: true; defaultValue: 1; inputScale: 100
            onMoved: value => root.gain = value
        }
    }
    TestCase {
        name: "SharedAudioVolume"
        when: windowShown
        function init() { root.gain = 1 }
        function checkAll(value) {
            compare(root.gain, value)
            for (let i = 0; i < controls.count; ++i) {
                compare(controls.itemAt(i).value, value)
                compare(controls.itemAt(i).to * controls.itemAt(i).inputScale, 300)
            }
        }
        function test_every_control_updates_all_others() {
            for (let i = 0; i < controls.count; ++i) {
                controls.itemAt(i).commitValue(3); checkAll(3)
                controls.itemAt(i).commitValue(0); checkAll(0)
                controls.itemAt(i).commitValue(2.25); checkAll(2.25)
                controls.itemAt(i).commitValue(1); checkAll(1)
            }
        }
        function test_drag_to_300_preserves_model_binding() {
            const slider = controls.itemAt(0)
            mousePress(slider, slider.width / 3, slider.height / 2)
            // First move crosses the drag threshold; a subsequent move drives
            // activeTranslation after the handler has captured its start value.
            mouseMove(slider, slider.width / 2, slider.height / 2, 20)
            mouseMove(slider, slider.width, slider.height / 2, 20)
            mouseRelease(slider, slider.width, slider.height / 2)
            compare(root.gain, 3)
            root.gain = 0.75; checkAll(0.75)
        }
        function test_typed_percent_and_clamps() {
            const slider = controls.itemAt(1)
            const input = findChild(slider, "vslider-editor-input")
            verify(input !== null)
            for (const row of [{typed:"300",value:3},{typed:"999",value:3},
                               {typed:"-10",value:0},{typed:"200",value:2}]) {
                slider.openEditor(); input.text = row.typed; slider.closeEditor(true)
                checkAll(row.value)
            }
        }
    }
}
