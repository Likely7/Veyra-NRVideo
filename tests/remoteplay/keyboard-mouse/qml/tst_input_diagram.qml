import QtQuick
import QtTest
import "../../../../qml/Veyra" as V
import "../../../../qml/Veyra/Ps5Bindings.js" as Bindings

Item {
    width: 1160; height: 620
    V.Ps5InputDiagram {
        id: diagram
        width: parent.width
        profile: testSettings.profile
        rows: Bindings.records(profile, "on_foot")
        selectedSpec: "KeyF"
        onInputSelected: key => selectedSpec = (byKey[key] || [{spec: key}])[0].spec
    }
    TestCase {
        name: "Ps5InputDiagram"
        when: windowShown
        function key(code) { return findChild(diagram, "key-" + code) }
        function saveDiagram(name) {
            const path = fixture.evidencePath(name)
            if (path) {
                waitForRendering(diagram, 100)
                grabImage(diagram).save(path)
            }
        }
        function test_physical_layout_and_resize() {
            compare(Bindings.keyboard.length, 104)
            compare(Bindings.keyboard.filter(cap => cap.bindable).length + Bindings.extraKeys.length, 91)
            verify(!key("Backquote").enabled && !key("End").enabled)
            verify(key("Numpad8").enabled && key("Home").enabled)
            compare(key("Digit2").text, "2")
            compare(key("Numpad2").text, "2")
            const pitch = key("KeyE").x - key("KeyW").x
            for (const entry of Bindings.keyboard) {
                const cap = key(entry.key)
                const tall = entry.key === "NumpadAdd" || entry.key === "NumpadEnter"
                compare(cap.height, (tall ? 2 : 1) * pitch - diagram.gap, entry.key)
                compare(cap.background.height, cap.height, entry.key + " background")
            }
            compare(key("KeyD").y - key("KeyE").y - key("KeyE").height, diagram.gap)
            compare(key("Digit2").x - key("Digit1").x - key("Digit1").width, diagram.gap)
            verify(key("KeyQ").x < key("KeyA").x && key("KeyA").x < key("KeyZ").x)
            verify(key("F5").x - key("F4").x > pitch)
            compare(key("ArrowUp").x, key("ArrowDown").x)
            compare(key("ArrowUp").y, key("ShiftRight").y)
            verify(key("NumpadEnter").height > key("Enter").height * 2)
            verify(key("Space").width > key("KeyS").width * 6)
            const capWidth = key("KeyA").width, mouseWidth = key("MouseLeft").width
            const viewport = findChild(diagram, "keyboard-viewport")
            const wide = diagram.width
            try {
                saveDiagram("diagram-wide.png")
                diagram.width = 560
                tryCompare(viewport, "width", 560)
                compare(key("KeyA").width, capWidth)
                compare(key("MouseLeft").width, mouseWidth)
                key("Numpad9").forceActiveFocus(Qt.TabFocusReason)
                tryVerify(() => viewport.contentX > 0)
                const right = key("Numpad9").mapToItem(viewport, key("Numpad9").width, 0).x
                verify(right <= viewport.width && right > 0)
                saveDiagram("diagram-numpad-compact.png")
                key("KeyW").forceActiveFocus(Qt.TabFocusReason)
                tryVerify(() => key("KeyW").mapToItem(viewport, 0, 0).x >= 0)
            } finally {
                diagram.width = wide
            }
        }
        function test_press_hold_and_selection() {
            tryCompare(findChild(diagram, "mouse-drawing"), "status", Image.Ready)
            for (const code of ["Tab", "MouseLeft"]) {
                const cap = key(code)
                const resting = cap.background.color.toString()
                mousePress(cap)
                compare(cap.down, true)
                verify(cap.background.color.toString() !== resting)
                mouseRelease(cap)
                compare(cap.down, false)
                verify(cap.selectedTile)
            }
            compare(key("Tab").gesture, Bindings.gestureName(diagram.profile, "Tab:hold"))
            const wheel = findChild(diagram, "mouse-region-WheelDown")
            mouseClick(wheel)
            compare(diagram.selectedSpec, "WheelDown")
        }
    }
}
