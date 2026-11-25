import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "Ps5Bindings.js" as Bindings

ColumnLayout {
    id: shortcuts
    required property var settings
    signal resolved()
    readonly property var conflict: settings.shortcutConflict || ({})
    property string listening: ""
    objectName: "player-shortcuts"
    spacing: 10

    function actionLabel(action) {
        return action === "overlay" ? qsTr("显示控制说明") : qsTr("控制键")
    }
    function describe(binding) {
        const action = Bindings.label(binding.labels, Bindings.caption(binding.action))
        const scene = Bindings.label(binding.contextLabels, Bindings.situationName({}, binding.context))
        if (binding.scope === "context_hotkeys") return qsTr("切换情境 · %1").arg(scene)
        const scope = binding.scope === "bindings_per_context" ? scene
            : binding.scope === "console_menu" ? qsTr("主机菜单")
            : binding.scope === "layers" ? qsTr("按键层 · %1").arg(scene)
            : qsTr("基础绑定")
        if (binding.layer) {
            const target = Bindings.label(binding.layerLabels, Bindings.situationName({}, binding.layer))
            return scope + " · " + qsTr("按键层 · %1").arg(target) + " · " + action
        }
        return scope + " · " + action
    }
    function updateDialog() {
        if (conflict.key) {
            listening = ""
            if (!clash.visible) clash.open()
        } else if (clash.visible) clash.close()
    }
    function apply(side) {
        if (settings.resolveShortcut(side, replacement.currentText)) resolved()
    }
    onConflictChanged: if (clash) updateDialog()
    Component.onCompleted: updateDialog()

    Label {
        Layout.fillWidth: true
        text: qsTr("此电脑上的播放器快捷键")
        font.family: Theme.fontUi
        font.pixelSize: 17
        font.weight: Font.DemiBold
        Layout.bottomMargin: 4
        color: Theme.t1
    }
    Repeater {
        model: ["overlay", "control"]
        RowLayout {
            id: row
            required property string modelData
            Layout.fillWidth: true
            spacing: 16
            Label {
                Layout.fillWidth: true
                text: shortcuts.actionLabel(row.modelData)
                color: Theme.t1
                wrapMode: Text.Wrap
                font.family: Theme.fontUi
            }
            Ps5EditorButton {
                id: assign
                objectName: "shortcut-" + row.modelData
                readonly property bool waiting: shortcuts.listening === row.modelData
                readonly property string currentKey: row.modelData === "overlay" ? shortcuts.settings.overlayKey : shortcuts.settings.controlKey
                text: waiting ? qsTr("按下新按键") : Bindings.caption(currentKey)
                Accessible.name: shortcuts.actionLabel(row.modelData) + " · " + text
                Layout.preferredWidth: Math.max(108, implicitWidth)
                checked: waiting
                onClicked: {
                    shortcuts.listening = waiting ? "" : row.modelData
                    forceActiveFocus()
                }
                onActiveFocusChanged: if (!activeFocus && waiting) shortcuts.listening = ""
                Keys.onShortcutOverride: event => { if (waiting) event.accepted = true }
                Keys.onPressed: event => {
                    if (!waiting) return
                    event.accepted = true
                    const key = shortcuts.settings.keyCode(event.key, event.nativeScanCode)
                    const chord = Qt.ControlModifier | Qt.AltModifier | Qt.ShiftModifier | Qt.MetaModifier
                    if (event.isAutoRepeat || (event.modifiers & chord) || !shortcuts.settings.shortcutKeys.includes(key)) return
                    shortcuts.listening = ""
                    shortcuts.settings.setShortcut(row.modelData, key)
                }
            }
        }
    }
    Switch {
        id: hintsSwitch
        objectName: "show-shortcut-hints"
        text: qsTr("游玩时显示快捷键提示")
        Layout.fillWidth: true
        font.family: Theme.fontUi
        horizontalPadding: 0
        verticalPadding: 7
        spacing: 12
        hoverEnabled: true
        checked: shortcuts.settings.showShortcutHints
        onCheckedChanged: {
            if (checked !== shortcuts.settings.showShortcutHints && !shortcuts.settings.setShowShortcutHints(checked))
                checked = Qt.binding(() => shortcuts.settings.showShortcutHints)
        }
        contentItem: Label {
            text: hintsSwitch.text
            font: hintsSwitch.font
            color: Theme.t1
            rightPadding: hintsSwitch.indicator.width + hintsSwitch.spacing
            wrapMode: Text.Wrap
            verticalAlignment: Text.AlignVCenter
        }
        indicator: VSwitch {
            x: hintsSwitch.width - width
            anchors.verticalCenter: parent.verticalCenter
            checked: hintsSwitch.checked
            // The native Switch owns pointer, keyboard and accessibility input for the entire row.
            enabled: false
            Rectangle {
                anchors.fill: parent
                anchors.margins: -3
                radius: height / 2
                visible: hintsSwitch.visualFocus || hintsSwitch.hovered
                color: "transparent"
                border.color: hintsSwitch.visualFocus ? Theme.accent : Theme.stroke2
                border.width: hintsSwitch.visualFocus ? 2 : 1
            }
        }
    }
    Label {
        Layout.fillWidth: true
        color: Theme.t2
        wrapMode: Text.Wrap
        font.family: Theme.fontUi
        text: qsTr("紧急释放 · %1").arg("Ctrl+Alt+Shift+Z")
    }
    Dialog {
        id: clash
        objectName: "shortcut-conflict-dialog"
        parent: Overlay.overlay
        anchors.centerIn: parent
        width: Math.min(510, parent ? parent.width - 24 : 510)
        modal: true
        focus: true
        closePolicy: Popup.CloseOnEscape
        title: qsTr("快捷键冲突")
        padding: 20
        header: Label {
            leftPadding: 20; rightPadding: 20; topPadding: 18
            text: clash.title
            color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: 18; font.weight: Font.DemiBold
        }
        onRejected: shortcuts.settings.cancelShortcutConflict()
        background: Rectangle { color: Theme.dialog; radius: 10; border.color: Theme.stroke2 }
        contentItem: ColumnLayout {
            spacing: 10
            Label {
                objectName: "shortcut-conflict-local"
                Layout.fillWidth: true
                color: Theme.t1
                wrapMode: Text.Wrap
                textFormat: Text.PlainText
                text: qsTr("此电脑 · %1 · %2").arg(shortcuts.actionLabel(shortcuts.conflict.action)).arg(Bindings.caption(shortcuts.conflict.key))
            }
            Label {
                Layout.fillWidth: true
                color: Theme.t1
                textFormat: Text.PlainText
                wrapMode: Text.Wrap
                text: shortcuts.conflict.otherAction ? qsTr("此电脑 · %1 · %2").arg(shortcuts.actionLabel(shortcuts.conflict.otherAction)).arg(Bindings.caption(shortcuts.conflict.key))
                      : qsTr("控制配置 · %1").arg(shortcuts.conflict.profile || "")
            }
            ScrollView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(assignmentsLabel.implicitHeight, clash.parent ? clash.parent.height * 0.23 : 120)
                visible: !shortcuts.conflict.otherAction
                contentWidth: availableWidth
                clip: true
                Label {
                    id: assignmentsLabel
                    objectName: "shortcut-conflict-game"
                    width: parent.width
                    color: Theme.t2
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    text: (shortcuts.conflict.assignments || []).map(shortcuts.describe).join("\n")
                }
            }
            Label { text: qsTr("替代按键"); color: Theme.t1 }
            Ps5EditorPicker {
                id: replacement
                objectName: "shortcut-replacement"
                Accessible.name: qsTr("替代按键")
                Layout.fillWidth: true
                model: shortcuts.conflict.freeKeys || []
                displayText: Bindings.caption(currentText)
                delegate: ItemDelegate { required property string modelData; width: replacement.width; text: Bindings.caption(modelData) }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 6
                Ps5EditorButton {
                    objectName: "change-player-shortcut"
                    text: qsTr("更改%1").arg(shortcuts.actionLabel(shortcuts.conflict.action))
                    enabled: replacement.currentIndex >= 0
                    onClicked: shortcuts.apply("player")
                }
                Ps5EditorButton {
                    objectName: "change-game-assignment"
                    text: shortcuts.conflict.otherAction ? qsTr("更改%1").arg(shortcuts.actionLabel(shortcuts.conflict.otherAction))
                          : qsTr("更改个人副本的游戏绑定")
                    enabled: replacement.currentIndex >= 0
                    onClicked: shortcuts.apply(shortcuts.conflict.otherAction ? "other" : "game")
                }
                Ps5EditorButton {
                    objectName: "cancel-shortcut-conflict"
                    text: qsTr("取消")
                    onClicked: shortcuts.settings.cancelShortcutConflict()
                }
            }
            Label {
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.Wrap
                color: Theme.err
                text: shortcuts.settings.error
            }
        }
    }
}
