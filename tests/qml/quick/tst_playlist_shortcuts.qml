import QtQuick
import QtQuick.Window
import QtTest

Item {
    Window {
        id: ownerWindow
        visible: true
        width: 800; height: 650
    Item {
    id: root
    width: 800; height: 650
    property var playlistWindow: null
    property int ownerEscapes: 0
    property int ambiguousEscapes: 0

    ListModel {
        id: playlist
        property bool busy: false
        property int sortMode: 0
        property int currentIndex: -1
        property int removed: 0
        property int played: 0
        property bool playing: false
        property bool autoAdvance: false
        signal error(string text)
        function indexOfPath(path) { return path === "selected.mp4" ? 0 : -1 }
        function remove(row) { ++removed }
        function play(row) { ++played }
    }
    // Same ownership as Main.qml: the main window handles Escape, except
    // while the independent playlist owns focus.
    Shortcut {
        sequence: "Escape"
        onActivated: ++root.ownerEscapes
        onActivatedAmbiguously: ++root.ambiguousEscapes
    }
    TestCase {
        name: "PlaylistShortcutOwnership"
        when: windowShown
        function initTestCase() {
            // The production test runner resolves all QML from its staging tree.
            const component = Qt.createComponent("../qml/Veyra/PlaylistWindow.qml")
            compare(component.status, Component.Ready, component.errorString())
            root.playlistWindow = component.createObject(root, {
                owner: ownerWindow,
                playlist: playlist, selectedPath: "selected.mp4"
            })
            verify(root.playlistWindow !== null)
        }
        function cleanupTestCase() { root.playlistWindow.destroy(); root.playlistWindow = null; ownerWindow.hide() }
        function init() {
            verify(root.Window.window !== null)
            compare(root.playlistWindow.owner, ownerWindow)
            root.playlistWindow.hide()
            ownerWindow.requestActivate()
            tryCompare(ownerWindow, "active", true)
            tryVerify(function() { return testWindowKeys.focused(ownerWindow) })
            root.ownerEscapes = 0; root.ambiguousEscapes = 0
            playlist.removed = 0; playlist.played = 0
        }
        function test_hidden_playlist_does_not_steal_keys() {
            verify(testWindowKeys.press(Qt.Key_Escape))
            tryCompare(root, "ownerEscapes", 1)
            compare(root.ambiguousEscapes, 0)
            verify(testWindowKeys.press(Qt.Key_Delete))
            verify(testWindowKeys.press(Qt.Key_Return))
            compare(playlist.removed, 0)
            compare(playlist.played, 0)
        }
        function test_visible_inactive_playlist_does_not_steal_keys() {
            root.playlistWindow.open()
            tryCompare(root.playlistWindow, "active", true)
            tryVerify(function() { return testWindowKeys.focused(root.playlistWindow) })
            ownerWindow.requestActivate()
            tryCompare(ownerWindow, "active", true)
            tryVerify(function() { return testWindowKeys.focused(ownerWindow) })
            verify(root.playlistWindow.visible)
            verify(testWindowKeys.press(Qt.Key_Escape))
            tryCompare(root, "ownerEscapes", 1)
            compare(root.ambiguousEscapes, 0)
            verify(root.playlistWindow.visible)
            verify(testWindowKeys.press(Qt.Key_Delete))
            verify(testWindowKeys.press(Qt.Key_Return))
            compare(playlist.removed, 0)
            compare(playlist.played, 0)
        }
        function test_active_playlist_keeps_its_keys() {
            root.playlistWindow.open()
            tryCompare(root.playlistWindow, "active", true)
            tryVerify(function() { return testWindowKeys.focused(root.playlistWindow) })
            verify(testWindowKeys.press(Qt.Key_Delete))
            verify(testWindowKeys.press(Qt.Key_Return))
            tryCompare(playlist, "removed", 1)
            tryCompare(playlist, "played", 1)
            verify(testWindowKeys.press(Qt.Key_Escape))
            tryCompare(root.playlistWindow, "visible", false)
            compare(root.ownerEscapes, 0)
            compare(root.ambiguousEscapes, 0)
        }
    }
    }
    }
}
