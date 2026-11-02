// Dialog routing and scrim. Each dialog owns its own editable content.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: host
    // Compatibility type used by the professional and node views.
    component VTextField: DialogTextField {}
    anchors.fill: parent
    z: 100

    // Active dialog: network/capture/ps5/moonlight/xbox/screen/subtitle/audio/save/manage.
    property string dialog: ""
    onDialogChanged: if (dialog !== "capture") legacyCapturePicker.close()

    function open(key) {
        if (key === "save") saveDialog.resetForOpen()
        if (key === "manage") {
            manageDialog.mode = veyra.nodeMode === 1 ? "node" : "list"
            manageDialog.feedback = ""
        }
        veyra.logUi("ui-dialog", "open " + key + " (was " + (host.dialog || "none") + ")")
        host.dialog = key
    }
    function close() { host.dialog = "" }

    signal startCapture()
    signal startPs5()
    signal startMoonlight()
    signal startXbox()
    signal startScreen()

    CaptureLegacyPicker {
        id: legacyCapturePicker
        formats: visible ? veyra.captureFormats : []
        selectedKey: veyra.captureFormatKey
        onSelected: key => { if (!veyra.captureQueryBusy) veyra.captureFormatKey = key }
    }
    Connections {
        target: veyra
        function onCaptureChanged() { if (veyra.captureQueryBusy) legacyCapturePicker.close() }
    }

    // Scrim (M16): the design dims the page behind the dialog, opacity .2s linear.
    // The native video window cannot be dimmed from QML; only the dialog panel is cut
    // out of it (videoCover below), so the picture beside the dialog stays undimmed.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)
        opacity: host.dialog !== "" ? 1 : 0
        visible: opacity > 0
        Behavior on opacity { NumberAnimation { duration: Theme.d(200) } }
        TapHandler { onTapped: host.close() }
    }
    // For the motion probe: the open dialog's scale.
    property real motionScale: 1

    // --- shared chrome ----------------------------------------------------

    NetworkSourceDialog {
        id: networkDialog
        dialogHost: host
    }

    // --- 采集卡 -----------------------------------------------------------
    // The 1.4.4 capture panel, in the new design: device details and formats are
    // queried asynchronously, only an explicitly chosen audio input is opened,
    // and the choice is remembered for "继续上次采集". Resolution, frame rate and
    // pixel format are three views of that one saved format key.
    CaptureSourceDialog {
        id: captureDialog
        dialogHost: host
        legacyPicker: legacyCapturePicker
    }

    // --- PS5 串流 ---------------------------------------------------------
    // The 1.4.4 Remote Play panel: saved pairings are encrypted in the user's
    // %LOCALAPPDATA%\Veyra\remoteplay and shared with 1.4.4.
    Ps5SourceDialog {
        id: ps5Dialog
        dialogHost: host    }

    // --- PC 串流（Sunshine / GameStream） ----------------------------------
    // Hosts, pairing, the host's apps and the stream-core settings. The effect chain is not here: once
    // connected, the picture goes through the software's own pages like any other source.
    PcSourceDialog {
        id: mlDialog
        dialogHost: host
    }

    // --- Xbox 串流（非官方） ---------------------------------------------------
    // Sign in with the Xbox (Microsoft) account on Microsoft's own page via a device code, pick a console,
    // start. The software never sees the password; only a refresh token is kept, encrypted for this user.
    XboxSourceDialog {
        id: xbDialog
        dialogHost: host    }

    // --- 屏幕捕获 ---------------------------------------------------------
    ScreenSourceDialog {
        id: screenDialog
        dialogHost: host
    }

    // --- 字幕设置 ---------------------------------------------------------
    SubtitleSettingsDialog {
        id: subtitleDialog
        dialogHost: host
    }

    // --- 音频设置 ---------------------------------------------------------
    AudioSettingsDialog {
        dialogHost: host
    }

    // --- 另存为预设 -------------------------------------------------------
    // The design lists every part that will be saved before asking for a name, so
    // the user can see what they are about to store.
    SavePresetDialog {
        id: saveDialog
        dialogHost: host
    }

    // --- 管理预设 ---------------------------------------------------------
    ManagePresetsDialog {
        id: manageDialog
        dialogHost: host
    }

    // --- small building blocks the design's dialogs use --------------------

}
