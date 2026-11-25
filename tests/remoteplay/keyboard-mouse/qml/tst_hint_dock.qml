import QtQuick
import QtTest
import "../../../../qml/Veyra" as V

Item {
    id: host
    width: 900; height: 540
    property var veyra: ({ running: false, captureRecovering: false, exportRunning: false })
    V.TopDock {
        id: dock
        currentPage: "min"
        pillToggleVisible: true
        ps5KeyboardSettings: testSettings
        onRequestTogglePill: pillHidden = !pillHidden
    }
    V.Ps5ShortcutSettings {
        id: settingsPage
        y: 140; width: 590
        settings: testSettings
    }
    TestCase {
        name: "Ps5ShortcutHintDock"
        when: windowShown
        function test_top_edge_buttons_and_settings_stay_in_sync() {
            verify(testSettings.setShowShortcutHints(true))
            mouseMove(host, host.width / 2, 120)
            mouseMove(host, host.width / 2, 5)
            tryCompare(dock, "opened", true)
            tryCompare(dock, "barY", 8)
            const eye = findChild(dock, "dock-pill-toggle")
            const hints = findChild(dock, "dock-ps5-shortcut-hints")
            const setting = findChild(settingsPage, "show-shortcut-hints")
            verify(eye.visible && hints.visible)
            compare(hints.x, eye.x + eye.width + 2)
            mouseClick(hints)
            compare(setting.checked, false)
            compare(hints.glyph, "keyboard-off")
            compare(dock.pillHidden, false)
            mouseClick(eye)
            compare(dock.pillHidden, true)
            compare(testSettings.showShortcutHints, false)
            mouseClick(setting)
            compare(hints.glyph, "keyboard")
            compare(dock.pillHidden, true)
            dock.ps5KeyboardSettings = null
            compare(hints.visible, false)
            verify(eye.visible)
        }
    }
}
