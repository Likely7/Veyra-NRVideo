import QtQuick
import QtQuick.Controls.Basic
import "Ps5Bindings.js" as Bindings
import QtQuick.Layouts
ColumnLayout {
    id: entry
    required property var settings
    required property Item host
    visible: !!settings
    Layout.fillWidth: true
    spacing: 8
    Button {
        id: launch
        objectName: "ps5-keyboard-open"
        Layout.fillWidth: true
        hoverEnabled: true
        leftPadding: 16; rightPadding: 16
        topPadding: 12; bottomPadding: 12
        Accessible.name: qsTr("键盘与鼠标")
        Accessible.description: profileLabel.text
        onClicked: entry.host.openPs5Keyboard(launch)
        contentItem: RowLayout {
            spacing: 12
            VIcon { name: "keyboard"; size: 24; color: Theme.accent }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 3
                Label { text: qsTr("键盘与鼠标"); color: Theme.t1; font.family: Theme.fontUi; font.weight: Font.DemiBold; font.pixelSize: 14 }
                Label {
                    id: profileLabel
                    Layout.fillWidth: true
                    text: qsTr("控制配置") + " · " + (entry.settings ? entry.settings.selected : "")
                    elide: Text.ElideRight
                    color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 12
                }
            }
            VIcon { name: "external"; size: 16; color: launch.hovered || launch.visualFocus ? Theme.accent : Theme.t2 }
        }
        background: Rectangle {
            radius: Theme.rCtl
            color: launch.down ? Qt.tint(Theme.card3, Theme.accentSoft) : launch.hovered ? Theme.card3 : Theme.card2
            border.width: launch.visualFocus ? 2 : 1
            border.color: launch.visualFocus || launch.down ? Theme.accent : Theme.stroke2
        }
    }
    VRow {
        visible: entry.settings && entry.settings.available
        label: qsTr("当前情境")
        RowLayout {
            spacing: 8
            Ps5EditorPicker {
                id: situation
                objectName: "active-situation"
                displayText: entry.settings ? Bindings.situationName(entry.settings.profile, currentText) : ""
                delegate: ItemDelegate { required property string modelData; width:situation.width; text:Bindings.situationName(entry.settings.profile,modelData) }
                model: entry.settings ? entry.settings.situations : []
                currentIndex: entry.settings ? Math.max(0,model.indexOf(entry.settings.situation)) : 0
                onActivated: entry.settings.correct(currentText)
            }
            Ps5EditorButton {
                objectName: "resume-capture"
                text: qsTr("恢复捕获输入")
                onClicked: entry.host.resumePs5Keyboard()
            }
        }
    }
}
