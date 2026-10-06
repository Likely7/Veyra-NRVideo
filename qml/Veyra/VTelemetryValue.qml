// Live statistics use a bounded timer rather than the display's animation clock.
// A rolling GPU measurement changes every source frame; restarting a long
// NumberAnimation for each reading otherwise keeps the UI drawing at 320 Hz.
import QtQuick

QtObject {
    id: root
    property real targetValue: 0
    property real value: 0
    property bool animate: true
    property int duration: 600
    property real fromValue: 0
    property real toValue: 0
    property double started: 0
    property bool ready: false

    function atTime(now) {
        const t = Math.min(1, Math.max(0, (now - started) / duration))
        const eased = 1 - Math.pow(1 - t, 3)
        return fromValue + (toValue - fromValue) * eased
    }

    function retarget() {
        if (!ready) return
        if (!animate || duration <= 0 || Math.abs(targetValue - value) < 0.00001) {
            tick.stop()
            value = targetValue
            return
        }
        const now = Date.now()
        // Advance the internal trajectory before replacing its destination.
        // Do not publish a value here: frequent readings still share one UI tick.
        // Taking only the last published value would stall when a reading always
        // arrived immediately before the timer callback.
        fromValue = tick.running ? atTime(now) : value
        toValue = targetValue
        started = now
        // New samples keep the timer's deadline. Restarting it could postpone
        // every tick forever when samples arrive faster than the UI cadence.
        if (!tick.running) tick.start()
    }
    onTargetValueChanged: retarget()
    onAnimateChanged: retarget()
    onDurationChanged: retarget()
    Component.onCompleted: { ready = true; value = targetValue }

    property Timer driver: Timer {
        id: tick
        interval: 33
        repeat: true
        onTriggered: {
            root.value = root.atTime(Date.now())
            if (Date.now() - root.started >= root.duration) { root.value = root.targetValue; stop() }
        }
    }
}
