import QtQuick
import QtTest
import Veyra

Item {
    width: 640
    height: 480
    Item {
        id: host
        anchors.fill: parent
        property string dialog: ""
        property bool onboardingActive: false
        property real motionScale: 1
        function open(key) { dialog = key }
        function close() { dialog = "" }
    }
    QtObject {
        id: bridge
        property var ps5LoginPrompt: ({})
        property string sentId: ""
        property string sentPin: ""
        function ps5SendLoginPin(id, pin) {
            if (ps5LoginPrompt.requestId !== id || ps5LoginPrompt.pending) return false
            sentId = id
            sentPin = pin
            ps5LoginPrompt = Object.assign({}, ps5LoginPrompt, {pending:true})
            return true
        }
    }
    Ps5LoginPinDialog { id: panel; dialogHost: host; backend: bridge; instantTransition: true }
    Ps5LoginPinField {
        id: manual
        backend: bridge
        prompt: bridge.ps5LoginPrompt
        active: host.dialog === "ps5" || host.dialog === "capture"
        visible: active
    }
    TestCase {
        name: "Ps5LoginPin"
        when: windowShown

        function init() {
            bridge.ps5LoginPrompt = ({})
            bridge.sentId = ""
            bridge.sentPin = ""
            host.onboardingActive = false
            host.close()
            panel.offeredRequestId = ""
            wait(0)
        }

        function test_prompt_validates_ascii_preserves_leading_zero_and_waits() {
            bridge.ps5LoginPrompt = ({requestId:"1"})
            tryCompare(host, "dialog", "ps5-pin")
            const input = findChild(panel, "ps5-login-pin-input")
            const submit = findChild(panel, "ps5-login-pin-submit")
            verify(input && submit)
            compare(input.echoMode, TextInput.Password)
            input.text = "012"
            verify(!submit.enabled)
            input.text = "０１２３"
            verify(!submit.enabled)
            input.text = "01234567"
            verify(!submit.enabled)
            input.text = "0123"
            verify(submit.enabled)
            mouseClick(submit)
            compare(bridge.sentId, "1")
            compare(bridge.sentPin, "0123")
            compare(input.text, "")
            verify(!input.enabled)
            bridge.ps5LoginPrompt = ({requestId:"2", incorrect:true})
            tryCompare(input, "enabled", true)
            compare(input.text, "")
            bridge.ps5LoginPrompt = ({})
            tryCompare(host, "dialog", "")
        }

        function test_manual_capture_input_keeps_focus_and_rejects_stale_prompt() {
            host.open("capture")
            bridge.ps5LoginPrompt = ({requestId:"3", controlOnly:true})
            const input = findChild(manual, "ps5-login-pin-input")
            const submit = findChild(manual, "ps5-login-pin-submit")
            input.forceActiveFocus()
            input.text = "5678"
            wait(0)
            compare(host.dialog, "capture")
            verify(input.activeFocus)
            bridge.ps5LoginPrompt = ({requestId:"3", controlOnly:true, incorrect:true})
            compare(input.text, "5678")
            bridge.ps5LoginPrompt = ({requestId:"4", controlOnly:true, incorrect:true})
            compare(input.text, "")
            verify(!submit.enabled)
            compare(bridge.sentId, "")
            input.text = "9012"
            mouseClick(submit)
            compare(bridge.sentId, "4")
            compare(bridge.sentPin, "9012")
            compare(host.dialog, "capture")
            host.close()
            wait(0)
            compare(host.dialog, "")
        }

        function test_does_not_replace_other_dialog_or_repeat_after_close() {
            host.open("update")
            bridge.ps5LoginPrompt = ({requestId:"5"})
            wait(0)
            compare(host.dialog, "update")
            host.close()
            tryCompare(host, "dialog", "ps5-pin")
            host.close()
            wait(30)
            compare(host.dialog, "")
            host.onboardingActive = true
            bridge.ps5LoginPrompt = ({requestId:"6"})
            wait(0)
            compare(host.dialog, "")
            host.onboardingActive = false
            tryCompare(host, "dialog", "ps5-pin")
        }
    }
}
