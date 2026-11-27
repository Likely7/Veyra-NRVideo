// SPDX-License-Identifier: GPL-3.0-only
pragma ComponentBehavior: Bound
import QtQuick
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

ColumnLayout {
    id: page
    objectName: "controller-panel-bindings"
    property var input: null
    property bool xbox: false
    property bool driving: false
    property bool active: true
    property Item learningRoot: page
    property int selected: -1
    property int selectedSlot: 0
    property bool listPreferred: false
    property string profileOperation: ""
    readonly property bool showList: listPreferred
    readonly property bool editingEnabled: input !== null && !input.previewing
    readonly property bool recording: (input?.learning ?? -1) >= 0
    readonly property var conflict: input?.bindingConflict || ({})
    readonly property bool conflictOpen: Boolean(conflict.key)
    readonly property var live: input?.padState ?? ({})
    readonly property var names: labels.names
    readonly property int driveLearningAction: names.length
    readonly property int quickLearningAction: names.length + 1
    signal layoutPicked(bool driving)
    spacing: 9

    ConsolePadLabels { id: labels; xbox: page.xbox }

    function binding(action, slot = 0) { return labels.binding(input, driving, action, slot) }
    function note(action) { return labels.actionNote(input, driving, action) }
    function choose(action, slot = -1) {
        if (!editingEnabled || action < 0 || action >= names.length) return
        input.cancelLearning()
        selected = action
        selectedSlot = slot < 0 ? 0 : slot
    }
    function pickFromDiagram(action) {
        if (!editingEnabled) return
        choose(action, 0)
        beginLearning(0)
        Qt.callLater(revealDiagramControls)
    }
    function revealDiagramControls() {
        const editor = diagramInspector.item
        if (!active || showList || !editor) return
        for (let view = diagramInspector.parent; view; view = view.parent) {
            if (!(view instanceof Flickable)) continue
            const top = diagramInspector.mapToItem(view.contentItem, 0, 0).y
            const bottom = top + editor.controlsBottom
            let target = view.contentY
            if (top < target || bottom - top >= view.height) target = top
            else if (bottom > target + view.height) target = bottom - view.height
            view.contentY = Math.max(view.originY, Math.min(target, view.originY + Math.max(0, view.contentHeight - view.height)))
            break
        }
    }
    function beginLearning(slot) {
        if (!editingEnabled || selected < 0) return
        selectedSlot = slot
        if (input.learning === selected && input.learningSlot === slot) {
            input.cancelLearning()
        } else if (typeof input.learnPanelBinding === "function") {
            input.learnPanelBinding(selected, learningRoot, driving, slot)
        } else {
            input.learn(selected, learningRoot, driving, slot)
        }
    }
    function changeLayout(on) {
        if (driving === on) return
        input?.cancelLearning()
        layoutPicked(on)
        if (input?.previewing) input.selectDriving(on)
    }
    function setPreview(on) {
        if (!input || input.previewing === on) return
        input.cancelLearning()
        if (on) input.selectDriving(driving)
        input.preview(on)
    }
    function cancelEditing() {
        if (recording || conflictOpen) {
            input?.cancelLearning()
            return true
        }
        if (profileList.opened || profileActions.opened || unlockMenu.opened) {
            profileList.close()
            profileActions.close()
            unlockMenu.close()
            return true
        }
        if (confirm.shown) {
            confirm.close()
            return true
        }
        if (profileOperation) {
            profileOperation = ""
            return true
        }
        if (input?.previewing) {
            input.preview(false)
            return true
        }
        return false
    }
    function resetLayout() {
        if (!input) return
        input.cancelLearning()
        confirm.operation = "reset"
        confirm.open()
    }
    function openProfileEdit(operation) {
        if (!editingEnabled) return
        input.cancelLearning()
        profileOperation = operation
        profileName.text = operation === "rename" ? input.profile : ""
        profileName.forceActiveFocus()
        profileName.selectAll()
    }
    function commitProfileEdit() {
        const name = profileName.text.trim()
        if (!editingEnabled || name.length === 0 || name === input.profile)
            return
        if (profileOperation === "rename") input.renameProfile?.(name)
        else input.selectProfile(name, true)
        if (input.profile === name) profileOperation = ""
    }

    onActiveChanged: if (!active) {
        if (recording || conflictOpen) input?.cancelLearning()
        if (input?.previewing) input.preview(false)
        profileList.close()
        profileActions.close()
        unlockMenu.close()
        confirm.close()
        profileOperation = ""
    }
    onXboxChanged: {
        selected = -1
        selectedSlot = 0
        profileOperation = ""
    }
    onDrivingChanged: {
        if (selected === 33 && !driving) selected = -1
        if (recording || conflictOpen) input?.cancelLearning()
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Text {
            text: qsTr("档案")
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
        }
        ControllerPanelButton {
            id: profilePicker
            objectName: "controller-panel-profile-picker"
            text: page.input?.profile === "默认" ? qsTr("默认") : page.input?.profile || "—"
            tip: qsTr("游戏档案")
            trailingIcon: "down"
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            enabled: page.editingEnabled
            activeFocusOnTab: enabled
            Accessible.role: Accessible.Button
            Accessible.name: qsTr("档案：%1").arg(text)
            Accessible.onPressAction: clicked()
            Keys.onReturnPressed: clicked()
            Keys.onSpacePressed: clicked()
            onClicked: {
                page.input?.cancelLearning()
                profileList.openAt(this, "down")
            }
        }
        ControllerPanelButton {
            objectName: "controller-panel-profile-add"
            icon: true
            iconName: "plus"
            tip: qsTr("新建档案")
            enabled: page.editingEnabled
            onClicked: page.openProfileEdit("create")
        }
        ControllerPanelButton {
            id: profileMenuButton
            objectName: "controller-panel-profile-actions"
            icon: true
            tip: qsTr("档案操作")
            Row {
                anchors.centerIn: parent
                spacing: 3
                Repeater {
                    model: 3
                    delegate: Rectangle { width: 3; height: 3; radius: 1.5; color: Theme.t2 }
                }
            }
            enabled: page.editingEnabled
            onClicked: {
                page.input?.cancelLearning()
                profileActions.openAt(this, "down")
            }
        }
    }

    VMenu {
        id: profileList
        title: qsTr("游戏档案")
        items: (page.input?.profiles || []).map(name => ({
            id: name, label: name === "默认" ? qsTr("默认") : name, checked: name === page.input?.profile
        }))
        onPicked: (index, item) => page.input?.selectProfile(item.id, false)
    }
    VMenu {
        id: profileActions
        title: qsTr("档案操作")
        items: [
            { label: qsTr("重命名档案"), disabled: page.input?.canRemoveProfile !== true },
            { label: qsTr("删除档案"), disabled: page.input?.canRemoveProfile !== true, danger: true },
            { label: qsTr("恢复默认键位") }
        ]
        onPicked: (index, item) => {
            if (index === 0) page.openProfileEdit("rename")
            else if (index === 1) {
                confirm.operation = "delete"
                confirm.open()
            } else page.resetLayout()
        }
    }
    VConfirm {
        id: confirm
        parent: page.Window.window?.contentItem || page
        property string operation: "reset"
        cardWidth: 380
        title: operation === "delete" ? qsTr("删除档案") : qsTr("恢复默认键位")
        text: operation === "delete"
            ? qsTr("删除档案“%1”？").arg(page.input?.profile || "")
            : qsTr("恢复%1布局的默认键位？").arg(page.driving ? qsTr("驾驶") : qsTr("步行"))
        onAccepted: {
            if (operation === "delete") page.input?.removeProfile?.()
            else page.input?.applyDefaults(page.driving)
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: page.profileOperation.length > 0
        spacing: 6
        Basic.TextField {
            id: profileName
            objectName: "controller-panel-profile-name"
            readonly property bool ps5LearningControl: true
            Layout.fillWidth: true
            Layout.minimumWidth: 0
            placeholderText: qsTr("档案名称")
            maximumLength: 40
            color: Theme.t1
            placeholderTextColor: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            background: Rectangle {
                radius: Theme.rCtl
                color: Theme.card3
                border.color: profileName.activeFocus ? Theme.accent : Theme.stroke2
            }
            onAccepted: page.commitProfileEdit()
        }
        ControllerPanelButton {
            objectName: "controller-panel-profile-save"
            primary: true
            text: qsTr("确定")
            enabled: profileName.text.trim().length > 0 && profileName.text.trim() !== page.input?.profile
            onClicked: page.commitProfileEdit()
        }
        ControllerPanelButton {
            text: qsTr("取消")
            ghost: true
            onClicked: page.profileOperation = ""
        }
    }

    Text {
        Layout.fillWidth: true
        visible: page.profileOperation.length > 0 && Boolean(page.input?.feedback)
        text: page.input?.feedback || ""
        wrapMode: Text.Wrap
        color: Theme.warn
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
    }

    RowLayout {
        Layout.fillWidth: true
        visible: page.xbox
        VSwitch {
            readonly property bool ps5LearningControl: true
            enabled: page.editingEnabled
            checked: page.input?.xboxOnlyProfile === true
            activeFocusOnTab: enabled
            Accessible.name: qsTr("仅用于 Xbox")
            Keys.onSpacePressed: toggled(!checked)
            onToggled: on => page.input?.setXboxOnlyProfile(on)
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("仅用于 Xbox")
            wrapMode: Text.Wrap
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: [qsTr("步行"), qsTr("驾驶")]
            delegate: ControllerPanelButton {
                required property int index
                required property string modelData
                objectName: "controller-panel-layout-" + index
                text: modelData
                primary: (index === 1) === page.driving
                fontSize: Theme.fsSmall
                maxTextWidth: 84
                onClicked: page.changeLayout(index === 1)
            }
        }
        Item { Layout.fillWidth: true; Layout.minimumWidth: 0 }
        Text {
            text: qsTr("试一试")
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
        }
        VSwitch {
            readonly property bool ps5LearningControl: true
            objectName: "controller-panel-preview"
            enabled: page.input !== null
            checked: page.input?.previewing === true
            activeFocusOnTab: enabled
            Accessible.name: qsTr("试一试")
            Keys.onSpacePressed: toggled(!checked)
            onToggled: on => page.setPreview(on)
        }
    }
    Text {
        Layout.fillWidth: true
        visible: page.input?.previewing === true
        text: qsTr("按 Esc 退出试用。")
        wrapMode: Text.Wrap
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
    }

    Flow {
        Layout.fillWidth: true
        spacing: 6
        Repeater {
            model: [
                { action: page.driveLearningAction, label: qsTr("切换步行／驾驶"), key: page.input?.driveKeyLabel ?? "F6" },
                { action: page.quickLearningAction, label: qsTr("键位速查"), key: page.input?.quickKeyLabel ?? "F2" },
                { action: -1, label: qsTr("释放键鼠"), key: page.input?.unlockLabel ?? "F1" }
            ]
            delegate: Row {
                id: shortcutRow
                required property var modelData
                readonly property bool listening: modelData.action >= 0 && modelData.action === page.input?.learning
                spacing: 4
                KeyCap {
                    id: shortcutKey
                    objectName: "controller-panel-shortcut-" + shortcutRow.modelData.action
                    anchors.verticalCenter: parent.verticalCenter
                    text: shortcutRow.listening ? "…" : shortcutRow.modelData.key
                    size: "s"
                    visualState: shortcutRow.listening ? "recording" : text === "—" ? "empty" : "normal"
                    interactive: page.input !== null && !page.input.previewing
                    accessibleText: qsTr("%1 · %2").arg(shortcutRow.modelData.label).arg(shortcutRow.modelData.key)
                    onClicked: {
                        if (shortcutRow.listening) page.input.cancelLearning()
                        else if (shortcutRow.modelData.action === page.driveLearningAction) page.input.learnDriveKey(page.learningRoot)
                        else if (shortcutRow.modelData.action === page.quickLearningAction) page.input.learnQuickKey(page.learningRoot)
                        else {
                            page.input.cancelLearning()
                            unlockMenu.openAt(this, "down")
                        }
                    }
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: shortcutRow.modelData.label
                    color: Theme.t1
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSmall
                }
            }
        }
    }
    VMenu {
        id: unlockMenu
        title: qsTr("释放键鼠")
        items: ["Esc", "Ctrl+Alt+Z", "Alt+Q", "F1", "~"].map((key, index) => ({
            label: key, checked: Number(page.input?.tuning?.unlock || 0) === index
        }))
        onPicked: index => page.input?.setTuning("unlock", index)
    }
    Text {
        Layout.fillWidth: true
        visible: (page.recording && page.input.learning >= page.driveLearningAction) ||
            (!page.recording && page.selected < 0 && Boolean(page.input?.feedback))
        text: page.input?.feedback || qsTr("按下新的快捷键…")
        wrapMode: Text.Wrap
        color: page.input?.feedback ? Theme.warn : Theme.accent
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsSmall
    }

    RowLayout {
        Layout.fillWidth: true
        Item { Layout.fillWidth: true }
        Repeater {
            model: [qsTr("图示"), qsTr("列表")]
            delegate: ControllerPanelButton {
                required property int index
                required property string modelData
                objectName: "controller-panel-binding-view-" + index
                text: modelData
                primary: (index === 1) === page.showList
                fontSize: Theme.fsSmall
                maxTextWidth: 74
                onClicked: {
                    if (page.recording || page.conflictOpen) page.input?.cancelLearning()
                    page.listPreferred = index === 1
                }
            }
        }
    }
    Loader {
        Layout.fillWidth: true
        active: !page.showList
        visible: active
        sourceComponent: ControllerDiagram {
            platform: page.xbox ? "xbox" : "ps5"
            driving: page.driving
            selected: page.selected
            editable: page.editingEnabled
            input: page.input
            live: page.live
            onPicked: action => page.pickFromDiagram(action)
        }
    }
    Loader {
        id: diagramInspector
        Layout.fillWidth: true
        active: !page.showList && page.selected >= 0
        visible: active
        sourceComponent: bindingEditor
    }
    Loader {
        Layout.fillWidth: true
        active: page.showList
        visible: active
        sourceComponent: ColumnLayout {
            objectName: "controller-panel-binding-list"
            spacing: 4
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: qsTr("按键")
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSmall
                }
                Text {
                    text: qsTr("主键／备用键")
                    color: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSmall
                }
            }
            Repeater {
                model: labels.groups(page.driving).filter(group => group.actions.length)
                delegate: ColumnLayout {
                    id: group
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 1
                    Text {
                        Layout.fillWidth: true
                        text: group.modelData.title
                        wrapMode: Text.Wrap
                        color: Theme.accent
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        font.weight: Font.DemiBold
                    }
                    Repeater {
                        model: group.modelData.actions
                        delegate: ColumnLayout {
                            id: listEntry
                            required property int modelData
                            Layout.fillWidth: true
                            spacing: 3
                            BindingRow {
                                objectName: "controller-panel-binding-row-" + action
                                Layout.fillWidth: true
                                action: listEntry.modelData
                                xbox: page.xbox
                                selected: action === page.selected
                                pressed: labels.pressed(page.live, action)
                                toggleBinding: labels.toggles(page.input, action, page.driving)
                                label: page.names[action]
                                note: page.note(action)
                                primary: page.binding(action)
                                alternate: page.binding(action, 1)
                                enabled: page.editingEnabled
                                onPicked: slot => page.choose(action, slot)
                            }
                            Loader {
                                Layout.fillWidth: true
                                active: listEntry.modelData === page.selected
                                visible: active
                                sourceComponent: bindingEditor
                            }
                        }
                    }
                }
            }
        }
    }

    Component {
        id: bindingEditor
        Rectangle {
            id: editor
            objectName: "controller-panel-binding-editor"
            readonly property real controlsBottom: form.y + editSlots.y + editSlots.height
            implicitHeight: form.implicitHeight + 20
            radius: Theme.rCard
            color: Theme.popover
            border.color: Theme.stroke2
            ColumnLayout {
                id: form
                x: 10
                y: 10
                width: Math.max(0, parent.width - 20)
                spacing: 7
                RowLayout {
                    Layout.fillWidth: true
                    ConsoleGlyph {
                        action: page.selected
                        platform: page.xbox ? "xbox" : "ps5"
                        size: 23
                        visualState: "selected"
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: page.note(page.selected) ? qsTr("%1 · %2").arg(page.names[page.selected]).arg(page.note(page.selected))
                            : page.names[page.selected]
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsH3
                        font.weight: Font.DemiBold
                    }
                    ControllerPanelButton {
                        icon: true
                        iconName: "x"
                        tip: qsTr("关闭")
                        ghost: true
                        onClicked: {
                            page.input?.cancelLearning()
                            page.selected = -1
                        }
                    }
                }
                RowLayout {
                    id: editSlots
                    Layout.fillWidth: true
                    spacing: 7
                    Repeater {
                        model: [qsTr("主键"), qsTr("备用键")]
                        delegate: ColumnLayout {
                            id: slotColumn
                            required property int index
                            required property string modelData
                            readonly property bool listening: page.input?.learning === page.selected
                                && page.input?.learningSlot === index && !page.conflictOpen
                            Layout.fillWidth: true
                            Layout.minimumWidth: 0
                            spacing: 3
                            Text {
                                Layout.fillWidth: true
                                text: slotColumn.modelData
                                wrapMode: Text.Wrap
                                color: Theme.t2
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsSmall
                            }
                            KeyCap {
                                objectName: "controller-panel-edit-slot-" + slotColumn.index
                                Layout.maximumWidth: slotColumn.width
                                interactive: page.editingEnabled
                                text: slotColumn.listening ? qsTr("按键…") : page.binding(page.selected, slotColumn.index)
                                visualState: slotColumn.listening ? "recording"
                                    : text === "—" ? "empty" : slotColumn.index === 1 ? "secondary" : "normal"
                                accessibleText: qsTr("%1：%2").arg(slotColumn.modelData).arg(text)
                                onClicked: page.beginLearning(slotColumn.index)
                            }
                        }
                    }
                    ControllerPanelButton {
                        objectName: "controller-panel-clear-binding"
                        Layout.alignment: Qt.AlignBottom
                        visible: !page.recording && page.binding(page.selected, page.selectedSlot) !== "—"
                        enabled: page.editingEnabled
                        text: qsTr("清除")
                        ghost: true
                        maxTextWidth: 60
                        onClicked: page.input?.clearBinding(page.selected, page.driving, page.selectedSlot)
                    }
                }
                Text {
                    Layout.fillWidth: true
                    visible: !page.conflictOpen && (page.recording || Boolean(page.input?.feedback))
                    text: page.input?.feedback || qsTr("按下键盘或鼠标键。Esc 取消，Backspace 清除。")
                    color: page.input?.feedback ? Theme.warn : Theme.accent
                    wrapMode: Text.Wrap
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsSmall
                }
                ColumnLayout {
                    objectName: "controller-panel-binding-conflict"
                    visible: page.conflictOpen
                    Layout.fillWidth: true
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("%1 已用于 %2。").arg(page.conflict.key || "").arg(page.names[page.conflict.source] ?? "")
                        wrapMode: Text.Wrap
                        color: Theme.warn
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsSmall
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 5
                        Repeater {
                            model: [qsTr("改用这个"), qsTr("取消")]
                            delegate: ControllerPanelButton {
                                required property int index
                                required property string modelData
                                text: modelData
                                primary: index === 0
                                ghost: index !== 0
                                onClicked: page.input?.resolveBindingConflict(index === 0)
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    visible: [1, 10, 16, 27].includes(page.selected) && !(page.driving && page.selected === 16)
                    Text {
                        Layout.fillWidth: true
                        text: qsTr("切换模式")
                        wrapMode: Text.Wrap
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    VSwitch {
                        readonly property bool ps5LearningControl: true
                        checked: labels.toggles(page.input, page.selected, page.driving)
                        enabled: page.editingEnabled
                        activeFocusOnTab: enabled
                        Accessible.name: qsTr("切换模式")
                        Keys.onSpacePressed: toggled(!checked)
                        onToggled: on => page.input?.setToggleAction(page.selected, on)
                    }
                }
                Basic.TextField {
                    id: actionNote
                    readonly property bool ps5LearningControl: true
                    objectName: "controller-panel-action-note"
                    Layout.fillWidth: true
                    enabled: page.editingEnabled && !page.recording
                    maximumLength: 12
                    text: page.note(page.selected)
                    placeholderText: qsTr("动作备注")
                    color: Theme.t1
                    placeholderTextColor: Theme.t2
                    font.family: Theme.fontUi
                    font.pixelSize: Theme.fsBody
                    background: Rectangle {
                        radius: Theme.rCtl
                        color: Theme.card3
                        border.color: actionNote.activeFocus ? Theme.accent : Theme.stroke2
                    }
                    onTextEdited: page.input?.setActionNote(page.selected, page.driving, text)
                }
            }
        }
    }
    ControllerPanelButton {
        objectName: "controller-panel-reset-layout"
        text: qsTr("恢复默认键位")
        enabled: page.editingEnabled
        onClicked: page.resetLayout()
    }
}
