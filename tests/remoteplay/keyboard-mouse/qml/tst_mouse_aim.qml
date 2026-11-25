import QtQuick
import QtTest
import "../../../../qml/Veyra" as V

Item {
    id: host
    width: 790
    height: 730
    property string copyName: ""
    V.Ps5MouseAim {
        id: page
        width: host.width
        settings: testSettings
        copyName: host.copyName
    }

    TestCase {
        name: "Ps5MouseAim"
        when: windowShown

        function item(name) {
            const found = findChild(page, name)
            verify(!!found, name)
            return found
        }
        function init() {
            verify(testSettings.choose(testSettings.profiles[0]))
            host.copyName = "Curve edit"
        }
        function test_reject_crossing_a_neighbour_and_save_a_valid_correction() {
            host.copyName = "Bounds"
            const preset = testSettings.profile
            const point = item("curve-point")
            point.currentIndex = 1
            point.activated(1)
            const stick = item("curve-stick")
            mouseClick(stick)
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_2)
            keyClick(Qt.Key_0)
            keyClick(Qt.Key_Return)
            mouseClick(item("curve-save"))
            compare(testSettings.selected, testSettings.profiles[0])
            compare(testSettings.profile, preset)
            verify(item("curve-error").visible)
            mouseClick(stick)
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_3)
            keyClick(Qt.Key_1)
            keyClick(Qt.Key_Return)
            verify(!item("curve-error").visible)
            const speed = item("curve-speed")
            mouseClick(speed)
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_7)
            keyClick(Qt.Key_Return)
            mouseClick(item("curve-save"))
            compare(testSettings.selected, host.copyName)
            compare(testSettings.profile.look.per_context.on_foot.camera_measurements.horizontal.points[1], [0.31, 7])
        }
        function test_restore_only_the_selected_axis_from_the_preset() {
            const preset = testSettings.profile
            host.copyName = "Personal aim"
            const copy = JSON.parse(JSON.stringify(preset))
            const aim = copy.look.per_context.aiming.camera_measurements
            aim.horizontal.points[2][1] = 11
            aim.vertical.points[2][1] = 8
            aim.fit = false
            verify(testSettings.saveCopy(host.copyName, copy))
            const scene = item("curve-situation")
            scene.currentIndex = 1
            scene.activated(1)
            mouseClick(item("curve-axis-vertical"))
            mouseClick(item("curve-restore"))
            mouseClick(item("curve-save"))
            const expected = JSON.parse(JSON.stringify(copy))
            expected.look.per_context.aiming.camera_measurements.vertical =
                    preset.look.per_context.aiming.camera_measurements.vertical
            compare(testSettings.profile, expected)
            verify(fixture.reloadCopy(host.copyName, testSettings.dpi))
        }
        function test_local_dpi_and_driving_sensitivity() {
            host.copyName = "Driving sensitivity"
            const preset = testSettings.profile
            const dpi = item("dpi")
            dpi.value = 2400
            dpi.valueModified()
            const driving = item("driving-sensitivity")
            driving.text = "1.375"
            driving.editingFinished()
            compare(testSettings.selected, host.copyName)
            const expected = JSON.parse(JSON.stringify(preset))
            expected.look.per_context.driving.sensitivity = 1.375
            compare(testSettings.profile, expected)
            compare(testSettings.dpi, 2400)
            verify(fixture.reloadCopy(host.copyName, 2400))
        }
        function test_drag_and_type_the_same_point() {
            const preset = testSettings.profile
            const curve = preset.look.per_context.on_foot.camera_measurements.horizontal
            const plot = item("curve-plot")
            const dot = item("curve-horizontal-4")
            const oldX = dot.x, oldY = dot.y
            const start = dot.mapToItem(plot, dot.width / 2, dot.height / 2)
            mouseDrag(plot, start.x, start.y, plot.width * 0.025, -plot.height * 0.03)
            const stick = item("curve-stick")
            const speed = item("curve-speed")
            verify(dot.x > oldX && dot.y < oldY)
            verify(Number(stick.text) > 60 && Number(stick.text) < 65)
            verify(Number(speed.text) > curve.points[4][1])

            mouseClick(stick)
            keyClick(Qt.Key_A, Qt.ControlModifier)
            keyClick(Qt.Key_6)
            keyClick(Qt.Key_4)
            keyClick(Qt.Key_Period)
            keyClick(Qt.Key_5)
            keyClick(Qt.Key_Return)
            fuzzyCompare(dot.x + dot.width / 2, plot.width * 0.645, 0.001)
            const editedSpeed = Number(speed.text)
            mouseClick(item("curve-save"))
            compare(testSettings.selected, host.copyName)
            const saved = testSettings.profile.look.per_context.on_foot.camera_measurements
            fuzzyCompare(saved.horizontal.points[4][0], 0.645, 0.0000001)
            fuzzyCompare(saved.horizontal.points[4][1], editedSpeed, 0.0000001)
            compare(saved.vertical, preset.look.per_context.on_foot.camera_measurements.vertical)
            compare(testSettings.profile.look.per_context.aiming, preset.look.per_context.aiming)
            compare(testSettings.profile.recognition_rules, preset.recognition_rules)
            verify(fixture.reloadCopy(host.copyName, testSettings.dpi))
            verify(testSettings.choose(testSettings.profiles[0]))
            compare(testSettings.profile, preset)
        }
    }
}
