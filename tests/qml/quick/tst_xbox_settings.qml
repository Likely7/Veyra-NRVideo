import QtQuick
import QtTest
import Veyra

Item {
    id: fixture
    width: 900; height: 1500

    QtObject {
        id: xboxMock
        property var state: ({
            signedIn: true, busy: false, status: "", code: "", verificationUri: "",
            selected: "XSX-1", selectedName: "Xbox Series X", lastLabel: "Xbox Series X",
            gamepad: true, softwareDecode: false, streaming: false, stream: ({})
        })
        property var consoles: [{ id: "XSX-1", name: "Xbox Series X", type: "Xbox Series X", power: "开机" }]
        property int loads: 0
        function load() { ++loads }
        function refresh() {}
        function select(id) { const next = Object.assign({}, state); next.selected = id; state = next }
        function set(key, value) { const next = Object.assign({}, state); next[key] = value; state = next }
    }

    QtObject {
        id: backend
        property var xbox: xboxMock
        property var xboxControl: ({ available: true, enabled: false, background: false, state: "off", message: "", keepaliveFps: -1, pad: "" })
        property var ps5Control: ({ available: false, enabled: false, state: "off", message: "", keepaliveFps: -1, pad: "" })
        property var ps5Profiles: []
        property var ps5: ({ profile: "" })
        property var preferences: ({})
        property string captureSignalLevel: "idle"
        property string captureSignalText: ""
        property string sourceSummary: ""
        property double captureFps: 0
        property string sourceFormatText: ""
        property string captureFormatLabel: ""
        property string capturePreviewUrl: ""
        property bool captureQueryBusy: false
        property var captureDevices: []
        property string captureDeviceId: ""
        property string captureDeviceLabel: ""
        property var captureResolutionOptions: []
        property string captureResolutionId: ""
        property var captureFrameRateOptions: []
        property string captureFrameRateId: ""
        property int captureHiddenFrameRates: 0
        property bool captureFormatCostHint: false
        property var capturePixelOptions: []
        property string capturePixelId: ""
        property bool captureBlackmagicDevice: false
        property string liveOpeningText: ""
        property bool captureMagewellDevice: false
        property string captureMagewellStatus: ""
        property double captureRequestedFps: 0
        property var captureAudioInputs: []
        property int captureAudioChoice: 0
        property bool captureVirtualAudioBusy: false
        property bool captureVirtualAudioInstalled: false
        property bool captureVirtualAudioRemovalBlocked: false
        property int captureAudioIngress: 0
        property bool captureMjpegSelected: false
        property int captureColorSpace: 0
        property int captureColorRange: 0
        property int captureBufferMode: 0
        property bool captureForceSdr: false
        property bool captureFlipVertical: false
        property string captureStatus: ""
        property int restarts: 0
        function refreshCaptureDevices() {}
        function xboxControlSet(key, value) {
            const next = Object.assign({}, xboxControl)
            next[key] = value
            xboxControl = next
        }
        function xboxControlRestart() { ++restarts }
    }

    Item {
        id: host
        anchors.fill: parent
        property string dialog: "capture"
        property real motionScale: 1
        function close() { dialog = "" }
        function open(key) { dialog = key }
        function startXbox() {}
    }

    Item { id: legacyStub; function open() {} }

    CaptureSourceDialog { id: captureDialog; property var veyra: backend; dialogHost: host; legacyPicker: legacyStub }
    XboxSourceDialog { id: xboxDialog; property var veyra: backend; dialogHost: host }

    TestCase {
        name: "XboxBackgroundControl"
        when: windowShown
        property bool oldMotion: false

        function initTestCase() { oldMotion = Theme.reduced; Theme.reduced = true; wait(60) }
        function cleanupTestCase() { Theme.reduced = oldMotion }

        function init() {
            backend.restarts = 0
            backend.xboxControl = { available: true, enabled: false, background: false, state: "off", message: "", keepaliveFps: -1, pad: "" }
            host.dialog = "capture"
            wait(20)
        }

        function test_capture_dialog_toggles_xbox_control_and_background() {
            const toggle = findChild(captureDialog, "capture-xbox-control")
            verify(toggle && toggle.visible)
            compare(toggle.checked, false)
            toggle.toggled(true)
            compare(backend.xboxControl.enabled, true)

            const bg = findChild(captureDialog, "capture-xbox-background")
            verify(bg && bg.visible)
            bg.toggled(true)
            compare(backend.xboxControl.background, true)

            backend.xboxControl = Object.assign({}, backend.xboxControl, { state: "connected", keepaliveFps: 30, pad: "Xbox Series X Controller" })
            const stateTag = findChild(captureDialog, "capture-xbox-state")
            verify(stateTag)
            compare(stateTag.kind, "ok")
            const padText = findChild(captureDialog, "capture-xbox-pad")
            verify(padText)
            compare(padText.text, "Xbox Series X Controller")

            backend.xboxControl = Object.assign({}, backend.xboxControl, { state: "failed", message: "test error" })
            const restartBtn = findChild(captureDialog, "capture-xbox-restart")
            verify(restartBtn && restartBtn.visible)
            restartBtn.clicked()
            compare(backend.restarts, 1)
        }

        function test_xbox_dialog_links_to_capture_settings() {
            host.dialog = "xbox"
            wait(20)
            const openCapture = findChild(xboxDialog, "xbox-open-capture")
            verify(openCapture && openCapture.visible)
            openCapture.clicked()
            compare(host.dialog, "capture")
        }
    }
}
