import QtQuick
import QtTest
import Veyra

Item {
    id: host
    width: 760
    height: 420
    property string dialog: "fixture"
    property string choice: ""
    property real motionScale: 1
    function close() { dialog = "" }

    DialogLayer {
        id: dialog
        dialogHost: host
        key: "fixture"
        title: "Connection settings"
        sub: "Choose the capture source and input format before connecting."
        dialogWidth: 760
        foot: "Close any other application using this capture device."
        actions: [
            { label: "Cancel" },
            { label: "Apply settings and reconnect", primary: true, icon: "play" }
        ]
        onActionTriggered: label => host.choice = label
        DialogTextField { implicitWidth: 485; text: "192.168.1.1" }
        Item { implicitHeight: 510 }
    }

    TestCase {
        name: "DialogLayout"
        when: windowShown

        function test_controls_inside_window_data() {
            return [
                { tag: "normal", width: 760, height: 420, horizontalScroll: false },
                { tag: "short", width: 720, height: 260, horizontalScroll: false },
                { tag: "portrait", width: 360, height: 330, horizontalScroll: true }
            ]
        }

        function test_controls_inside_window(data) {
            host.width = data.width
            host.height = data.height
            host.dialog = ""
            host.choice = ""
            host.dialog = "fixture"
            tryCompare(dialog, "motionS", 1)
            compare(dialog.width, Math.min(dialog.dialogWidth, data.width - 24))
            verify(dialog.bodyViewport.height > 38)
            compare(dialog.bodyViewport.contentWidth > dialog.bodyViewport.width, data.horizontalScroll)
            const action = dialog.primaryAction
            verify(action && action.visible)
            const location = action.mapToItem(host, 0, 0)
            verify(location.x >= 0 && location.y >= 0)
            verify(location.x + action.width <= host.width)
            verify(location.y + action.height <= host.height)
            mouseClick(action)
            compare(host.choice, "Apply settings and reconnect")
        }
    }
}
