// Native owned window: the D3D12 video HWND cannot cover the playlist.
import QtQuick
import QtQuick.Window
import QtQuick.Controls
import QtQuick.Layouts

Window {
    id: win
    objectName: "playlistWindow"
    required property Window owner
    required property var playlist
    signal addFiles()
    signal addFolder()
    transientParent: owner
    flags: Qt.Tool | Qt.FramelessWindowHint
    color: "transparent"
    width: 500; height: 600
    minimumWidth: 420; minimumHeight: 320
    property bool positioned: false
    property string selectedPath: ""
    property string dragPath: ""
    property real dragY: 0
    property bool dragging: false
    property string failure: ""
    readonly property var sortOptions: [
        { id: "0", label: qsTr("手动顺序") },
        { id: "1", label: qsTr("名称 A → Z") }, { id: "2", label: qsTr("名称 Z → A") },
        { id: "3", label: qsTr("修改时间：新 → 旧") }, { id: "4", label: qsTr("修改时间：旧 → 新") },
        { id: "5", label: qsTr("文件大小：大 → 小") }, { id: "6", label: qsTr("文件大小：小 → 大") }
    ]
    function open() {
        if (!positioned) {
            const s = owner.screen
            const right = s ? s.virtualX + s.width : owner.x + owner.width
            const bottom = s ? s.virtualY + s.height : owner.y + owner.height
            x = Math.max(s ? s.virtualX : 0, Math.min(owner.x + owner.width - width - 24, right - width))
            y = Math.max(s ? s.virtualY : 0, Math.min(owner.y + 60, bottom - height))
            positioned = true
        }
        show(); raise(); requestActivate()
    }
    function toggle() { if (visible) hide(); else open() }
    function removeSelected() {
        const row = playlist.indexOfPath(selectedPath)
        if (row >= 0) playlist.remove(row)
    }
    function finishDrag(commit) {
        if (dragging && commit) {
            const at = dragY + list.contentY
            let target = list.indexAt(list.width / 2, at)
            // indexAt returns -1 in the inter-row spacing as well as outside
            // the list. Resolve a nearby row before treating it as an end.
            if (target < 0) target = list.indexAt(list.width / 2, at - list.spacing)
            if (target < 0) target = list.indexAt(list.width / 2, at + list.spacing)
            if (target < 0) target = dragY < list.height / 2 ? 0 : playlist.count - 1
            playlist.move(playlist.indexOfPath(dragPath), target)
        }
        dragging = false; dragPath = ""
    }
    onClosing: event => { event.accepted = false; hide() }
    onVisibleChanged: if (!visible) finishDrag(false)
    Connections {
        target: win.playlist
        function onError(text) { win.failure = text; errorTimer.restart() }
    }
    Connections {
        target: win.owner
        function onClosing(event) { win.hide() }
    }
    Timer { id: errorTimer; interval: 6000; onTriggered: win.failure = "" }
    // Window.active also includes an owned activation group and can stay true
    // while hidden. Handle these keys only along this window's actual focus
    // path; ShortcutOverride keeps the owner's fullscreen Escape out of it.
    FocusScope {
        anchors.fill: parent
        focus: true
        Keys.onShortcutOverride: event => {
            if (win.visible && (event.key === Qt.Key_Escape || event.key === Qt.Key_Delete ||
                                event.key === Qt.Key_Return || event.key === Qt.Key_Enter))
                event.accepted = true
        }
        Keys.onPressed: event => {
            if (!win.visible) return
            if (event.key === Qt.Key_Escape) win.hide()
            else if (event.key === Qt.Key_Delete) win.removeSelected()
            else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter)
                win.playlist.play(win.playlist.indexOfPath(win.selectedPath))
            else return
            event.accepted = true
        }
    Rectangle {
        anchors.fill: parent
        radius: Theme.rWindow
        color: Theme.dialog
        border.width: 1; border.color: Theme.stroke2
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 14
            spacing: 10
            Item {
                Layout.fillWidth: true; implicitHeight: 34
                MouseArea {
                    objectName: "playlist-title-drag"
                    anchors.fill: parent
                    onPressed: win.startSystemMove()
                }
                RowLayout {
                    anchors.fill: parent; spacing: 8
                    VIcon { name: "list"; size: 19; color: Theme.accent }
                    Text {
                        text: qsTr("播放列表")
                        font.family: Theme.fontUi; font.pixelSize: 16; font.weight: Font.DemiBold
                        color: Theme.t1
                    }
                    Text { text: qsTr("%1 项").arg(win.playlist.count); font.pixelSize: 11; color: Theme.t3 }
                    Item { Layout.fillWidth: true }
                    VButton {
                        objectName: "playlist-close"
                        icon: true; iconName: "x"; ghost: true; tip: qsTr("关闭窗口")
                        onClicked: win.hide()
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                VButton { objectName: "playlist-add-folder"; text: qsTr("导入文件夹"); iconName: "folder"; primary: true; onClicked: win.addFolder() }
                VButton { objectName: "playlist-add-files"; text: qsTr("添加文件"); iconName: "plus"; onClicked: win.addFiles() }
                Item { Layout.fillWidth: true }
                VButton {
                    objectName: "playlist-clear"
                    text: qsTr("清空"); ghost: true; enabled: win.playlist.count > 0 || win.playlist.busy
                    tip: qsTr("仅清空列表，不删除硬盘文件")
                    onClicked: { win.playlist.clear(); win.selectedPath = ""; win.finishDrag(false) }
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                VSelect {
                    objectName: "playlist-sort"
                    Layout.fillWidth: true
                    title: qsTr("排序")
                    options: win.sortOptions
                    value: win.sortOptions[win.playlist.sortMode].label
                    enabled: !win.dragging
                    onPicked: id => win.playlist.sortBy(Number(id))
                }
                VButton { objectName: "playlist-previous"; icon: true; iconName: "left"; tip: qsTr("上一项"); enabled: win.playlist.count > 0; onClicked: win.playlist.previous() }
                VButton { objectName: "playlist-next"; icon: true; iconName: "right"; tip: qsTr("下一项"); enabled: win.playlist.count > 0; onClicked: win.playlist.next() }
                VButton {
                    objectName: "playlist-locate"
                    icon: true; iconName: "play"; tip: qsTr("定位正在播放的文件")
                    enabled: win.playlist.currentIndex >= 0
                    onClicked: list.positionViewAtIndex(win.playlist.currentIndex, ListView.Contain)
                }
            }
            Item {
                Layout.fillWidth: true; Layout.fillHeight: true; Layout.minimumHeight: 80
                ListView {
                    id: list
                    objectName: "playlist-list"
                    anchors.fill: parent; clip: true; spacing: 4
                    model: win.playlist
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }
                    delegate: Rectangle {
                        id: row
                        required property int index
                        required property string entryName
                        required property string entryPath
                        required property string entrySize
                        required property string entryModified
                        required property bool entryCurrent
                        required property string entryState
                        objectName: "playlist-row-" + index
                        width: list.width - 10; height: 64; radius: 9
                        color: win.selectedPath === entryPath ? Theme.card3 : entryCurrent ? Theme.accentSoft : rowMouse.containsMouse ? Theme.card2 : "transparent"
                        border.width: 1
                        border.color: win.dragging && win.dragPath === entryPath ? Theme.accent : "transparent"
                        MouseArea {
                            id: rowMouse
                            objectName: "playlist-select-" + row.index
                            anchors.fill: parent; anchors.leftMargin: 32; anchors.rightMargin: 40
                            hoverEnabled: true
                            onClicked: win.selectedPath = row.entryPath
                            onDoubleClicked: win.playlist.play(row.index)
                            ToolTip.visible: containsMouse && !win.dragging
                            ToolTip.delay: 900
                            ToolTip.text: row.entryPath
                        }
                        Item {
                            width: 32; height: parent.height
                            VIcon { anchors.centerIn: parent; name: "grip"; size: 16; color: Theme.t3 }
                            MouseArea {
                                objectName: "playlist-grip-" + row.index
                                anchors.fill: parent; cursorShape: pressed ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                                preventStealing: true
                                property real startY: 0
                                onPressed: mouse => {
                                    win.selectedPath = row.entryPath; win.dragPath = row.entryPath
                                    startY = mapToItem(list, mouse.x, mouse.y).y; win.dragY = startY
                                }
                                onPositionChanged: mouse => {
                                    if (!pressed) return
                                    win.dragY = mapToItem(list, mouse.x, mouse.y).y
                                    if (Math.abs(win.dragY - startY) > 5) win.dragging = true
                                }
                                onReleased: win.finishDrag(true)
                                onCanceled: win.finishDrag(false)
                            }
                        }
                        ColumnLayout {
                            anchors.left: parent.left; anchors.leftMargin: 36
                            anchors.right: parent.right; anchors.rightMargin: 42
                            anchors.verticalCenter: parent.verticalCenter; spacing: 4
                            RowLayout {
                                Layout.fillWidth: true; spacing: 6
                                Text {
                                    Layout.fillWidth: true
                                    text: row.entryName; elide: Text.ElideRight; color: row.entryCurrent ? Theme.accent : Theme.t1
                                    font.family: Theme.fontUi; font.pixelSize: 12
                                }
                                Text {
                                    objectName: "playlist-state-" + row.index
                                    visible: row.entryCurrent
                                    text: {
                                        switch (row.entryState) {
                                        case "playing": return qsTr("正在播放")
                                        case "paused": return qsTr("已暂停")
                                        case "opening": return qsTr("正在打开")
                                        case "image": return qsTr("正在查看")
                                        case "ended": return qsTr("已播完")
                                        case "failed": return qsTr("播放失败")
                                        default: return qsTr("已停止")
                                        }
                                    }
                                    color: row.entryState === "failed" ? Theme.err : Theme.accent
                                    font.pixelSize: 10
                                }
                            }
                            Text { Layout.fillWidth: true; text: row.entrySize + " · " + row.entryModified; color: Theme.t3; elide: Text.ElideRight; font.pixelSize: 10 }
                        }
                        VButton {
                            objectName: "playlist-remove-" + row.index
                            anchors.right: parent.right; anchors.rightMargin: 4; anchors.verticalCenter: parent.verticalCenter
                            icon: true; ghost: true; iconName: "x"; tip: qsTr("从列表移除，不删除文件")
                            onClicked: win.playlist.remove(row.index)
                        }
                    }
                }
                Text {
                    anchors.centerIn: parent; width: parent.width - 32
                    visible: win.playlist.count === 0
                    text: win.playlist.busy ? qsTr("正在扫描文件夹…") : qsTr("导入文件夹，或将文件 / 文件夹拖到这里\n支持子文件夹 · 双击播放 · 拖动左侧手柄调整顺序")
                    color: Theme.t3; wrapMode: Text.WordWrap; horizontalAlignment: Text.AlignHCenter
                    font.family: Theme.fontUi; font.pixelSize: 12; lineHeight: 1.5
                }
                Rectangle {
                    visible: win.dragging; x: 0; y: Math.max(0, Math.min(list.height - 2, win.dragY))
                    width: list.width; height: 2; color: Theme.accent
                }
                Timer {
                    interval: 40; repeat: true; running: win.dragging
                    onTriggered: {
                        const step = win.dragY < 28 ? -12 : win.dragY > list.height - 28 ? 12 : 0
                        list.contentY = Math.max(0, Math.min(Math.max(0, list.contentHeight - list.height), list.contentY + step))
                    }
                }
            }
            RowLayout {
                Layout.fillWidth: true; spacing: 8
                Text { text: qsTr("播完后播放下一项"); color: Theme.t2; font.pixelSize: 11 }
                VSwitch { objectName: "playlist-auto-advance"; checked: win.playlist.autoAdvance; onToggled: checked => win.playlist.autoAdvance = checked }
                Item { Layout.fillWidth: true }
            }
            Text {
                objectName: "playlist-status"
                Layout.fillWidth: true
                text: win.failure || win.playlist.status || qsTr("拖入只追加列表，不打断当前播放")
                color: win.failure ? Theme.err : Theme.t3; elide: Text.ElideRight; font.pixelSize: 10
            }
        }
    }
    DropArea {
        id: filesDrop
        objectName: "playlist-drop"
        anchors.fill: parent
        onDropped: drop => {
            console.log("playlist-drop", "hasUrls=" + drop.hasUrls, "count=" + drop.urls.length)
            if (!drop.hasUrls || !drop.urls.length) return
            drop.accept(Qt.CopyAction); win.playlist.addUrls(drop.urls)
        }
        Rectangle {
            anchors.fill: parent; anchors.margins: 2; radius: Theme.rWindow
            visible: filesDrop.containsDrag; color: Theme.accentSoft
            border.width: 2; border.color: Theme.accent
            Text { anchors.centerIn: parent; text: qsTr("松开以追加到播放列表"); color: Theme.t1; font.pixelSize: 16 }
        }
    }
    Repeater {
        model: [
            { e: Qt.LeftEdge, c: Qt.SizeHorCursor }, { e: Qt.RightEdge, c: Qt.SizeHorCursor },
            { e: Qt.TopEdge, c: Qt.SizeVerCursor }, { e: Qt.BottomEdge, c: Qt.SizeVerCursor },
            { e: Qt.LeftEdge | Qt.TopEdge, c: Qt.SizeFDiagCursor }, { e: Qt.RightEdge | Qt.BottomEdge, c: Qt.SizeFDiagCursor },
            { e: Qt.RightEdge | Qt.TopEdge, c: Qt.SizeBDiagCursor }, { e: Qt.LeftEdge | Qt.BottomEdge, c: Qt.SizeBDiagCursor }
        ]
        delegate: MouseArea {
            required property var modelData
            readonly property bool l: (modelData.e & Qt.LeftEdge) !== 0
            readonly property bool r: (modelData.e & Qt.RightEdge) !== 0
            readonly property bool t: (modelData.e & Qt.TopEdge) !== 0
            readonly property bool b: (modelData.e & Qt.BottomEdge) !== 0
            readonly property bool corner: (l || r) && (t || b)
            objectName: "playlist-resize-" + modelData.e
            z: 130
            width: corner ? 12 : (l || r) ? 6 : win.width - 24
            height: corner ? 12 : (t || b) ? 6 : win.height - 24
            x: l ? 0 : r ? win.width - width : 12
            y: t ? 0 : b ? win.height - height : 12
            cursorShape: modelData.c
            onPressed: win.startSystemResize(modelData.e)
        }
    }
    }
}
