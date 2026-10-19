import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

Popup {
    id: picker
    objectName: "capture-legacy-picker"
    property var formats: []
    property string selectedKey: ""
    property var filtered: []
    signal selected(string key)
    parent: Overlay.overlay
    modal: true
    focus: true
    width: Math.min(760, Math.max(1, parent ? parent.width - 32 : 640))
    height: Math.min(560, Math.max(1, parent ? parent.height - 32 : 480))
    x: parent ? Math.round((parent.width - width) / 2) : 0
    y: parent ? Math.round((parent.height - height) / 2) : 0
    padding: 18
    closePolicy: Popup.CloseOnEscape | Popup.CloseOnPressOutside
    background: Rectangle { readonly property bool videoCover: picker.visible; readonly property real coverRadius: 14; color: Theme.bgOuter; radius: 14; border.color: Theme.stroke }

    function applySearch() {
        const words = search.text.toLowerCase().trim().split(/\s+/).filter(w => w.length)
        const snapshot = formats
        filtered = words.length ? snapshot.filter(f => {
            const label = f.label.toLowerCase()
            return words.every(w => label.indexOf(w) >= 0)
        }) : snapshot
    }
    onFormatsChanged: if (visible) applySearch()
    onOpened: { search.text = ""; applySearch(); search.forceActiveFocus() }
    onClosed: filtered = []
    Timer { id: searchDelay; interval: 150; onTriggered: picker.applySearch() }

    contentItem: ColumnLayout {
        spacing: 10
        RowLayout {
            Layout.fillWidth: true
            Text { Layout.fillWidth: true; text: qsTr("旧版格式列表"); color: Theme.t1; font.family: Theme.fontUi; font.pixelSize: 16; font.bold: true }
            VButton { text: qsTr("关闭"); ghost: true; onClicked: picker.close() }
        }
        Text {
            Layout.fillWidth: true
            text: qsTr("分辨率、帧率和像素格式合并显示；包含非常用帧率，与主界面共用同一个精确选择。范围候选将在连接时验证。")
            color: Theme.t3; font.family: Theme.fontUi; font.pixelSize: 11; wrapMode: Text.WordWrap
        }
        Basic.TextField {
            id: search
            objectName: "capture-legacy-search"
            Layout.fillWidth: true
            placeholderText: qsTr("搜索，例如：1920 60 NV12")
            color: Theme.t1; placeholderTextColor: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: 12
            background: Rectangle { color: Qt.rgba(1,1,1,.05); radius: 8; border.color: Theme.stroke }
            onTextEdited: searchDelay.restart()
        }
        Text { text: qsTr("%1 / %2 个格式").arg(picker.filtered.length).arg(picker.formats.length); color: Theme.t3; font.pixelSize: 11 }
        ListView {
            id: list
            objectName: "capture-legacy-list"
            Layout.fillWidth: true; Layout.fillHeight: true
            clip: true
            model: picker.filtered
            reuseItems: true
            ScrollBar.vertical: Basic.ScrollBar {}
            delegate: Basic.ItemDelegate {
                required property var modelData
                width: list.width
                height: 46
                enabled: modelData.disabled !== true
                highlighted: modelData.id === picker.selectedKey
                contentItem: Text {
                    text: modelData.label
                    color: modelData.disabled ? Theme.t3 : modelData.danger ? Theme.err : modelData.costHint ? Theme.warn : Theme.t1
                    font.family: Theme.fontMono; font.pixelSize: 11
                    elide: Text.ElideRight; verticalAlignment: Text.AlignVCenter
                }
                background: Rectangle { radius: 6; color: parent.highlighted ? Qt.rgba(1,1,1,.1) : parent.hovered ? Qt.rgba(1,1,1,.05) : "transparent" }
                ToolTip.visible: hovered
                ToolTip.text: modelData.label
                onClicked: { picker.selected(modelData.id); picker.close() }
            }
            Text { anchors.centerIn: parent; visible: list.count === 0; text: qsTr("没有匹配的格式"); color: Theme.t3; font.pixelSize: 12 }
        }
    }
}
