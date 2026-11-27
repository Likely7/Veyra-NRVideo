import QtQuick
import QtQuick.Layouts

Rectangle {
    id: card

    objectName: "controller-panel-status"
    property string level: "idle"
    property string title: ""
    property string detail: ""
    property string action: ""
    property var shortcuts: []
    signal actionRequested()

    implicitHeight: content.implicitHeight + 24
    radius: Theme.rCard
    color: Theme.card2
    border.width: 1
    border.color: Theme.stroke2

    RowLayout {
        id: content
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 12
        spacing: 10

        Rectangle {
            Layout.alignment: Qt.AlignTop
            Layout.topMargin: 5
            implicitWidth: 9
            implicitHeight: 9
            radius: 5
            color: card.level === "ok" ? Theme.ok
                : card.level === "warn" ? Theme.warn
                : card.level === "err" ? Theme.err : Theme.t3
            Behavior on color { ColorAnimation { duration: Theme.d(Theme.durFast) } }
        }

        ColumnLayout {
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            spacing: 4

            Text {
                objectName: "controller-panel-status-title"
                Layout.fillWidth: true
                text: card.title
                textFormat: Text.PlainText
                color: Theme.t1
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsBody
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            Text {
                objectName: "controller-panel-status-detail"
                Layout.fillWidth: true
                visible: text.length > 0
                text: card.detail
                color: Theme.t2
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
            }

            Flow {
                Layout.fillWidth: true
                visible: card.shortcuts.length > 0
                spacing: 6

                Repeater {
                    model: card.shortcuts
                    delegate: Row {
                        required property var modelData
                        spacing: 4

                        KeyCap {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.key
                            size: "s"
                        }

                        Text {
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSmall
                        }
                    }
                }
            }
        }

        ControllerPanelButton {
            objectName: "controller-panel-status-action"
            Layout.alignment: Qt.AlignTop
            visible: card.action.length > 0
            text: card.action
            maxTextWidth: Math.max(66, card.width / 3)
            onClicked: card.actionRequested()
        }
    }
}
