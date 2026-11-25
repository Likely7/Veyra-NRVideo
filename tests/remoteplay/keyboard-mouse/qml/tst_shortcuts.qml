import QtQuick
import QtTest
import "../../../../qml/Veyra" as V

Item {
    width: 900
    height: 680
    V.Ps5KeyboardDialog { id: page; anchors.fill: parent; settings: testSettings }

    TestCase {
        name: "Ps5PlayerShortcuts"
        when: windowShown

        function item(name) {
            const found = findChild(page, name)
            verify(found !== null, name)
            return found
        }
        function replacement(key) {
            const box = item("shortcut-replacement")
            const position = box.find(key)
            verify(position >= 0, "Available replacement " + key)
            box.currentIndex = position
        }
        function choose(name) {
            const picker = item("profile-picker")
            picker.currentIndex = picker.find(name)
            picker.activated(picker.currentIndex)
        }
        function cleanup() {
            testSettings.cancelShortcutConflict()
            verify(testSettings.setShortcut("overlay", "F1"))
            verify(testSettings.setShortcut("control", "F3"))
            choose(testSettings.profiles[0])
            compare(testSettings.selected, testSettings.profiles[0])
        }
        function test_00_remember_each_shortcut_on_this_pc() {
            compare(testSettings.overlayKey, "F1")
            compare(testSettings.controlKey, "F3")
            item("section-3").clicked()
            const overlay = item("shortcut-overlay")
            compare(overlay.text, "F1")
            overlay.clicked()
            keyClick(Qt.Key_F11)
            compare(overlay.text, "F11")
            compare(testSettings.controlKey, "F3")
            item("shortcut-control").clicked()
            keyClick(Qt.Key_F12)
            compare(item("shortcut-control").text, "F12")
            verify(fixture.reloadShortcuts("F11", "F12"))
        }
        function test_hint_switch_is_saved_across_profiles() {
            item("section-3").clicked()
            const hints = item("show-shortcut-hints")
            compare(hints.checked, true)
            mouseClick(hints, hints.indicator.x + hints.indicator.width / 2, hints.height / 2)
            compare(testSettings.showShortcutHints, false)
            const preset = testSettings.selected
            verify(testSettings.saveCopy("Hint preference test", testSettings.profile))
            choose(preset)
            compare(hints.checked, false)
            verify(fixture.reloadShortcuts(testSettings.overlayKey, testSettings.controlKey))
            hints.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            compare(testSettings.showShortcutHints, true)
            verify(fixture.reloadShortcuts(testSettings.overlayKey, testSettings.controlKey))
        }

        function test_cancel_or_reassign_a_profile_shortcut() {
            const preset = testSettings.profiles[0]
            verify(testSettings.choose(preset))
            const original = testSettings.profile
            verify(!testSettings.setShortcut("overlay", "F7"))
            compare(testSettings.shortcutConflict.key, "F7")
            compare(testSettings.shortcutConflict.assignments[0].context, "driving")
            testSettings.cancelShortcutConflict()
            compare(testSettings.overlayKey, "F1")
            verify(!testSettings.setShortcut("overlay", "F7"))
            verify(testSettings.resolveShortcut("game", "F10"), testSettings.error)
            verify(testSettings.selected !== preset)
            compare(testSettings.profile.context_hotkeys.driving, "F10")
            compare(testSettings.profile.recognition_rules, original.recognition_rules)
            choose(preset)
            tryCompare(item("shortcut-conflict-dialog"), "visible", true)
            const conflictImage = fixture.evidencePath("shortcut-conflict.png")
            if (conflictImage) {
                const dialog = item("shortcut-conflict-dialog").contentItem.parent
                waitForRendering(dialog, 80)
                grabImage(dialog).save(conflictImage)
            }
            compare(item("profile-picker").currentText, testSettings.selected)
            verify(item("shortcut-conflict-local").text.includes("F7"))
            verify(item("shortcut-conflict-game").text.includes("Driving") ||
                   item("shortcut-conflict-game").text.includes("驾驶"))
            compare(testSettings.overlayKey, "F7")
            replacement("F1")
            item("change-player-shortcut").clicked()
            compare(testSettings.selected, preset)
            compare(testSettings.profile, original)
        }

        function test_import_and_game_edit_keep_conflicts_transactional() {
            const preset = testSettings.profiles[0]
            verify(testSettings.choose(preset))
            const source = JSON.parse(JSON.stringify(testSettings.profile))
            source.bindings["F11"] = "enter_vehicle"
            source.bindings["F11:double"] = "jump"
            source.bindings_per_context.driving.F11 = "exit_vehicle"
            source.context_hotkeys.driving = "F12"
            verify(testSettings.saveCopy("Source profile", source), testSettings.error)
            verify(testSettings.exportProfile(fixture.exportUrl()))
            choose(preset)
            verify(testSettings.setShortcut("overlay", "F11"))
            verify(testSettings.setShortcut("control", "F12"))
            const before = testSettings.profiles
            item("section-4").clicked()
            const dialog = item("import-dialog")
            function startImport() {
                dialog.selectedFile = fixture.exportUrl()
                item("import-button").clicked()
                tryCompare(dialog, "visible", true)
                dialog.accept()
                tryCompare(item("shortcut-conflict-dialog"), "visible", true)
                compare(testSettings.shortcutConflict.key, "F11")
                verify(item("shortcut-conflict-game").text.includes("Enter vehicle"))
            }
            startImport()
            replacement("F10")
            item("change-game-assignment").clicked()
            compare(testSettings.shortcutConflict.key, "F12")
            item("cancel-shortcut-conflict").clicked()
            compare(testSettings.profiles, before)
            compare(testSettings.selected, preset)
            verify(fixture.reloadShortcuts("F11", "F12"))
            startImport()
            replacement("F10")
            item("change-game-assignment").clicked()
            replacement("F3")
            item("change-player-shortcut").clicked()
            const personal = testSettings.profile
            compare(personal.bindings.F10, "enter_vehicle")
            compare(personal.bindings["F10:double"], "jump")
            compare(personal.bindings_per_context.driving.F10, "exit_vehicle")
            compare(personal.recognition_rules, source.recognition_rules)
            compare(personal.context_hotkeys.driving, "F12")
            compare(personal.bindings.F11, undefined)
            compare(testSettings.overlayKey, "F11")
            compare(testSettings.controlKey, "F3")
            verify(testSettings.exportProfile(fixture.exportUrl()))
            verify(testSettings.importProfile(fixture.exportUrl(), "Conflict round trip"))
            compare(testSettings.profile, personal)
            verify(fixture.reloadCopy("Conflict round trip", testSettings.dpi))

            const edit = JSON.parse(JSON.stringify(personal))
            edit.bindings.F11 = "enter_vehicle"
            verify(!testSettings.saveCopy("Conflict round trip", edit))
            compare(testSettings.shortcutConflict.key, "F11")
            testSettings.cancelShortcutConflict()
            compare(testSettings.profile, personal)
        }
        function test_shared_player_keys_require_an_explicit_choice() {
            verify(!testSettings.setShortcut("control", "F1"))
            compare(testSettings.shortcutConflict.action, "control")
            compare(testSettings.shortcutConflict.otherAction, "overlay")
            verify(!testSettings.resolveShortcut("player", "KeyW"))
            verify(fixture.reloadShortcuts("F1", "F3"))
            verify(testSettings.resolveShortcut("other", "F10"))
            verify(fixture.reloadShortcuts("F10", "F1"))
            verify(!testSettings.setShortcut("control", "ControlLeft"))
            compare(testSettings.controlKey, "F1")
            verify(testSettings.setShortcut("control", "F3"))
        }
        function test_binding_editor_moves_inherited_input_onto_player_key() {
            verify(testSettings.setShortcut("overlay", "F11"))
            item("section-0").clicked()
            item("view-0").clicked()
            item("key-KeyF").clicked()
            const destination = item("destination")
            compare(destination.find("F6"), -1, "F6 selects the on-foot situation in this profile")
            destination.currentIndex = destination.find("F11")
            verify(destination.currentIndex >= 0)
            item("move").clicked()
            verify(testSettings.shortcutConflict.key === "F11", testSettings.error || item("copy-name").text)
            tryCompare(item("shortcut-conflict-dialog"), "visible", true)
            verify(item("shortcut-conflict-game").text.includes("Enter vehicle"))
            replacement("F10")
            item("change-player-shortcut").clicked()
            compare(testSettings.overlayKey, "F10")
            compare(testSettings.profile.bindings_per_context.on_foot.F11, "enter_vehicle")
            compare(testSettings.profile.bindings.KeyF, "enter_vehicle")
            compare(testSettings.profile.bindings_per_context.driving.KeyF, "exit_vehicle")
            compare(testSettings.profile.context_hotkeys.on_foot, "F6")
        }
        function test_situation_layer_conflicts_even_without_a_controller_button() {
            const profile = JSON.parse(JSON.stringify(testSettings.profile))
            profile.bindings["F11:layer=aiming"] = "no_function"
            verify(testSettings.saveCopy("Hold to aim", profile), testSettings.error)
            verify(!testSettings.setShortcut("overlay", "F11"))
            compare(testSettings.shortcutConflict.key, "F11")
            verify(item("shortcut-conflict-game").text.includes("Aiming") ||
                   item("shortcut-conflict-game").text.includes("瞄准"))
            verify(testSettings.resolveShortcut("game", "F10"))
            compare(testSettings.profile.bindings["F10:layer=aiming"], "no_function")
        }
    }
}
