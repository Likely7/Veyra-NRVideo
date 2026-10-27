// manage dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: manageDialog
    key: "manage"
    glyph: "settings"
    title: qsTr("管理预设")
    sub: qsTr("导出页和极简模式都从这里的预设中选择")
    dialogWidth: 620
    property string mode: "list"
    property string feedback: ""
    property bool feedbackError: false
    function runAction(action, index, name) {
        let ok = false
        if (action === "rename") ok = veyra.renamePreset(index, name)
        else if (action === "duplicate") ok = veyra.duplicatePreset(index)
        else if (action === "delete") ok = veyra.deletePreset(index)
        else if (action === "default") ok = veyra.setDefaultPreset(index)
        feedbackError = !ok
        if (action === "rename")
            feedback = ok ? qsTr("预设已改名") : qsTr("改名失败：名称不能为空、超过48字或与已有预设重复")
        else if (action === "duplicate")
            feedback = ok ? qsTr("预设已复制") : qsTr("复制失败：预设数量可能已达上限")
        else if (action === "delete")
            feedback = ok ? qsTr("预设已删除") : qsTr("删除预设失败")
        else if (action === "default")
            feedback = ok ? (index < 0 ? qsTr("已取消启动默认预设") : qsTr("已设为启动默认预设")) : qsTr("启动默认预设保存失败")
        return ok
    }
    actions: [{ label: qsTr("完成"), primary: true }]
    onActionTriggered: dialogHost.close()

    ColumnLayout {
        Layout.fillWidth: true
        spacing: 8
        // presets.js manage: the mode toggle, then 导入 / 导出 on the right.
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            VSeg {
                objectName: "preset-manage-mode"
                options: [{ id: "list", label: qsTr("列表预设") }, { id: "node", label: qsTr("节点预设") }]
                current: manageDialog.mode
                onPicked: mode => { manageDialog.mode = mode; manageDialog.feedback = "" }
            }
            Item { Layout.fillWidth: true }
            VButton { objectName: "preset-import"; ghost: true; iconName: "import"; text: qsTr("导入"); onClicked: veyra.importPresetDialog() }
            VButton {
                id: presetExportBtn
                objectName: "preset-export"
                ghost: true; iconName: "upload"; text: qsTr("导出")
                enabled: presetRows.count > 0
                onClicked: presetExportMenu.openAt(presetExportBtn, "down")
            }
        }
        Text {
            visible: presetRows.count === 0
            text: qsTr("这个模式还没有预设。")
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
        }
        Repeater {
            id: presetRows
            model: manageDialog.shown
                   ? veyra.presets.filter(p => p.nodeMode === (manageDialog.mode === "node")) : []
            delegate: Rectangle {
                id: presetRow
                required property var modelData
                objectName: "preset-row-" + modelData.index
                Layout.fillWidth: true
                implicitHeight: 54
                radius: 12
                color: Theme.card2
                border.width: 1
                border.color: Theme.stroke
                readonly property bool isDefault: veyra.defaultPresetIndex === modelData.index
                // .prow.bye (M41): shrink and fade over .25s, then delete.
                property real motionS: 1
                scale: motionS
                ParallelAnimation {
                    id: rowBye
                    NumberAnimation { target: presetRow; property: "motionS"; to: 0.9; duration: Theme.d(250); easing.bezierCurve: Theme.easeOut }
                    NumberAnimation { target: presetRow; property: "opacity"; to: 0; duration: Theme.d(250); easing.bezierCurve: Theme.easeOut }
                    onFinished: manageDialog.runAction("delete", presetRow.modelData.index)
                }
                function remove() {
                    if (Theme.d(250) <= 0) manageDialog.runAction("delete", modelData.index)
                    else rowBye.restart()
                }
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 8
                    spacing: 8
                    // .pico
                    Rectangle {
                        implicitWidth: 30; implicitHeight: 30; radius: 9
                        color: Qt.rgba(1, 1, 1, 0.05)
                        VIcon { anchors.centerIn: parent; name: modelData.nodeMode ? "nodes" : "layers"; size: 14; color: Theme.t2 }
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 2
                        DialogTextField {
                            id: presetNameEdit
                            objectName: "preset-rename-" + modelData.index
                            Layout.fillWidth: true
                            enabled: !modelData.builtin
                            text: modelData.name
                            onEdited: name => {
                                const trimmed = name.trim()
                                if (trimmed === modelData.name) return
                                if (!manageDialog.runAction("rename", modelData.index, trimmed)) {
                                    presetNameEdit.text = modelData.name
                                }
                            }
                        }
                        Text {
                            Layout.fillWidth: true
                            text: modelData.note.length > 0 ? modelData.note
                                  : (modelData.nodeMode ? qsTr("节点预设") : qsTr("列表预设"))
                            color: Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: 11
                            elide: Text.ElideRight
                        }
                    }
                    VTag { objectName: "preset-default-tag-" + modelData.index; visible: presetRow.isDefault; kind: "acc"; text: qsTr("启动默认") }
                    VButton {
                        objectName: "preset-default-" + modelData.index
                        icon: true; ghost: true; iconName: "home"
                        tip: presetRow.isDefault ? qsTr("取消启动默认") : qsTr("设为启动默认")
                        onClicked: manageDialog.runAction("default", presetRow.isDefault ? -1 : modelData.index)
                    }
                    VButton {
                        objectName: "preset-duplicate-" + modelData.index
                        icon: true; ghost: true; iconName: "dup"
                        tip: qsTr("复制")
                        onClicked: manageDialog.runAction("duplicate", modelData.index)
                    }
                    VButton {
                        objectName: "preset-delete-" + modelData.index
                        icon: true; ghost: true; iconName: "trash"
                        tip: qsTr("删除")
                        enabled: !modelData.builtin
                        onClicked: presetRow.remove()
                    }
                }
            }
        }
        VMenu {
            id: presetExportMenu
            objectName: "preset-export-menu"
            title: qsTr("导出预设")
            items: veyra.presets.filter(p => p.nodeMode === (manageDialog.mode === "node"))
                .map(p => ({ label: p.name, note: p.note, icon: "upload", preset: p.index }))
            onPicked: (i, o) => veyra.exportPresetDialog(o.preset)
        }
        Text {
            objectName: "preset-manage-feedback"
            Layout.fillWidth: true
            visible: manageDialog.feedback.length > 0
            text: manageDialog.feedback
            color: manageDialog.feedbackError ? Theme.warn : Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
            wrapMode: Text.WordWrap
        }
    }
}
