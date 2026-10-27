// subtitle dialog: source/settings logic is owned by this component.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: subtitleDialog
    objectName: "subtitle-dialog"
    key: "subtitle"
    glyph: "type"
    title: qsTr("字幕设置")
    sub: qsTr("实时预览，设置对所有文件生效")
    dialogWidth: 620
    actions: [{ label: qsTr("恢复默认") }, { label: qsTr("完成"), primary: true }]
    onActionTriggered: label => {
        if (label !== qsTr("恢复默认")) { dialogHost.close(); return }
        // The look only: tracks and the timing offset belong to the file.
        const defaults = { subtitleEnabled: true, subtitleSize: 22, subtitleFont: 0, subtitleOutline: 2,
                           subtitleBackground: false, subtitleMargin: 0, subtitleFit: false, subtitleLines: 2 }
        for (const key in defaults) veyra.setPreference(key, defaults[key])
        veyra.logUi("ui-subtitle", "style reset to defaults")
    }

    readonly property var prefs: veyra.preferences
    function pref(key, fallback) { return prefs[key] !== undefined ? prefs[key] : fallback }
    readonly property var trackOptions: [{ id: "-1", label: qsTr("关闭", "off") }]
        .concat(veyra.subtitleTracks.map(t => ({ id: String(t.index), label: t.label + " · " + t.note, disabled: !t.usable })))
    function trackLabel(index) {
        const t = veyra.subtitleTracks.find(x => x.index === index)
        return t ? t.label : qsTr("关闭", "off")
    }

    // dialogs.js .sub-prev: a sample line over the picture, drawn with the
    // current style so every change shows at once.
    Rectangle {
        id: subPreview
        objectName: "subtitle-preview"
        Layout.fillWidth: true
        Layout.topMargin: 4
        implicitHeight: 180
        radius: 10
        border.width: 1
        border.color: Theme.stroke
        clip: true
        gradient: Gradient {
            GradientStop { position: 0; color: "#1B2230" }
            GradientStop { position: 1; color: "#0B0C10" }
        }
        readonly property real scaleFactor: height / 540
        Image {
            anchors.fill: parent
            anchors.margins: 1
            fillMode: Image.PreserveAspectCrop
            asynchronous: true
            visible: status === Image.Ready
            source: subtitleDialog.shown && veyra.hasSource && !veyra.isCapture && veyra.duration > 0
                    ? "image://veyra-thumb/" + veyra.thumbnailGeneration + "/" + Math.floor(veyra.position / 2) * 2000 : ""
        }
        Column {
            objectName: "subtitle-preview-lines"
            visible: subtitleDialog.pref("subtitleEnabled", true)
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12 + subtitleDialog.pref("subtitleMargin", 0) * subPreview.scaleFactor
            spacing: 2
            Repeater {
                model: veyra.subtitleText.length > 0 ? veyra.subtitleText.split("\n").slice(0, 2)
                                                     : [qsTr("这是字幕预览，"), qsTr("改动会实时显示在这里。")]
                delegate: Rectangle {
                    required property string modelData
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: line.implicitWidth + 16
                    height: line.implicitHeight + 2
                    radius: 4
                    color: subtitleDialog.pref("subtitleBackground", false) ? Qt.rgba(0, 0, 0, 0.6) : "transparent"
                    Text {
                        id: line
                        anchors.centerIn: parent
                        text: modelData
                        color: "#FFFFFF"
                        readonly property var families: ["", "SimHei", "SimSun", "DengXian", "Arial", "Segoe UI"]
                        font.family: families[subtitleDialog.pref("subtitleFont", 0)] || Theme.fontUi
                        font.pixelSize: Math.max(12, subtitleDialog.pref("subtitleSize", 22) * 0.8)
                        font.weight: Font.DemiBold
                        style: subtitleDialog.pref("subtitleOutline", 2) > 0 ? Text.Outline : Text.Normal
                        styleColor: Qt.rgba(0, 0, 0, subtitleDialog.pref("subtitleOutline", 2) >= 3 ? 1 : 0.85)
                    }
                }
            }
        }
    }

    DialogSection { text: qsTr("字幕轨") }
    DialogGroup {
        VRow {
            label: qsTr("主字幕")
            hint: veyra.subtitleTracks.length === 0 ? (veyra.subtitleStatus.length > 0 ? veyra.subtitleStatus : (veyra.hasSource ? qsTr("当前片源没有字幕") : qsTr("打开视频后显示可用字幕"))) : ""
            VSelect {
                objectName: "subtitle-primary"
                implicitWidth: 220
                value: subtitleDialog.trackLabel(veyra.subtitlePrimary)
                options: subtitleDialog.trackOptions
                onPicked: id => veyra.subtitlePrimary = Number(id)
            }
        }
        VRow {
            label: qsTr("副字幕（双语）")
            hint: qsTr("显示在主字幕上方")
            VSelect {
                objectName: "subtitle-secondary"
                implicitWidth: 220
                value: subtitleDialog.trackLabel(veyra.subtitleSecondary)
                options: subtitleDialog.trackOptions
                onPicked: id => veyra.subtitleSecondary = Number(id)
            }
        }
        VRow {
            label: qsTr("外挂字幕")
            hint: qsTr("文本字幕 · PGS/SUP · VobSub（IDX+SUB）")
            VButton { objectName: "subtitle-load"; text: qsTr("加载文件…"); onClicked: veyra.loadSubtitleDialog() }
        }
        VRow {
            label: qsTr("默认显示副字幕")
            hint: qsTr("打开新文件时自动选第二条可用字幕")
            VSwitch {
                checked: subtitleDialog.pref("subtitleSecondLanguage", false)
                onToggled: checked => veyra.setPreference("subtitleSecondLanguage", checked)
            }
        }
    }
    DialogSection { text: qsTr("时间") }
    DialogGroup {
        VRow {
            label: qsTr("主字幕延时")
            hint: qsTr("Z / X 微调 50 ms，Shift 为 1 s；正值 = 字幕推后")
            // .stepper: − value +, 50 ms a step; the value can be typed.
            RowLayout {
                objectName: "subtitle-delay-stepper"
                spacing: 4
                VButton { objectName: "subtitle-delay-minus"; icon: true; iconName: "minus"; implicitWidth: 28; implicitHeight: 28; onClicked: veyra.nudgeSubtitle(-50) }
                DialogTextField {
                    objectName: "subtitle-delay-value"
                    implicitWidth: 86
                    text: veyra.subtitleOffsetMs + " ms"
                    onEdited: text => { const v = parseInt(text); if (isFinite(v)) veyra.subtitleOffsetMs = v }
                }
                VButton { objectName: "subtitle-delay-plus"; icon: true; iconName: "plus"; implicitWidth: 28; implicitHeight: 28; onClicked: veyra.nudgeSubtitle(50) }
            }
        }
        VRow {
            label: qsTr("按音轨自动对齐")
            hint: veyra.subtitleStatus.length > 0 ? veyra.subtitleStatus : qsTr("分析前 30 分钟的人声与字幕时间")
            VButton {
                text: veyra.subtitleAligning ? qsTr("分析中…") : qsTr("自动对齐")
                enabled: !veyra.subtitleAligning && veyra.subtitlePrimary >= 0
                onClicked: veyra.autoAlignSubtitle()
            }
        }
    }
    DialogSection { text: qsTr("样式") }
    DialogGroup {
        VRow {
            label: qsTr("显示字幕")
            hint: qsTr("快捷键 B")
            VSwitch {
                objectName: "subtitle-enabled"
                checked: subtitleDialog.pref("subtitleEnabled", true)
                onToggled: checked => veyra.setPreference("subtitleEnabled", checked)
            }
        }
        VRow {
            label: qsTr("字号")
            value: subtitleDialog.pref("subtitleSize", 22) + " px"
            VSlider {
                objectName: "subtitle-size"
                implicitWidth: 170
                from: 16; to: 56; value: subtitleDialog.pref("subtitleSize", 22)
                onMoved: veyra.setPreference("subtitleSize", Math.round(value))
            }
        }
        VRow {
            label: qsTr("字体")
            VSelect {
                objectName: "subtitle-font"
                implicitWidth: 170
                readonly property var names: [qsTr("字幕原字体"), qsTr("黑体"), qsTr("宋体"), qsTr("等线"), "Arial", "Segoe UI"]
                value: names[subtitleDialog.pref("subtitleFont", 0)]
                options: names.map((n, i) => ({ id: String(i), label: n }))
                onPicked: id => veyra.setPreference("subtitleFont", Number(id))
            }
        }
        VRow {
            label: qsTr("描边")
            VSeg {
                objectName: "subtitle-outline"
                options: [{ id: "0", label: qsTr("无") }, { id: "1", label: qsTr("细") }, { id: "2", label: qsTr("标准") }, { id: "3", label: qsTr("粗") }]
                current: String(subtitleDialog.pref("subtitleOutline", 2))
                onPicked: id => veyra.setPreference("subtitleOutline", Number(id))
            }
        }
        VRow {
            label: qsTr("背景条")
            VSwitch {
                checked: subtitleDialog.pref("subtitleBackground", false)
                onToggled: checked => veyra.setPreference("subtitleBackground", checked)
            }
        }
        VRow {
            label: qsTr("底部距离")
            value: subtitleDialog.pref("subtitleMargin", 0) + " px"
            VSlider {
                objectName: "subtitle-margin"
                implicitWidth: 170
                from: 0; to: 240; value: subtitleDialog.pref("subtitleMargin", 0)
                onMoved: veyra.setPreference("subtitleMargin", Math.round(value))
            }
        }
        VRow {
            label: qsTr("长句自动缩小")
            hint: qsTr("默认保持字号；开启后超过目标行数才缩小")
            VSwitch {
                checked: subtitleDialog.pref("subtitleFit", false)
                onToggled: checked => veyra.setPreference("subtitleFit", checked)
            }
        }
        VRow {
            visible: subtitleDialog.pref("subtitleFit", false)
            label: qsTr("目标行数")
            value: String(subtitleDialog.pref("subtitleLines", 2))
            VSlider {
                implicitWidth: 170
                from: 1; to: 8; value: subtitleDialog.pref("subtitleLines", 2)
                onMoved: veyra.setPreference("subtitleLines", Math.round(value))
            }
        }
    }
}
