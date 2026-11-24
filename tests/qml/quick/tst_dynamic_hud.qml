import QtQuick
import QtTest
import Veyra

Item {
    width: 375
    height: 180

    QtObject {
        id: settings
        property string fgBackendName: "vfg"
        property bool hudLock: true
        property bool dynamicHud: false
    }
    DynamicHudRow {
        id: row
        width: parent.width
        height: implicitHeight
        backend: settings
        namePrefix: "list"
    }

    TestCase {
        name: "DynamicHudSwitch"
        when: windowShown

        function test_independent_switch_and_provider_change() {
            const toggle = findChild(row, "list-dynamic-hud")
            verify(toggle.visible && toggle.enabled)
            verify(!toggle.checked)
            mouseClick(toggle)
            tryCompare(settings, "dynamicHud", true)
            compare(settings.hudLock, true)
            settings.fgBackendName = "xess"
            verify(!row.visible)
            compare(settings.dynamicHud, true)
            settings.fgBackendName = "vfg"
            verify(row.visible && toggle.checked)
            mouseClick(toggle)
            tryCompare(settings, "dynamicHud", false)
            compare(settings.hudLock, true)
        }
    }
}
