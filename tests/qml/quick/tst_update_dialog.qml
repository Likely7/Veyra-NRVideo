import QtQuick
import QtTest
import Veyra

Item {
    id: host
    width: 340
    height: 320
    property string dialog: "update"
    property real motionScale: 1
    function close() { dialog = "" }

    QtObject {
        id: updaterStub
        property bool available: false
        property bool downloading: false
        property bool installing: false
        property bool ready: false
        property bool restarting: false
        property int progress: 0
        property int installStep: 0
        property string errorText: ""
        property string targetVersion: "2.2.2"
        property string sizeText: "728 MB"
        property string releaseNotes: ""
        signal requested(string action)
        function start() { requested("start") }
        function cancel() { requested("cancel") }
        function restart() { requested("restart") }
    }

    UpdateDialog {
        id: updatePanel
        dialogHost: host
        update: updaterStub
        instantTransition: true
    }
    SignalSpy { id: actionsSpy; target: updaterStub; signalName: "requested" }

    TestCase {
        name: "UpdateDialog"
        when: windowShown

        function init() {
            host.dialog = "update"
            updaterStub.available = false
            updaterStub.downloading = false
            updaterStub.installing = false
            updaterStub.ready = false
            updatePanel.exportRunning = false
            updaterStub.errorText = ""
            actionsSpy.clear()
        }

        function click(item) {
            verify(item !== null)
            mouseClick(item, item.width / 2, item.height / 2)
        }

        function test_downloadThenInstallThenRestart() {
            const bar = findChild(updatePanel, "software-update-progress")
            updaterStub.downloading = true
            updaterStub.progress = 37
            compare(bar.visible, true)
            compare(bar.to, 100)
            compare(bar.value, 37)

            updaterStub.downloading = false
            updaterStub.installing = true
            updaterStub.installStep = 2
            compare(bar.visible, true)
            compare(bar.to, 4)
            compare(bar.value, 2)
            compare(updatePanel.primaryAction, null)

            updaterStub.installing = false
            updaterStub.ready = true
            compare(bar.visible, false)
            compare(actionsSpy.count, 0)
            tryVerify(() => updatePanel.primaryAction !== null)
            click(updatePanel.primaryAction)
            compare(actionsSpy.count, 1)
            compare(actionsSpy.signalArguments[0][0], "restart")
        }

        function test_restartWaitsForExport() {
            updaterStub.ready = true
            updatePanel.exportRunning = true
            tryVerify(() => updatePanel.primaryAction !== null)
            compare(updatePanel.primaryAction.enabled, false)
            click(updatePanel.primaryAction)
            compare(actionsSpy.count, 0)
            updatePanel.exportRunning = false
            compare(updatePanel.primaryAction.enabled, true)
        }

        function test_failureOffersRetry() {
            updaterStub.available = true
            updaterStub.errorText = "Installation failed"
            tryVerify(() => updatePanel.primaryAction !== null)
            click(updatePanel.primaryAction)
            compare(actionsSpy.count, 1)
            compare(actionsSpy.signalArguments[0][0], "start")
        }
    }
}
