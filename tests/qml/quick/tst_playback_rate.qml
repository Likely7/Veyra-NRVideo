import QtQuick
import QtTest
import Veyra

Item {
    id: root
    width: 600; height: 400
    QtObject {
        id: veyra
        property bool hasSource: true
        property bool isCapture: false
        property bool running: true
        property real duration: 100
        property real playbackRate: 1
        function logUi(category, text) { }
    }
    PlaybackRateButton { id: rate; objectName: "test-rate"; x: 200; y: 220 }
    function byText(item, text) {
        if (item.text === text) return item
        for (const child of item.children || []) {
            const found = byText(child, text)
            if (found) return found
        }
        return null
    }
    TestCase {
        name: "PlaybackRate"
        when: windowShown
        function test_rates() {
            for (const value of [1.5, 2, 3, 1]) {
                mouseClick(rate, rate.width / 2, rate.height / 2)
                const menu = findChild(rate, "test-rate-menu")
                verify(menu !== null)
                tryCompare(menu, "visible", true)
                wait(500)
                const label = root.byText(menu.contentItem, value + "×")
                verify(label !== null)
                mouseClick(label, label.width / 2, label.height / 2)
                tryCompare(veyra, "playbackRate", value)
                compare(rate.rateLabel, value + "×")
            }
            veyra.isCapture = true
            compare(rate.visible, false)
            veyra.isCapture = false
            veyra.duration = 0
            compare(rate.visible, false)
        }
    }
}
