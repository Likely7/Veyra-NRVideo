// The five dialogs, rebuilt from the design's dialogs.js.
//
// Each mirrors the design's structure: a header with a colour-plated icon and a
// subtitle, a scrolling body of sections (a heading with a hairline, then a group
// of label/control rows), and a footer with the actions.
//
// Scope: the dialog edits only what the engine actually carries. Device and screen
// target lists come from the source's own enumeration; force-SDR and flip are real
// settings. The format list, audio monitoring, bitstream passthrough, subtitle
// style and remote-play detail are not wired to anything yet, and each says so on
// the page instead of being drawn as a control that would do nothing.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: host
    anchors.fill: parent
    z: 100

    // Which dialog is showing: "" | capture | ps5 | screen | subtitle | audio
    property string dialog: ""

    function open(key) { host.dialog = key }
    function close() { host.dialog = "" }

    signal startCapture()
    signal startPs5()
    signal startScreen()

    // Scrim: the design dims the page behind the dialog.
    Rectangle {
        anchors.fill: parent
        color: Qt.rgba(0, 0, 0, 0.55)
        visible: host.dialog !== ""
        TapHandler { onTapped: host.close() }
    }

    // --- shared chrome ----------------------------------------------------
    component DLayer: Rectangle {
        id: dlg
        property string title: ""
        property string sub: ""
        property string glyph: ""
        property int dialogWidth: 640
        property var actions: []
        default property alias body: bodyCol.data
        signal actionTriggered(string label)

        anchors.centerIn: parent
        width: dialogWidth
        implicitHeight: Math.min(720, header.height + bodyScroll.contentHeight + footer.height + 40)
        height: implicitHeight
        radius: 16
        color: Theme.dialog
        border.width: 1
        border.color: Theme.stroke2
        visible: host.dialog !== ""
        scale: visible ? 1.0 : 0.9
        Behavior on scale { NumberAnimation { duration: Theme.d(550); easing.bezierCurve: Theme.spring } }

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            RowLayout {
                id: header
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 12
                Rectangle {
                    implicitWidth: 34; implicitHeight: 34; radius: 10
                    color: Qt.rgba(1, 1, 1, 0.06)
                    VIcon { anchors.centerIn: parent; name: dlg.glyph }
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 2
                    Text {
                        text: dlg.title
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsH2
                        font.weight: Font.DemiBold; font.variableAxes: Theme.axesDemiBold
                    }
                    Text {
                        Layout.fillWidth: true
                        visible: dlg.sub.length > 0
                        text: dlg.sub
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 12
                        wrapMode: Text.WordWrap
                    }
                }
                VButton { icon: true; iconName: "x"; ghost: true; onClicked: host.close() }
            }

            Flickable {
                id: bodyScroll
                Layout.fillWidth: true
                Layout.fillHeight: true
                Layout.leftMargin: 20
                Layout.rightMargin: 20
                contentHeight: bodyCol.implicitHeight
                clip: true
                ScrollBar.vertical: ScrollBar { }
                ColumnLayout {
                    id: bodyCol
                    width: parent.width
                    spacing: 0
                }
            }

            RowLayout {
                id: footer
                Layout.fillWidth: true
                Layout.margins: 16
                spacing: 8
                Item { Layout.fillWidth: true }
                Repeater {
                    model: dlg.actions
                    delegate: VButton {
                        required property var modelData
                        text: modelData.label
                        primary: modelData.primary === true
                        onClicked: dlg.actionTriggered(modelData.label)
                    }
                }
            }
        }
    }

    // --- 采集卡 -----------------------------------------------------------
    DLayer {
        visible: host.dialog === "capture"
        glyph: "video"
        title: "采集卡"
        sub: "连接设备后，先关闭增强确认基础画面，再按需开启"
        dialogWidth: 620
        actions: [
            { label: "取消" },
            { label: "连接并开始", primary: true }
        ]
        onActionTriggered: label => {
            host.close()
            if (label === "连接并开始") host.startCapture()
        }

        DSection { text: "设备" }
        DGroup {
            VRow {
                label: "视频输入设备"
                hint: veyra.captureDevices.length === 0 ? "未检测到采集设备" : ""
                VSelect {
                    value: veyra.captureDeviceLabel
                    options: veyra.captureDevices
                    onPicked: id => veyra.captureDeviceId = id
                }
            }
        }
        DSection { text: "格式与画面" }
        DGroup {
            VRow {
                label: "转为 SDR 显示"
                hint: "收到 HDR 也按 SDR 预览，立即生效"
                VSwitch {
                    checked: veyra.captureForceSdr
                    onToggled: veyra.captureForceSdr = checked
                }
            }
            VRow {
                label: "画面上下翻转"
                hint: "采集画面倒置时开启，立即生效"
                VSwitch {
                    checked: veyra.captureFlipVertical
                    onToggled: veyra.captureFlipVertical = checked
                }
            }
        }
        DSection { text: "尚未接入" }
        DNote {
            text: "格式列表（分辨率 · 帧率 · 像素格式）、色彩空间与范围、设备缓冲、"
                + "音频监听设备、Dolby / DTS 位流、限定输入帧率：引擎侧尚未暴露这些设置，"
                + "这里不放假控件。当前按设备默认格式打开。"
        }
    }

    // --- PS5 串流 ---------------------------------------------------------
    DLayer {
        visible: host.dialog === "ps5"
        glyph: "gamepad"
        title: "PS5 串流"
        sub: "局域网 Remote Play · 凭据加密保存在本机"
        dialogWidth: 640
        actions: [
            { label: "取消" },
            { label: "连接", primary: true }
        ]
        onActionTriggered: label => {
            host.close()
            if (label === "连接") host.startPs5()
        }

        DSection { text: "主机" }
        DGroup {
            VRow {
                label: "串流状态"
                hint: veyra.remotePlayState.length > 0 ? veyra.remotePlayState : "未连接"
            }
            VRow {
                label: "主机地址"
                hint: "PS5 设置 → 网络 → 连接状态 → 查看连接状态"
                VTextField {
                    implicitWidth: 180
                    placeholder: "192.168.1.x"
                    onEdited: text => veyra.remotePlayHost = text
                }
            }
        }
        DSection { text: "配对" }
        DGroup {
            VRow {
                label: "8 位配对码"
                hint: "首次配对需要：PS5 远程游玩 → 关联设备"
                VTextField {
                    implicitWidth: 150
                    placeholder: "••••••••"
                    onEdited: text => veyra.remotePlayPin = text
                }
            }
        }
        DNote {
            text: "输入分辨率、编码（H.264 / H.265）、请求码率、解码方式、"
                + "DualSense 转发与陀螺仪校准：尚未接入界面。外网串流暂未实现。"
        }
    }

    // --- 屏幕捕获 ---------------------------------------------------------
    DLayer {
        visible: host.dialog === "screen"
        glyph: "monitor"
        title: "屏幕捕获"
        sub: "把一个窗口或整块显示器作为片源"
        dialogWidth: 700
        actions: [
            { label: "取消" },
            { label: "开始捕获", primary: true }
        ]
        onActionTriggered: label => {
            host.close()
            if (label === "开始捕获") host.startScreen()
        }

        DSection { text: "目标" }
        DGroup {
            VRow {
                label: "捕获目标"
                hint: veyra.screenTargets.length === 0 ? "未找到可捕获的窗口或显示器" : ""
                VSelect {
                    value: veyra.screenTargetLabel
                    options: veyra.screenTargets
                    onPicked: id => veyra.screenTargetId = id
                }
            }
            VRow {
                label: "重新枚举"
                hint: "窗口打开或关闭后刷新列表"
                VButton {
                    text: "刷新"
                    onClicked: veyra.refreshCaptureTargets()
                }
            }
        }
        DNote {
            text: "捕获方式（Windows Graphics Capture / DXGI）、帧率上限、"
                + "鼠标指针、裁剪与“填满窗口”：尚未接入界面；当前按整块目标区域采集。"
        }
    }

    // --- 字幕设置 ---------------------------------------------------------
    DLayer {
        visible: host.dialog === "subtitle"
        glyph: "type"
        title: "字幕设置"
        sub: "实时预览，设置对所有文件生效"
        dialogWidth: 620
        actions: [{ label: "完成", primary: true }]
        onActionTriggered: host.close()

        DNote {
            text: "字幕轨选择、字号、字体、描边、背景条、底部距离与延时尚未接入："
                + "字幕样式与轨道由旧界面的字幕层持有，引擎侧没有对应设置，"
                + "本轮不在这里维护第二份会与渲染结果不一致的状态。"
        }
    }

    // --- 音频设置 ---------------------------------------------------------
    DLayer {
        visible: host.dialog === "audio"
        glyph: "music"
        title: "音频设置"
        dialogWidth: 560
        actions: [{ label: "完成", primary: true }]
        onActionTriggered: host.close()

        DSection { text: "音轨" }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                visible: veyra.audioTracks.length === 0
                text: "当前源没有可选音轨。"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
            }
            Repeater {
                model: veyra.audioTracks
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 52
                    radius: 12
                    color: Theme.card2
                    border.width: 1
                    border.color: veyra.selectedAudioTrack === modelData.index ? Theme.accent : Theme.stroke
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        spacing: 12
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: modelData.label
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                            }
                            Text {
                                text: modelData.channels > 0 ? modelData.channels + " 声道" : ""
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: 11
                            }
                        }
                        VIcon {
                            visible: veyra.selectedAudioTrack === modelData.index
                            name: "check"
                            color: Theme.accent
                        }
                    }
                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: veyra.selectedAudioTrack = modelData.index }
                }
            }
        }
        DSection { text: "输出" }
        DGroup {
            VRow {
                label: "音量"
                value: Math.round(veyra.volume * 100) + "%"
                VSlider {
                    implicitWidth: 170
                    from: 0; to: 1; value: veyra.volume
                    onMoved: veyra.volume = value
                }
            }
            VRow {
                label: "静音"
                VSwitch {
                    checked: veyra.muted
                    onToggled: veyra.muted = checked
                }
            }
        }
        DSection { text: "音画同步" }
        DGroup {
            VRow {
                label: "手动偏移"
                hint: "负值 = 声音提前"
                value: veyra.audioOffsetMs + " ms"
                VSlider {
                    implicitWidth: 170
                    center: true
                    from: -500; to: 500; value: veyra.audioOffsetMs
                    onMoved: veyra.audioOffsetMs = Math.round(value / 10) * 10
                }
            }
        }
        DNote {
            text: "输出设备选择与立体声下混尚未接入：当前跟随系统默认设备。"
        }
    }

    // --- 另存为预设 -------------------------------------------------------
    // The design lists every part that will be saved before asking for a name, so
    // the user can see what they are about to store.
    DLayer {
        id: saveDialog
        visible: host.dialog === "save"
        glyph: "plus"
        title: "另存为预设"
        sub: "预设会保存下面勾选的部分；应用时只覆盖勾选的部分"
        dialogWidth: 620
        property string presetName: ""
        property var chosen: ({})
        actions: [
            { label: "取消" },
            { label: "保存", primary: true }
        ]
        onActionTriggered: label => {
            if (label !== "保存") { host.close(); return }
            if (presetName.length === 0) { saveNote.text = "预设需要一个名字"; return }
            let mask = 0
            for (const part of veyra.presetSaveParts) {
                if (chosen[part.id] === true) mask |= (1 << ["chain","color","fg","audio"].indexOf(part.id))
            }
            if (veyra.savePresetAs(presetName, mask, false)) host.close()
            else saveNote.text = "保存失败：名称可能已存在"
        }

        DSection { text: "将要保存的内容" }
        DGroup {
            Repeater {
                model: veyra.presetSaveParts
                delegate: VRow {
                    required property var modelData
                    label: modelData.label
                    hint: modelData.summary
                    VSwitch {
                        // A part at its default is still offered, but starts off: a
                        // preset that stored nothing would be a surprise.
                        checked: modelData.meaningful
                        onToggled: {
                            const c = Object.assign({}, saveDialog.chosen)
                            c[modelData.id] = checked
                            saveDialog.chosen = c
                        }
                    }
                }
            }
        }
        DSection { text: "名称" }
        VTextField {
            Layout.fillWidth: true
            placeholder: "例如：夜间游戏"
            onEdited: text => saveDialog.presetName = text
        }
        Text {
            id: saveNote
            Layout.fillWidth: true
            Layout.topMargin: 6
            text: ""
            color: Theme.warn
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
            wrapMode: Text.WordWrap
        }
    }

    // --- 管理预设 ---------------------------------------------------------
    DLayer {
        visible: host.dialog === "manage"
        glyph: "settings"
        title: "管理预设"
        sub: "内置预设可以复制，不能改名或删除"
        dialogWidth: 620
        actions: [{ label: "完成", primary: true }]
        onActionTriggered: host.close()

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 6
            Text {
                visible: veyra.presets.length === 0
                text: "还没有保存过预设。"
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: Theme.fsSmall
            }
            Repeater {
                model: veyra.presets
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 46
                    radius: 12
                    color: Theme.card2
                    border.width: 1
                    border.color: veyra.defaultPresetIndex === modelData.index ? Theme.accent : Theme.stroke
                    RowLayout {
                        anchors.fill: parent
                        anchors.leftMargin: 10
                        anchors.rightMargin: 8
                        spacing: 8
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: modelData.name
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                                elide: Text.ElideRight
                            }
                            Text {
                                Layout.fillWidth: true
                                text: modelData.note.length > 0 ? modelData.note
                                      : (modelData.nodeMode ? "节点预设" : "列表预设")
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: 11
                                elide: Text.ElideRight
                            }
                        }
                        VTag { visible: modelData.builtin; text: "内置" }
                        VTag { visible: veyra.defaultPresetIndex === modelData.index; kind: "acc"; text: "启动默认" }
                        VButton {
                            ghost: true
                            text: "复制"
                            onClicked: veyra.duplicatePreset(modelData.index)
                        }
                        VButton {
                            ghost: true
                            text: "设为默认"
                            visible: !modelData.builtin
                            onClicked: veyra.setDefaultPreset(modelData.index)
                        }
                        VButton {
                            ghost: true
                            text: "删除"
                            visible: !modelData.builtin
                            onClicked: veyra.deletePreset(modelData.index)
                        }
                    }
                }
            }
        }
    }

    // --- small building blocks the design's dialogs use --------------------
    component DSection: RowLayout {
        property string text: ""
        Layout.fillWidth: true
        Layout.topMargin: 14
        Layout.bottomMargin: 4
        spacing: 8
        VEyebrow { text: parent.text }
        Rectangle {
            Layout.fillWidth: true
            implicitHeight: 1
            color: Theme.stroke
        }
    }

    component DGroup: VGroup {
        Layout.fillWidth: true
    }

    component DNote: Rectangle {
        property string text: ""
        Layout.fillWidth: true
        Layout.topMargin: 8
        implicitHeight: noteText.implicitHeight + 18
        radius: 9
        color: Qt.rgba(0.961, 0.784, 0.294, 0.06)
        Text {
            id: noteText
            anchors.fill: parent
            anchors.margins: 9
            text: parent.text
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsSmall
            wrapMode: Text.WordWrap
        }
    }

    component VTextField: Rectangle {
        property string placeholder: ""
        property alias text: field.text
        signal edited(string text)
        implicitHeight: 30
        radius: 9
        color: Qt.rgba(1, 1, 1, 0.04)
        border.width: 1
        border.color: field.activeFocus ? Theme.accent : Theme.stroke
        TextInput {
            id: field
            anchors.fill: parent
            anchors.leftMargin: 10
            anchors.rightMargin: 10
            verticalAlignment: TextInput.AlignVCenter
            color: Theme.t1
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            selectByMouse: true
            onEditingFinished: parent.edited(text)
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            anchors.left: parent.left
            anchors.leftMargin: 10
            visible: field.text.length === 0
            text: parent.placeholder
            color: Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
        }
    }
}
