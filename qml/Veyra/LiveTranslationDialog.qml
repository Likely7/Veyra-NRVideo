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
    // 0: Windows Live Captions (speech); 1: text drawn on the picture (OCR), e.g. game
    // subtitles from a capture card or PS5 Remote Play.
    readonly property int source: translation && translation.captionSource !== undefined ? translation.captionSource : 0
    readonly property bool screenSource: source === 1
    readonly property int region: translation && translation.screenRegion !== undefined ? translation.screenRegion : 0
    readonly property rect customRegion: translation && translation.customRegion !== undefined
        ? translation.customRegion : Qt.rect(0.08, 0.70, 0.84, 0.27)
    readonly property var sources: [
        {id: "0", label: qsTr("Windows 实时字幕（语音）")},
        {id: "1", label: qsTr("画面文字（采集卡 / PS5 字幕）")}
    ]
    readonly property var regions: [
        {id: "0", label: qsTr("底部字幕")},
        {id: "1", label: qsTr("底部整行")},
        {id: "2", label: qsTr("顶部字幕")},
        {id: "3", label: qsTr("整个画面")},
        {id: "4", label: qsTr("自定义区域")}
    ]
    function setRegion(x, y, w, h) {
        if (root.translation) root.translation.customRegion = Qt.rect(x, y, w, h)
    }
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
            visible: root.translation !== null
            label: qsTr("字幕来源")
            hintBelow: true
            hint: root.screenSource
                ? qsTr("用 Windows 文字识别（OCR）读取画面上的字幕：采集卡、PS5 远程游玩、Xbox/电脑串流或视频。")
                : qsTr("识别系统播放的声音。")
            VSelect {
                objectName: "live-translation-source"
                implicitWidth: 248
                options: root.sources
                value: root.sources[Math.max(0, Math.min(root.sources.length - 1, root.source))].label
                onPicked: id => { root.translation.captionSource = Number(id) }
            }
        }
        VRow {
            label: qsTr("在画面上显示实时翻译")
            hintBelow: true
            hint: root.supported
                ? (root.screenSource
                    ? qsTr("读取画面上的英文字幕等文字并翻译，译文显示在 Veyra 预览画面内。")
                    : qsTr("使用 Windows 实时字幕识别系统播放的声音，译文显示在 Veyra 预览画面内。"))
                : (root.screenSource ? qsTr("画面文字识别需要 Windows 10 或更新版本")
                                     : qsTr("实时翻译需要 Windows 11 22H2 或更新版本"))
            VSwitch {
                objectName: "live-translation-enabled"
                enabled: root.supported
                checked: root.translation ? root.translation.enabled : false
                onToggled: value => { root.translation.enabled = value }
            }
        }
        VRow {
            visible: root.supported && !root.screenSource
            label: qsTr("语音识别")
            hint: qsTr("首次使用需要在 Windows 中下载语音包，并把字幕语言设成原文语言。")
            VButton {
                objectName: "live-translation-windows"
                text: qsTr("打开系统字幕")
                onClicked: root.translation.openWindowsCaptions()
            }
        }
        VRow {
            visible: root.supported && root.screenSource
            label: qsTr("识别区域")
            hintBelow: true
            hint: root.region === 0 || root.region === 2
                ? qsTr("只读取这一部分画面，并只保留居中的字幕，忽略两侧的界面文字。")
                : qsTr("只读取这一部分画面。区域越小，识别越快、越准确。")
            VSelect {
                objectName: "live-translation-region"
                implicitWidth: 224
                options: root.regions
                value: root.regions[Math.max(0, Math.min(root.regions.length - 1, root.region))].label
                onPicked: id => { root.translation.screenRegion = Number(id) }
            }
        }
        // What the last read found, so it is visible whether the area holds the subtitles.
        Text {
            objectName: "live-translation-reading"
            Layout.fillWidth: true
            visible: root.supported && root.screenSource && root.translation !== null && root.translation.enabled
            readonly property string reading: root.translation && root.translation.screenReading ? root.translation.screenReading : ""
            readonly property int words: root.translation && root.translation.screenWords ? root.translation.screenWords : 0
            text: reading.length ? qsTr("画面上读到：%1").arg(reading)
                : words > 0 ? qsTr("读到 %1 个词，但不像居中的字幕").arg(words)
                : qsTr("识别区域内暂时没有文字")
            color: reading.length ? Theme.t2 : Theme.t3
            font.family: Theme.fontUi
            font.pixelSize: 12
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
        }
        VRow {
            visible: root.supported && root.screenSource && root.region === 4
            label: qsTr("区域左边")
            VSlider {
                objectName: "live-translation-region-left"
                implicitWidth: 170
                from: 0; to: 0.95; inputScale: 100
                valueFromModel: true // the model clamps the region; the knob follows it
                value: root.customRegion.x
                onMoved: value => root.setRegion(value, root.customRegion.y, root.customRegion.width, root.customRegion.height)
            }
        }
        VRow {
            visible: root.supported && root.screenSource && root.region === 4
            label: qsTr("区域宽度")
            VSlider {
                implicitWidth: 170
                from: 0.05; to: 1; inputScale: 100
                valueFromModel: true
                value: root.customRegion.width
                onMoved: value => root.setRegion(root.customRegion.x, root.customRegion.y, value, root.customRegion.height)
            }
        }
        VRow {
            visible: root.supported && root.screenSource && root.region === 4
            label: qsTr("区域上边")
            VSlider {
                implicitWidth: 170
                from: 0; to: 0.95; inputScale: 100
                valueFromModel: true // the model clamps the region; the knob follows it
                value: root.customRegion.y
                onMoved: value => root.setRegion(root.customRegion.x, value, root.customRegion.width, root.customRegion.height)
            }
        }
        VRow {
            visible: root.supported && root.screenSource && root.region === 4
            label: qsTr("区域高度")
            VSlider {
                implicitWidth: 170
                from: 0.05; to: 1; inputScale: 100
                valueFromModel: true
                value: root.customRegion.height
                onMoved: value => root.setRegion(root.customRegion.x, root.customRegion.y, root.customRegion.width, value)
            }
        }
        VRow {
            visible: root.supported
            label: qsTr("翻译引擎")
            hintBelow: true
            hint: root.provider === 0
                ? qsTr("在 CPU 上运行，字幕文本留在本机。")
                : root.screenSource ? qsTr("启用后会把识别出的画面文字发送给所选服务。")
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
            hintBelow: root.screenSource
            hint: root.screenSource ? qsTr("画面文字按此语言识别，“自动识别原文”按英语识别；其他语言需在 Windows 中安装对应语言。") : ""
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
