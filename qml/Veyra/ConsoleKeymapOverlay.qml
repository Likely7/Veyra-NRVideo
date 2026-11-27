// SPDX-License-Identifier: GPL-3.0-only
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: overlay
    property var input: null
    property bool active: false
    property bool locked: false
    property bool suppressed: false
    property bool xbox: false
    property bool referenceOpen: false
    property bool showDriving: false
    property string hintText: ""
    readonly property real uiScale: Math.max(1, Math.min(1.65, height / 800))
    signal editRequested()

    ConsolePadLabels { id: labels; xbox: overlay.xbox }

    function availableKeys(driving, action) {
        const primary = driving ? input?.drivingBindings : input?.bindings
        const alternate = driving ? input?.drivingAlternateBindings : input?.alternateBindings
        return [primary?.[action], alternate?.[action]].filter(key => key && key !== "—").join(" / ")
    }
    function keysFor(driving, actions) {
        return actions.map(action => availableKeys(driving, action)).filter(Boolean).join(" · ")
    }
    function row(action) {
        const keys = availableKeys(showDriving, action)
        if (!keys) return ""
        const notes = showDriving ? input?.drivingActionNotes : input?.actionNotes
        const note = notes?.[action] || ""
        return keys + "  →  " + labels.names[action] + (note ? " · " + note : "")
    }
    function showHint(reason) {
        if (!active || !input) return
        const walk = qsTr("步行")
        const drive = qsTr("驾驶")
        const toggle = input.driveKeyLabel
        const quick = input.quickKeyLabel
        let parts = []
        if (reason === "locked") {
            parts.push(qsTr("键鼠已接管"), input.driving ? drive : walk)
            if (toggle && toggle !== "—")
                parts.push(qsTr("%1 切到%2").arg(toggle).arg(input.driving ? walk : drive))
            if (quick && quick !== "—") parts.push(qsTr("%1 查看键位").arg(quick))
            parts.push(qsTr("%1 释放鼠标").arg(input.unlockLabel))
        } else if (reason === "layout") {
            parts.push(qsTr("已切到%1").arg(input.driving ? drive : walk))
            if (input.driving) {
                const gas = availableKeys(true, 17)
                const brake = availableKeys(true, 16)
                const steer = keysFor(true, [20, 21])
                if (gas) parts.push(qsTr("%1 油门").arg(gas))
                if (brake) parts.push(qsTr("%1 刹车").arg(brake))
                if (steer) parts.push(qsTr("%1 转向").arg(steer))
            } else {
                const move = keysFor(false, [18, 19, 20, 21])
                if (move) parts.push(qsTr("%1 移动").arg(move))
            }
            if (toggle && toggle !== "—")
                parts.push(qsTr("%1 切回%2").arg(toggle).arg(input.driving ? walk : drive))
        } else if (reason === "released") {
            parts.push(qsTr("已释放键鼠"), qsTr("点击画面重新接管"), "Ctrl+Alt+F1 " + qsTr("键鼠设置"))
        } else {
            parts.push(qsTr("点击画面接管键鼠"))
            if (quick && quick !== "—") parts.push(qsTr("%1 查看键位").arg(quick))
        }
        hintFade.stop()
        hint.opacity = 1
        hintText = parts.join(" · ")
        hintTimer.restart()
    }
    function toggleReference() {
        if (!active || suppressed) return
        if (referenceOpen) { referenceOpen = false; return }
        showDriving = input?.driving === true
        referenceOpen = true
        referenceTimer.restart()
    }

    onActiveChanged: {
        if (active) showHint(locked ? "locked" : "ready")
        else { hintText = ""; referenceOpen = false }
    }
    onLockedChanged: showHint(locked ? "locked" : "released")
    onSuppressedChanged: if (suppressed) referenceOpen = false
    Connections {
        target: overlay.input
        ignoreUnknownSignals: true
        function onDrivingChanged() {
            if (overlay.referenceOpen) overlay.showDriving = overlay.input.driving
            if (overlay.locked) overlay.showHint("layout")
        }
        function onQuickReferenceRequested() { overlay.toggleReference() }
        function onQuickReferenceScrolled(delta) {
            if (!reference.visible) return
            rowsView.contentY = Math.max(0, Math.min(Math.max(0, rowsView.contentHeight - rowsView.height),
                rowsView.contentY - delta * 0.5))
        }
    }
    Timer { id: hintTimer; interval: 4700; onTriggered: hintFade.start() }
    NumberAnimation {
        id: hintFade
        target: hint
        property: "opacity"
        to: 0
        duration: 300
        onFinished: overlay.hintText = ""
    }
    Timer { id: referenceTimer; interval: 15000; onTriggered: overlay.referenceOpen = false }

    Rectangle {
        id: hint
        objectName: "ps5-input-hint"
        readonly property bool videoCover: visible
        readonly property real coverRadius: radius
        visible: overlay.active && !overlay.suppressed && !overlay.referenceOpen && overlay.hintText.length > 0
        anchors.top: parent.top
        anchors.topMargin: 55
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.min(parent.width - 24, hintLabel.implicitWidth + 24)
        height: hintLabel.implicitHeight + 12
        radius: 8
        color: Theme.dialog
        border.color: Theme.stroke2
        TapHandler { enabled: !overlay.locked; onTapped: overlay.editRequested() }
        Text {
            id: hintLabel
            anchors.centerIn: parent
            width: hint.width - 24
            text: overlay.hintText
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Math.round(Theme.fsSmall * overlay.uiScale)
        }
    }

    Rectangle {
        id: reference
        objectName: "console-keymap-reference"
        readonly property bool videoCover: visible
        readonly property real coverRadius: radius
        visible: overlay.active && !overlay.suppressed && overlay.referenceOpen
        width: Math.min(parent.width * 0.5 - 18, 336 * overlay.uiScale)
        height: Math.min(parent.height * 0.7, 530 * overlay.uiScale)
        anchors.right: parent.right
        anchors.rightMargin: Math.max(12, parent.width * 0.022)
        anchors.verticalCenter: parent.verticalCenter
        radius: 12
        color: Qt.rgba(Theme.dialog.r, Theme.dialog.g, Theme.dialog.b, 0.96)
        border.color: Theme.stroke2
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 12
            spacing: 8
            Text {
                Layout.fillWidth: true
                text: qsTr("当前：%1").arg(overlay.input?.driving ? qsTr("驾驶") : qsTr("步行"))
                color: Theme.t1
                wrapMode: Text.Wrap
                font.family: Theme.fontUi
                font.bold: true
                font.pixelSize: Math.round(Theme.fsBody * overlay.uiScale)
            }
            VSeg {
                current: overlay.showDriving ? "drive" : "walk"
                options: [{id:"walk", label:qsTr("步行")}, {id:"drive", label:qsTr("驾驶")}]
                onPicked: mode => {
                    overlay.showDriving = mode === "drive"
                    rowsView.contentY = 0
                    referenceTimer.restart()
                }
            }
            Flickable {
                id: rowsView
                objectName: "console-keymap-bindings"
                Layout.fillWidth: true
                Layout.fillHeight: true
                contentWidth: width
                contentHeight: rows.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                ScrollBar.vertical: ScrollBar { }
                ColumnLayout {
                    id: rows
                    width: rowsView.width - 8
                    spacing: 6
                    Repeater {
                        model: [
                            { title: qsTr("移动"), actions: overlay.showDriving ? [18,19,26,10] : [18,19,20,21,26,10] },
                            { title: qsTr("驾驶"), actions: overlay.showDriving ? [17,16,20,21] : [] },
                            { title: qsTr("视角"), actions: [22,23,24,25], mouse: true },
                            { title: qsTr("战斗"), actions: overlay.showDriving ? [8,9,11,32] : [17,16,8,9,11,32] },
                            { title: qsTr("交互"), actions: [0,1,2,3] },
                            { title: qsTr("菜单"), actions: overlay.xbox ? [4,5,6,7,12,13,15,27] : [4,5,6,7,12,13,14,15,27,28,29,30,31] }
                        ]
                        delegate: ColumnLayout {
                            id: group
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 3
                            readonly property var lines: {
                                const result = modelData.actions.map(action => overlay.row(action)).filter(Boolean)
                                const tune = overlay.input?.tuning
                                if (modelData.mouse && tune?.mouseLook !== false) {
                                    const ads = overlay.availableKeys(overlay.showDriving, Number(tune?.adsAction ?? 16))
                                    result.unshift(qsTr("鼠标 → 右摇杆"))
                                    if (ads) result.push(qsTr("%1 开镜 · %2×").arg(ads).arg(Number(tune?.adsScale ?? 1).toFixed(2)))
                                }
                                return result
                            }
                            visible: lines.length > 0
                            Text {
                                Layout.fillWidth: true
                                text: group.modelData.title
                                color: Theme.accent
                                font.family: Theme.fontUi
                                font.bold: true
                                font.pixelSize: Math.round(Theme.fsSmall * overlay.uiScale)
                            }
                            Repeater {
                                model: group.lines
                                delegate: Text {
                                    required property string modelData
                                    Layout.fillWidth: true
                                    text: modelData
                                    color: Theme.t1
                                    wrapMode: Text.Wrap
                                    font.family: Theme.fontUi
                                    font.pixelSize: Math.round(Theme.fsSmall * overlay.uiScale)
                                }
                            }
                        }
                    }
                }
            }
            Repeater {
                model: {
                    const entries = []
                    if (overlay.input?.driveKeyLabel && overlay.input.driveKeyLabel !== "—")
                        entries.push(qsTr("%1 切换步行／驾驶").arg(overlay.input.driveKeyLabel))
                    if (overlay.input?.unlockLabel)
                        entries.push(qsTr("%1 释放鼠标").arg(overlay.input.unlockLabel))
                    if (overlay.input?.quickKeyLabel && overlay.input.quickKeyLabel !== "—")
                        entries.push(qsTr("%1 关闭本面板").arg(overlay.input.quickKeyLabel))
                    return entries
                }
                delegate: Text {
                    required property string modelData
                    Layout.fillWidth: true
                    text: modelData
                    color: Theme.t2
                    wrapMode: Text.Wrap
                    font.family: Theme.fontUi
                    font.pixelSize: Math.round(Theme.fsSmall * overlay.uiScale)
                }
            }
        }
    }
}
