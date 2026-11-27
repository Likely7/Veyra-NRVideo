import QtQuick
import QtQuick.Layouts

DialogLayer {
    id: panel
    required property var backend
    readonly property var prompt: backend.ps5LoginPrompt || ({})
    readonly property string requestId: prompt.requestId || ""
    property string offeredRequestId: ""
    property bool inlinePrompt: false
    objectName: "ps5-login-pin-dialog"
    key: "ps5-pin"
    title: qsTr("PS5 要求输入登录 PIN")
    sub: qsTr("请输入 PS5 主机的 4 位登录 PIN。")
    glyph: "gamepad"
    dialogWidth: 400
    minimumBodyWidth: 0

    function refreshPrompt() {
        if (inlinePrompt) {
            if (shown) dialogHost.close()
            return
        }
        if (!requestId.length) {
            if (shown) dialogHost.close()
            return
        }
        if (!dialogHost.dialog.length && !dialogHost.onboardingActive && !prompt.pending && offeredRequestId !== requestId) {
            offeredRequestId = requestId
            dialogHost.open(key)
        }
        if (shown) {
            offeredRequestId = requestId
            entry.focusEditor()
        }
    }
    onRequestIdChanged: Qt.callLater(refreshPrompt)
    onInlinePromptChanged: Qt.callLater(refreshPrompt)
    Component.onCompleted: Qt.callLater(refreshPrompt)
    Connections {
        target: panel.dialogHost
        function onDialogChanged() { Qt.callLater(panel.refreshPrompt) }
        function onOnboardingActiveChanged() { Qt.callLater(panel.refreshPrompt) }
    }

    Ps5LoginPinField {
        id: entry
        Layout.alignment: Qt.AlignHCenter
        backend: panel.backend
        prompt: panel.prompt
        active: panel.shown
    }
    DialogNote { visible: entry.feedback.length > 0; text: entry.feedback }
}
