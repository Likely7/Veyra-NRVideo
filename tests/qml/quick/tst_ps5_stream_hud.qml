import QtQuick
import QtTest
import Veyra 1.0

Item {
    width: 720
    height: 200

    QtObject {
        id: bridge
        property var moonlight: null
        property var xbox: null
        property bool ps5VideoActive: true
        property bool moonlightCaptured: false
        property bool moonlightStatsVisible: false
        property int sampleCalls: 0
        function ps5StreamStatistics() {
            ++sampleCalls
            return {
                width: 1920, height: 1080, codec: "H.265", hdr: true,
                hardware: true, decodeConfirmed: true, decodeMs: 1.2,
                videoMbps: 18.5, requestedMbps: 30, receivedFps: 29.5,
                decodedFps: 29.4, submitFps: 88.2, dropped: 6,
                packets: 317, lost: 4
            }
        }
    }

    StreamHud { id: overlay; player: bridge }

    TestCase {
        name: "Ps5StreamHud"
        when: windowShown
        function test_samplesOnlyWhileVisible() {
            verify(!overlay.visible)
            wait(1100)
            compare(bridge.sampleCalls, 0)

            bridge.moonlightStatsVisible = true
            tryCompare(bridge, "sampleCalls", 1)
            verify(overlay.visible)
            const format = findChild(overlay, "stream-hud-format")
            verify(format.text.startsWith("PS5") && format.text.includes("1920×1080") && format.text.includes("H.265"))
            const bitrates = findChild(overlay, "stream-hud-ps5-bitrate")
            verify(bitrates.visible && bitrates.text.includes("18.5 Mbps") && bitrates.text.includes("30 Mbps"))
            const packets = findChild(overlay, "stream-hud-ps5-packets")
            verify(packets.visible && packets.text.includes("317") && packets.text.includes("4"))
            const rates = findChild(overlay, "stream-hud-rates")
            verify(rates.text.includes("29.5") && rates.text.includes("29.4") && rates.text.includes("88.2"))
            tryCompare(bridge, "sampleCalls", 2)

            bridge.ps5VideoActive = false
            verify(!overlay.visible)
            bridge.xbox = {state: {streaming: true, stream: {width: 2560, height: 1440, codec: "H.264"}}}
            verify(overlay.visible && format.text.startsWith("Xbox"))
            verify(!packets.visible)
            wait(1100)
            compare(bridge.sampleCalls, 2)
        }
    }
}
