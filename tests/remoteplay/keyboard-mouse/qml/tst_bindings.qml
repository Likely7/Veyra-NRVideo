import QtQuick
import QtQuick.Controls
import QtTest
import "../../../../qml/Veyra" as V

Item {
    width: 1150
    height: 800
    // No capture device or PS5 connection is opened by the source dialogs.
    readonly property var veyra: ({
        ps5Keyboard: testSettings, ps5Profiles: [], ps5Control: {available:true},
        ps5: {profile:"", busy:false, active:false, host:"", account:"", status:""},
        preferences: {}, captureSignalLevel:"idle", captureStatus:"", captureSignalText:"",
        captureFormatLabel:"", captureQueryBusy:false, captureDevices:[], captureDeviceLabel:"",
        captureDeviceId:"", captureResolutionId:"", captureResolutionOptions:[], captureFrameRateId:"",
        captureFrameRateOptions:[], capturePixelId:"", capturePixelOptions:[], captureAudioInputs:[],
        captureMagewellDevice:false, captureBlackmagicDevice:false, captureVirtualAudioBusy:false,
        captureVirtualAudioInstalled:false, captureForceSdr:false, captureFlipVertical:false, liveOpeningText:"",
        refreshCaptureDevices:function(){}, ps5Load:function(){}
    })
    Item {
        id: host
        anchors.fill: parent
        property string dialog: ""
        property real motionScale: 1
        function openPs5Keyboard(opener) { editorWindow.open(opener) }
        function open(key) {
            if (key === "ps5-keyboard") openPs5Keyboard(null)
            else dialog = key
        }
        function close() { dialog = "" }
        function resumePs5Keyboard() { close(); editorWindow.resumeCapture() }
    }
    V.Ps5SourceDialog { id: remote; dialogHost:host; instantTransition:true }
    V.CaptureSourceDialog { id: capture; dialogHost:host; instantTransition:true; legacyPicker:null }
    readonly property var editor: editorWindow.editor
    V.Ps5KeyboardWindow {
        id: editorWindow
        owner: host.Window.window
        settings: testSettings
        onResumeRequested: host.resumePs5Keyboard()
    }
    TestCase {
        name: "Ps5KeyboardEditor"
        when: windowShown

        function control(panel, name) {
            const item = findChild(panel, name)
            verify(item !== null, name)
            return item
        }
        function clickIn(panel, item) {
            verify(item.enabled && item.visible, item.objectName)
            const viewport = panel.bodyViewport
            tryVerify(() => viewport.width > 0 && viewport.height > 0)
            wait(20)
            let ancestor = item
            while (ancestor && ancestor !== viewport.contentItem) ancestor = ancestor.parent
            if (ancestor) {
                const y = item.mapToItem(viewport.contentItem, 0, 0).y
                viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, y - viewport.height / 2))
            }
            wait(5)
            mouseClick(item)
        }
        function openFrom(panel) {
            host.open(panel.key)
            tryCompare(panel, "shown", true)
            clickIn(panel, control(panel, "ps5-keyboard-open"))
            tryVerify(() => editor !== null && editorWindow.visible)
            verify(control(editor,"copy-name").text.length > 0, "Personal copy is initialized on open")
            compare(control(editor,"profile-picker").currentText, testSettings.selected)
        }
        function saveView(name) {
            const path = fixture.evidencePath(name)
            if (path) {
                wait(80)
                grabImage(editor).save(path)
            }
        }
        function mode(index) {
            clickIn(editor, control(editor, "view-" + index))
            compare(editor.viewMode, index)
        }
        function init() { host.Window.window.requestActivate(); tryCompare(host.Window.window, "active", true); verify(testSettings.choose(testSettings.profiles[0])) }
        function cleanup() { editorWindow.close(); host.close(); testSettings.release(); fixture.disconnectConsole(); tryCompare(testSettings, "available", false) }
        function pick(name, value) {
            const box = control(editor, name)
            const index = box.find(value)
            verify(index >= 0, name + " offers " + value)
            box.currentIndex = index
            box.activated(index)
        }
        function editDestination(key) {
            pick("destination", key)
            clickIn(editor, control(editor, "move"))
        }
        function enterCopyName(name) {
            clickIn(editor, control(editor, "section-4"))
            control(editor, "copy-name").text = name
            clickIn(editor, control(editor, "section-0"))
        }
        function test_driving_override_free_swap_replace_cancel() {
            openFrom(remote)
            enterCopyName("Driving test")
            pick("preview", "driving")
            mode(0)
            clickIn(editor, control(editor, "key-KeyW"))
            editDestination("KeyH")
            verify(control(editor, "conflict").text.includes("Accelerate"))
            verify(control(editor, "conflict").text.toLowerCase().includes("headlight"))
            clickIn(editor, control(editor, "cancel-move"))
            compare(testSettings.selected, testSettings.profiles[0])
            editDestination("KeyH")
            clickIn(editor, control(editor, "swap"))
            compare(testSettings.selected, "Driving test")
            compare(testSettings.profile.bindings_per_context.driving.KeyH, "accelerate")
            compare(testSettings.profile.bindings_per_context.driving.KeyW, "headlights")
            compare(testSettings.profile.bindings.KeyW, "move_forward")
            mode(2)
            control(editor, "search").text = "Accelerate"
            clickIn(editor, control(editor, "binding-KeyH"))
            editDestination("KeyE")
            clickIn(editor, control(editor, "replace"))
            compare(testSettings.profile.bindings_per_context.driving.KeyE, "accelerate")
            verify(!findChild(editor, "binding-KeyH"))
            editDestination("KeyB")
            compare(testSettings.profile.bindings_per_context.driving.KeyB, "accelerate")
            verify(!control(editor,"replace").visible)
            verify(fixture.reloadCopy("Driving test", testSettings.dpi))
            control(editor, "done").clicked()
            openFrom(capture)
            pick("preview", "driving")
            mode(0)
            clickIn(editor, control(editor, "key-KeyB"))
            verify(control(editor,"selected-binding").text.includes("Accelerate"))
        }
        function test_base_edit_and_profile_round_trip() {
            openFrom(capture)
            const original = testSettings.profile
            enterCopyName("Road")
            mode(0)
            clickIn(editor, control(editor, "key-KeyF"))
            clickIn(editor, control(editor, "base-edit"))
            editDestination("KeyB")
            verify(control(editor,"affected-situations").text.includes(control(editor,"preview").displayText))
            compare(testSettings.selected, testSettings.profiles[0])
            clickIn(editor, control(editor, "replace"))
            compare(testSettings.profile.bindings.KeyB, "enter_vehicle")
            compare(testSettings.profile.bindings.KeyF, undefined)
            compare(testSettings.profile.bindings_per_context.driving.KeyF, "exit_vehicle")
            clickIn(editor, control(editor, "section-4"))
            const required = original.required_game_settings.find(s => s.importance === "required")
            const recommended = original.required_game_settings.find(s => s.importance !== "required")
            for (const item of [required, recommended]) {
                const row = control(editor, "setting-" + item.id)
                verify(row.visible)
                compare(row.label, item.label.en)
                compare(row.value, item.value_label.en)
            }
            clickIn(editor, control(editor, "section-1"))
            const dpi = control(editor, "dpi")
            dpi.value = 2400
            dpi.valueModified()
            for (const [field, value] of [["sensitivity", 1.75], ["aim_sensitivity_multiplier", 0.85]]) {
                const input = control(editor, field)
                input.text = String(value)
                input.editingFinished()
                compare(testSettings.profile.look[field], value)
            }
            const portable = testSettings.profile
            function useFileDialog(action) {
                clickIn(editor, control(editor, "section-4"))
                const dialog = control(editor, action + "-dialog")
                dialog.selectedFile = fixture.exportUrl()
                clickIn(editor, control(editor, action + "-button"))
                tryCompare(dialog, "visible", true)
                compare(dialog.selectedFile.toString(), fixture.exportUrl().toString())
                dialog.accept()
                compare(testSettings.error, "")
            }
            useFileDialog("export")
            pick("profile-picker", testSettings.profiles[0])
            compare(testSettings.profile, original)
            compare(dpi.value, 2400)
            useFileDialog("import")
            compare(testSettings.selected, "roundtrip")
            compare(testSettings.profile, portable)
            compare(testSettings.profile.look.per_context, original.look.per_context)
            compare(testSettings.profile.recognition_rules, original.recognition_rules)
            compare(testSettings.profile.look.dpi, original.look.dpi)
            compare(testSettings.dpi, 2400)
            verify(fixture.reloadCopy("roundtrip", 2400))
            useFileDialog("import")
            compare(testSettings.selected, "roundtrip (2)")
            verify(testSettings.profiles.includes("roundtrip"))
            control(editor, "done").clicked()
            openFrom(remote)
            mode(0)
            clickIn(editor, control(editor, "key-KeyB"))
            verify(control(editor, "selected-binding").text.includes("Enter vehicle"))
        }
        function test_00_optional_preview_can_be_skipped_and_reopened() {
            fixture.connectConsole()
            tryCompare(testSettings, "available", true)
            editorWindow.requestControl()
            const offer = control(editorWindow, "ps5-input-check")
            tryCompare(offer, "offer", true)
            const offerImage = fixture.evidencePath("input-check-offer.png")
            if (offerImage) { waitForRendering(offer, 80); grabImage(offer).save(offerImage) }
            control(offer, "practice-skip").clicked()
            tryCompare(testSettings, "captured", true)
            testSettings.release()
            verify(testSettings.inputCheckOffered)
            verify(testSettings.setShortcut("control", "F11"))
            verify(testSettings.setShortcut("overlay", "F12"))
            for (const connected of [true, false]) {
                if (!connected) fixture.disconnectConsole()
                tryCompare(testSettings, "available", connected)
                openFrom(remote)
                clickIn(editor, control(editor, "section-3"))
                clickIn(editor, control(editor, "try-controls"))
                const check = control(editorWindow, "ps5-input-check")
                const pad = control(check, "practice-pad")
                tryCompare(check, "offer", false)
                const before = fixture.submissions()
                pad.forceActiveFocus()
                keyClick(Qt.Key_W)
                tryCompare(check, "lastKey", "W")
                mouseMove(pad, 30, 35)
                tryCompare(check, "moved", true)
                keyClick(Qt.Key_F11)
                tryCompare(check, "localControl", true)
                keyClick(Qt.Key_F12)
                tryCompare(check, "localOverlay", true)
                const practiceImage = fixture.evidencePath("input-check-overlay.png")
                if (practiceImage) { waitForRendering(check, 80); grabImage(check).save(practiceImage) }
                compare(testSettings.captured, false)
                compare(fixture.submissions(), before)
                control(check, "practice-done").clicked()
                tryVerify(() => editor && editor.section === 3)
                control(editor, "done").clicked()
                host.close()
            }
            verify(testSettings.setShortcut("control", "F3"))
            verify(testSettings.setShortcut("overlay", "F1"))
        }
        function test_device_identity_follows_the_focused_practice_window() {
            openFrom(remote)
            clickIn(editor, control(editor, "section-3"))
            clickIn(editor, control(editor, "try-controls"))
            tryCompare(editorWindow, "active", true)
            compare(fixture.sharedDeviceObservations(editorWindow, host.Window.window),
                    ["", "", "Practice keyboard"])
        }
        function test_practice_numpad_keys_and_modifier_chords() {
            verify(testSettings.setShortcut("control", "Numpad1"))
            verify(testSettings.setShortcut("overlay", "Numpad2"))
            try {
                openFrom(remote)
                clickIn(editor, control(editor, "section-3"))
                clickIn(editor, control(editor, "try-controls"))
                const check = control(editorWindow, "ps5-input-check")
                control(check, "practice-pad").forceActiveFocus()
                fixture.practiceKey(Qt.Key_1, 0x02, Qt.NoModifier) // Top-row 1
                fixture.practiceKey(Qt.Key_1, 0x4f, Qt.KeypadModifier | Qt.ControlModifier)
                compare(check.localControl, false)
                fixture.practiceKey(Qt.Key_1, 0x4f, Qt.KeypadModifier)
                compare(check.localControl, true)
                fixture.practiceKey(Qt.Key_2, 0x50, Qt.KeypadModifier | Qt.ShiftModifier)
                compare(check.localOverlay, false)
                fixture.practiceKey(Qt.Key_2, 0x50, Qt.KeypadModifier)
                compare(check.localOverlay, true)
                fixture.practiceKey(Qt.Key_1, 0x4f, Qt.KeypadModifier)
                verify(!check.localControl && !check.localOverlay && !testSettings.captured)
            } finally {
                verify(testSettings.setShortcut("control", "F3"))
                verify(testSettings.setShortcut("overlay", "F1"))
            }
        }
        function test_default_action_list_and_press_to_assign() {
            openFrom(remote)
            compare(editor.section, 0)
            compare(editor.viewMode, 2)
            enterCopyName("Typed key")
            control(editor, "search").text = "Enter vehicle"
            clickIn(editor, control(editor, "binding-KeyF"))
            clickIn(editor, control(editor, "listen-binding"))
            keyClick(Qt.Key_B)
            tryCompare(testSettings, "selected", "Typed key")
            compare(testSettings.profile.bindings_per_context.on_foot.KeyB, "enter_vehicle")
        }
        function test_press_to_assign_preserves_a_situation_shortcut() {
            openFrom(remote)
            enterCopyName("Reserved situation key")
            control(editor, "search").text = "Enter vehicle"
            clickIn(editor, control(editor, "binding-KeyF"))
            const original = JSON.stringify(testSettings.profile)
            const preset = testSettings.selected
            const listen = control(editor, "listen-binding")
            editorWindow.requestActivate()
            tryCompare(editorWindow, "active", true)
            clickIn(editor, listen)
            verify(editor.listening && listen.activeFocus)
            fixture.practiceKey(Qt.Key_F6, 0x40, Qt.NoModifier)
            compare(editor.listening, false)
            compare(testSettings.selected, preset)
            compare(JSON.stringify(testSettings.profile), original)
            const conflict = control(editor, "conflict")
            verify(conflict.visible && conflict.text.includes("F6"))
            verify(conflict.text.includes("Enter vehicle"))
            verify(conflict.text.includes(control(editor, "preview").displayText))
            verify(!control(editor, "swap").visible && !control(editor, "replace").visible)
            clickIn(editor, listen)
            fixture.practiceKey(Qt.Key_B, 0x30, Qt.NoModifier)
            compare(testSettings.selected, "Reserved situation key")
            compare(testSettings.profile.bindings_per_context.on_foot.KeyB, "enter_vehicle")
            compare(testSettings.profile.context_hotkeys.on_foot, "F6")
        }
        function test_controller_callouts_follow_preview_and_accept_keyboard() {
            openFrom(remote)
            const liveSituation = testSettings.situation
            mode(1)
            const triangle = control(editor, "callout-triangle")
            verify(triangle.Accessible.name.includes("F"))
            verify(triangle.Accessible.name.includes("Enter vehicle"))
            const create = control(editor, "callout-share")
            create.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            compare(editor.filterControl, "share")
            compare(editor.chosen, undefined)
            verify(create.Accessible.name.includes(control(editor, "selected-binding").text))
            pick("preview", "driving")
            verify(control(editor, "pad-share").selectedTile)
            verify(create.Accessible.name.includes(control(editor, "selected-binding").text))
            verify(triangle.Accessible.name.includes("Exit vehicle"))
            pick("preview", "on_foot")
            clickIn(editor, control(editor, "pad-r2"))
            verify(control(editor, "selected-binding").text.includes("Shoot"))
            pick("preview", "driving")
            verify(control(editor, "pad-r2").selectedTile)
            verify(control(editor, "selected-binding").text.includes("Accelerate"))
            verify(control(editor, "binding-KeyW").visible)
            compare(testSettings.situation, liveSituation)
        }
        function test_controller_mouse_motion_follows_the_profiles_stick() {
            openFrom(remote)
            mode(1)
            for (const [scene, stick] of [["on_foot", "right_stick"], ["aiming", "right_stick"], ["map", "left_stick"]]) {
                pick("preview", scene)
                const callout = control(editor, "callout-" + stick)
                clickIn(editor, callout)
                const motion = control(editor, "binding-MouseMotion")
                clickIn(editor, motion)
                verify(callout.Accessible.name.includes(editor.chosen.shortTitle))
                verify(control(editor, "selected-binding").text.includes(editor.chosen.shortTitle))
                verify(!control(editor, "listen-binding").visible)
                verify(!control(editor, "move").visible)
                clickIn(editor, control(editor, "callout-" + (stick === "left_stick" ? "right_stick" : "left_stick")))
                verify(!findChild(editor, "binding-MouseMotion"))
            }
        }
        function test_controller_tab_moves_from_callouts_to_bindings() {
            openFrom(remote)
            mode(1)
            control(editor, "callout-ps").forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Tab)
            tryCompare(control(editor, "binding-KeyE"), "activeFocus", true)
        }
        function test_front_view_button_positions() {
            openFrom(remote)
            mode(1)
            const drawing = control(editor, "controller-drawing")
            tryCompare(drawing, "status", Image.Ready)
            tryVerify(() => drawing.visible && drawing.width > 80 && drawing.height > 40)
            // Press the pictured controls directly. Stick travel uses the outer rim.
            const points = [
                ["l2", 62, 8], ["l1", 46, 15], ["r2", 208, 8], ["r1", 224, 15],
                ["share", 74, 26], ["options", 196, 26], ["touchpad", 135, 35],
                ["triangle", 217, 36], ["square", 197, 55], ["circle", 236, 55], ["cross", 217, 75],
                ["dpad_up", 54, 43], ["dpad_down", 54, 68], ["dpad_left", 42, 56], ["dpad_right", 66, 56],
                ["left_stick", 80, 91], ["l3", 93, 91], ["right_stick", 190, 91], ["r3", 177, 91],
                ["ps", 135, 89]
            ]
            for (const [name, x, y] of points) {
                const viewport = editor.bodyViewport
                const px = x * drawing.width / 270
                const py = y * drawing.height / 184
                const pos = drawing.mapToItem(viewport.contentItem, px, py)
                viewport.contentY = Math.max(0, Math.min(viewport.contentHeight - viewport.height, pos.y - viewport.height / 2))
                wait(20)
                mouseClick(drawing, px, py)
                compare(editor.filterControl, name, "Click on the drawing at " + x + ", " + y)
            }
        }
        function test_compact_controller_shows_the_selection_beside_the_drawing() {
            openFrom(remote)
            const previousWidth = editorWindow.width
            const previousHeight = editorWindow.height
            try {
                editorWindow.width = 660
                editorWindow.height = 565
                tryCompare(editor, "width", 660)
                tryCompare(editor, "height", 565)
                mode(1)
                const viewport = editor.bodyViewport
                const drawing = control(editor, "controller-drawing")
                const summary = control(editor, "compact-controller-selection")
                const bindings = control(summary, "compact-control-bindings")
                function verifyVisibleBesideDrawing() {
                    tryVerify(() => summary.visible && bindings.visible && bindings.height > 0)
                    const pad = drawing.mapToItem(viewport, 0, 0)
                    const card = summary.mapToItem(viewport, 0, 0)
                    verify(pad.y >= 0 && pad.y + drawing.height <= viewport.height)
                    verify(card.y >= 0 && card.y + summary.height <= viewport.height)
                    verify(card.x >= pad.x + drawing.width && card.x + summary.width <= viewport.width)
                }
                verifyVisibleBesideDrawing()
                verify(bindings.text.includes("F") && bindings.text.includes("Enter vehicle"))
                saveView("controller-compact.png")
                mouseClick(control(editor, "pad-r2"))
                pick("preview", "driving")
                verifyVisibleBesideDrawing()
                verify(bindings.text.includes("W") && bindings.text.includes("Accelerate"))
                saveView("controller-compact-driving.png")
            } finally {
                editorWindow.width = previousWidth
                editorWindow.height = previousHeight
            }
        }
        function test_device_selection_and_search() {
            openFrom(remote)
            mode(0)
            clickIn(editor, control(editor, "key-F7"))
            verify(control(editor, "selected-binding").text.includes("Driving"))
            clickIn(editor, control(editor, "key-KeyF"))
            saveView("keyboard-page.png")
            mode(1)
            const triangle = control(editor, "pad-triangle")
            verify(triangle.selectedTile, "Controller highlights the selected input's output")
            clickIn(editor, triangle)
            saveView("controller-page.png")
            verify(control(editor, "binding-KeyF").text.includes("Enter vehicle"))
            clickIn(editor, control(editor, "pad-right_stick"))
            verify(control(editor, "binding-Tab:hold").visible)
            clickIn(editor, control(editor, "pad-touchpad"))
            verify(control(editor, "binding-KeyM").visible)
            verify(control(editor, "binding-KeyV").visible)
            mode(0)
            clickIn(editor, control(editor, "key-MouseLeft"))
            verify(control(editor, "selected-binding").text.includes("Shoot"))
            mode(2)
            const search = control(editor, "search")
            tryCompare(search, "activeFocus", true)
            for (const key of [Qt.Key_V, Qt.Key_E, Qt.Key_H, Qt.Key_I, Qt.Key_C, Qt.Key_L, Qt.Key_E]) keyClick(key)
            const row = control(editor, "binding-KeyF")
            verify(row.visible)
            verify(!findChild(editor, "binding-KeyW"))
            keyClick(Qt.Key_Tab)
            tryCompare(row, "activeFocus", true)
            keyClick(Qt.Key_Space)
            verify(control(editor, "selected-binding").text.includes("Enter vehicle"))
            control(editor, "done").clicked()
        }
        function test_layer_without_button_requires_a_move_decision() {
            const personal = JSON.parse(JSON.stringify(testSettings.profile))
            personal.bindings["F11:layer=aiming"] = "no_function"
            personal.layers = {quick: {label: {en: "Quick actions"}, bindings: {KeyB: "jump"}}}
            personal.bindings["F2:layer_toggle=quick"] = "no_function"
            personal.bindings.F12 = "no_function"
            verify(testSettings.saveCopy("Layer binding", personal), testSettings.error)
            openFrom(remote)
            mode(0)
            clickIn(editor, control(editor, "key-F2"))
            compare(editor.selectedSpec, "F2:layer_toggle=quick")
            verify(editor.chosen.title.includes("Quick actions"))
            verify(editor.chosen.input.includes(qsTr("切换")))
            clickIn(editor, control(editor, "key-F11"))
            compare(editor.selectedSpec, "F11:layer=aiming")
            verify(editor.chosen.title.includes("Aiming"))
            compare(editor.chosen.controls, [])
            verify(!control(editor, "selected-binding").text.includes("No function"))
            clickIn(editor, control(editor, "key-KeyF"))
            editDestination("F11")
            verify(control(editor, "conflict").text.includes("Aiming"))
            clickIn(editor, control(editor, "cancel-move"))
            compare(testSettings.profile, personal)
            editDestination("F11")
            clickIn(editor, control(editor, "swap"))
            let changed = testSettings.profile.bindings_per_context.on_foot
            compare(changed["KeyF:layer=aiming"], "no_function")
            compare(changed.F11, "enter_vehicle")
            editDestination("KeyF")
            clickIn(editor, control(editor, "replace"))
            changed = testSettings.profile.bindings_per_context.on_foot
            compare(changed.KeyF, "enter_vehicle")
            compare(changed["KeyF:layer=aiming"], undefined)
            editDestination("F12")
            compare(testSettings.profile.bindings_per_context.on_foot.F12, "enter_vehicle")
            verify(!control(editor, "conflict").visible)
        }
        function test_swap_gesture_groups_and_combined_outputs() {
            const personal = JSON.parse(JSON.stringify(testSettings.profile))
            personal.bindings["KeyF:double"] = "jump"
            personal.bindings["KeyB"] = "melee"
            personal.bindings["KeyB:double"] = "map"
            personal.outputs.enter_vehicle.left_stick = [0, 1]
            personal.macros = { two_buttons: { steps: [{tap:["square","circle"]}] } }
            personal.outputs.sequence = {macro:"two_buttons"}
            personal.bindings.KeyN = "sequence"
            verify(testSettings.saveCopy("Gestures", personal), testSettings.error)
            openFrom(remote)
            mode(0)
            clickIn(editor, control(editor, "key-KeyF"))
            clickIn(editor, control(editor, "gesture-KeyF:double"))
            compare(editor.selectedSpec, "KeyF:double")
            verify(control(editor, "selected-binding").text.includes("Jump"))
            mode(1)
            clickIn(editor, control(editor, "pad-square"))
            verify(control(editor, "binding-KeyN").visible)
            clickIn(editor, control(editor, "pad-left_stick"))
            verify(control(editor, "binding-KeyF").text.includes("Enter vehicle"))
            clickIn(editor, control(editor, "binding-KeyF"))
            const destinations = control(editor,"destination")
            compare(destinations.find("F7"), -1)
            compare(destinations.find("Backquote"), -1)
            editDestination("KeyB")
            clickIn(editor, control(editor, "swap"))
            const changed = testSettings.profile.bindings_per_context.on_foot
            compare(changed.KeyB, "enter_vehicle")
            compare(changed["KeyB:double"], "jump")
            compare(changed.KeyF, "melee")
            compare(changed["KeyF:double"], "map")
        }
        function test_edit_during_capture_and_native_releases() {
            fixture.connectConsole()
            tryCompare(testSettings, "available", true)
            function resume() {
                host.open(remote.key)
                clickIn(remote, control(remote, "resume-capture"))
                tryCompare(testSettings, "captured", true)
            }
            resume()
            verify(fixture.nativeKey(0x46, 0x21, true))
            tryVerify(() => fixture.triangleDown())
            openFrom(remote)
            compare(testSettings.captured, false)
            compare(fixture.neutral(), true)
            const before = fixture.submissions()
            mode(2)
            keyClick(Qt.Key_F)
            fixture.physicalCross()
            compare(fixture.submissions(), before)
            mode(0)
            enterCopyName("During play")
            mode(0)
            clickIn(editor, control(editor, "key-KeyF"))
            editDestination("KeyB")
            compare(testSettings.profile.bindings_per_context.on_foot.KeyB, "enter_vehicle")
            pick("preview", "driving")
            compare(testSettings.situation, "on_foot")
            const done = control(editor, "done")
            verify(done)
            mouseClick(done)
            wait(50)
            compare(testSettings.captured, false)
            openFrom(capture)
            mouseClick(control(editor, "resume"))
            tryCompare(testSettings, "captured", true)
            verify(fixture.nativeKey(0x42, 0x30, true))
            tryVerify(() => fixture.triangleDown())
            fixture.releaseBy("focus")
            for (const release of ["shortcut", "alt-tab", "focus"]) {
                resume()
                verify(fixture.nativeKey(0x42, 0x30, true))
                tryVerify(() => fixture.triangleDown())
                fixture.releaseBy(release)
                verify(!testSettings.captured, "released after " + release)
                verify(fixture.neutral())
                wait(30)
                compare(testSettings.captured, false)
            }
            fixture.allowMouseGrab(false)
            host.open(remote.key)
            clickIn(remote, control(remote, "resume-capture"))
            tryVerify(() => editor !== null && editorWindow.visible)
            verify(control(editor, "dialog-foot").visible)
            compare(control(editor, "dialog-foot").text, testSettings.error)
            verify(testSettings.error.length > 0)
            fixture.allowMouseGrab(true)
            fixture.disconnectConsole()
            tryCompare(testSettings, "available", false)
        }
        function test_release_before_next_situation_poll() {
            fixture.connectConsole()
            verify(testSettings.resume(), testSettings.error)
            verify(fixture.nativeKey(0x76, 0x41, true)) // F7 selects Driving.
            verify(fixture.nativeKey(0x76, 0x41, false))
            verify(fixture.nativeKey(0x57, 0x11, true))
            verify(fixture.waitForThrottle())
            fixture.releaseBy("shortcut")
            compare(testSettings.situation, "driving")
            verify(fixture.neutral())
            verify(testSettings.resume(), testSettings.error)
            verify(fixture.nativeKey(0x57, 0x11, true))
            verify(fixture.waitForThrottle())
            // The provider can replace the console before the next UI poll.
            fixture.connectConsole()
            testSettings.release()
            verify(testSettings.resume(), testSettings.error)
            compare(testSettings.situation, "on_foot")
            testSettings.correct("driving")
            fixture.releaseBy("focus")
            compare(testSettings.situation, "driving")
            verify(testSettings.saveCopy("Other game", testSettings.profile))
            verify(testSettings.choose(testSettings.profiles[0]))
            verify(testSettings.resume(), testSettings.error)
            compare(testSettings.situation, "on_foot")
        }
        function test_keyboard_launch_and_escape() {
            host.open(remote.key)
            const launcher = control(remote, "ps5-keyboard-open")
            launcher.forceActiveFocus(Qt.TabFocusReason)
            keyClick(Qt.Key_Space)
            tryVerify(() => editor && editorWindow.visible)
            verify(!control(editor, "move").enabled)
            const preview = control(editor, "preview")
            verify(preview.width >= 110 && preview.displayText.length > 0)
            mode(2)
            control(editor, "search").text = "__no_such_binding__"
            verify(!findChild(editor, "binding-KeyF"))
            keyClick(Qt.Key_Escape)
            tryCompare(editorWindow, "visible", false)
            compare(host.dialog, remote.key)
            tryCompare(launcher, "activeFocus", true)
        }
        function test_window_resize_and_return() {
            openFrom(remote)
            compare(editor.Window.window, editorWindow)
            verify(editorWindow !== host.Window.window)
            const ownerWidth = host.width
            const ownerHeight = host.height
            for (const size of [[1000,720], [590,480]]) {
                editorWindow.width = size[0]
                editorWindow.height = size[1]
                tryCompare(editor, "width", size[0])
                tryCompare(editor, "height", size[1])
                mode(0)
                clickIn(editor, control(editor, "key-KeyF"))
                const label = control(editor, "selected-binding")
                verify(label.text.includes("Triangle"))
                const pos = label.mapToItem(editor, 0, 0)
                verify(pos.y >= 0 && pos.y + label.height <= editor.height)
                compare(host.width, ownerWidth)
                compare(host.height, ownerHeight)
                const viewport = editor.bodyViewport
                const space = control(editor, "key-Space")
                space.forceActiveFocus(Qt.TabFocusReason)
                tryVerify(() => {
                    const top = space.mapToItem(viewport, 0, 0).y
                    return top >= 0 && top + space.height <= viewport.height
                }, 1000, "The focused key is visible above the inspector")
            }
            editorWindow.close()
            compare(host.dialog, remote.key)
            openFrom(capture)
            compare(editorWindow.width, 590)
            compare(editorWindow.height, 480)
            editorWindow.width = 1000
            editorWindow.height = 720
        }
        function test_offline_from_both_video_sources() {
            compare(testSettings.available, false)
            for (const panel of [remote, capture]) {
                openFrom(panel)
                mode(0)
                clickIn(editor, control(editor, "key-KeyF"))
                const selection = control(editor, "selected-binding")
                verify(selection.text.includes("Enter vehicle"))
                verify(selection.text.includes("Triangle"))
                control(editor, "done").clicked()
                compare(host.dialog, panel.key)
                verify(!editorWindow.visible)
            }
        }
    }
}
