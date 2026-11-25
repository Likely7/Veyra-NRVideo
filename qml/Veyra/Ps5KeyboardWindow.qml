import QtQuick
import QtQuick.Window

Window {
    id: win
    required property var owner
    required property var settings
    signal resumeRequested()
    readonly property alias editor: editorLoader.item
    property Item opener
    property bool positioned: false
    property bool ownerClosing: false
    property string screenMode: "editor"
    property bool startAfterCheck: false
    property int initialSection: 0
    readonly property bool pendingResume: focusTimeout.running

    objectName: "ps5-keyboard-window"
    title: qsTr("键盘与鼠标") + " · Veyra"
    transientParent: owner
    modality: Qt.WindowModal
    flags: Qt.Window | Qt.WindowTitleHint | Qt.WindowSystemMenuHint | Qt.WindowMinMaxButtonsHint | Qt.WindowCloseButtonHint
    color: Theme.dialog
    minimumWidth: Math.min(560, Screen.desktopAvailableWidth)
    minimumHeight: Math.min(410, Screen.desktopAvailableHeight - 48)

    // Hiding a modal HWND can queue a focus change after the button handler returns.
    // Start capture only after the player's native window has regained focus.
    function resumeCapture() {
        Qt.callLater(function() {
            if (win.ownerClosing || Qt.application.state !== Qt.ApplicationActive) return
            if (!owner.active) owner.requestActivate()
            focusTimeout.restart()
            if (owner.active) finishResume()
        })
    }
    function finishResume() {
        if (!focusTimeout.running) return
        focusTimeout.stop()
        if (owner.active && Qt.application.state === Qt.ApplicationActive && !settings.resume()) open(null)
    }
    Timer {
        id: focusTimeout
        interval: 750
        onTriggered: {
            if (win.ownerClosing || Qt.application.state !== Qt.ApplicationActive) return
            if (win.owner.active && win.settings.resume()) return
            win.open(null)
        }
    }
    function requestControl() {
        if (!settings || !settings.available) return
        if (settings.inputCheckOffered) resumeCapture()
        else {
            startAfterCheck = true
            open(null, "offer")
        }
    }
    function open(from, mode) {
        if (!settings) return
        screenMode = mode || "editor"
        if (screenMode === "editor") { startAfterCheck = false; initialSection = 0 }
        if (!visible) {
            opener = from || null
            if (!positioned) {
                if (owner.screen) screen = owner.screen
                const left = screen.virtualX, top = screen.virtualY
                const availableW = Screen.desktopAvailableWidth, availableH = Screen.desktopAvailableHeight
                width = Math.min(1160, availableW - 32)
                height = Math.min(820, availableH - 72)
                x = Math.round(Math.max(left + 8, Math.min(owner.x + (owner.width - width) / 2,
                                                            left + availableW - width - 8)))
                y = Math.round(Math.max(top + 32, Math.min(owner.y + (owner.height - height) / 2,
                                                           top + availableH - height - 16)))
                positioned = true
            }
            settings.edit(true)
            show()
        }
        if (visibility === Window.Minimized) showNormal()
        raise()
        requestActivate()
    }
    onVisibleChanged: {
        if (!visible && settings) {
            if (settings.shortcutConflict && settings.shortcutConflict.key) settings.cancelShortcutConflict()
            settings.edit(false)
            if (opener && opener.visible) opener.forceActiveFocus(Qt.OtherFocusReason)
            opener = null
            if (!ownerClosing && owner.visible) Qt.callLater(function() {
                if (!win.visible && win.owner.visible && !win.ownerClosing &&
                    Qt.application.state === Qt.ApplicationActive) win.owner.requestActivate()
            })
        }
    }
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state !== Qt.ApplicationActive) focusTimeout.stop()
        }
    }
    Connections {
        target: win.owner
        function onActiveChanged() {
            if (win.owner.active && focusTimeout.running) Qt.callLater(win.finishResume)
        }
        function onClosing(event) { win.ownerClosing = true; focusTimeout.stop(); win.close() }
    }
    Loader {
        id: editorLoader
        anchors.fill: parent
        active: win.visible && win.screenMode === "editor"
        onLoaded: item.section = win.initialSection
        sourceComponent: Ps5KeyboardDialog {
            settings: win.settings
            onPracticeRequested: { win.startAfterCheck = false; win.screenMode = "practice" }
            onDismissRequested: resume => {
                win.close()
                if (resume) win.resumeRequested()
            }
        }
    }
    Loader {
        anchors.fill: parent
        active: win.visible && win.screenMode !== "editor"
        sourceComponent: Ps5InputCheck {
            settings: win.settings
            offer: win.screenMode === "offer"
            startAfterCheck: win.startAfterCheck
            onTryRequested: {
                win.settings.acknowledgeInputCheck()
                win.screenMode = "practice"
            }
            onFinishRequested: start => {
                win.settings.acknowledgeInputCheck()
                if (start) {
                    win.close()
                    win.resumeCapture()
                } else if (win.startAfterCheck) win.close()
                else {
                    win.initialSection = 3
                    win.screenMode = "editor"
                }
            }
        }
    }
}
