// G1.6 component smoke tests: each control is driven through real mouse events and
// its visible state checked, so a control that draws but does nothing fails here.
import QtQuick
import QtTest
import Veyra

Item {
    id: root
    width: 800
    height: 600

    VSwitch { id: sw; x: 10; y: 10 }
    SignalSpy { id: swSpy; target: sw; signalName: "toggled" }

    VSeg {
        id: seg
        x: 10; y: 50
        options: [{ id: "a", label: "2K" }, { id: "b", label: "4K" }, { id: "c", label: "8K" }]
        current: "a"
        onPicked: id => current = id
    }

    VSlider { id: slider; x: 10; y: 100; width: 200; from: 0; to: 100; value: 50 }
    SignalSpy { id: sliderSpy; target: slider; signalName: "moved" }

    VAccordion {
        id: acc
        x: 300; y: 10; width: 320
        title: "超分辨率"
        summary: "测试"
        glyph: "sparkles"
        enabledSwitch: false
        Rectangle { implicitWidth: 100; implicitHeight: 120; color: "red" }
    }

    Item { id: anchorItem; x: 300; y: 400; width: 120; height: 30 }
    VMenu {
        id: menu
        title: "测试"
        items: [{ label: "甲", checked: true }, { sep: true }, { label: "乙" }, { label: "丙", disabled: true }]
    }
    VMenu {
        id: dynamicMenu
        title: "动态菜单"
        aboveLimit: 360
        items: [{ label: "甲" }]
    }
    SignalSpy { id: menuSpy; target: menu; signalName: "picked" }

    TestCase {
        name: "Components"
        when: windowShown

        function initTestCase() { Theme.reduced = true }   // no waiting on transitions

        function test_switch() {
            compare(sw.checked, false)
            mouseClick(sw)
            compare(sw.checked, true)
            compare(swSpy.count, 1)
            compare(swSpy.signalArguments[0][0], true)
            mouseClick(sw)
            compare(sw.checked, false)
        }

        function test_seg() {
            compare(seg.currentIndex, 0)
            // Click the third label; the indicator follows it.
            const third = seg.currentItem.parent.children[2]
            mouseClick(third)
            compare(seg.current, "c")
            compare(seg.currentIndex, 2)
            tryCompare(seg, "indicatorX", seg.targetX)
        }

        function test_slider_tap() {
            sliderSpy.clear()
            mouseClick(slider, 150, slider.height / 2)
            fuzzyCompare(slider.value, 75, 1)
            compare(sliderSpy.count, 1)
        }

        function test_slider_drag() {
            // Drag from the knob 40 px right: the value moves by 40/200 of the range,
            // not by a running total (the G1.5 drag fix).
            slider.value = 50
            sliderSpy.clear()
            const y = slider.height / 2
            mousePress(slider, 100, y)
            for (let i = 1; i <= 8; ++i) mouseMove(slider, 100 + i * 5, y)
            mouseRelease(slider, 140, y)
            fuzzyCompare(slider.value, 70, 2)
            compare(sliderSpy.count, 1)
        }

        function test_accordion() {
            compare(acc.open, false)
            compare(acc.implicitHeight, acc.headerHeight)
            mouseClick(acc, 40, acc.headerHeight / 2)
            compare(acc.open, true)
            verify(acc.implicitHeight >= acc.headerHeight + 120)
            mouseClick(acc, 40, acc.headerHeight / 2)
            compare(acc.open, false)
        }

        function test_menu() {
            menu.openAt(anchorItem, "up")
            tryCompare(menu, "opened", true)
            // Opens above the anchor, from its centre.
            verify(menu.y + menu.height <= anchorItem.y)
            compare(menu.checks[0], true)
            // Pick 乙 (index 2): the tick moves at once, the signal comes 130 ms later.
            const rows = findRows(menu.contentItem)
            compare(rows.length, 3)
            mouseClick(rows[1])
            compare(menu.checks[2], true)
            compare(menu.checks[0], false)
            tryCompare(menuSpy, "count", 1)
            compare(menuSpy.signalArguments[0][0], 2)
            tryCompare(menu, "visible", false)
            // A disabled row does nothing.
            menu.openAt(anchorItem, "up")
            tryCompare(menu, "opened", true)
            mouseClick(findRows(menu.contentItem)[2])
            wait(200)
            compare(menuSpy.count, 1)
            menu.close()
        }

        function test_menu_repositions_after_content_growth() {
            dynamicMenu.openAt(anchorItem, "up")
            tryCompare(dynamicMenu, "opened", true)
            const initialHeight = dynamicMenu.height
            dynamicMenu.items = [{ label: "甲", note: "第一项" },
                                 { label: "乙", note: "第二项" },
                                 { label: "丙", note: "第三项" }]
            tryVerify(() => dynamicMenu.height > initialHeight)
            tryVerify(() => dynamicMenu.y + dynamicMenu.height <= dynamicMenu.aboveLimit)
            dynamicMenu.close()
        }

        // The option rows: Rectangles with radius 9 inside the menu's list.
        function findRows(item) {
            let out = []
            for (let i = 0; i < item.children.length; ++i) {
                const c = item.children[i]
                if (c.radius === 9 && c.implicitHeight > 20) out.push(c)
                out = out.concat(findRows(c))
            }
            return out
        }
    }
}
