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

Item {
    id: root
    signal requestPage(string page)

    property string section: "look"

    readonly property var sections: [
        { id: "look",  label: "通用与外观" },
        { id: "play",  label: "播放" },
        { id: "ps5",   label: "PS5 串流" },
        { id: "keys",  label: "快捷键" },
        { id: "comp",  label: "组件与许可" },
        { id: "about", label: "关于" }
    ]

    // --- nav ---------------------------------------------------------------
    Rectangle {
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
                    Behavior on color { ColorAnimation { duration: 200 } }
                    Text {
                        anchors.left: parent.left
                        anchors.leftMargin: 10
                        anchors.verticalCenter: parent.verticalCenter
                        text: modelData.label
                        color: root.section === modelData.id ? Theme.t1 : Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: 13
                        font.weight: Font.Medium
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
            ScrollBar.vertical: ScrollBar { }

            ColumnLayout {
                id: body
                x: 26
                y: 22
                width: parent.width - 52
                spacing: 12

                // --- 通用与外观 ------------------------------------------
                ColumnLayout {
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
                                options: [{ id: "auto", label: "自动隐藏" }, { id: "always", label: "始终显示" }]
                                current: "auto"
                                onPicked: id => {}
                            }
                        }
                        VRow {
                            label: "减少动画"
                            hint: "关闭弹性与转场"
                            VSwitch {
                                checked: veyra.reducedMotion
                                onToggled: veyra.reducedMotion = checked
                            }
                        }
                        VRow {
                            label: "打开时的默认页面"
                            hint: "首页 = 选择片源的页面（点顶部 Logo 也能回到这里）"
                            VSelect {
                                value: veyra.defaultPageLabel
                                options: [
                                    { id: "home", label: "首页" },
                                    { id: "min", label: "极简模式" },
                                    { id: "pro", label: "专业模式" }
                                ]
                                onPicked: id => veyra.defaultPage = id
                            }
                        }
                    }
                }

                // --- 播放 ------------------------------------------------
                ColumnLayout {
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
                            label: "音频偏移"
                            hint: "正值延后音频"
                            value: veyra.audioOffsetMs + " ms"
                            VSlider {
                                implicitWidth: 150
                                center: true
                                from: -2000; to: 2000; value: veyra.audioOffsetMs
                                onMoved: veyra.audioOffsetMs = Math.round(value)
                            }
                        }
                        VRow {
                            label: "音量"
                            value: Math.round(veyra.volume * 100) + "%"
                            VSlider {
                                implicitWidth: 150
                                from: 0; to: 1; value: veyra.volume
                                onMoved: veyra.volume = value
                            }
                        }
                    }
                    Rectangle {
                        Layout.fillWidth: true
                        implicitHeight: subNote.implicitHeight + 20
                        radius: 9
                        color: Qt.rgba(0.961, 0.784, 0.294, 0.06)
                        Text {
                            id: subNote
                            anchors.fill: parent
                            anchors.margins: 10
                            text: "字幕默认字号、描边与截图保存位置尚未接入设置；引擎侧没有对应字段，这里不放假控件。"
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // --- PS5 串流 --------------------------------------------
                ColumnLayout {
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
                            label: "串流状态"
                            hint: veyra.remotePlayState.length > 0 ? veyra.remotePlayState : "未连接"
                            VButton { text: "管理"; onClicked: veyra.openPs5Dialog() }
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
                            text: "串流编码、请求码率与 PSN 账号管理尚未接入设置页；这些参数在 PS5 对话框里调整。"
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: Theme.fsSmall
                            wrapMode: Text.WordWrap
                        }
                    }
                }

                // --- 快捷键 ----------------------------------------------
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 12
                    visible: root.section === "keys"
                    VH1 { text: "快捷键" }
                    Text {
                        text: "当前生效的快捷键；重新绑定尚未实现。"
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: Theme.fsBody
                    }
                    VGroup {
                        Repeater {
                            model: [
                                { a: "播放 / 暂停", k: "Space" },
                                { a: "后退 / 前进 10 秒", k: "← / →" },
                                { a: "截图", k: "Ctrl + S" }
                            ]
                            delegate: VRow {
                                required property var modelData
                                label: modelData.a
                                VTag { text: modelData.k }
                            }
                        }
                    }
                }

                // --- 组件与许可 ------------------------------------------
                ColumnLayout {
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
                            hint: "GitHub · Likely7/Veyra-NRVideo"
                            VButton { text: "打开"; onClicked: veyra.openProjectPage() }
                        }
                        VRow {
                            label: "诊断信息"
                            hint: "复制后附在反馈里"
                            VButton { text: "复制"; onClicked: veyra.copyDiagnostics() }
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
}
