import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

ColumnLayout {
    id: page

    objectName: "controller-panel-method-page"
    property var bridge: null
    property var input: null
    property bool xbox: false
    property bool captureOnly: true
    readonly property bool keyboard: xbox
        ? (input?.xboxKeyboardEnabled ?? input?.keyboardEnabled) === true
        : (input?.ps5KeyboardEnabled ?? input?.keyboardEnabled) === true
    readonly property var control: (xbox ? bridge?.xboxControl : captureOnly ? bridge?.ps5Control : bridge?.ps5) || ({})
    readonly property string padName: control.pad || (control.controller ? qsTr("已检测到手柄") : "")
    readonly property string padDetail: padName.length > 0 && !xbox
        ? qsTr("%1 · %2").arg(padName).arg(control.gyro === true ? qsTr("有陀螺仪") : qsTr("无陀螺仪"))
        : padName
    readonly property bool defaultProfile: !input?.profile || input.profile === "默认"
    property bool showDefaultHint: false
    signal methodPicked()

    spacing: 12

    function pick(keyboardMode) {
        if (!input) return
        input.keyboardEnabled = keyboardMode
        showDefaultHint = keyboardMode && defaultProfile
        methodPicked()
    }

    component MethodCard: AbstractButton {
        id: choice
        property string description: ""
        property string iconName: ""
        property bool chosen: false
        readonly property bool emphasized: chosen || activeFocus

        Layout.fillWidth: true
        Layout.minimumWidth: 0
        padding: 12
        implicitHeight: Math.max(72, contentItem.implicitHeight + topPadding + bottomPadding)
        hoverEnabled: true
        focusPolicy: Qt.StrongFocus
        Accessible.name: text
        Accessible.description: description
        Keys.onReturnPressed: clicked()

        background: Rectangle {
            radius: Theme.rCard
            color: choice.chosen ? Theme.accentSoft : choice.hovered ? Theme.card3 : Theme.card2
            border.width: choice.emphasized ? 2 : 1
            border.color: choice.emphasized ? Theme.accent : Theme.stroke2
        }

        contentItem: RowLayout {
            spacing: 10

            VIcon {
                Layout.alignment: Qt.AlignTop
                Layout.topMargin: 2
                name: choice.iconName
                size: 20
                color: choice.chosen ? Theme.accent : Theme.t2
            }

            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                spacing: 5

                Text {
                    Layout.fillWidth: true
                    text: choice.text
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsBody
                    font.weight: Font.DemiBold
                    wrapMode: Text.Wrap
                }
                Text {
                    Layout.fillWidth: true
                    text: choice.description
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSmall
                    wrapMode: Text.Wrap
                }
            }

            VIcon {
                Layout.alignment: Qt.AlignTop
                visible: choice.chosen
                name: "check"
                color: Theme.accent
            }
        }
    }

    MethodCard {
        objectName: "controller-panel-method-physical"
        enabled: !!page.input
        text: qsTr("实体手柄")
        description: qsTr("直接使用这台电脑上的手柄，串流软件创建的虚拟手柄也可用。")
        iconName: "gamepad"
        chosen: !page.keyboard
        onClicked: page.pick(false)
    }

    MethodCard {
        objectName: "controller-panel-method-keyboard"
        enabled: !!page.input
        text: qsTr("键盘鼠标")
        description: qsTr("用键鼠模拟手柄，可改键位和瞄准手感。")
        iconName: "sliders"
        chosen: page.keyboard
        onClicked: page.pick(true)
    }

    Rectangle {
        objectName: "controller-panel-physical-pad"
        Layout.fillWidth: true
        visible: !page.keyboard
        implicitHeight: deviceInfo.implicitHeight + 24
        color: Theme.card2
        radius: Theme.rCard
        border.width: 1
        border.color: Theme.stroke2
        RowLayout {
            id: deviceInfo
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 12
            spacing: 10
            VIcon { name: "gamepad"; color: Theme.t2 }
            Text {
                Layout.fillWidth: true
                text: page.padDetail || qsTr("未检测到手柄")
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }
        }
    }

    Text {
        Layout.fillWidth: true
        objectName: "controller-panel-default-binding-hint"
        visible: page.keyboard && page.showDefaultHint && page.defaultProfile
        text: qsTr("已套用默认键位，可在「键位」里修改。")
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
        wrapMode: Text.Wrap
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: Theme.rowMinHeight
        visible: page.keyboard
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: qsTr("档案")
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }
        Basic.ComboBox {
            id: profiles
            objectName: "controller-panel-method-profile"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            implicitHeight: Theme.ctlHeight
            model: page.input?.profiles || []
            currentIndex: model.indexOf(page.input?.profile || "")
            Accessible.name: qsTr("档案")
            onActivated: page.input?.selectProfile(currentText)
            background: Rectangle {
                radius: Theme.rCtl
                color: Theme.card2
                border.width: 1
                border.color: profiles.activeFocus ? Theme.accent : Theme.stroke2
            }
            contentItem: Text {
                leftPadding: 10
                rightPadding: Theme.ctlHeight
                text: profiles.displayText === "默认" ? qsTr("默认") : profiles.displayText
                textFormat: Text.PlainText
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
                elide: Text.ElideRight
                verticalAlignment: Text.AlignVCenter
            }
            indicator: VIcon {
                x: profiles.width - width - 10
                y: (profiles.height - height) / 2
                name: "down"
                color: Theme.t2
            }
            popup: Popup {
                y: profiles.height
                width: profiles.width
                padding: 4
                implicitHeight: Math.min(220, contentItem.implicitHeight + padding * 2)
                background: Rectangle { radius: Theme.rCtl; color: Theme.popover; border.color: Theme.stroke2 }
                contentItem: ListView {
                    clip: true
                    implicitHeight: contentHeight
                    model: profiles.popup.visible ? profiles.delegateModel : null
                    currentIndex: profiles.highlightedIndex
                    ScrollBar.vertical: VScrollBar {}
                }
            }
            delegate: ItemDelegate {
                required property string modelData
                required property int index
                width: ListView.view ? ListView.view.width : profiles.width
                highlighted: index === profiles.highlightedIndex
                contentItem: Text {
                    text: modelData === "默认" ? qsTr("默认") : modelData
                    textFormat: Text.PlainText
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsBody
                    elide: Text.ElideRight
                    verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { radius: Theme.rCtl; color: highlighted ? Theme.card3 : "transparent" }
            }
        }
    }

    RowLayout {
        Layout.fillWidth: true
        Layout.minimumHeight: Theme.rowMinHeight
        visible: page.keyboard
        spacing: 10

        Text {
            Layout.fillWidth: true
            text: qsTr("同时转发实体手柄")
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            wrapMode: Text.Wrap
        }

        VSwitch {
            objectName: "controller-panel-method-mix-physical"
            checked: page.input?.tuning?.mixPhysical !== false
            activeFocusOnTab: true
            border.width: activeFocus ? 1 : 0
            border.color: Theme.accent
            Accessible.role: Accessible.CheckBox
            Accessible.name: qsTr("同时转发实体手柄")
            Accessible.checked: checked
            Accessible.onToggleAction: toggled(!checked)
            Keys.onReturnPressed: toggled(!checked)
            Keys.onSpacePressed: toggled(!checked)
            onToggled: on => page.input?.setTuning("mixPhysical", on)
        }
    }
}
