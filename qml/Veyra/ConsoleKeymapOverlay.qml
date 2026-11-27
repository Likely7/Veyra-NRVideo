// SPDX-License-Identifier: GPL-3.0-only
pragma ComponentBehavior: Bound
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
    property var hintParts: []
    readonly property string hintText: hintParts.map(part => part.keys.concat([part.label]).join(" ")).join(" · ")
    readonly property real uiScale: Math.max(1, Math.min(1.65, height / 800))
    signal editRequested()

    ConsolePadLabels { id: labels; xbox: overlay.xbox }

    function chords(driving, action) {
        return [labels.binding(input, driving, action), labels.binding(input, driving, action, 1)]
            .filter(key => key && key !== "—")
    }
    function groupChords(driving, actions) { return actions.reduce((keys, action) => keys.concat(chords(driving, action)), []) }
    function row(action) {
        const keys = chords(showDriving, action)
        if (!keys.length) return ""
        const note = labels.actionNote(input, showDriving, action)
        return keys.join(" / ") + "  →  " + labels.names[action] + (note ? " · " + note : "")
    }
    function showHint(reason) {
        if (!active || !input) return
        const walking = qsTr("步行")
        const driving = qsTr("驾驶")
        const other = input.driving ? walking : driving
        const parts = []
        const add = (label, keys = []) => {
            parts.push({ label: label, keys: keys.filter(key => key && key !== "—") })
        }
        if (reason === "locked") {
            add(qsTr("键鼠控制中 · %1").arg(input.driving ? driving : walking))
            if (input.driveKeyLabel && input.driveKeyLabel !== "—")
                add(qsTr("切到%1").arg(other), [input.driveKeyLabel])
            if (input.quickKeyLabel && input.quickKeyLabel !== "—")
                add(qsTr("键位速查"), [input.quickKeyLabel])
            add(qsTr("释放键鼠"), [input.unlockLabel])
            if (input.lowLatencyActive === true) add(qsTr("低延迟模式 · 补帧暂停"))
        } else if (reason === "layout") {
            add(qsTr("已切到%1").arg(input.driving ? driving : walking))
            if (input.driving) {
                const gas = chords(true, 17)
                const brake = chords(true, 16)
                const steer = groupChords(true, [20, 21])
                if (gas.length) add(qsTr("油门"), gas)
                if (brake.length) add(qsTr("刹车"), brake)
                if (steer.length) add(qsTr("转向"), steer)
            } else {
                const move = groupChords(false, [18, 19, 20, 21])
                if (move.length) add(qsTr("移动"), move)
            }
            if (input.driveKeyLabel && input.driveKeyLabel !== "—")
                add(qsTr("切回%1").arg(other), [input.driveKeyLabel])
        } else if (reason === "released") {
            add(qsTr("已释放键鼠"))
            add(qsTr("点击画面开始键鼠控制"))
            add(qsTr("手柄面板"), ["Ctrl+Alt+F1"])
        } else {
            add(qsTr("点击画面开始键鼠控制"))
            if (input.quickKeyLabel && input.quickKeyLabel !== "—")
                add(qsTr("键位速查"), [input.quickKeyLabel])
        }
        hintFade.stop()
        hint.opacity = 1
        hintParts = parts
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
        else { hintParts = []; referenceOpen = false }
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
        function onAimStatusChanged() {
            if (overlay.input.aimTesting) overlay.referenceOpen = false
        }
        function onQuickReferenceScrolled(delta) {
            if (!reference.visible) return
            rowsView.contentY = Math.max(0, Math.min(Math.max(0, rowsView.contentHeight - rowsView.height),
                rowsView.contentY - delta * 0.5))
        }
    }
    Timer { id: hintTimer; interval: 4700; onTriggered: if (overlay.input?.aimTesting !== true) hintFade.start() }
    NumberAnimation {
        id: hintFade
        target: hint
        property: "opacity"
        to: 0
        duration: Theme.d(300)
        onFinished: overlay.hintParts = []
    }
    Timer { id: referenceTimer; interval: 15000; onTriggered: overlay.referenceOpen = false }

    FontMetrics {
        id: hintMetrics
        font.family: Theme.fontUi
        font.pixelSize: Math.round(Theme.fsSmall * overlay.uiScale)
    }
    Rectangle {
        id: hint
        objectName: "ps5-input-hint"
        readonly property bool videoCover: visible
        readonly property real coverRadius: radius
        readonly property bool calibrating: overlay.input?.aimTesting === true
        visible: overlay.active && !overlay.suppressed && !overlay.referenceOpen &&
            (overlay.hintParts.length > 0 || calibrating)
        anchors.top: parent.top
        anchors.topMargin: 55
        anchors.horizontalCenter: parent.horizontalCenter
        width: Math.max(1, Math.min(parent.width - 24, calibrating ? aimLabel.implicitWidth + 24
            : hintMetrics.advanceWidth(overlay.hintText) + 18 + 18 * overlay.hintParts.reduce((sum, part) => sum + part.keys.length, 0)))
        height: (calibrating ? aimLabel.implicitHeight : hintFlow.implicitHeight) + 12
        radius: 8
        color: Theme.dialog
        border.color: Theme.stroke2
        TapHandler { enabled: !overlay.locked && !hint.calibrating; onTapped: overlay.editRequested() }
        Text {
            id: aimLabel
            anchors.centerIn: parent
            width: Math.max(0, hint.width - 24)
            visible: hint.calibrating
            text: overlay.input?.aimStatus || ""
            wrapMode: Text.Wrap
            horizontalAlignment: Text.AlignHCenter
            color: Theme.t1
            font.family: hintMetrics.font.family
            font.pixelSize: hintMetrics.font.pixelSize
        }
        Flow {
            id: hintFlow
            anchors.centerIn: parent
            width: Math.max(0, hint.width - 18)
            visible: !hint.calibrating
            spacing: 6
            Repeater {
                model: overlay.hintParts
                delegate: Flow {
                    id: part
                    required property var modelData
                    width: Math.min(hintFlow.width, hintMetrics.advanceWidth(modelData.label) +
                        modelData.keys.reduce((sum, chord) =>
                            sum + Math.max(18, hintMetrics.advanceWidth(chord) + 8) + 3, 0))
                    spacing: 3
                    Repeater {
                        model: part.modelData.keys
                        delegate: KeyCap {
                            required property string modelData
                            size: "s"
                            text: modelData
                        }
                    }
                    Text {
                        width: Math.min(implicitWidth, part.width)
                        text: part.modelData.label
                        color: Theme.t1
                        font.family: hintMetrics.font.family
                        font.pixelSize: hintMetrics.font.pixelSize
                        wrapMode: Text.Wrap
                        topPadding: part.modelData.keys.length ? 1 : 0
                    }
                }
            }
        }
    }

    Rectangle {
        id: reference
        objectName: "console-keymap-reference"
        readonly property bool videoCover: visible
        readonly property real coverRadius: radius
        visible: overlay.active && !overlay.suppressed && overlay.referenceOpen
        width: Math.max(1, Math.min(parent.width * 0.5 - 18, 336 * overlay.uiScale))
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
            spacing: 6
            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: qsTr("键位速查")
                    color: Theme.t1
                    wrapMode: Text.Wrap
                    font.family: Theme.fontUi
                    font.bold: true
                    font.pixelSize: Math.round(Theme.fsBody * overlay.uiScale)
                }
                ControllerPanelButton {
                    objectName: "controller-panel-reference-edit"
                    text: qsTr("编辑")
                    ghost: true
                    maxTextWidth: 88
                    enabled: !overlay.locked
                    onClicked: {
                        overlay.editRequested()
                        overlay.referenceOpen = false
                    }
                }
            }
            Text {
                Layout.fillWidth: true
                text: qsTr("当前：%1").arg(overlay.input?.driving ? qsTr("驾驶") : qsTr("步行"))
                color: Theme.t2
                wrapMode: Text.Wrap
                font.family: Theme.fontUi
                font.pixelSize: Math.round(Theme.fsSmall * overlay.uiScale)
            }
            RowLayout {
                Layout.fillWidth: true
                Repeater {
                    model: [qsTr("步行"), qsTr("驾驶")]
                    delegate: ControllerPanelButton {
                        required property int index
                        required property string modelData
                        text: modelData
                        primary: overlay.showDriving === (index === 1)
                        maxTextWidth: 66
                        fontSize: Theme.fsSmall
                        onClicked: {
                            overlay.showDriving = index === 1
                            rowsView.contentY = 0
                            referenceTimer.restart()
                        }
                    }
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
                    width: Math.max(0, rowsView.width - 8)
                    spacing: 6
                    Repeater {
                        model: labels.groups(overlay.showDriving)
                        delegate: ColumnLayout {
                            id: group
                            required property int index
                            required property var modelData
                            Layout.fillWidth: true
                            spacing: 2
                            readonly property var actions: modelData.actions.filter(action =>
                                overlay.chords(overlay.showDriving, action).length)
                            readonly property bool mouseLook: index === 1 && overlay.input?.tuning?.mouseLook !== false
                            visible: actions.length > 0 || mouseLook
                            Text {
                                Layout.fillWidth: true
                                text: group.modelData.title
                                color: Theme.accent
                                font.family: Theme.fontUi
                                font.bold: true
                                font.pixelSize: Math.round(Theme.fsSmall * overlay.uiScale)
                            }
                            Text {
                                Layout.fillWidth: true
                                visible: group.mouseLook
                                text: qsTr("鼠标 → 右摇杆")
                                wrapMode: Text.Wrap
                                color: Theme.t2
                                font.family: Theme.fontUi
                                font.pixelSize: Math.round(Theme.fsSmall * overlay.uiScale)
                            }
                            Repeater {
                                model: group.actions
                                delegate: BindingRow {
                                    required property int modelData
                                    Layout.fillWidth: true
                                    compact: true
                                    action: modelData
                                    xbox: overlay.xbox
                                    uiScale: overlay.uiScale
                                    label: labels.names[action]
                                    note: labels.actionNote(overlay.input, overlay.showDriving, action)
                                    primary: labels.binding(overlay.input, overlay.showDriving, action)
                                    alternate: labels.binding(overlay.input, overlay.showDriving, action, 1)
                                    toggleBinding: labels.toggles(overlay.input, action, overlay.showDriving)
                                }
                            }
                            Text {
                                Layout.fillWidth: true
                                readonly property var ads: overlay.chords(overlay.showDriving,
                                    Number(overlay.input?.tuning?.adsAction ?? 16))
                                visible: group.mouseLook && ads.length > 0
                                text: qsTr("%1 开镜 · %2×").arg(ads.join(" / ")).arg(Number(overlay.input?.tuning?.adsScale ?? 1).toFixed(2))
                                color: Theme.t2
                                wrapMode: Text.Wrap
                                font.family: Theme.fontUi
                                font.pixelSize: Math.round(Theme.fsSmall * overlay.uiScale)
                            }
                        }
                    }
                }
            }
            Repeater {
                model: [
                    { key: overlay.input?.driveKeyLabel, label: qsTr("切换步行／驾驶") },
                    { key: overlay.input?.unlockLabel, label: qsTr("释放键鼠") },
                    { key: overlay.input?.quickKeyLabel, label: qsTr("关闭本面板") }
                ].filter(item => item.key && item.key !== "—")
                delegate: RowLayout {
                    id: help
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 5
                    KeyCap {
                        text: help.modelData.key
                        size: "s"
                        Layout.maximumWidth: Math.max(24, reference.width * 0.57)
                    }
                    Text {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        text: help.modelData.label
                        color: Theme.t2
                        wrapMode: Text.Wrap
                        font.family: Theme.fontUi
                        font.pixelSize: Math.round(Theme.fsSmall * overlay.uiScale)
                    }
                }
            }
        }
    }
}
