import QtQuick
import QtTest
import Veyra

Item {
    VTelemetryValue { id: motion; duration: 120 }
    SignalSpy { id: changes; target: motion; signalName: "valueChanged" }
    Timer {
        id: samples
        interval: 10
        repeat: true
        property int sequence: 0
        onTriggered: { ++sequence; motion.targetValue = sequence % 2 ? 0.7 : 0.9 }
    }
    TestCase {
        name: "Telemetry"
        when: windowShown
        function init() {
            samples.stop()
            samples.sequence = 0
            motion.animate = false
            motion.targetValue = 0
            motion.duration = 120
            motion.animate = true
            changes.clear()
        }
        function cleanup() { samples.stop(); motion.animate = false }
        function test_converges_and_retargets() {
            motion.targetValue = 1
            wait(50)
            verify(motion.value > 0 && motion.value < 1)
            motion.targetValue = 0.2
            tryCompare(motion, "value", 0.2, 400)
            verify(!motion.driver.running)
            motion.targetValue = 0.8
            tryCompare(motion, "value", 0.8, 400)
        }
        function test_frequent_samples_progress_without_display_rate_animation() {
            samples.start()
            wait(700)
            samples.stop()
            verify(motion.value > 0.5, "frequent readings must not postpone every animation tick")
            verify(changes.count > 5 && changes.count < 35,
                   "telemetry must progress without a change on every high-refresh display tick: " + changes.count)
            motion.targetValue = 0.75
            tryCompare(motion, "value", 0.75, 400)
        }
        function test_hidden_or_reduced_motion_snaps_and_stops() {
            motion.targetValue = 0.9
            wait(40)
            motion.animate = false
            compare(motion.value, 0.9)
            verify(!motion.driver.running)
            motion.targetValue = 0.1
            compare(motion.value, 0.1)
            motion.animate = true
            motion.targetValue = 0.7
            tryCompare(motion, "value", 0.7, 400)
        }
        function test_zero_duration_during_animation() {
            motion.targetValue = 1
            wait(40)
            motion.duration = 0
            compare(motion.value, 1)
            verify(!motion.driver.running)
            motion.targetValue = 0.3
            compare(motion.value, 0.3)
        }
    }
}
