import QtQuick
import QtQuick.Controls.Basic
import QtQuick.Layouts
import "Ps5Bindings.js" as Bindings

Item {
    id: player
    required property var settings
    required property bool playerActive
    signal controlRequested()
    signal editRequested(Item opener)

    readonly property bool connected: !!settings && settings.available
    readonly property bool captured: !!settings && settings.captured
    readonly property Item focusItem: Window.window ? Window.window.activeFocusItem : null
    readonly property bool listen: connected && playerActive && !!Window.window && Window.window.active &&
                                   !(focusItem instanceof TextInput || focusItem instanceof TextEdit) &&
                                   !(Overlay.overlay && Overlay.overlay.visible)
    visible: connected && playerActive
    function updateWindow() {
        if (settings && Window.window) settings.setPlayerWindow(Window.window, listen)
    }
    onListenChanged: updateWindow()
    Component.onCompleted: updateWindow()
    Connections {
        target: player.settings
        function onControlRequested() { if (player.listen) player.controlRequested() }
    }

    Rectangle {
        id: bar
        objectName: "ps5-player-toolbar"
        visible: !player.captured || player.settings.showShortcutHints
        readonly property bool videoCover: true
        readonly property int coverRadius: 8
        x: 8; y: 8
        width: Math.min(Math.max(0, player.width - 16),
                        (player.captured ? returnHint.width : controlAction.width + edit.width + controls.spacing)
                        + hint.width + controls.spacing + 16)
        height: controls.height + 12
        radius: coverRadius
        color: Theme.dialog
        border.color: Theme.stroke2

        Flow {
            id: controls
            x: 8; y: 6
            width: parent.width - 16
            spacing: 8
            Ps5EditorButton {
                id: controlAction
                objectName: "ps5-player-control"
                primary: true
                visible: !player.captured
                text: player.settings ? qsTr("控制 PS5 · %1").arg(Bindings.shortcutName(player.settings.controlKey)) : ""
                onClicked: player.controlRequested()
            }
            Ps5EditorButton {
                id: edit
                objectName: "ps5-player-edit"
                visible: !player.captured
                text: qsTr("编辑控制设置")
                onClicked: player.editRequested(edit)
            }
            Label {
                id: returnHint
                objectName: "ps5-player-return-hint"
                visible: player.captured
                height: edit.height
                leftPadding: 4
                verticalAlignment: Text.AlignVCenter
                text: player.settings ? qsTr("返回 Windows · %1").arg(Bindings.shortcutName(player.settings.controlKey)) : ""
                color: Theme.t1; font.family: Theme.fontUi; font.bold: true
            }
            Label {
                id: hint
                objectName: "ps5-player-hint"
                height: edit.height
                leftPadding: 2; rightPadding: 4
                verticalAlignment: Text.AlignVCenter
                text: !player.settings ? "" : (player.settings.captured
                      ? Bindings.situationName(player.settings.profile, player.settings.situation) + "   ·   " : "")
                      + qsTr("显示按键 · %1").arg(Bindings.shortcutName(player.settings.overlayKey))
                color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 12
            }
        }
    }

    Rectangle {
        id: overlay
        objectName: "ps5-bindings-overlay"
        readonly property bool videoCover: true
        readonly property bool passiveVideoCover: true
        readonly property int coverRadius: 8
        visible: player.visible && !!player.settings && player.settings.overlayVisible
        x: bar.x; y: bar.visible ? bar.y + bar.height + 6 : bar.y
        width: Math.min(980, Math.max(0, player.width - 16))
        readonly property int columns: Math.max(1, Math.floor((width - 20) / 270))
        readonly property int rowHeight: 34
        readonly property int maxRows: Math.max(1, Math.floor((player.height - y - rows.y - 16) / rowHeight))
        readonly property int capacity: columns * maxRows
        readonly property var bindings: visible ? Bindings.records(player.settings.profile, player.settings.situation) : []
        readonly property var modes: {
            if (!visible) return []
            const hotkeys = player.settings.profile.context_hotkeys || {}
            const keyOrder = player.settings.shortcutKeys
            return player.settings.situations.filter(id => !!hotkeys[id]).map(id => ({
                id: id, key: hotkeys[id], name: Bindings.situationName(player.settings.profile, id)
            })).sort((left, right) => keyOrder.indexOf(left.key) - keyOrder.indexOf(right.key))
        }
        readonly property int modeColumns: Math.max(1, Math.floor((width - 20) / 148))
        readonly property int modeCapacity: modeColumns * (player.height < 320 ? 1 : 2)
        readonly property int modePages: Math.max(1, Math.ceil(modes.length / modeCapacity))
        readonly property int bindingPages: Math.max(1, Math.ceil(bindings.length / capacity))
        readonly property int pages: Math.max(bindingPages, modePages)
        property int page: 0
        onBindingsChanged: page = 0
        onPagesChanged: page = 0
        height: rows.y + rows.height + 8
        color: Theme.dialog
        border.color: Theme.stroke2
        radius: coverRadius

        // A small picture can fit only part of the list. Cycle those pages so
        // wheel input and every mapped key continue to reach the game.
        Timer {
            interval: 6000
            repeat: true
            running: overlay.visible && overlay.pages > 1
            onTriggered: overlay.page = (overlay.page + 1) % overlay.pages
        }
        RowLayout {
            id: heading
            x: 10; y: 7; width: parent.width - 20
            spacing: 8
            Label {
                text: qsTr("当前情境")
                visible: overlay.width >= 430
                font.family: Theme.fontUi; font.pixelSize: 12; color: Theme.t2
            }
            Label {
                objectName: "ps5-live-situation"
                Layout.fillWidth: true
                elide: Text.ElideRight
                text: overlay.visible ? Bindings.situationName(player.settings.profile, player.settings.situation) : ""
                font.family: Theme.fontUi; font.bold: true; color: Theme.t1
            }
            Label {
                visible: overlay.pages > 1
                text: (overlay.page + 1) + " / " + overlay.pages
                font.family: Theme.fontMono; font.pixelSize: 11; color: Theme.t2
            }
            Label {
                objectName: "ps5-live-control-key"
                Layout.maximumWidth: overlay.width * 0.39
                text: overlay.visible ? qsTr("返回 Windows · %1").arg(Bindings.shortcutName(player.settings.controlKey)) : ""
                wrapMode: Text.Wrap
                font.family: Theme.fontUi; font.pixelSize: 12; color: Theme.t2
            }
            Label {
                objectName: "ps5-live-hide-key"
                text: overlay.visible ? qsTr("隐藏 · %1").arg(Bindings.shortcutName(player.settings.overlayKey)) : ""
                font.family: Theme.fontUi; font.pixelSize: 12; color: Theme.t2
            }
        }
        Label {
            id: modeTitle
            visible: overlay.modes.length > 0
            x: 10; y: heading.y + heading.height + 6
            text: qsTr("切换情境")
            color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 12
        }
        Grid {
            id: modesGrid
            x: 10; y: modeTitle.y + (modeTitle.visible ? modeTitle.height : 0)
            columns: overlay.modeColumns
            height: 28 * Math.ceil(Math.min(overlay.modes.length, overlay.modeCapacity) / columns)
            readonly property int firstMode: (overlay.page % overlay.modePages) * overlay.modeCapacity
            Repeater {
                model: overlay.modes.slice(modesGrid.firstMode, modesGrid.firstMode + overlay.modeCapacity)
                RowLayout {
                    id: mode
                    required property var modelData
                    readonly property bool current: !!player.settings && player.settings.situation === modelData.id
                    width: (overlay.width - 20) / overlay.modeColumns
                    height: 28
                    spacing: 6
                    Label {
                        objectName: "ps5-mode-key-" + mode.modelData.id
                        text: Bindings.shortcutName(mode.modelData.key)
                        leftPadding: 5; rightPadding: 5
                        color: Theme.accent; font.family: Theme.fontMono; font.pixelSize: 12
                        background: Rectangle { color: mode.current ? Theme.accentSoft : Theme.card2; radius: 3; border.color: mode.current ? Theme.accent : Theme.stroke2 }
                    }
                    Label {
                        Layout.fillWidth: true; Layout.rightMargin: 6
                        text: mode.modelData.name
                        font.weight: mode.current ? Font.DemiBold : Font.Normal
                        elide: Text.ElideRight; color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: 12
                    }
                }
            }
        }
        Rectangle {
            x: modesGrid.x; y: modesGrid.y + modesGrid.height + 1
            width: overlay.width - 20; height: 1
            color: Theme.stroke2
        }
        Grid {
            id: rows
            x: 10; y: modesGrid.y + modesGrid.height + 6
            columns: overlay.columns
            readonly property int firstBinding: (overlay.page % overlay.bindingPages) * overlay.capacity
            Repeater {
                model: overlay.bindings.slice(rows.firstBinding, rows.firstBinding + overlay.capacity)
                delegate: RowLayout {
                    id: binding
                    required property var modelData
                    width: (overlay.width - 20) / overlay.columns
                    height: overlay.rowHeight
                    spacing: 6
                    Label {
                        objectName: "ps5-live-input-" + binding.modelData.spec
                        Layout.maximumWidth: binding.width * 0.42
                        text: binding.modelData.spec.includes(":") || binding.modelData.key.startsWith("Mouse")
                            ? binding.modelData.input
                            : Bindings.shortcutName(binding.modelData.spec)
                        wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                        color: Theme.accent; font.family: Theme.fontMono; font.pixelSize: 12
                    }
                    Label {
                        objectName: "ps5-live-" + binding.modelData.spec
                        Layout.fillWidth: true; Layout.rightMargin: 8
                        text: binding.modelData.fixed ? binding.modelData.shortTitle + " · " + binding.modelData.output
                                                      : binding.modelData.shortTitle
                        wrapMode: Text.Wrap; maximumLineCount: 2; elide: Text.ElideRight
                        color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: 12
                    }
                }
            }
        }
    }
}
