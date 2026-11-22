import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: pinDialog
    required property var backend
    readonly property var prompt: backend.ps5LoginPrompt || ({})
    readonly property bool promptActive: prompt.active === true && prompt.required === true && !cancelRequested
    property string requestId: ""
    property bool submitted: false
    property bool cancelRequested: false
    property string localError: ""
    property bool componentReady: false
    objectName: "ps5-login-pin-dialog"
    key: "ps5-pin"
    title: qsTr("输入 PlayStation 登录 PIN")
    sub: qsTr("请输入主机用户的 4 位登录码，验证后将自动继续连接。")
    glyph: "gamepad"
    dialogWidth: 440
    actions: []

    function updatePrompt() {
        if (prompt.active !== true || prompt.required !== true) {
            if (dialogHost.dialog === key) dialogHost.dialog = ""
            pin.text = ""; requestId = ""; submitted = false; cancelRequested = false; localError = ""
            return
        }
        if (requestId !== prompt.requestId) {
            requestId = prompt.requestId
            pin.text = ""; submitted = false; cancelRequested = false; localError = ""
            Qt.callLater(function() { if (pinDialog.promptActive) pin.forceActiveFocus() })
        }
        if (prompt.status === 6) submitted = false
        if (!cancelRequested && dialogHost.dialog !== key) dialogHost.open(key)
    }
    onPromptChanged: if (componentReady) updatePrompt()
    Component.onCompleted: { componentReady = true; updatePrompt() }

    function submit() {
        if (!submitButton.enabled) return
        if (backend.ps5SubmitLoginPin(pin.text, requestId)) {
            submitted = true; pin.text = ""; localError = ""
        } else localError = qsTr("本次请求已变化，未提交 PIN。请等待连接状态更新。")
    }
    function cancel() {
        if (!requestId.length || cancelRequested) return
        if (!backend.ps5CancelLoginPin(requestId)) {
            localError = qsTr("连接状态已更新，请稍候。")
            updatePrompt()
            return
        }
        cancelRequested = true
        pin.text = ""
        if (dialogHost.dialog === key) dialogHost.dialog = ""
    }

    Item {
        Layout.fillWidth: true
        implicitHeight: 72
        Rectangle {
            width: 184; height: 52; anchors.centerIn: parent
            radius: 10; color: Theme.card2
            border.color: pin.activeFocus ? Theme.accent : Theme.stroke
            TextInput {
                id: pin
                objectName: "ps5-login-pin-input"
                anchors.fill: parent; anchors.margins: 8
                horizontalAlignment: TextInput.AlignHCenter
                verticalAlignment: TextInput.AlignVCenter
                color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: 28
                echoMode: TextInput.Password; maximumLength: 4; selectByMouse: true
                inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
                validator: RegularExpressionValidator { regularExpression: /[0-9]{0,4}/ }
                enabled: pinDialog.prompt.canSubmit === true && !pinDialog.submitted
                Keys.onReturnPressed: pinDialog.submit()
                Keys.onEnterPressed: pinDialog.submit()
                Keys.onEscapePressed: pinDialog.cancel()
            }
        }
    }
    DialogNote {
        objectName: "ps5-login-pin-message"
        text: pinDialog.localError || (pinDialog.submitted && pinDialog.prompt.canSubmit === true
              ? qsTr("正在提交，等待主机确认…") : pinDialog.prompt.message || "")
    }
    RowLayout {
        Layout.fillWidth: true; Layout.topMargin: 12; spacing: 8
        Item { Layout.fillWidth: true }
        VButton { objectName: "ps5-login-pin-cancel"; text: qsTr("取消连接"); ghost: true; onClicked: pinDialog.cancel() }
        VButton {
            id: submitButton
            objectName: "ps5-login-pin-confirm"
            text: pinDialog.submitted || pinDialog.prompt.canSubmit !== true ? qsTr("验证中…") : qsTr("继续连接")
            primary: true
            enabled: pinDialog.promptActive && pinDialog.prompt.canSubmit === true && !pinDialog.submitted && /^[0-9]{4}$/.test(pin.text)
            onClicked: pinDialog.submit()
        }
    }
}
