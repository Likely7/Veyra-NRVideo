// 设置, rebuilt from the design (pages-b.js PAGES.set + pages.css .set).
//
// Design: a 220px nav column with six sections (通用与外观 / 播放 / PS5 串流 /
// 快捷键 / 组件与许可 / 关于) and a large body. The design deliberately has NO
// capture-card section here: capture settings live in the capture dialog.
//
// Rows that would need engine support this build does not have say so, rather than
// being drawn as switches that would do nothing.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

VPage {
    id: root
    signal requestPage(string page)

    property string section: "look"
    // The shortcut row waiting for a new key ("" = none).
    property string capturingKey: ""
    onSectionChanged: {
        capturingKey = ""
        if (section === "ps5") veyra.ps5Load()
        // M32: the section's groups redraw one after another (pages-b.js
        // render(true): 480 ms, i*40 ms, y 12).
        Qt.callLater(() => stagger(sectionColumns[section] || null))
    }
    readonly property var sectionColumns: ({ look: secLook, play: secPlay, ps5: secPs5, keys: secKeys, comp: secComp, about: secAbout })
    Component { id: staggerComp; VRise.Stagger {} }
    function stagger(col) {
        if (Theme.reduced || !col) return
        let i = 0
        for (let k = 0; k < col.children.length; ++k) {
            const c = col.children[k]
            if (!c.visible || c.motionDy === undefined) continue
            staggerComp.createObject(root, { target: c, delay: i * 40, span: 480, dy: 12 }).start()
            ++i
        }
    }

    readonly property var sections: [
        { id: "look",  label: "通用与外观", icon: "home" },
        { id: "play",  label: "播放", icon: "play" },
        { id: "ps5",   label: "PS5 串流", icon: "gamepad" },
        { id: "keys",  label: "快捷键", icon: "zap" },
        { id: "comp",  label: "组件与许可", icon: "box" },
        { id: "about", label: "关于", icon: "info" }
    ]

    // --- nav ---------------------------------------------------------------
    Rectangle {
        id: navCard
        anchors.left: parent.left
        anchors.leftMargin: 14
        anchors.top: parent.top
        anchors.topMargin: 14
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        width: 220
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke

        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 10
            spacing: 2
            VH2 { text: "设置"; Layout.margins: 8 }
            Item { implicitHeight: 6 }
            Repeater {
                model: root.sections
                delegate: Rectangle {
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 36
                    radius: 9
                    color: root.section === modelData.id ? Theme.card3
                         : navHover.hovered ? Qt.rgba(1, 1, 1, 0.04) : "transparent"
                    Behavior on color { ColorAnimation { duration: Theme.d(200) } }
                    // .setnav button: icon, 10px, label.
                    Row {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: 10
                        VIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            name: modelData.icon
                            size: 15
                            color: navLabel.color
                        }
                        Text {
                            id: navLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            color: root.section === modelData.id ? Theme.t1 : Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: 13
                            font.weight: Font.Medium
                        }
                    }
                    HoverHandler { id: navHover; cursorShape: Qt.PointingHandCursor }
                    TapHandler { onTapped: root.section = modelData.id }
                }
            }
            Item { Layout.fillHeight: true }
        }
    }

    // --- body --------------------------------------------------------------
    Rectangle {
        id: bodyCard
        anchors.left: parent.left
        anchors.leftMargin: 14 + 220 + 10
        anchors.right: parent.right
        anchors.rightMargin: 14
        anchors.top: parent.top
        anchors.topMargin: 14
        anchors.bottom: parent.bottom
        anchors.bottomMargin: 14
        radius: Theme.rCard
        color: Theme.card
        border.width: 1
        border.color: Theme.stroke
        clip: true

        Flickable {
            anchors.fill: parent
            contentHeight: body.implicitHeight + 44
            clip: true
            ScrollBar.vertical: VScrollBar { }

            ColumnLayout {
                id: body
                x: 26
                y: 22
                // .setbody section { max-width: 720px }
                width: Math.min(parent.width - 52, 720)
                spacing: 12

                // --- 通用与外观 ------------------------------------------
                ColumnLayout {
                    id: secLook
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "look"
                    VH1 { text: "通用与外观" }
                    Text {
                        text: "界面随时可调，不影响播放和增强设置。"
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    VGroup {
                        VRow {
                            label: "页面切换栏"
                            hint: "平时隐藏；鼠标碰到窗口顶部时弹下来"
                            VSeg {
                                objectName: "set-dock"
                                options: [{ id: "auto", label: "自动隐藏" }, { id: "always", label: "始终显示" }]
                                current: veyra.preferences.dockPinned === true ? "always" : "auto"
                                onPicked: id => veyra.setPreference("dockPinned", id === "always")
                            }
                        }
                        VRow {
                            label: "强调色"
                            hint: "开关、进度、选中状态"
                            // .swatch: two colour dots, the chosen one ringed and 1.12x.
                            Row {
                                objectName: "set-accent"
                                spacing: 10
                                Repeater {
                                    model: [{ id: "orange", c: "#FF8A3D", tip: "橙色" }, { id: "white", c: "#F3F3F5", tip: "白色光晕" }]
                                    delegate: Rectangle {
                                        required property var modelData
                                        readonly property bool on: (veyra.preferences.accent || "orange") === modelData.id
                                        objectName: "set-accent-" + modelData.id
                                        width: 24; height: 24; radius: 12
                                        color: modelData.c
                                        border.width: 2
                                        border.color: on ? "#FFFFFF" : "transparent"
                                        scale: on ? 1.12 : 1
                                        Behavior on scale { NumberAnimation { duration: Theme.d(400); easing.bezierCurve: Theme.spring } }
                                        HoverHandler { id: swHover; cursorShape: Qt.PointingHandCursor }
                                        TapHandler { onTapped: veyra.setPreference("accent", modelData.id) }
                                        ToolTip.visible: swHover.hovered
                                        ToolTip.text: modelData.tip
                                    }
                                }
                            }
                        }
                        VRow {
                            label: "减少动画"
                            hint: "关闭弹性与转场"
                            VSwitch {
                                checked: veyra.reducedMotion
                                onToggled: checked => veyra.reducedMotion = checked
                            }
                        }
                        VRow {
                            label: "滑块键盘微调"
                            hint: "点一下滑块后用 ← → 调整，每次的幅度；Esc 或单击画面交还方向键给快进快退"
                            VSeg {
                                objectName: "set-slider-step"
                                options: [{ id: "1", label: "1" }, { id: "0.1", label: "0.1" }, { id: "0.01", label: "0.01" }]
                                current: String(veyra.preferences.sliderKeyStep !== undefined ? veyra.preferences.sliderKeyStep : 0.1)
                                onPicked: id => veyra.setPreference("sliderKeyStep", Number(id))
                            }
                        }
                        VRow {
                            label: "背景渐变强度"
                            hint: "深黑底上的极轻渐变与抖动（防色带）"
                            VSeg {
                                objectName: "set-backdrop"
                                options: [{ id: "0", label: "无" }, { id: "1", label: "轻" }, { id: "2", label: "中" }]
                                current: String(veyra.preferences.backdrop !== undefined ? veyra.preferences.backdrop : 1)
                                onPicked: id => veyra.setPreference("backdrop", Number(id))
                            }
                        }
                        VRow {
                            label: "界面缩放"
                            hint: (veyra.preferences.uiScale || 0) !== veyra.uiScaleActive ? "重启后生效" : "自动 = 跟随 Windows 显示缩放"
                            VSeg {
                                objectName: "set-scale"
                                options: [{ id: "0", label: "自动" }, { id: "100", label: "100%" }, { id: "125", label: "125%" }, { id: "150", label: "150%" }]
                                current: String(veyra.preferences.uiScale || 0)
                                onPicked: id => veyra.setPreference("uiScale", Number(id))
                            }
                        }
                        VRow {
                            label: "启动窗口大小"
                            hint: "下次启动生效；超过屏幕时按屏幕缩小并居中"
                            VSelect {
                                objectName: "set-window-size"
                                readonly property var sizes: [
                                    { id: "1280x800", label: "1280 × 800" },
                                    { id: "1600x1000", label: "1600 × 1000" },
                                    { id: "1920x1200", label: "1920 × 1200" },
                                    { id: "last", label: "记住上次大小" }
                                ]
                                options: sizes
                                value: (sizes.find(o => o.id === (veyra.preferences.windowSize || "1280x800")) || sizes[0]).label
                                onPicked: id => veyra.setPreference("windowSize", id)
                            }
                        }
                        VRow {
                            label: "GPU 占用监控"
                            hint: "专业页「GPU 占用」只统计这一块显卡" + (veyra.gpuMonitorName ? "；当前：" + veyra.gpuMonitorName : "")
                            VSelect {
                                objectName: "set-monitor-gpu"
                                implicitWidth: 230
                                options: veyra.gpuMonitorChoices
                                value: (options.find(o => o.id === (veyra.preferences.monitorGpu || "")) || options[0] || { label: "—" }).label
                                onPicked: id => veyra.setPreference("monitorGpu", id)
                            }
                        }
                    }
                    VGroup {
                        VRow {
                            label: "打开时的默认页面"
                            hint: "首页 = 选择片源的页面（点顶部 Logo 也能回到这里）"
                            VSeg {
                                objectName: "set-default-page"
                                options: [
                                    { id: "home", label: "首页" },
                                    { id: "min", label: "极简模式" },
                                    { id: "pro", label: "专业模式" },
                                    { id: "last", label: "上次" }
                                ]
                                current: veyra.defaultPage
                                onPicked: id => veyra.defaultPage = id
                            }
                        }
                        VRow {
                            label: "界面语言"
                            hint: "2.0 只提供简体中文界面"
                            VSelect {
                                objectName: "set-language"
                                implicitWidth: 140
                                value: "简体中文"
                                options: [{ id: "zh-CN", label: "简体中文" }]
                            }
                        }
                    }
                }

                // --- 播放 ------------------------------------------------
                ColumnLayout {
                    id: secPlay
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "play"
                    VH1 { text: "播放" }
                    Text {
                        text: "文件播放与字幕的默认行为。"
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    VGroup {
                        VRow {
                            label: "解码"
                            hint: (veyra.preferences.decode || "auto") === "hardware"
                                  ? "强制硬解 = 只用硬件解码，打不开时报错不回退；下次打开文件生效"
                                  : "自动 = 优先硬解，失败回退软解；下次打开文件生效"
                            VSeg {
                                objectName: "set-decode"
                                options: [{ id: "auto", label: "自动" }, { id: "hardware", label: "强制硬解" }, { id: "software", label: "软解" }]
                                current: veyra.preferences.decode || "auto"
                                onPicked: id => veyra.setPreference("decode", id)
                            }
                        }
                        VRow {
                            label: "HDR 输出格式"
                            hint: "HDR10 默认；补帧固定 HDR10，需要 Windows HDR 开启"
                            VSeg { options: [{id:"0",label:"HDR10"},{id:"1",label:"scRGB 浮点"}]; current: String(veyra.hdrOutputMode); onPicked: id => veyra.hdrOutputMode = Number(id) }
                        }
                        VRow {
                            label: "记住播放位置"
                            hint: "重新打开同一个文件时从上次位置继续"
                            VSwitch {
                                objectName: "set-resume"
                                checked: veyra.preferences.rememberPosition !== false
                                onToggled: checked => veyra.setPreference("rememberPosition", checked)
                            }
                        }
                        VRow {
                            label: "字幕默认字号"
                            hint: "双行不自动缩小；更多样式在字幕设置里"
                            VSeg {
                                objectName: "set-subsize"
                                readonly property int size: veyra.preferences.subtitleSize || 22
                                options: [{ id: "18", label: "小" }, { id: "22", label: "中" }, { id: "28", label: "大" }]
                                current: size <= 19 ? "18" : size >= 26 ? "28" : "22"
                                onPicked: id => veyra.setPreference("subtitleSize", Number(id))
                            }
                        }
                        VRow {
                            label: "截图保存位置"
                            hint: veyra.screenshotDirectory + "（SDR 存 PNG，HDR 存 JPEG XR）"
                            RowLayout {
                                spacing: 6
                                VButton { text: "打开"; ghost: true; onClicked: veyra.openScreenshotDirectory() }
                                VButton { objectName: "set-shotdir"; text: "更改…"; onClicked: veyra.chooseScreenshotDirectory() }
                            }
                        }
                    }
                    // 补帧说明 (moved here from the 补帧 page, user decision 2026-09-29).
                    VGroup {
                        objectName: "set-smooth-motion"
                        VRow {
                            label: "Smooth Motion 开启方法"
                            hint: "NVIDIA 驱动级 AI 插帧，与软件内补帧二选一"
                            VButton {
                                text: smoothHelp.visible ? "收起" : "查看"
                                ghost: true
                                onClicked: smoothHelp.visible = !smoothHelp.visible
                            }
                        }
                        Text {
                            id: smoothHelp
                            visible: false
                            Layout.fillWidth: true
                            Layout.leftMargin: 12; Layout.rightMargin: 12; Layout.bottomMargin: 10
                            text: "只用驱动补帧时：在专业模式的补帧页关闭补帧，再到 NVIDIA App 打开 Smooth Motion（AI 插帧）。
"
                                  + "只用 DLSS / XeSS 补帧时：到 NVIDIA App 关闭 Smooth Motion。
"
                                  + "软件不检测、不拦截两者叠加，也不修改驱动配置；AMD FSR 补帧暂不提供。"
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                            wrapMode: Text.WordWrap
                            lineHeight: 1.3
                        }
                    }
                    VGroup {
                        VRow {
                            label: "声音同步"
                            hint: veyra.audioSyncMode === 0 ? (veyra.audioSyncLive ? "软件估算补偿 " + veyra.audioCompensationMs.toFixed(0) + " ms" : "自动估算 · 采集卡 / PS5 实时输入时生效")
                                  : veyra.audioSyncMode === 1 ? "手动偏移 · 实时输入时生效" : "关闭补偿"
                            VSeg {
                                options: [{ id: "0", label: "自动估算" }, { id: "1", label: "手动" }, { id: "2", label: "关闭" }]
                                current: String(veyra.audioSyncMode)
                                onPicked: id => veyra.audioSyncMode = Number(id)
                            }
                        }
                        VRow {
                            label: "音频偏移"
                            hint: "正值延后音频"
                            value: veyra.audioOffsetMs + " ms"
                            VSlider {
                                implicitWidth: 150
                                center: true
                                from: -250; to: 250; value: veyra.audioOffsetMs; enabledControl: veyra.audioSyncMode === 1
                                onMoved: veyra.audioOffsetMs = Math.round(value)
                            }
                        }
                        VRow {
                            label: "音量"
                            value: Math.round(veyra.volume * 100) + "%"
                            VSlider {
                                objectName: "settings-volume"
                                implicitWidth: 150
                                from: 0; to: 1; value: veyra.volume; inputScale: 100
                                onMoved: veyra.volume = value
                            }
                        }
                    }
                    VGroup {
                        VRow {
                            label: "输出设备与下混"
                            hint: veyra.audioOutputStatus
                            VButton { text: "音频设置…"; onClicked: veyra.openAudioDialog() }
                        }
                    }
                }

                // --- PS5 串流 --------------------------------------------
                ColumnLayout {
                    id: secPs5
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "ps5"
                    VH1 { text: "PS5 串流" }
                    Text {
                        text: "主机与 PSN 凭据加密保存在用户数据目录。"
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    VGroup {
                        VRow {
                            label: "已保存主机"
                            hint: (veyra.ps5.host || "").length > 0 ? veyra.ps5.host + (veyra.remotePlayState.length > 0 ? " · " + veyra.remotePlayState : "") : "还没有配对的主机"
                            VButton { text: "管理"; onClicked: veyra.openPs5Dialog() }
                        }
                        VRow {
                            label: "画质"
                            VSelect {
                                objectName: "set-ps5-quality"
                                implicitWidth: 170
                                readonly property var qualities: [{ id: "0", label: "720p · 30 fps" }, { id: "1", label: "720p · 60 fps" }, { id: "2", label: "1080p · 30 fps" }, { id: "3", label: "1080p · 60 fps" }]
                                options: qualities
                                value: qualities[veyra.ps5.quality || 0].label
                                onPicked: id => veyra.ps5Set("quality", Number(id))
                            }
                        }
                        VRow {
                            label: "编码"
                            hint: "HDR 需要 PS5 实际输出 HDR 且 Windows HDR 开启"
                            VSeg {
                                objectName: "set-ps5-codec"
                                options: [{ id: "0", label: "H.264" }, { id: "1", label: "H.265" }, { id: "2", label: "H.265 HDR" }]
                                current: String(veyra.ps5.codec || 0)
                                onPicked: id => veyra.ps5Set("codec", Number(id))
                            }
                        }
                        VRow {
                            label: "请求码率"
                            hint: "发给 PS5 的带宽请求，主机不保证达到"
                            VSelect {
                                objectName: "set-ps5-bitrate"
                                implicitWidth: 130
                                readonly property var bitrates: [5, 10, 15, 20, 30, 50, 80, 100].map((m, i) => ({ id: String(i), label: m + " Mbps" }))
                                options: bitrates
                                value: bitrates[veyra.ps5.bitrate || 0].label
                                onPicked: id => veyra.ps5Set("bitrate", Number(id))
                            }
                        }
                        VRow {
                            label: "PSN 账号"
                            hint: veyra.ps5.psnReady ? "已登录" : "未登录 · 在 PS5 串流窗口里登录"
                            VButton {
                                objectName: "set-ps5-psn-logout"
                                text: veyra.ps5.psnReady ? "退出" : "去登录"
                                ghost: true
                                enabled: !veyra.ps5.busy
                                onClicked: veyra.ps5.psnReady ? veyra.ps5PsnForget() : veyra.openPs5Dialog()
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: psNote.implicitHeight + 20
                        radius: 9
                        color: Qt.rgba(0.961, 0.784, 0.294, 0.06)
                        Text {
                            id: psNote
                            anchors.fill: parent
                            anchors.margins: 10
                            text: "这里和 PS5 串流窗口是同一份设置，下次连接时一起保存到该主机的配对档案。"
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // --- 快捷键 ----------------------------------------------
                ColumnLayout {
                    id: secKeys
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "keys"
                    VH1 { text: "快捷键" }
                    Text {
                        text: "点击右侧按键，再按下新的组合键即可重新绑定；Esc 取消。"
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    VGroup {
                        Repeater {
                            model: [
                                { id: "playPause", a: "播放 / 暂停" },
                                { id: "fullscreen", a: "全屏", note: "F11 / Alt+Enter 也可以" },
                                { id: "lock", a: "锁定全屏控制条" },
                                { id: "hold", a: "按住查看原画" },
                                { id: "screenshot", a: "截图" },
                                { id: "toggleMode", a: "切换极简 / 专业" }
                            ]
                            delegate: VRow {
                                id: keyRow
                                required property var modelData
                                label: modelData.a
                                hint: modelData.note || ""
                                Rectangle {
                                    objectName: "key-" + keyRow.modelData.id
                                    readonly property bool capturing: root.capturingKey === keyRow.modelData.id
                                    implicitWidth: Math.max(64, keyText.implicitWidth + 20)
                                    implicitHeight: 26
                                    radius: 7
                                    color: capturing ? Theme.accentSoft : Qt.rgba(1, 1, 1, 0.06)
                                    border.width: 1
                                    border.color: capturing ? Theme.accent : Theme.stroke
                                    Text {
                                        id: keyText
                                        anchors.centerIn: parent
                                        text: parent.capturing ? "按下新按键…" : veyra.shortcuts[keyRow.modelData.id]
                                        color: Theme.t1
                                        font.family: Theme.fontMono
                                        font.pixelSize: Theme.fsSmall
                                    }
                                    HoverHandler { cursorShape: Qt.PointingHandCursor }
                                    TapHandler { onTapped: { root.capturingKey = keyRow.modelData.id; keyCatcher.forceActiveFocus() } }
                                }
                            }
                        }
                        VRow {
                            label: "恢复默认"
                            VButton { text: "全部还原"; ghost: true; onClicked: veyra.resetShortcuts() }
                        }
                    }
                    VGroup {
                        Repeater {
                            model: [
                                { a: "后退 / 前进 5 秒", k: "← / →" },
                                { a: "播放 / 暂停（播放文件时）", k: "单击画面" },
                                { a: "音量", k: "↑ / ↓" },
                                { a: "打开文件 / 导出页", k: "Ctrl+O / Ctrl+E" },
                                { a: "字幕开关 / 主轨 / 副轨", k: "B / T / Y" },
                                { a: "字幕延时 50 ms（Shift = 1 s）", k: "Z / X" }
                            ]
                            delegate: VRow {
                                required property var modelData
                                label: modelData.a
                                hint: "固定"
                                VTag { text: modelData.k }
                            }
                        }
                    }
                    // Captures the next key combination for the row being rebound.
                    Item {
                        id: keyCatcher
                        focus: false
                        Keys.onPressed: event => {
                            if (root.capturingKey === "") return
                            event.accepted = true
                            if (event.key === Qt.Key_Escape) { root.capturingKey = ""; return }
                            const modifierOnly = [Qt.Key_Control, Qt.Key_Shift, Qt.Key_Alt, Qt.Key_Meta].indexOf(event.key) >= 0
                            if (modifierOnly) return
                            const parts = []
                            if (event.modifiers & Qt.ControlModifier) parts.push("Ctrl")
                            if (event.modifiers & Qt.AltModifier) parts.push("Alt")
                            if (event.modifiers & Qt.ShiftModifier) parts.push("Shift")
                            const names = { [Qt.Key_Space]: "Space", [Qt.Key_Tab]: "Tab", [Qt.Key_Return]: "Return", [Qt.Key_Enter]: "Enter",
                                            [Qt.Key_Backspace]: "Backspace", [Qt.Key_Delete]: "Del", [Qt.Key_Home]: "Home", [Qt.Key_End]: "End",
                                            [Qt.Key_PageUp]: "PgUp", [Qt.Key_PageDown]: "PgDown", [Qt.Key_Insert]: "Ins" }
                            let name = names[event.key]
                            if (!name && event.key >= Qt.Key_F1 && event.key <= Qt.Key_F24) name = "F" + (event.key - Qt.Key_F1 + 1)
                            if (!name && event.text && event.text.trim().length === 1) name = event.text.toUpperCase()
                            if (!name && event.key >= 0x20 && event.key < 0x7F) name = String.fromCharCode(event.key)
                            if (!name) return
                            parts.push(name)
                            if (veyra.setShortcut(root.capturingKey, parts.join("+"))) root.capturingKey = ""
                        }
                    }
                }

                // --- 组件与许可 ------------------------------------------
                ColumnLayout {
                    id: secComp
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "comp"
                    VH1 { text: "组件与许可" }
                    Text {
                        Layout.fillWidth: true
                        text: "运行组件均为实验运行时，非 NVIDIA 官方合作或认证。详细哈希见 release-runtime-manifest.json。"
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                        wrapMode: Text.WordWrap
                    }
                    VGroup {
                        // Reported by the engine's component state rather than a
                        // hand-written list that could drift from the package.
                        Repeater {
                            model: veyra.componentList
                            delegate: VRow {
                                required property var modelData
                                label: modelData.name
                                hint: modelData.detail
                                VTag {
                                    text: modelData.experimental ? "实验" : (modelData.loaded ? "已加载" : "未加载")
                                    kind: modelData.experimental ? "exp" : (modelData.loaded ? "ok" : "")
                                }
                            }
                        }
                    }
                }

                // --- 关于 ------------------------------------------------
                ColumnLayout {
                    id: secAbout
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "about"
                    RowLayout {
                        spacing: 18
                        Image {
                            source: "logo.png"
                            sourceSize.width: 88
                            fillMode: Image.PreserveAspectFit
                        }
                        ColumnLayout {
                            spacing: 4
                            VH1 { text: "Veyra" }
                            Text {
                                text: "版本 " + veyra.version
                                color: Theme.t2
                                font.family: Theme.fontUi
                                font.pixelSize: Theme.fsBody
                            }
                        }
                    }
                    VGroup {
                        VRow {
                            label: "检查更新"
                            hint: "GitHub · Likely7/Veyra-NRVideo · 发布页"
                            VButton { text: "检查"; onClicked: veyra.openReleasesPage() }
                        }
                        VRow {
                            label: "反馈问题"
                            hint: "打开 GitHub Issues，诊断信息已自动复制"
                            VButton { text: "打开"; onClicked: veyra.openFeedbackPage() }
                        }
                        VRow {
                            label: "交流群与赞助"
                            hint: "项目主页 README 里的二维码"
                            VButton { text: "查看"; onClicked: veyra.openProjectPage() }
                        }
                        VRow {
                            label: "诊断信息"
                            hint: "复制后附在反馈里"
                            VButton { text: "复制"; onClicked: veyra.copyDiagnostics() }
                        }
                        VRow {
                            label: "日志"
                            hint: veyra.logFile
                            VButton { text: "打开目录"; ghost: true; onClicked: veyra.openLogFolder() }
                        }
                        VRow {
                            label: "用户数据"
                            hint: veyra.dataDirectory
                            VButton { text: "打开目录"; ghost: true; onClicked: veyra.openDataFolder() }
                        }
                    }
                    // The diagnostics the engine actually reports, shown verbatim.
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: 220
                        radius: 9
                        color: Theme.card2
                        clip: true
                        Flickable {
                            anchors.fill: parent
                            anchors.margins: 10
                            contentHeight: diagText.implicitHeight
                            clip: true
                            ScrollBar.vertical: VScrollBar { }
                            Text {
                                id: diagText
                                width: parent.width
                                text: veyra.diagnosticsReport()
                                color: Theme.t2
                                font.family: Theme.fontMono
                                font.pixelSize: Theme.fsSmall
                                wrapMode: Text.Wrap
                            }
                        }
                    }
                }

                Item { Layout.preferredHeight: 12 }
            }
        }
    }

    // [data-in] entrance order from pages-b.js PAGES.set.
    VRise { target: navCard; d: 0 }
    VRise { target: bodyCard; d: 1 }
}
