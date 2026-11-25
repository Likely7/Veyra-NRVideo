import QtQuick
import QtTest
import "../../../../qml/Veyra" as V

Item {
    width: 980
    height: 720
    V.Ps5PlayerControls { id: player; anchors.fill: parent; settings: testSettings; playerActive: true }
    V.Ps5ShortcutSettings { id: shortcuts; visible: false; settings: testSettings }

    TestCase {
        name: "Ps5BindingHints"
        when: windowShown

        function init() {
            player.Window.window.requestActivate()
            tryCompare(player.Window.window, "active", true)
            verify(testSettings.choose(testSettings.profiles[0]))
            fixture.connectConsole()
            tryCompare(testSettings, "available", true)
        }
        function cleanup() {
            testSettings.release()
            fixture.disconnectConsole()
            verify(testSettings.choose(testSettings.profiles[0]))
            verify(testSettings.setShortcut("overlay", "F1"))
            verify(testSettings.setShortcut("control", "F3"))
        }
        function test_player_shortcuts_distinguish_keypad_digits() {
            verify(testSettings.setShortcut("control", "Numpad2"))
            verify(testSettings.setShortcut("overlay", "Digit2"))
            const keypad = findChild(shortcuts, "shortcut-control").text
            compare(findChild(shortcuts, "shortcut-overlay").text, "2")
            verify(keypad.includes("2") && keypad !== "2")
            verify(findChild(player, "ps5-player-control").text.endsWith(keypad))
            verify(testSettings.resume(), testSettings.error)
            verify(fixture.nativeKey(0x32, 0x03, true))
            verify(fixture.nativeKey(0x32, 0x03, false))
            verify(findChild(player, "ps5-player-return-hint").text.endsWith(keypad))
            verify(findChild(player, "ps5-live-control-key").text.endsWith(keypad))
            verify(findChild(player, "ps5-live-hide-key").text.endsWith(" · 2"))
        }
        function test_f1_lists_mouse_motion_and_situation_shortcuts() {
            const profile = JSON.parse(JSON.stringify(testSettings.profile))
            profile.context_hotkeys.on_foot = "Digit2"
            profile.context_hotkeys.map = "Numpad2"
            profile.bindings.Digit1 = "jump"
            profile.bindings.Numpad1 = "melee"
            verify(testSettings.saveCopy("Map shortcut", profile), testSettings.error)
            verify(testSettings.resume(), testSettings.error)
            verify(fixture.nativeKey(0x70, 0x3b, true))
            verify(fixture.nativeKey(0x70, 0x3b, false))
            const overlay = findChild(player, "ps5-bindings-overlay")
            tryCompare(overlay, "visible", true)
            compare(findChild(overlay, "ps5-mode-key-on_foot").text, "2")
            const keypad = findChild(overlay, "ps5-mode-key-map").text
            verify(keypad.includes("2") && keypad !== "2")
            compare(findChild(overlay, "ps5-live-input-Digit1").text, "1")
            compare(findChild(overlay, "ps5-live-input-Numpad1").text, qsTr("小键盘 %1").arg("1"))
            for (const [scene, action, stick] of [
                ["on_foot", qsTr("环视"), qsTr("右摇杆")],
                ["map", qsTr("移动光标"), qsTr("左摇杆")]
            ]) {
                testSettings.correct(scene)
                tryCompare(testSettings, "situation", scene)
                const motion = findChild(overlay, "ps5-live-MouseMotion")
                verify(!!motion && motion.visible)
                verify(motion.text.includes(action) && motion.text.includes(stick), motion.text)
            }
            testSettings.correct("system_nav")
            tryCompare(testSettings, "situation", "system_nav")
            verify(!findChild(overlay, "ps5-live-MouseMotion"))
        }
    }
}
