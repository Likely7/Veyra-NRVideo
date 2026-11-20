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

    readonly property bool useVeyraAudio: translation !== null && translation.recognitionSource === 0
    readonly property bool englishOnly: useVeyraAudio && translation.recognitionModel === 0
    readonly property var recognitionSources: [
        {id: "0", label: qsTr("Veyra 音频")},
        {id: "1", label: qsTr("Windows 实时字幕（旧方式）"), disabled: !translation || !translation.windowsCaptionsSupported}
    ]
    readonly property var recognitionModels: [
        {id: "0", label: qsTr("Parakeet TDT · 英语")},
        {id: "1", label: qsTr("Whisper Turbo · 多语言")}
    ]
    readonly property var recognitionThreadOptions: [
        {id: "0", label: qsTr("自动")}, {id: "1", label: "1"}, {id: "2", label: "2"}, {id: "3", label: "3"},
        {id: "4", label: "4"}, {id: "5", label: "5"}, {id: "6", label: "6"}, {id: "7", label: "7"}, {id: "8", label: "8"}
    ]
    readonly property var subtitleLineOptions: [{id: "1", label: "1"}, {id: "2", label: "2"}, {id: "3", label: "3"}]
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
            hint: qsTr("识别播放中的声音，翻译后的字幕显示在 Veyra 预览画面内。")
            VSwitch {
                objectName: "live-translation-enabled"
                enabled: root.supported
                checked: root.translation ? root.translation.enabled : false
                onToggled: value => { root.translation.enabled = value }
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("识别来源")
            VSelect {
                objectName: "live-translation-recognition-source"
                implicitWidth: 248
                options: root.recognitionSources
                value: root.translation ? root.languageLabel(options, String(root.translation.recognitionSource)) : ""
                onPicked: id => { root.translation.recognitionSource = Number(id) }
            }
        }
        VRow {
            visible: root.supported && !root.useVeyraAudio
            label: qsTr("语音识别")
            hintBelow: true
            hint: qsTr("首次使用请在 Windows 下载语音包，并保持实时字幕窗口打开。")
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
                : qsTr("启用后会把识别出的字幕文本发送给所选服务。")
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
                enabled: !root.englishOnly
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
        VRow {
            visible: root.supported
            label: qsTr("译文行数上限")
            VSelect {
                options: root.subtitleLineOptions
                value: String(Math.min(3, Math.max(1, Number(veyra.preferences.subtitleLines ?? 2))))
                onPicked: id => { veyra.setPreference("subtitleLines", Number(id)) }
            }
        }
    }

    DialogGroup {
        visible: root.useVeyraAudio
        VRow {
            label: qsTr("语音识别模型")
            VSelect {
                objectName: "live-translation-recognition-model"
                implicitWidth: 248
                options: root.recognitionModels
                value: root.translation ? root.languageLabel(options, String(root.translation.recognitionModel)) : ""
                enabled: root.translation && !root.translation.recognitionDownloading
                onPicked: id => { root.translation.recognitionModel = Number(id) }
            }
        }
        VRow {
            label: root.translation && root.translation.recognitionInstalled ? qsTr("识别模型已就绪") : qsTr("识别模型按需下载")
            hintBelow: true
            hint: (root.englishOnly
                ? qsTr("NVIDIA Parakeet · CC BY 4.0")
                : qsTr("OpenAI Whisper · MIT · 建议性能较强的 CPU"))
                + qsTr(" · 首次下载 %1 MB").arg(root.translation ? Math.ceil(root.translation.recognitionBytes / 1000000) : 0)
            RowLayout {
                VButton {
                    objectName: "live-translation-recognition-download"
                    text: root.translation && root.translation.recognitionDownloading ? qsTr("取消下载") : qsTr("下载模型")
                    enabled: root.translation && (!root.translation.recognitionInstalled || root.translation.recognitionDownloading)
                    onClicked: {
                        if (root.translation.recognitionDownloading) root.translation.cancelRecognitionDownload()
                        else root.translation.downloadRecognitionModel()
                    }
                }
                VButton {
                    objectName: "live-translation-recognition-delete"
                    visible: root.translation && root.translation.recognitionInstalled
                    enabled: root.translation && !root.translation.enabled && !root.translation.recognitionDownloading
                    text: qsTr("删除模型")
                    onClicked: root.translation.deleteRecognitionModel()
                }
            }
        }
        ProgressBar {
            objectName: "live-translation-recognition-progress"
            Layout.fillWidth: true
            visible: root.translation !== null && root.translation.recognitionDownloading
            value: root.translation ? root.translation.recognitionProgress : 0
        }
        VRow {
            label: qsTr("识别线程")
            VSelect {
                options: root.recognitionThreadOptions
                value: root.translation ? root.languageLabel(options, String(root.translation.recognitionThreads)) : ""
                onPicked: id => { root.translation.recognitionThreads = Number(id) }
            }
        }
        Text {
            objectName: "live-translation-recognition-status"
            Layout.fillWidth: true
            text: root.translation ? root.translation.recognitionStatus : ""
            visible: text.length > 0
            color: Theme.t2
            font.family: Theme.fontUi
            font.pixelSize: Theme.fsBody
            wrapMode: Text.Wrap
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
    Text {
        Layout.fillWidth: true
        visible: root.translation && root.translation.enabled &&
            (root.translation.recognitionRtf > 0 || root.translation.averageTranslationMs > 0 || root.translation.skippedSentences > 0)
        text: !root.translation ? "" : qsTr("识别实时倍率 %1 · 翻译平均 %2 ms · 跳过 %3 句")
            .arg(Number(root.translation.recognitionRtf ?? 0).toFixed(2))
            .arg(Math.round(root.translation.averageTranslationMs ?? 0))
            .arg(root.translation.skippedSentences ?? 0)
        color: Theme.t2
        font.family: Theme.fontUi
        font.pixelSize: Theme.fsBody
        wrapMode: Text.Wrap
    }

}
