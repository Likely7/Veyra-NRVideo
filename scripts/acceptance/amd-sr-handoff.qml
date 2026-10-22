// Loaded only into a private test copy of Main.qml (production bridge).
// AMD field report 2026-10-22: SR cannot be turned off. Turns FSR SR on in list
// mode, then off, and logs what the interface and the engine report each time.
import QtQuick
Item {
    id: test
    property int step: 0
    property int ticks: 0
    property int mark: 0
    function state(tag) {
        console.log("SRTEST", tag, "srEnabled=" + veyra.srEnabled, "srActive=" + veyra.srActive,
                    "videoSrQuality=" + veyra.videoSrQuality, "status=" + veyra.statusText)
    }
    Connections {
        target: veyra
        function onNotice(message, error) { console.log("SRTEST notice", error, message) }
    }
    Timer {
        interval: 200; repeat: true; running: true
        onTriggered: {
            ++test.ticks
            if (test.ticks > 400) { console.log("SRTEST FAIL deadline"); Qt.quit(); return }
            if (test.step === 0) {
                if (!veyra.hasSource || test.ticks < 20) return
                veyra.nodeMode = 0
                veyra.nrEnabled = false
                veyra.fgEnabled = false
                veyra.videoSrQuality = 5
                veyra.srTargetIndex = 2
                veyra.srEnabled = true
                test.state("requested-on")
                test.mark = test.ticks; test.step = 1
            } else if (test.step === 1) {
                if (test.ticks - test.mark < 30) return
                test.state("after-on")
                veyra.srEnabled = false
                test.state("requested-off")
                test.mark = test.ticks; test.step = 2
            } else if (test.step === 2) {
                if (test.ticks - test.mark < 30) return
                test.state("after-off")
                console.log("SRTEST DONE")
                Qt.quit()
            }
        }
    }
}
