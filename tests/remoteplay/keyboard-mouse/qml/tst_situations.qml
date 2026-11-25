import QtQuick
import QtTest
import "../../../../qml/Veyra" as V
import "../../../../qml/Veyra/Ps5Bindings.js" as Bindings

Item {
    id: host
    width: 1000
    height: 730
    V.Ps5KeyboardDialog { id: editor; anchors.fill: parent; settings: testSettings }

    TestCase {
        name: "Ps5Situations"
        when: windowShown

        function test_profile_detection_inputs_and_shortcuts() {
            const initial = testSettings.situation
            mouseClick(findChild(editor, "section-2"))
            const driving = findChild(editor, "situation-driving")
            verify(!!driving)
            compare(driving.hotkey, "F7")
            verify(driving.sources.includes(qsTr("自适应扳机")))
            verify(driving.inputs.some(name => name.includes("Enter vehicle")))
            const flying = findChild(editor, "situation-flying")
            verify(flying.sources.includes(qsTr("触觉反馈")))
            compare(flying.hotkey, "F9")
            const aim = findChild(editor, "situation-aiming")
            compare(aim.inputs, [qsTr("按住 %1").arg(Bindings.inputName({}, "MouseRight"))])
            const menu = findChild(editor, "situation-system_nav")
            compare(menu.sources, [])
            compare(menu.inputs, [])
            compare(menu.hotkey, "")
            compare(testSettings.situation, initial)
        }

        function test_imported_bindings_and_legacy_feedback_forms() {
            const profile = {
                contexts: ["play", "zoom"],
                bindings: {"KeyR:layer=zoom": "none"},
                bindings_per_context: {play: {KeyR: "none"}},
                outputs: {none: {button: "none"}},
                look: {per_context: {zoom: {mouse_use: "aim"}}},
                auto_context_from_feedback: {seat: {context: "play", keep: true}, scope: "zoom"}
            }
            compare(Bindings.detectionSources(profile, "play"), [qsTr("手柄反馈")])
            compare(Bindings.detectionSources(profile, "zoom"), [qsTr("手柄反馈")])
            verify(!Bindings.situationInputs(profile).zoom)
            delete profile.bindings_per_context.play
            compare(Bindings.situationInputs(profile).zoom, [qsTr("按住 %1").arg("R")])
            profile.context_hotkeys = {play: "KeyR"}
            verify(!Bindings.situationInputs(profile).zoom)
        }
    }
}
