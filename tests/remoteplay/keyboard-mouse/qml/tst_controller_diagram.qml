import QtQuick
import QtTest
import "../../../../qml/Veyra" as V

Item {
    id: surface
    width: 1180
    height: 580
    V.Ps5ControllerDiagram {
        id: diagram
        anchors.fill: parent
        rows: []
        selectedControl: ""
        unassigned: "Unassigned"
        viewportHeight: height
        onControlSelected: control => selectedControl = control
    }
    Component {
        id: clippedView
        Rectangle {
            anchors.fill: parent
            color: "#142536"
            Item {
                y: 120
                width: parent.width
                height: 160
                clip: true
                V.Ps5ControllerDiagram {
                    y: -210
                    width: parent.width
                    height: 580
                    rows: []
                    selectedControl: "l3"
                    unassigned: "Unassigned"
                    viewportHeight: parent.height
                }
            }
        }
    }

    TestCase {
        name: "DualSenseDiagram"
        when: windowShown
        readonly property var drawing: findChild(diagram, "controller-drawing")

        function point(x, y) {
            return Qt.point(x * drawing.width / 270, y * drawing.height / 184)
        }
        function sample(image, coordinates) {
            return image.pixel(Math.round(coordinates[0] * image.width / 270),
                               Math.round(coordinates[1] * image.height / 184)).toString()
        }
        function frame() {
            tryCompare(drawing, "status", Image.Ready)
            waitForRendering(drawing, 30)
            return grabImage(drawing)
        }
        function init() {
            mouseMove(diagram, 0, 0)
            diagram.forceActiveFocus()
            diagram.selectedControl = ""
        }
        function test_mask_data() {
            // One point on the cap and one on the neighboring surface.
            return [
                {tag: "touchpad", on: [100, 35], off: [86, 59]},
                {tag: "share", on: [74, 26], off: [78, 21]},
                {tag: "options", on: [196, 26], off: [192, 21]},
                {tag: "ps", on: [134, 88], off: [127, 85]},
                {tag: "dpad_up", on: [53, 46], off: [47, 50]},
                {tag: "dpad_right", on: [64, 56], off: [57, 48]},
                {tag: "dpad_down", on: [54, 65], off: [60, 61]},
                {tag: "dpad_left", on: [43, 55], off: [49, 62]},
                {tag: "triangle", on: [216, 35], off: [225, 44]},
                {tag: "square", on: [197, 55], off: [206, 64]},
                {tag: "circle", on: [236, 55], off: [245, 64]},
                {tag: "cross", on: [216, 69], off: [225, 83]},
                {tag: "left_stick", on: [110, 91], off: [93, 91]},
                {tag: "l3", on: [93, 91], off: [110, 91]},
                {tag: "right_stick", on: [160, 91], off: [177, 91]},
                {tag: "r3", on: [177, 91], off: [160, 91]},
                {tag: "l2", on: [62, 7], off: [45, 15]},
                {tag: "l1", on: [45, 15], off: [62, 7]},
                {tag: "r2", on: [208, 7], off: [225, 15]},
                {tag: "r1", on: [225, 15], off: [208, 7]}
            ]
        }
        function test_mask(data) {
            const idle = frame()
            diagram.selectedControl = data.tag
            const selected = frame()
            verify(sample(idle, data.on) !== sample(selected, data.on), "Tint the physical part")
            compare(sample(idle, data.off), sample(selected, data.off), "Leave the adjacent surface clear")
        }
        function test_stick_rim_shoulder_seams_and_touchpad_edges() {
            for (const [id, x, y] of [["left_stick", 110, 91], ["l3", 93, 91],
                                      ["right_stick", 160, 91], ["r3", 177, 91],
                                      ["touchpad", 85, 17], ["touchpad", 180, 49],
                                      ["l1", 65, 10.5], ["l2", 65, 7], ["r1", 205, 10.5], ["r2", 205, 7]]) {
                const p = point(x, y)
                mouseClick(drawing, p.x, p.y)
                compare(diagram.selectedControl, id)
            }
        }
        function test_hover_and_keyboard_focus_identify_the_part() {
            const idle = frame()
            const callout = findChild(diagram, "callout-dpad_up")
            mouseMove(callout)
            tryCompare(diagram, "hoveredControl", "dpad_up")
            verify(sample(frame(), [53, 46]) !== sample(idle, [53, 46]))
            mouseMove(diagram, 0, 0)
            callout.forceActiveFocus(Qt.TabFocusReason)
            tryCompare(diagram, "focusedControl", "dpad_up")
            verify(sample(frame(), [53, 46]) !== sample(idle, [53, 46]))
            keyClick(Qt.Key_Space)
            compare(diagram.selectedControl, "dpad_up")
        }
        function test_scrolled_leaders_stay_inside_the_viewport() {
            const view = createTemporaryObject(clippedView, surface)
            verify(view)
            waitForRendering(view, 100)
            const image = grabImage(view)
            for (const y of [116, 285])
                for (let x = 0; x < image.width; ++x)
                    compare(image.pixel(x, y).toString(), "#142536", "Clipped leader at " + x + ", " + y)
        }
    }
}
