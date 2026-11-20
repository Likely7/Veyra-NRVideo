import QtQuick
import QtTest
import Veyra

Item {
    id: root
    width: 1000
    height: 580
    property int underlyingActions: 0
    property bool anchorAvailable: true

    Item {
        id: surface
        width: 1000
        height: 580
        VButton {
            id: capture
            x: 36
            y: 88
            text: "Capture"
            onClicked: ++root.underlyingActions
        }
        VButton {
            id: other
            x: 15
            y: 15
            text: "Open file"
            onClicked: ++root.underlyingActions
        }
        Shortcut { sequence: "Ctrl+O"; onActivated: ++root.underlyingActions }
        Shortcut { sequence: "Right"; onActivated: ++root.underlyingActions }
        Shortcut { sequence: "F9"; onActivated: ++root.underlyingActions }
        Flickable {
            id: view
            x: 180
            y: 40
            width: surface.width - 240
            height: surface.height - 80
            clip: true
            contentHeight: 1000
            Rectangle {
                id: row
                x: 6
                y: 755
                width: view.width - 16
                height: 38
            }
        }
        OnboardingGuide {
            id: guide
            parent: surface
            targetFor: key => {
                if (!root.anchorAvailable) return null
                return key === "capture" ? {item: capture} : {item: row, viewport: view}
            }
        }
    }

    TestCase {
        name: "OnboardingGuide"
        when: windowShown
        property bool originalReducedMotion

        function control(name) {
            const item = findChild(guide.contentItem, "onboarding-" + name)
            verify(item !== null, name)
            return item
        }

        function startGuide() {
            guide.start()
            tryCompare(guide, "opened", true)
            tryVerify(() => guide.spotlight.width > 0)
        }

        function init() {
            originalReducedMotion = Theme.reduced
            root.underlyingActions = 0
            root.anchorAvailable = true
            surface.width = 1000
            surface.height = 580
            view.contentY = 0
        }

        function cleanup() {
            guide.close()
            Theme.reduced = originalReducedMotion
        }

        function test_all_steps_and_finish() {
            const targets = ["capture", "device", "resolution", "capture-rate", "format",
                             "input-color", "connect", "sr", "nr", "hdr", "fg", "dedup", "timing"]
            startGuide()
            compare(control("close").Accessible.name, control("close").tip)
            verify(!control("previous").enabled)
            for (let index = 0; index < targets.length; ++index) {
                compare(guide.stepIndex, index)
                compare(guide.step.target, targets[index])
                tryVerify(() => control("spotlight").visible)
                mouseClick(control("next"))
            }
            tryCompare(guide, "opened", false)
            compare(root.underlyingActions, 0)
            startGuide()
            compare(guide.stepIndex, 0)
        }

        function test_modal_clicks_and_player_shortcuts() {
            mouseClick(other)
            keyClick(Qt.Key_F9)
            compare(root.underlyingActions, 2)
            startGuide()
            mouseClick(other)
            keyClick(Qt.Key_O, Qt.ControlModifier)
            keyClick(Qt.Key_F9)
            compare(root.underlyingActions, 2)
            compare(guide.stepIndex, 0)
            mouseClick(control("spotlight"))
            compare(root.underlyingActions, 2)
            compare(guide.stepIndex, 1)
            keyClick(Qt.Key_Right)
            compare(guide.stepIndex, 2)
            compare(root.underlyingActions, 2)
            keyClick(Qt.Key_Left)
            compare(guide.stepIndex, 1)
            keyClick(Qt.Key_Escape)
            tryCompare(guide, "opened", false)
            mouseClick(other)
            compare(root.underlyingActions, 3)
        }

        function test_compact_layout_scroll_and_resize() {
            surface.width = 720
            surface.height = 260
            startGuide()
            guide.goTo(4)
            tryVerify(() => view.contentY > 0 && guide.spotlight.width > 0)

            const hole = control("spotlight")
            const tip = control("tip")
            const copy = control("copy")
            verify(hole.y >= view.y && hole.y + hole.height <= view.y + view.height)
            verify(tip.y + tip.height <= hole.y || tip.y >= hole.y + hole.height)
            verify(tip.width + tip.x <= surface.width && tip.height + tip.y <= surface.height)
            tryVerify(() => copy.height > 0 && copy.contentHeight > copy.height, 800,
                      "Expected scrollable text, viewport=" + copy.height + ", content=" + copy.contentHeight)
            mouseWheel(copy, copy.width / 2, copy.height / 2, 0, -120)
            tryVerify(() => copy.contentY > copy.originY)
            const before = guide.spotlight.width
            surface.width = 950
            tryVerify(() => guide.spotlight.width > before)
            mouseClick(control("previous"))
            compare(guide.stepIndex, 3)
        }

        function test_missing_anchor_keeps_navigation() {
            startGuide()
            root.anchorAvailable = false
            tryVerify(() => guide.spotlight.width === 0)
            mouseClick(control("next"))
            compare(guide.stepIndex, 1)
            root.anchorAvailable = true
            tryVerify(() => guide.spotlight.width > 0)
            mouseClick(control("close"))
            tryCompare(guide, "opened", false)
        }

        function test_reduced_motion() {
            Theme.reduced = false
            startGuide()
            const pulse = control("pulse")
            const initial = pulse.opacity
            tryVerify(() => pulse.opacity > initial + 0.05, 1000)
            Theme.reduced = true
            wait(60)
            const fixed = pulse.opacity
            wait(120)
            compare(pulse.opacity, fixed)
            verify(control("spotlight").visible)
        }
    }
}
