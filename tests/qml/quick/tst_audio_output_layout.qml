import QtQuick
import QtTest
import Veyra

Item {
    id: host
    width: 680; height: 680
    property string dialog: "audio"
    property real motionScale: 1
    function close() { dialog = "" }

    AudioSettingsDialog { id: audio; dialogHost: host; instantTransition: true }

    TestCase {
        name: "AudioOutputLayout"
        when: windowShown

        function cleanup() { veyra.setPreference("audioForceStereo", false) }

        function test_existing_stereo_preference_and_auto_output() {
            const selector = findChild(audio, "audio-output-layout")
            verify(selector !== null)
            compare(selector.current, "auto")
            veyra.setPreference("audioForceStereo", true)
            compare(selector.current, "stereo")
            selector.picked("auto")
            compare(veyra.preferences.audioForceStereo, false)
            compare(selector.current, "auto")
            selector.picked("stereo")
            compare(veyra.preferences.audioForceStereo, true)
        }
    }
}
