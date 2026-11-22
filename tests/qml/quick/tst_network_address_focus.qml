import QtQuick
import QtTest
import Veyra

Item {
    width: 800
    height: 540

    Item {
        id: host
        anchors.fill: parent
        property string dialog: ""
        property real motionScale: 1
        function close() { host.dialog = "" }
    }

    TextInput {
        id: clipboardText
        text: "https://example.invalid/video.mkv"
        visible: false
    }

    NetworkSourceDialog {
        id: networkLayer
        dialogHost: host
        instantTransition: true
    }

    TestCase {
        name: "NetworkAddressFocus"
        when: windowShown

        function test_pasteWithoutClick() {
            clipboardText.selectAll()
            clipboardText.copy()
            host.dialog = "network"
            const address = findChild(networkLayer, "network-address")
            verify(address !== null)
            tryVerify(() => address.children.some(child => child.activeFocus))
            keyClick(Qt.Key_V, Qt.ControlModifier)
            tryCompare(address, "text", clipboardText.text)
            host.close()
        }
    }
}
