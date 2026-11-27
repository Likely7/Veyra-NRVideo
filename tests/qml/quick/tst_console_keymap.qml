import QtQuick
import QtTest
import Veyra 1.0

Item {
    width: 453
    height: 740

    QtObject {
        id: mapper
        property bool driving: false
        property string driveKeyLabel: "F6"
        property string quickKeyLabel: "F2"
        property string unlockLabel: "F1"
        property bool aimTesting: false
        property string aimStatus: ""
        property var bindings: {
            let keys = Array(35).fill("—")
            for (let action = 4; action <= 25; ++action) keys[action] = "K" + action
            keys[3] = "F"
            keys[18] = "W"
            keys[34] = "T"
            return keys
        }
        property var alternateBindings: {
            let keys = Array(35).fill("—")
            keys[3] = "Ctrl+F"
            return keys
        }
        property var drivingBindings: {
            let keys = Array(35).fill("—")
            keys[17] = "Up"
            keys[16] = "Down"
            keys[20] = "A"
            keys[21] = "D"
            keys[34] = "T"
            return keys
        }
        property var drivingAlternateBindings: []
        property var actionNotes: ["", "", "", "Enter car"]
        property var drivingActionNotes: []
        property var tuning: ({ mouseLook: true, adsScale: 1.7, adsAction: 16 })
        signal quickReferenceRequested()
        signal quickReferenceScrolled(int delta)
    }

    Component {
        id: fixture
        ConsoleKeymapOverlay { width: 453; height: 740; input: mapper }
    }

    TestCase {
        name: "ConsoleKeymapOverlay"
        when: windowShown

        function init() {
            mapper.driving = false
            mapper.aimTesting = false
            mapper.aimStatus = ""
        }

        function expires(overlay, hint, message) {
            verify(hint.visible)
            verify(overlay.hintText.indexOf(message) !== -1, overlay.hintText)
            wait(4600)
            verify(hint.visible)
            tryCompare(hint, "visible", false, 1800)
            compare(hint.videoCover, false)
        }

        function test_hintEventsExpire() {
            const overlay = createTemporaryObject(fixture, parent)
            verify(overlay)
            const hint = findChild(overlay, "ps5-input-hint")
            overlay.active = true
            expires(overlay, hint, "F2 键位速查")
            wait(1000)
            compare(hint.visible, false)
            overlay.locked = true
            expires(overlay, hint, "F1 释放键鼠")
            mapper.driving = true
            expires(overlay, hint, "Up 油门")
            overlay.locked = false
            expires(overlay, hint, "Ctrl+Alt+F1 手柄面板")
        }

        function test_xbox_reference_uses_same_key_positions() {
            const overlay = createTemporaryObject(fixture, parent, { xbox: true, active: true, locked: true })
            verify(overlay)
            mapper.quickReferenceRequested()
            compare(overlay.referenceOpen, true)
            const primary = findChild(overlay, "controller-panel-list-primary-3")
            const alternate = findChild(overlay, "controller-panel-list-alternate-3")
            verify(primary && alternate)
            compare(primary.text, "F")
            compare(alternate.text, "Ctrl+F")
            verify(primary.accessibleText.includes("Y，主键 F，备用键 Ctrl+F"))
            compare(findChild(overlay, "controller-panel-list-primary-34").text, "T")
            mapper.quickReferenceRequested()
            compare(overlay.referenceOpen, false)
            mapper.driving = true
            mapper.quickReferenceRequested()
            verify(overlay.showDriving)
            compare(findChild(overlay, "controller-panel-list-primary-34").text, "T")
            mapper.quickReferenceRequested()
            compare(overlay.referenceOpen, false)
            compare(overlay.locked, true)
        }

        function test_calibrationInstructionsAndResult() {
            const overlay = createTemporaryObject(fixture, parent, { active: true, locked: true })
            mapper.quickReferenceRequested()
            verify(overlay.referenceOpen)
            mapper.aimTesting = true
            mapper.aimStatus = "Press Space to save calibration"
            const hint = findChild(overlay, "ps5-input-hint")
            const label = findChild(overlay, "console-aim-status")
            verify(hint.visible && label.visible)
            compare(hint.opacity, 1)
            compare(overlay.referenceOpen, false)
            mapper.quickReferenceRequested()
            compare(overlay.referenceOpen, false)
            mapper.aimTesting = false
            mapper.aimStatus = "Calibration saved"
            overlay.locked = false
            verify(hint.visible && label.visible)
            compare(label.text, "Calibration saved")
        }

        function test_referenceFit_data() {
            return [
                { tag: "narrow", width: 453, height: 740 },
                { tag: "window", width: 1120, height: 760 },
                { tag: "4K", width: 3840, height: 2160 }
            ]
        }

        function test_referenceFit(data) {
            const overlay = createTemporaryObject(fixture, parent, {
                width: data.width, height: data.height, active: true
            })
            verify(overlay)
            overlay.locked = true
            const reference = findChild(overlay, "console-keymap-reference")
            const hint = findChild(overlay, "ps5-input-hint")
            mapper.quickReferenceRequested()
            compare(reference.visible, true)
            compare(hint.visible, false)
            compare(overlay.locked, true)
            verify(reference.x > data.width / 2)
            verify(reference.x + reference.width <= data.width)
            verify(reference.height <= data.height * 0.7)
            const primary = findChild(reference, "controller-panel-list-primary-3")
            const alternate = findChild(reference, "controller-panel-list-alternate-3")
            verify(primary && alternate)
            compare(primary.text, "F")
            compare(alternate.text, "Ctrl+F")
            compare(primary.parent.parent.note, "Enter car")
            const inward = findChild(reference, "controller-panel-list-primary-34")
            verify(inward)
            compare(inward.text, "T")
            verify(inward.accessibleText.includes("双摇杆向内推"))
            if (data.tag === "narrow") {
                const list = findChild(reference, "console-keymap-bindings")
                tryVerify(() => list.contentHeight > list.height)
                mapper.quickReferenceScrolled(-120)
                verify(list.contentY > 0)
            }
            mapper.quickReferenceRequested()
            compare(reference.visible, false)
            compare(overlay.locked, true)
            overlay.locked = false
            mapper.quickReferenceRequested()
            compare(reference.visible, true)
            overlay.suppressed = true
            compare(reference.visible, false)
            compare(reference.videoCover, false)
        }

        function test_referenceExpires() {
            const overlay = createTemporaryObject(fixture, parent, { active: true })
            verify(overlay)
            const reference = findChild(overlay, "console-keymap-reference")
            mapper.quickReferenceRequested()
            verify(reference.visible)
            tryCompare(reference, "visible", false, 16500)
            compare(reference.videoCover, false)
        }
    }
}
