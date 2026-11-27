import QtQuick
import QtQuick.Layouts

RowLayout {
    id: field
    required property var backend
    property var prompt: ({})
    property bool active: false
    readonly property string requestId: prompt.requestId || ""
    readonly property string feedback: prompt.pending ? qsTr("正在等待 PS5 验证…")
        : rejected || prompt.submitFailed ? qsTr("PIN 提交失败，请重试。")
        : prompt.incorrect ? qsTr("PIN 不正确，请重新输入。") : ""
    property bool rejected: false
    spacing: 6

    function reset() {
        if (pin) pin.clear()
        rejected = false
    }
    function focusEditor() { if (pin.enabled) pin.forceActiveFocus() }
    function submit() {
        if (!submitButton.enabled) return
        rejected = !backend.ps5SendLoginPin(requestId, pin.text)
        pin.clear()
    }
    onRequestIdChanged: reset()
    onActiveChanged: if (!active) reset()

    Rectangle {
        implicitWidth: 86
        implicitHeight: Theme.ctlHeight
        radius: Theme.rCtl
        color: Theme.card2
        border.color: pin.activeFocus ? Theme.accent : Theme.stroke
        TextInput {
            id: pin
            objectName: "ps5-login-pin-input"
            anchors.fill: parent
            anchors.margins: 6
            horizontalAlignment: TextInput.AlignHCenter
            verticalAlignment: TextInput.AlignVCenter
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            color: Theme.t1
            echoMode: TextInput.Password
            passwordMaskDelay: 0
            inputMethodHints: Qt.ImhDigitsOnly | Qt.ImhSensitiveData | Qt.ImhNoPredictiveText
            validator: RegularExpressionValidator { regularExpression: /^[0-9]{4}$/ }
            enabled: field.active && field.requestId.length > 0 && !field.prompt.pending
            selectByMouse: true
            onTextEdited: field.rejected = false
            onAccepted: field.submit()
        }
    }
    VButton {
        id: submitButton
        objectName: "ps5-login-pin-submit"
        text: qsTr("提交")
        primary: true
        enabled: pin.enabled && pin.acceptableInput
        onClicked: field.submit()
    }
}
