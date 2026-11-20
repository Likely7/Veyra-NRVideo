import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

DialogLayer {
    id: root
    objectName: "live-translation-dialog"
    key: "translation"
    title: qsTr("实时翻译")
    glyph: "type"
    dialogWidth: 580
    actions: [{ label: qsTr("完成"), primary: true }]
    onActionTriggered: dialogHost.close()

    readonly property var translation: veyra.liveTranslation || null
    readonly property bool supported: translation !== null && translation.supported
    readonly property int provider: translation ? translation.provider : 0
    readonly property var providers: [
        {id: "0", label: qsTr("本地模型")},
        {id: "1", label: qsTr("自定义 API（OpenAI 兼容）")},
        {id: "2", label: "MyMemory"}
    ]
    readonly property var languages: [
        {id: "zh-CN", label: qsTr("简体中文")}, {id: "zh-TW", label: qsTr("繁體中文")},
        {id: "en", label: "English"}, {id: "ja", label: "日本語"}, {id: "ko", label: "한국어"},
        {id: "fr", label: "Français"}, {id: "de", label: "Deutsch"}, {id: "es", label: "Español"},
        {id: "pt-BR", label: "Português (Brasil)"}, {id: "ru", label: "Русский"},
        {id: "it", label: "Italiano"}, {id: "ar", label: "العربية"},
        {id: "th", label: "ไทย"}, {id: "vi", label: "Tiếng Việt"}
    ]
    readonly property var sourceLanguages: provider === 2 ? languages :
        [{id: "", label: qsTr("自动识别原文") }].concat(languages)
    function languageLabel(options, value) {
        return options.find(option => option.id === value)?.label || value
    }

    DialogGroup {
        VRow {
            label: qsTr("在画面上显示实时翻译")
            hintBelow: true
            hint: root.supported
                ? qsTr("使用 Windows 实时字幕识别系统播放的声音，译文显示在 Veyra 预览画面内。")
                : qsTr("实时翻译需要 Windows 11 22H2 或更新版本")
            VSwitch {
                objectName: "live-translation-enabled"
                enabled: root.supported
                checked: root.translation ? root.translation.enabled : false
                onToggled: value => { root.translation.enabled = value }
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("语音识别")
            hint: qsTr("首次使用需要在 Windows 中下载语音包，并把字幕语言设成原文语言。")
            VButton {
                objectName: "live-translation-windows"
                text: qsTr("打开系统字幕")
                onClicked: root.translation.openWindowsCaptions()
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("翻译引擎")
            hintBelow: true
            hint: root.provider === 0
                ? qsTr("在 CPU 上运行，字幕文本留在本机。")
                : qsTr("启用后会把系统字幕文本发送给所选服务。")
            VSelect {
                objectName: "live-translation-provider"
                implicitWidth: 224
                options: root.providers
                value: root.providers[root.provider].label
                onPicked: id => { root.translation.provider = Number(id) }
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("原文语言")
            VSelect {
                options: root.sourceLanguages
                value: root.translation ? root.languageLabel(options, root.translation.sourceLanguage) : ""
                onPicked: id => { root.translation.sourceLanguage = id }
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("翻译成")
            VSelect {
                objectName: "live-translation-target"
                options: root.languages
                value: root.translation ? root.languageLabel(options, root.translation.targetLanguage) : ""
                onPicked: id => { root.translation.targetLanguage = id }
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("同时显示原文")
            VSwitch {
                checked: root.translation ? root.translation.showOriginal : true
                onToggled: value => { root.translation.showOriginal = value }
            }
        }
    }

    DialogGroup {
        visible: root.supported && root.provider === 0
        VRow {
            label: root.translation && root.translation.modelInstalled ? qsTr("本地模型已就绪") : qsTr("本地模型")
            hintBelow: true
            hint: qsTr("Qwen2.5 1.5B Q4 与 CPU 运行库 · 按需下载 %1 GB")
                .arg(root.translation ? (root.translation.downloadBytes / 1000000000).toFixed(2) : "")
            RowLayout {
                VButton {
                    objectName: "live-translation-download"
                    text: root.translation && root.translation.downloading ? qsTr("取消下载")
                        : root.translation && root.translation.modelInstalled ? qsTr("验证运行库") : qsTr("下载模型")
                    enabled: root.translation !== null && (!root.translation.enabled || root.translation.downloading
                        || !root.translation.modelInstalled)
                    onClicked: {
                        if (root.translation.downloading) root.translation.cancelModelDownload()
                        else root.translation.downloadModel()
                    }
                }
            }
        }
        ProgressBar {
            objectName: "live-translation-progress"
            Layout.fillWidth: true
            visible: root.translation !== null && root.translation.downloading
            value: root.translation ? root.translation.downloadProgress : 0
        }
    }

    DialogGroup {
        visible: root.supported && root.provider === 1
        VRow {
            label: qsTr("接口地址")
            DialogTextField {
                objectName: "live-translation-api-base"
                implicitWidth: 248
                placeholder: "https://example.com/v1"
                text: root.translation ? root.translation.apiBase : ""
                onTyped: value => { root.translation.apiBase = value }
            }
        }
        VRow {
            label: qsTr("接口模型名")
            DialogTextField {
                implicitWidth: 248
                text: root.translation ? root.translation.apiModel : ""
                onTyped: value => { root.translation.apiModel = value }
            }
        }
        VRow {
            label: "API Key"
            hint: qsTr("仅本次运行有效")
            DialogTextField {
                implicitWidth: 248
                echoMode: TextInput.Password
                onTyped: value => { if (root.translation) root.translation.setApiKey(value) }
            }
        }
    }
    Text {
        objectName: "live-translation-status"
        Layout.fillWidth: true
        visible: text.length > 0
        text: root.translation ? root.translation.status : ""
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
        wrapMode: Text.Wrap
    }
}
