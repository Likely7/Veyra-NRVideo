import QtQuick
import QtQuick.Window
import QtTest
import VeyraTest 1.0

Item {
    id: fixture

    Window {
        id: shell
        width: 720
        height: 370
        property string shownPage: "home"
        property string leavingPage: ""
        property bool fullscreen: false
        property bool maximized: false

        Item {
            id: surface
            anchors.fill: parent
        }
        HomePage {
            id: page
            home: surface
            pageId: "home"
        }

        Shortcut {
            sequence: "Space"
            onActivated: veyra.homeOpenAction = "global"
        }
    }
    TestCase {
        name: "HomeSourceKeyboard"
        when: windowShown

        function cleanup() {
            shell.hide()
            if (fixture.Window.window) fixture.Window.window.requestActivate()
        }

        function test_capture_and_xbox() {
            shell.show()
            shell.requestActivate()
            tryVerify(() => testWindowKeys.focused(shell))
            const capture = findChild(page, "home-source-capture")
            const xbox = findChild(page, "home-source-xbox")
            verify(!!capture && !!xbox)
            verify(capture.activeFocusOnTab && xbox.activeFocusOnTab)
            capture.forceActiveFocus()
            verify(testWindowKeys.press(Qt.Key_Space))
            compare(veyra.homeOpenAction, "capture")
            xbox.forceActiveFocus()
            verify(testWindowKeys.press(Qt.Key_Return))
            compare(veyra.homeOpenAction, "xbox")
        }
    }
}
