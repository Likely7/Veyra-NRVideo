import QtQuick
import QtQuick.Controls.Basic
import QtTest
import "../../../../qml/Veyra" as V

Item {
    id: player
    width: 900
    height: 590
    property bool playerEnabled: true
    property int editRequests: 0
    property int fullscreenRequests: 0
    Shortcut { sequence: "F11"; onActivated: player.fullscreenRequests++ }
    TextField { id: field; y: 530; visible: false }
    Popup { id: modal; modal: true; focus: true; width: 180; height: 80 }

    V.Ps5PlayerControls {
        id: controls
        anchors.fill: parent
        settings: testSettings
        playerActive: player.playerEnabled
        onControlRequested: testSettings.resume()
        onEditRequested: player.editRequests++
    }

    TestCase {
        name: "Ps5PlayerControls"
        when: windowShown

        function init() {
            player.playerEnabled = true
            player.Window.window.requestActivate()
            tryCompare(player.Window.window, "active", true)
            fixture.connectConsole()
            tryCompare(testSettings, "available", true)
            verify(testSettings.setShortcut("overlay", "F1"))
            verify(testSettings.setShortcut("control", "F3"))
        }
        function cleanup() {
            verify(testSettings.setShowShortcutHints(true))
            modal.close()
            field.visible = false
            player.forceActiveFocus()
            testSettings.release()
            fixture.disconnectConsole()
            tryCompare(testSettings, "available", false)
            verify(testSettings.setShortcut("overlay", "F1"))
            verify(testSettings.setShortcut("control", "F3"))
        }
        function test_rebound_fullscreen_key_and_text_focus() {
            verify(testSettings.setShortcut("control", "F11"))
            player.forceActiveFocus()
            function pressF11() {
                if (fixture.realWindowKeys()) {
                    verify(fixture.postPlayerKey(0x7a, 0x57))
                } else {
                    // Offscreen Qt has no HWND dispatcher. Deliver the Qt key
                    // only when the real native filter passes the message on.
                    if (!fixture.nativeKey(0x7a, 0x57, true)) keyClick(Qt.Key_F11)
                    fixture.nativeKey(0x7a, 0x57, false)
                }
            }
            pressF11()
            tryCompare(testSettings, "captured", true)
            compare(player.fullscreenRequests, 0)
            pressF11()
            tryCompare(testSettings, "captured", false)
            compare(player.fullscreenRequests, 0)
            field.visible = true
            field.forceActiveFocus()
            verify(field.activeFocus)
            pressF11()
            tryCompare(player, "fullscreenRequests", 1)
            compare(testSettings.captured, false)
            modal.open()
            tryCompare(modal, "opened", true)
            verify(!fixture.nativeKey(0x7a, 0x57, true))
            compare(testSettings.captured, false)
        }
        function test_bare_f10_system_key_controls_only_the_focused_player() {
            verify(testSettings.setShortcut("control", "F10"))
            player.forceActiveFocus()
            verify(!fixture.nativeSystemKey(0x79, 0x44, true, true)) // Alt+F10
            verify(!fixture.nativeSystemKey(0x79, 0x44, false, true))
            verify(!testSettings.captured)
            verify(fixture.nativeSystemKey(0x79, 0x44, true, false))
            tryCompare(testSettings, "captured", true)
            verify(fixture.nativeSystemKey(0x79, 0x44, false, false))
            verify(fixture.nativeSystemKey(0x79, 0x44, true, false))
            tryCompare(testSettings, "captured", false)
            verify(fixture.nativeSystemKey(0x79, 0x44, false, false))
            field.visible = true
            field.forceActiveFocus()
            verify(!fixture.nativeSystemKey(0x79, 0x44, true, false))
            verify(!testSettings.captured)
        }
        function test_control_key_returns_held_input_to_windows() {
            verify(testSettings.resume(), testSettings.error)
            verify(fixture.nativeKey(0x46, 0x21, true)) // F holds Triangle.
            tryVerify(() => fixture.triangleDown())
            verify(fixture.nativeKey(0x72, 0x3d, true))
            compare(testSettings.captured, false)
            verify(fixture.neutral())
            fixture.physicalCross()
            verify(fixture.crossDown())
            verify(fixture.nativeKey(0x72, 0x3d, false))
            compare(testSettings.captured, false)
        }
        function test_live_overlay_and_control_handoff_data() {
            return [{ tag: "visible hints", showHints: true }, { tag: "hidden hints", showHints: false }]
        }
        function test_live_overlay_and_control_handoff(data) {
            verify(testSettings.setShowShortcutHints(data.showHints))
            const toggle = findChild(controls, "ps5-player-control")
            const edit = findChild(controls, "ps5-player-edit")
            const overlay = findChild(controls, "ps5-bindings-overlay")
            verify(toggle.visible && edit.visible && !overlay.visible)
            verify(toggle.text.includes("F3"))
            const toolbar = findChild(controls, "ps5-player-toolbar")
            verify(toolbar.width >= toggle.width + edit.width && toolbar.height >= toggle.height)
            const toolbarImage = fixture.evidencePath("player-toolbar.png")
            if (toolbarImage) { waitForRendering(toolbar, 80); grabImage(toolbar).save(toolbarImage) }
            const previousEditRequests = player.editRequests
            mouseClick(edit)
            compare(player.editRequests, previousEditRequests + 1)
            mouseClick(toggle)
            tryCompare(testSettings, "captured", true)
            verify(!toggle.visible && !edit.visible)
            const returnHint = findChild(controls, "ps5-player-return-hint")
            const overlayHint = findChild(controls, "ps5-player-hint")
            compare(toolbar.visible, data.showHints)
            compare(returnHint.visible, data.showHints)
            compare(overlayHint.visible, data.showHints)
            verify(returnHint.text.includes("F3") && overlayHint.text.includes("F1"))
            verify(fixture.nativeKey(0x46, 0x21, true))
            tryVerify(() => fixture.triangleDown())
            const focus = player.Window.window.activeFocusItem
            verify(fixture.nativeKey(0x70, 0x3b, true))
            tryCompare(overlay, "visible", true)
            compare(overlay.y, data.showHints ? toolbar.y + toolbar.height + 6 : toolbar.y)
            compare(findChild(overlay, "ps5-live-situation").text, "On foot")
            for (const [situation, key] of Object.entries({on_foot: "F6", driving: "F7", menu: "F8", flying: "F9"}))
                compare(findChild(overlay, "ps5-mode-key-" + situation).text, key)
            verify(!findChild(overlay, "ps5-mode-key-aiming"))
            verify(fixture.nativeRepeat(0x70, 0x3b))
            verify(fixture.nativeKey(0x70, 0x3b, false))
            verify(overlay.visible && testSettings.captured && fixture.triangleDown())
            compare(player.Window.window.activeFocusItem, focus)
            const before = findChild(overlay, "ps5-live-KeyF")
            verify(before.text.includes("Enter vehicle"), before.text)
            const image = fixture.evidencePath(data.showHints ? "ps5-player-overlay.png" : "ps5-player-overlay-without-hints.png")
            if (image) grabImage(controls).save(image)
            fixture.physicalCross()
            verify(fixture.triangleDown() && !fixture.crossDown())
            verify(fixture.nativeKey(0x46, 0x21, false))
            verify(fixture.nativeKey(0x76, 0x41, true)) // Profile correction to Driving.
            verify(fixture.nativeKey(0x76, 0x41, false))
            tryCompare(testSettings, "situation", "driving")
            compare(findChild(overlay, "ps5-live-situation").text, "Driving")
            verify(findChild(overlay, "ps5-live-KeyW").text.includes("Accelerate"))
            verify(fixture.nativeKey(0x57, 0x11, true))
            verify(fixture.waitForThrottle())
            verify(fixture.nativeKey(0x70, 0x3b, true))
            tryCompare(overlay, "visible", false)
            verify(fixture.nativeKey(0x70, 0x3b, false))
            verify(testSettings.captured && fixture.waitForThrottle())
            compare(player.Window.window.activeFocusItem, focus)
            verify(fixture.nativeKey(0x72, 0x3d, true))
            verify(!testSettings.captured && fixture.neutral())
            verify(toggle.visible && edit.visible && !returnHint.visible)
            verify(fixture.nativeRepeat(0x72, 0x3d))
            verify(!testSettings.captured)
            verify(fixture.nativeKey(0x72, 0x3d, false))
            verify(fixture.nativeKey(0x72, 0x3d, true))
            tryCompare(testSettings, "captured", true)
            verify(fixture.nativeKey(0x72, 0x3d, false))
        }
        function test_situation_keys_fit_a_small_overlay() {
            const oldWidth = player.width, oldHeight = player.height
            try {
                player.width = 300
                player.height = 270
                verify(testSettings.setShowShortcutHints(false))
                verify(testSettings.resume())
                verify(fixture.nativeKey(0x70, 0x3b, true))
                verify(fixture.nativeKey(0x70, 0x3b, false))
                const overlay = findChild(controls, "ps5-bindings-overlay")
                tryCompare(overlay, "visible", true)
                const modes = ["on_foot", "driving", "menu", "flying"]
                for (let index = 0; index < modes.length; ++index) {
                    overlay.page = index
                    tryVerify(() => !!findChild(overlay, "ps5-mode-key-" + modes[index]))
                    verify(overlay.y + overlay.height <= player.height)
                }
                verify(testSettings.captured)
            } finally {
                player.width = oldWidth
                player.height = oldHeight
            }
        }
        function test_imported_console_menu_matches_its_controller_output() {
            const preset = testSettings.profiles[0]
            verify(testSettings.choose(preset))
            const source = JSON.parse(JSON.stringify(testSettings.profile))
            source.console_menu = {KeyH: "cross"}
            source.context_hotkeys.system_nav = "F10"
            try {
                verify(testSettings.saveCopy("Menu export", source), testSettings.error)
                verify(testSettings.exportProfile(fixture.exportUrl()))
                verify(testSettings.choose(preset))
                verify(testSettings.importProfile(fixture.exportUrl(), "Imported menu"), testSettings.error)
                verify(testSettings.resume(), testSettings.error)
                verify(fixture.nativeSystemKey(0x79, 0x44, true, false))
                verify(fixture.nativeSystemKey(0x79, 0x44, false, false))
                verify(fixture.nativeKey(0x48, 0x23, true))
                tryVerify(() => fixture.crossDown())
                tryCompare(testSettings, "situation", "system_nav")
                verify(fixture.nativeKey(0x70, 0x3b, true))
                verify(fixture.nativeKey(0x70, 0x3b, false))
                const overlay = findChild(controls, "ps5-bindings-overlay")
                tryCompare(overlay, "visible", true)
                compare(findChild(overlay, "ps5-live-situation").text, qsTr("主机菜单"))
                compare(findChild(overlay, "ps5-mode-key-system_nav").text, "F10")
                verify(findChild(overlay, "ps5-live-KeyH").text.includes("Cross"))
                verify(!findChild(overlay, "ps5-live-KeyF"))
                testSettings.release()
                verify(testSettings.resume(), testSettings.error)
                verify(fixture.nativeKey(0x48, 0x23, true))
                tryVerify(() => fixture.crossDown())
                testSettings.correct("on_foot")
                compare(testSettings.situation, "on_foot")
                testSettings.correct("system_nav")
                compare(testSettings.situation, "system_nav")
            } finally {
                testSettings.release()
                verify(testSettings.choose(preset))
            }
        }
        function test_player_gates_and_rebound_keys() {
            verify(testSettings.setShortcut("overlay", "F2"))
            verify(testSettings.setShortcut("control", "F4"))
            verify(findChild(controls, "ps5-player-control").text.includes("F4"))
            verify(!fixture.nativeKey(0x72, 0x3d, true))
            player.playerEnabled = false
            verify(!fixture.nativeKey(0x73, 0x3e, true))
            player.playerEnabled = true
            verify(fixture.nativeKey(0x73, 0x3e, true))
            verify(fixture.nativeKey(0x73, 0x3e, false))
            verify(testSettings.captured)
            verify(fixture.nativeKey(0x70, 0x3b, true))
            verify(!testSettings.overlayVisible)
            verify(fixture.nativeKey(0x71, 0x3c, true))
            verify(testSettings.overlayVisible)
            verify(findChild(controls, "ps5-live-control-key").text.includes("F4"))
            verify(findChild(controls, "ps5-live-hide-key").text.includes("F2"))
            fixture.releaseBy("shortcut")
            verify(!testSettings.captured && !testSettings.overlayVisible)
            fixture.disconnectConsole()
            tryCompare(testSettings, "available", false)
            verify(!findChild(controls, "ps5-player-control").visible)
            verify(!fixture.nativeKey(0x73, 0x3e, true))
        }
        function test_live_video_routes_data() {
            return [{ tag: "Remote Play", hdmi: false, viewer: false },
                    { tag: "Capture card", hdmi: true, viewer: false },
                    { tag: "View-only stream", hdmi: false, viewer: true },
                    { tag: "View-only capture card", hdmi: true, viewer: true }]
        }
        function test_live_video_routes(data) {
            if (!fixture.hasConsoleTransport()) skip("Configure VEYRA_FFMPEG_ROOT to run the sender checks")
            verify(fixture.connectVideoRoute(data.hdmi, data.viewer))
            tryCompare(testSettings, "available", !data.viewer)
            const overlay = findChild(controls, "ps5-bindings-overlay")
            if (data.viewer) {
                verify(!findChild(controls, "ps5-player-control").visible)
                verify(!fixture.nativeKey(0x72, 0x3d, true))
                compare(fixture.sentCount(), 0)
                return
            }
            verify(fixture.nativeKey(0x72, 0x3d, true))
            verify(fixture.nativeKey(0x72, 0x3d, false))
            verify(fixture.nativeKey(0x46, 0x21, true))
            tryVerify(() => fixture.sentTriangle())
            const start = fixture.sentCount()
            const focus = player.Window.window.activeFocusItem
            verify(fixture.nativeKey(0x70, 0x3b, true))
            verify(fixture.nativeKey(0x70, 0x3b, false))
            verify(overlay.visible && findChild(overlay, "ps5-live-KeyF").text.includes("Enter vehicle"))
            fixture.physicalCross()
            tryVerify(() => fixture.sentCount() >= start + 2)
            verify(fixture.heldTriangleSince(start), "Every submitted state kept the held action")
            verify(fixture.nativeKey(0x46, 0x21, false))
            verify(fixture.nativeKey(0x76, 0x41, true))
            verify(fixture.nativeKey(0x76, 0x41, false))
            tryCompare(testSettings, "situation", "driving")
            verify(findChild(overlay, "ps5-live-KeyW").text.includes("Accelerate"))
            verify(fixture.nativeKey(0x57, 0x11, true))
            tryVerify(() => fixture.sentThrottle())
            const throttle = fixture.sentCount()
            verify(fixture.nativeKey(0x70, 0x3b, true))
            verify(fixture.nativeKey(0x70, 0x3b, false))
            verify(testSettings.captured && !overlay.visible)
            compare(player.Window.window.activeFocusItem, focus)
            tryVerify(() => fixture.sentCount() >= throttle + 2)
            verify(fixture.heldThrottleSince(throttle))
            fixture.releaseBy("focus")
            verify(!testSettings.captured)
            tryVerify(() => fixture.sentNeutral())
        }
    }
}
