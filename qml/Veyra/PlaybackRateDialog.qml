import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Popup {
    id: dialog
    parent: Overlay.overlay
    // The cinema controls live in a non-activating, masked tool window. An
    // independent popup accepts keyboard focus and cannot be cut off by its mask.
    popupType: Popup.Window
    width: Math.min(360, parent ? parent.width-32 : 360)
    height: body.implicitHeight+32
    x: parent ? (parent.width-width)/2 : 0
    y: parent ? (parent.height-height)/2 : 0
    padding: 16
    modal: true
    dim: true
    focus: true
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    property string errorText: ""
    function applyRate() {
        if (!input.acceptableInput) { errorText=qsTr("请输入 0.25～4.00 之间的倍速"); return }
        const rate=Number(input.text)
        veyra.playbackRate=rate
        if (Math.abs(veyra.playbackRate-rate)>0.000001) { errorText=qsTr("当前播放状态无法修改倍速"); return }
        close()
    }
    onOpened: {
        errorText=""
        input.text=Number(veyra.playbackRate).toFixed(2)
        input.forceActiveFocus(); input.selectAll()
    }
    background: Rectangle {
        objectName: "videoCover"
        property real coverRadius: 16
        radius: 16; color: Theme.dialog
        border.width: 1; border.color: Theme.stroke2
    }
    contentItem: ColumnLayout {
        id: body
        spacing: 14
        Text { text: qsTr("自定义播放速度"); color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: Theme.fsH2; font.weight: Font.DemiBold }
        Text { text: qsTr("范围 0.25～4.00 倍，保持原有音调"); color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: Theme.fsBody }
        Basic.TextField {
            id: input
            objectName: dialog.objectName+"-input"
            Accessible.name: qsTr("播放倍速")
            Layout.fillWidth: true
            color: Theme.t1; font.family: Theme.fontMono; font.pixelSize: 18
            selectByMouse: true
            validator: DoubleValidator { bottom: veyra.minimumPlaybackRate; top: veyra.maximumPlaybackRate; decimals: 2; notation: DoubleValidator.StandardNotation; locale: "C" }
            background: Rectangle { radius: 8; color: Theme.bgOuter; border.width: 1; border.color: input.activeFocus ? Theme.t2 : Theme.stroke2 }
            onAccepted: dialog.applyRate()
        }
        Text { Layout.fillWidth: true; visible: dialog.errorText.length>0; text: dialog.errorText; color: Theme.t2; font.family: Theme.fontUi; wrapMode: Text.WordWrap }
        RowLayout {
            Layout.fillWidth: true
            Item { Layout.fillWidth: true }
            VButton { objectName: dialog.objectName+"-cancel"; text: qsTr("取消"); onClicked: dialog.close() }
            VButton { objectName: dialog.objectName+"-apply"; text: qsTr("应用"); primary: true; onClicked: dialog.applyRate() }
        }
    }
}
