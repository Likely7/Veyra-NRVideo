import QtQuick
import QtTest
import Veyra

Item {
    width: 480; height: 360

    QtObject {
        id: bridge
        property string sourceKind: "capture"
        property var preferences: ({ captureAudioMonitor: false })
        property var audioTracks: []
        property int selectedAudioTrack: -1
        property int writes: 0
        function setPreference(key, value) {
            if (key !== "captureAudioMonitor")
                return false
            preferences = { captureAudioMonitor: value }
            ++writes
            return true
        }
    }

    Item { id: anchor; x: 220; y: 300; width: 28; height: 28 }
    PlayerAudioMenu { id: menu; backend: bridge }
    SignalSpy { id: settingsSpy; target: menu; signalName: "settingsRequested" }

    TestCase {
        name: "CaptureAudioMenu"
        when: windowShown

        function init() {
            bridge.sourceKind = "capture"
            bridge.preferences = { captureAudioMonitor: false }
            bridge.audioTracks = []
            bridge.selectedAudioTrack = -1
            bridge.writes = 0
            settingsSpy.clear()
        }

        function cleanup() { menu.close() }

        function findOption(item, text) {
            if (item.objectName === "vmenu-option-label" && item.text === text)
                return item
            for (const child of item.children || []) {
                const found = findOption(child, text)
                if (found)
                    return found
            }
            return null
        }

        function clickOption(text) {
            menu.openAt(anchor, "up")
            tryCompare(menu, "opened", true)
            const row = findOption(menu.contentItem, text)
            verify(row !== null)
            mouseClick(row)
            tryCompare(menu, "visible", false)
        }

        function test_capture_without_tracks_can_restore_local_sound() {
            verify(menu.captureSource)
            verify(menu.monitorOff)
            compare(menu.items[0].tag, qsTr("已关闭"))
            compare(bridge.writes, 0)
            clickOption(qsTr("本机音频监听"))
            compare(bridge.preferences.captureAudioMonitor, true)
            compare(bridge.writes, 1)
            verify(!menu.monitorOff)
            verify(menu.items[0].checked)
            clickOption(qsTr("本机音频监听"))
            compare(bridge.preferences.captureAudioMonitor, false)
            compare(bridge.writes, 2)
            compare(bridge.selectedAudioTrack, -1)
        }

        function test_trackless_capture_opens_audio_settings() {
            clickOption(qsTr("音频设置…"))
            compare(settingsSpy.count, 1)
            compare(bridge.writes, 0)
        }

        function test_file_track_selection_leaves_capture_monitoring_alone() {
            bridge.sourceKind = "file"
            verify(!menu.captureSource)
            verify(!menu.monitorOff)
            verify(menu.items[0].disabled)
            bridge.audioTracks = [{ label: "Main", index: 0, channels: 2 },
                                  { label: "Commentary", index: 1, channels: 2 }]
            clickOption("Commentary")
            compare(bridge.selectedAudioTrack, 1)
            compare(bridge.writes, 0)
            compare(bridge.preferences.captureAudioMonitor, false)
        }
    }
}
