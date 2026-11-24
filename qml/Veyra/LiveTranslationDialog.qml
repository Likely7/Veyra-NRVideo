import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs as NativeDialogs
import QtQuick.Layouts

DialogLayer {
    id: root
    objectName: "live-translation-dialog"
    key: "translation"
    title: qsTr("实时翻译")
    glyph: "type"
    dialogWidth: 580
    actions: root.supported
        ? [{ label: qsTr("字幕样式") }, { label: qsTr("完成"), primary: true }]
        : [{ label: qsTr("完成"), primary: true }]
    onActionTriggered: label => {
        if (label !== qsTr("字幕样式")) {
            dialogHost.close()
            return
        }
        bodyViewport.contentY = Math.min(styleSection.y,
            Math.max(0, bodyViewport.contentHeight - bodyViewport.height))
    }

    readonly property var translation: veyra.liveTranslation || null
    readonly property bool supported: translation !== null && translation.supported
    readonly property int provider: translation ? translation.provider : 0
    readonly property int source: translation ? translation.recognitionSource : 0
    readonly property bool screenSource: source === 2
    readonly property int region: translation ? translation.screenRegion : 0
    readonly property rect customRegion: translation ? translation.customRegion : Qt.rect(0.08, 0.70, 0.84, 0.27)

    readonly property bool useVeyraAudio: translation !== null && translation.recognitionSource === 0
    readonly property bool englishOnly: useVeyraAudio && translation.recognitionModel === 0
    readonly property var recognitionSources: [
        {id: "0", label: qsTr("Veyra 音频")},
        {id: "1", label: qsTr("Windows 实时字幕（旧方式）"), disabled: !translation || !translation.windowsCaptionsSupported},
        {id: "2", label: qsTr("画面文字（采集卡 / PS5 字幕）"), disabled: !translation || !translation.screenTextSupported}
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
    readonly property int subtitleSize: veyra.preferences.liveSubtitleSize || veyra.preferences.subtitleSize || 22
    readonly property color subtitleColor: veyra.preferences.liveSubtitleColor || "#ffffff"
    readonly property real subtitleShadow: (veyra.preferences.liveSubtitleShadow ?? -1) >= 0
        ? veyra.preferences.liveSubtitleShadow : ((veyra.preferences.subtitleOutline ?? 2) > 0 ? 1.8 : 0)
    readonly property int subtitleLines: Math.min(3, Math.max(1,
        veyra.preferences.liveSubtitleLines || (veyra.preferences.subtitleLines ?? 2)))
    NativeDialogs.ColorDialog {
        id: captionColor
        title: qsTr("选择字幕颜色")
        onAccepted: veyra.setPreference("liveSubtitleColor",
            Qt.rgba(selectedColor.r, selectedColor.g, selectedColor.b, 1).toString())
    }
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
                : root.useVeyraAudio ? qsTr("识别播放中的声音，翻译后的字幕显示在 Veyra 预览画面内。")
                                     : qsTr("识别系统播放的声音。")
            VSelect {
                objectName: "live-translation-source"
                implicitWidth: 248
                options: root.recognitionSources
                value: root.languageLabel(options, String(root.source))
                onPicked: id => { root.translation.recognitionSource = Number(id) }
            }
        }
        VRow {
            label: qsTr("在画面上显示实时翻译")
            hintBelow: true
            hint: root.useVeyraAudio
                ? qsTr("识别播放中的声音，翻译后的字幕显示在 Veyra 预览画面内。")
                : root.supported
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
            visible: root.supported && root.source === 1
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
                ? qsTr("在本机运行，字幕文本留在本机。")
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
            hintBelow: root.screenSource
            hint: root.screenSource ? qsTr("画面文字按此语言识别，“自动识别原文”按英语识别；其他语言需在 Windows 中安装对应语言。") : ""
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
                ? qsTr("NVIDIA Parakeet · 仅识别英语 · CC BY 4.0")
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
            objectName: "live-translation-recognition-threads"
            label: qsTr("识别线程")
            hintBelow: true
            hint: root.translation && root.translation.recognitionEffectiveThreads > 0
                && root.translation.recognitionEffectiveThreads !== root.translation.recognitionThreads
                ? qsTr("实际使用 %1 个线程").arg(root.translation.recognitionEffectiveThreads) : ""
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
            hint: qsTr("Qwen2.5 1.5B Q4 与显卡/CPU 运行库 · 按需下载 %1 GB")
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
        VRow {
            label: qsTr("用显卡运行本地模型")
            hintBelow: true
            hint: qsTr("用显卡（Vulkan）翻译，每句只需不到一秒；没有可用显卡时自动改用 CPU。")
            VSwitch {
                objectName: "live-translation-gpu"
                checked: root.translation && root.translation.useGpu !== undefined ? root.translation.useGpu : true
                onToggled: value => { root.translation.useGpu = value }
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

    DialogSection { id: styleSection; visible: root.supported; text: qsTr("字幕样式") }
    DialogGroup {
        visible: root.supported
        Rectangle {
            objectName: "live-subtitle-preview"
            Layout.fillWidth: true
            implicitHeight: Math.max(92, sample.contentHeight + 22)
            radius: 10
            color: Theme.videoBlack
            border.width: 1
            border.color: Theme.stroke
            clip: true
            Text {
                anchors.centerIn: parent
                anchors.horizontalCenterOffset: root.subtitleShadow
                anchors.verticalCenterOffset: root.subtitleShadow
                visible: root.subtitleShadow > 0
                width: sample.width
                text: sample.text
                font: sample.font
                color: Qt.rgba(0, 0, 0, 0.59)
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
            // A lighter sample background makes both the dark shadow and the
            // foreground colour visible while editing.
            gradient: Gradient {
                GradientStop { position: 0; color: "#6c7685" }
                GradientStop { position: 1; color: "#404b60" }
            }
            Text {
                id: sample
                objectName: "live-subtitle-sample"
                anchors.centerIn: parent
                width: parent.width - 32
                text: qsTr("字幕预览")
                color: root.subtitleColor
                font.family: "Microsoft YaHei UI"
                font.pixelSize: Math.round(root.subtitleSize * 24 / 22)
                style: (veyra.preferences.subtitleOutline ?? 2) > 0 ? Text.Outline : Text.Normal
                styleColor: "#c0000000"
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
            }
        }
        VRow {
            label: qsTr("字号")
            value: root.subtitleSize + " px"
            VSlider {
                objectName: "live-subtitle-size"
                from: 16; to: 56; stepSize: 1
                value: root.subtitleSize
                valueFromModel: true
                onMoved: value => veyra.setPreference("liveSubtitleSize", Math.round(value))
            }
        }
        VRow {
            label: qsTr("文字颜色")
            RowLayout {
                Rectangle {
                    width: 18; height: 18; radius: 4
                    color: root.subtitleColor
                    border.width: 1
                    border.color: Theme.stroke2
                }
                VButton {
                    objectName: "live-subtitle-color"
                    text: root.subtitleColor.toString().toUpperCase()
                    onClicked: {
                        captionColor.selectedColor = root.subtitleColor
                        captionColor.open()
                    }
                }
            }
        }
        VRow {
            label: qsTr("阴影距离")
            value: root.subtitleShadow + " px"
            VSlider {
                objectName: "live-subtitle-shadow"
                from: 0; to: 8; stepSize: 1
                value: root.subtitleShadow
                valueFromModel: true
                onMoved: value => veyra.setPreference("liveSubtitleShadow", Math.round(value))
            }
        }
        VRow {
            label: qsTr("译文行数上限")
            VSelect {
                options: root.subtitleLineOptions
                value: String(root.subtitleLines)
                onPicked: id => veyra.setPreference("liveSubtitleLines", Number(id))
            }
        }
        VButton {
            objectName: "live-subtitle-reset"
            Layout.alignment: Qt.AlignRight
            text: qsTr("恢复默认")
            ghost: true
            onClicked: {
                veyra.setPreference("liveSubtitleSize", 0)
                veyra.setPreference("liveSubtitleColor", "#ffffff")
                veyra.setPreference("liveSubtitleShadow", -1)
                veyra.setPreference("liveSubtitleLines", 0)
            }
        }
    }

}
