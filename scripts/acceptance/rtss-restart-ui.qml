// Injected only into the local candidate during acceptance, using its real bridge.
import QtQuick
Item {
    id: test
    property var appWindow: Window.window
    property string mode: "check"
    property string evidence: ""
    property bool expectedSoftware: false
    property bool expectedCompatibility: false
    property bool expectedServer: false
    property int ticks: 0
    property int suggestions: 0
    property bool accepting: false
    property bool finishing: false
    function find(item, name) {
        if (item.objectName === name) return item
        for (const child of item.children || []) {
            const hit = find(child, name)
            if (hit) return hit
        }
        return null
    }
    function require(ok, message) {
        if (ok) return
        timer.stop()
        console.error("RTSS_RESTART_FAIL", mode, message)
        Qt.quit()
        throw new Error(message)
    }
    function finish(label) {
        if (finishing) return
        finishing = true
        require(suggestions === 0, "replacement/check run suggested another restart")
        const dialog = find(appWindow.contentItem, "overlay-restart-confirm")
        require(dialog && !dialog.shown, "unexpected restart dialog")
        if (vySoftwareUi) require(appWindow.color.a === 1, "software window must be opaque")
        appWindow.contentItem.grabToImage(result => {
            require(result.saveToFile(evidence + "/" + label + ".png"), "screenshot not saved")
            console.log("RTSS_RESTART_PASS", label, "software=" + vySoftwareUi,
                        "compatibility=" + veyra.overlayCompatActive,
                        "server=" + veyra.rivaTunerRunning(), "version=" + veyra.version)
            Qt.quit()
        })
    }
    Connections {
        target: veyra
        function onOverlayRestartSuggested() { test.suggestions++ }
    }
    Timer {
        id: timer
        interval: 100; repeat: true; running: true
        onTriggered: {
            if (++test.ticks > 230) { test.require(false, "23-second deadline"); return }
            if (!test.evidence.length || test.finishing) return
            if (test.mode === "late") {
                if (veyra.overlayCompatActive) {
                    test.require(vySoftwareUi, "compatibility restart did not switch renderer")
                    if (test.ticks > 55) test.finish("late-child")
                    return
                }
                if (test.accepting) return
                const dialog = test.find(test.appWindow.contentItem, "overlay-restart-confirm")
                if (!dialog || !dialog.shown) return
                test.require(test.suggestions === 1 && veyra.rivaTunerRunning(), "late prompt needs one suggestion and a live server")
                const accept = test.find(dialog, "overlay-restart-confirm-accept")
                test.require(accept !== null, "restart button missing")
                test.accepting = true
                console.log("RTSS_RESTART_ACCEPT", "software=" + vySoftwareUi)
                accept.clicked() // Executes the actual button's QML onClicked/onAccepted path.
                return
            }
            if (test.mode === "normal") {
                if (veyra.uiScaleActive !== 125) {
                    if (test.ticks < 15 || test.accepting) return
                    test.require(veyra.setPreference("uiScale", 125), "scale setting not saved")
                    test.accepting = true
                    console.log("RTSS_NORMAL_RESTART_ACCEPT")
                    veyra.restartApplication()
                    return
                }
                test.require(!vySoftwareUi && !veyra.overlayCompatActive, "normal restart retained a compatibility latch")
                if (test.ticks > 55) test.finish("normal-child")
                return
            }
            test.require(vySoftwareUi === test.expectedSoftware, "wrong UI renderer")
            test.require(veyra.overlayCompatActive === test.expectedCompatibility, "wrong compatibility state")
            test.require(veyra.rivaTunerRunning() === test.expectedServer, "wrong server state")
            if (test.ticks > 55) test.finish("check")
        }
    }
}
