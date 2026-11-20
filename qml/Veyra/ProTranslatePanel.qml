// 实时翻译页：控制 tools/translate-bridge.ps1 侧车。
// 侧车通过 UI Automation 读取 Windows 实时字幕，翻译后在视频画面内显示双语字幕。
// 字幕是 Veyra 主窗口的附属窗：跟随窗口与画面、随最小化一起收起，切换应用时
// 不会消失也不会重建，所以翻译过程不会被打断。
// 限制：它仍是独立窗口，不进入 Veyra 的录制与截图；HDR 内容下为 SDR 亮度。
import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: root
    Layout.fillWidth: true
    spacing: 6

    property bool online: false
    property string bridgeStatus: qsTr("未启动")
    property string modelState: ""
    property int startAttempts: 0
    property string sampleOriginal: ""
    property string sampleTranslated: ""

    readonly property string endpoint: "http://127.0.0.1:17881"
    // 开关与各项设置都存在本地属性里：preferences 的写入由引擎决定是否回写通知，
    // 本地属性保证这一次会话里不会出现"点了又弹回去"。改动同时写进 preferences。
    property bool enabled: false
    property string engine: "mymemory"
    property string srcLang: "en-US"
    property string dstLang: "zh-CN"
    property string mode: "both"
    property int fontSize: 26
    property int inset: 96
    // 出厂默认全部关闭：新装用户第一次进来不显示任何字幕装饰，需要时逐项打开。
    property bool backdrop: false
    // 新的一句字幕淡入并轻微上浮；逐词追加时不做动画，否则整行会一直闪。
    property bool textAnim: false
    property string posV: "bottom"
    property string posH: "center"
    // 文字描边与阴影：开关 + 粗细/距离（按 26 px 字号为基准，字号变化时等比缩放）。
    property bool outline: false
    property real outlineWidth: 1.4
    property bool shadow: false
    property real shadowDepth: 2.4
    // 视频画面在窗口客户区里的位置（逻辑像素）。侧车用它把字幕夹在画面内，
    // 因此字幕不会跑到右侧面板、播放条或其它窗口上面。
    property var videoBox: Qt.rect(0, 0, 0, 0)
    property var shellWin: null
    property string apiBase: ""
    property string apiKey: ""
    property string apiModel: ""
    // 密钥不回传给面板：侧车只告诉"存过没有"，面板没碰过密码框就不把它推回去，
    // 否则每次握手都会用空值覆盖掉侧车自己保存的那份。
    property bool apiKeySet: false
    property bool apiKeyTouched: false
    property bool cfgAdopted: false
    property string docName: ""
    property int docChars: 0
    property bool testing: false
    property string apiTest: ""
    // 侧车回报的上一次翻译失败原因（密钥错、模型名错、连不上）。
    property string apiError: ""

    function loadPrefs() {
        const p = veyra.preferences
        root.enabled = p.translateOn === true
        root.engine = p.translateEngine || "mymemory"
        root.srcLang = p.translateSource || "en-US"
        root.dstLang = p.translateTarget || "zh-CN"
        root.mode = p.translateLayout || "both"
        root.fontSize = Number(p.translateSize || 26)
        root.inset = Number(p.translateInset || 96)
        root.backdrop = p.translateBackdrop === true
        root.textAnim = p.translateTextAnim === true
        root.posV = p.translateVPos || "bottom"
        root.posH = p.translateHPos || "center"
        root.outline = p.translateOutline === true
        root.outlineWidth = Number(p.translateOutlineWidth || 1.4)
        root.shadow = p.translateShadow === true
        root.shadowDepth = Number(p.translateShadowDepth || 2.4)
        root.apiBase = p.translateApiBase || ""
        root.apiKey = p.translateApiKey || ""
        root.apiModel = p.translateModel || ""
    }

    function http(path, done) {
        const xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE)
                return
            done(xhr.status, xhr.responseText)
        }
        xhr.open("GET", endpoint + path)
        xhr.timeout = 1200
        xhr.send()
    }

    function query() {
        return "engine=" + encodeURIComponent(engine)
            + "&src=" + encodeURIComponent(srcLang)
            + "&dst=" + encodeURIComponent(dstLang)
            + "&mode=" + encodeURIComponent(mode)
            + "&size=" + fontSize
            + "&inset=" + inset
            + "&backdrop=" + (backdrop ? "1" : "0")
            + "&vpos=" + encodeURIComponent(posV)
            + "&stroke=" + (outline ? "1" : "0")
            + "&strokeW=" + outlineWidth.toFixed(2)
            + "&shadow=" + (shadow ? "1" : "0")
            + "&shadowD=" + shadowDepth.toFixed(2)
            + "&anim=" + (textAnim ? "1" : "0")
            + "&hpos=" + encodeURIComponent(posH)
            + "&vx=" + Math.round(videoBox.x)
            + "&vy=" + Math.round(videoBox.y)
            + "&vw=" + Math.round(videoBox.width)
            + "&vh=" + Math.round(videoBox.height)
            + "&apiBase=" + encodeURIComponent(apiBase)
            + "&model=" + encodeURIComponent(apiModel)
            + (apiKeyTouched ? "&apiKey=" + encodeURIComponent(apiKey) : "")
    }

    // 接口地址/模型/参考文档都存在侧车的配置文件里（引擎的偏好表不接受这些键），
    // 所以第一次连上时先读回来，再推配置，避免用空值把侧车里的设置冲掉。
    function adoptConfig(s) {
        if (root.cfgAdopted || !s)
            return
        root.cfgAdopted = true
        // 引擎选择也存在侧车配置文件里：preferences 不保存 translateEngine，
        // 否则每次重启都会退回 MyMemory，自定义接口等于失效。
        if (s.engine)
            root.engine = s.engine
        if (!root.apiBase && s.apiBase)
            root.apiBase = s.apiBase
        if (!root.apiModel && s.apiModel)
            root.apiModel = s.apiModel
        root.apiKeySet = s.apiKeySet === true
        if (s.doc) {
            root.docName = s.doc
            root.docChars = Number(s.docChars || 0)
        }
    }

    // 让侧车弹出它自己的文件选择框（QML 没有通用文件对话框），选完由侧车读取内容。
    function pickDoc() {
        http("/pickdoc", function(status) {
            if (status !== 200)
                root.bridgeStatus = qsTr("侧车未响应，无法选择文件")
        })
    }

    function clearDoc() {
        http("/config?docClear=1", function() {})
        root.docName = ""
        root.docChars = 0
    }

    function testApi() {
        testing = true
        apiTest = qsTr("正在请求…")
        const xhr = new XMLHttpRequest()
        xhr.onreadystatechange = function() {
            if (xhr.readyState !== XMLHttpRequest.DONE)
                return
            root.testing = false
            let r = null
            try { r = JSON.parse(xhr.responseText) } catch (e) {}
            if (!r) {
                root.apiTest = qsTr("测试失败：侧车没有响应")
                return
            }
            if (!r.ok && r.error === "no base url") {
                root.apiTest = qsTr("失败：请先填写接口地址")
                return
            }
            if (!r.ok && r.error === "empty reply") {
                root.apiTest = qsTr("失败：模型返回为空")
                return
            }
            root.apiTest = r.ok ? qsTr("连通：") + r.out
                                 : qsTr("失败：") + (r.error || qsTr("未知错误"))
        }
        xhr.open("GET", endpoint + "/apitest")
        xhr.timeout = 25000
        xhr.send()
    }

    // 输入框里的改动随手推给侧车：editingFinished 只在回车或失焦时触发，
    // 粘贴密钥后直接点「测试」（按钮不抢焦点）或关掉面板，密钥就丢了。
    // typed 走 400ms 防抖，避免每敲一个字符就写一次配置文件。
    function commitField(which, text, immediate) {
        if (which === "base") {
            if (text === root.apiBase)
                return
            root.apiBase = text
            veyra.setPreference("translateApiBase", text)
        } else if (which === "model") {
            if (text === root.apiModel)
                return
            root.apiModel = text
            veyra.setPreference("translateModel", text)
        } else {
            // 空框失焦同样会触发 edited：不比较内容就会用空串覆盖侧车里已存的密钥。
            if (text === root.apiKey)
                return
            root.apiKey = text
            root.apiKeyTouched = true
            root.apiKeySet = text.length > 0
            veyra.setPreference("translateApiKey", text)
        }
        if (immediate)
            root.pushConfig()
        else
            apiPush.restart()
    }

    Timer {
        id: apiPush
        interval: 400
        onTriggered: root.pushConfig()
    }

    // 画面矩形由主窗口按当前页面算出来（专业页要排除右侧面板和播放条）。
    // 返回 true 表示矩形变了，调用方据此决定是否再推一次配置。
    // 没有画面的页面（首页、设置）返回空矩形：推过去之后侧车会把字幕藏起来，
    // 不会留在上一个页面算出的位置上。
    function syncVideoBox() {
        if (!root.shellWin)
            root.shellWin = root.Window.window
        if (!root.shellWin || typeof root.shellWin.videoRect !== "function")
            return false
        const r = root.shellWin.videoRect()
        const ok = !!r && r.width > 0 && r.height > 0
        const nx = ok ? r.x : 0
        const ny = ok ? r.y : 0
        const nw = ok ? r.width : 0
        const nh = ok ? r.height : 0
        const b = root.videoBox
        if (Math.abs(nx - b.x) < 1 && Math.abs(ny - b.y) < 1
                && Math.abs(nw - b.width) < 1 && Math.abs(nh - b.height) < 1)
            return false
        root.videoBox = Qt.rect(nx, ny, nw, nh)
        return true
    }

    function pushConfig() {
        syncVideoBox()
        http("/config?" + query(), function() {})
    }

    // 面板开关和快捷键都走这里：状态、偏好、侧车启动/停止保持一致。
    function setEnabled(on) {
        if (root.enabled === on)
            return
        root.enabled = on
        veyra.setPreference("translateOn", on)
        if (on)
            root.startBridge()
        else
            root.stopBridge()
    }

    function startBridge() {
        // QML 不能直接起进程：交给外壳打开 tools/translate-bridge.lnk，
        // 快捷方式直接调用带 -WindowStyle Hidden 的 PowerShell，不会闪控制台窗口。
        const url = Qt.resolvedUrl("../../tools/translate-bridge.lnk")
        veyra.logUi("ui-translate", "start bridge " + url)
        Qt.openUrlExternally(url)
        bridgeStatus = qsTr("正在启动…")
        online = false
        startAttempts = 0
        retry.restart()
    }

    // 打开专业模式时若上次是开启状态，先握手；没有响应就直接拉起侧车。
    Component.onCompleted: {
        loadPrefs()
        // 侧车保存的接口地址/模型/参考文档要读回来：引擎的偏好表里没有这些键，
        // 面板每次重启都是空白，先推配置就会把侧车里存的值冲掉。
        http("/state", function(status, body) {
            if (status !== 200) {
                if (root.enabled)
                    root.startBridge()
                return
            }
            root.online = true
            try { root.adoptConfig(JSON.parse(body)) } catch (e) {}
            if (root.enabled)
                root.pushConfig()
        })
    }

    function stopBridge() {
        http("/stop", function() {
            online = false
            bridgeStatus = qsTr("已关闭")
        })
    }

    function poll() {
        http("/state", function(status, body) {
            if (status !== 200) {
                if (online) {
                    online = false
                    bridgeStatus = qsTr("侧车未响应")
                }
                return
            }
            let s = null
            try { s = JSON.parse(body) } catch (e) { return }
            const wasOnline = online
            online = true
            root.adoptConfig(s)
            root.docName = s.doc || ""
            root.docChars = Number(s.docChars || 0)
            if (s.apiKeySet === true)
                root.apiKeySet = true
            // 切换页面/缩放窗口后画面矩形会变，重新推一次让字幕跟着画面走。
            // 侧车刚重启时本地面板里的矩形没变，也要补推一次，否则新侧车
            // 会一直按上次的（或默认的）位置和字号画字幕。
            if (root.syncVideoBox() || !wasOnline)
                root.pushConfig()
            modelState = s.model || ""
            // 侧车只回报英文状态码，这里翻成面板上的说法。
            const map = {
                "starting": qsTr("正在启动…"),
                "starting captions": qsTr("正在启动实时字幕"),
                "waiting for caption text": qsTr("等待实时字幕文本"),
                "listening for audio": qsTr("等待语音"),
                "running": qsTr("正在翻译"),
                "loading local model": qsTr("本地模型加载中…"),
                "local model missing": qsTr("本地模型文件缺失"),
                "local model failed": qsTr("本地模型启动失败"),
                "port busy": qsTr("端口被占用"),
                "translate failed": qsTr("翻译失败")
            }
            bridgeStatus = map[s.message] || s.message || qsTr("运行中")
            root.apiError = s.message === "translate failed" ? (s.err || "") : ""
            sampleOriginal = s.original || ""
            sampleTranslated = s.translated || ""
        })
    }

    Timer {
        id: poller
        interval: 600
        repeat: true
        // 接口测试会占住侧车的控制线程，这时轮询必然超时，暂停即可避免误报"侧车未响应"。
        running: root.enabled && !root.testing
        triggeredOnStart: true
        onTriggered: root.poll()
    }

    Timer {
        id: retry
        interval: 1500
        repeat: true
        onTriggered: {
            if (root.online) {
                stop()
                return
            }
            // 侧车起不来时不要无限拉起新进程：多次失败后停在提示上。
            if (root.startAttempts >= 12) {
                stop()
                root.bridgeStatus = qsTr("侧车启动失败")
                return
            }
            root.startAttempts++
            root.http("/state", function(status, body) {
                if (status !== 200)
                    return
                root.online = true
                root.startAttempts = 0
                stop()
                // 刚被拉起的侧车已经从它自己的配置文件读过接口地址/模型/引擎，
                // 如果这里直接推配置，空值会把这些设置全部冲掉。
                try { root.adoptConfig(JSON.parse(body)) } catch (e) {}
                root.pushConfig()
            })
        }
    }

    VGroup {
        VRow {
            label: qsTr("实时翻译")
            hint: qsTr("读取 Windows 实时字幕并翻译；字幕贴在画面里，切换应用也不中断")
            hintBelow: true
            VSwitch {
                checked: root.enabled
                onToggled: checked => root.setEnabled(checked)
            }
        }
        VRow {
            label: qsTr("状态")
            // 翻译失败时把侧车回报的原因一起显示出来（密钥错、模型不存在…），
            // 否则只看到「翻译失败」，无从判断要改哪一项。
            hint: root.apiError.length > 0
                  ? root.bridgeStatus + "\n" + root.apiError
                  : root.bridgeStatus
            hintBelow: true
            VDot {
                off: !root.online
                warn: root.enabled && !root.online
            }
        }
        VRow {
            visible: root.engine === "local"
            label: qsTr("本地模型")
            hint: {
                const map = {
                    "ready": qsTr("已就绪（llama.cpp + GPU）"),
                    "loading": qsTr("加载中…"),
                    "missing": qsTr("文件缺失：tools\\llm 下未找到 exe 或 gguf"),
                    "failed": qsTr("启动失败，详见 logs\\llama-server.err.log"),
                    "off": qsTr("未加载")
                }
                return map[root.modelState] || qsTr("未加载")
            }
            hintBelow: true
            VDot {
                off: root.modelState !== "ready"
                warn: root.modelState === "failed" || root.modelState === "missing"
            }
        }
        VRow {
            label: qsTr("翻译引擎")
            hint: root.engine === "local"
                  ? qsTr("使用内置 Hy-MT2 1.8B（llama.cpp + GPU）；首次启用加载约 6 秒，显存约 3.1 GB")
                  : ""
            hintBelow: root.engine === "local"
            VSelect {
                options: [
                    { id: "local", label: qsTr("本地 Hy-MT2（GPU）") },
                    { id: "mymemory", label: qsTr("MyMemory（免密钥）") },
                    { id: "google", label: qsTr("Google（免密钥，可能限流）") },
                    { id: "openai", label: qsTr("OpenAI 兼容") }
                ]
                value: {
                    const map = { "local": qsTr("本地 Hy-MT2（GPU）"),
                                  "mymemory": qsTr("MyMemory（免密钥）"),
                                  "google": qsTr("Google（免密钥，可能限流）"),
                                  "openai": qsTr("OpenAI 兼容") }
                    return map[root.engine] || qsTr("MyMemory（免密钥）")
                }
                onPicked: picked => {
                    root.engine = picked
                    veyra.setPreference("translateEngine", picked)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("源语言")
            hint: qsTr("必须与 Windows 实时字幕里的语言一致")
            VSelect {
                options: [
                    { id: "en-US", label: qsTr("英语") },
                    { id: "ja-JP", label: qsTr("日语") },
                    { id: "zh-CN", label: qsTr("中文") },
                    { id: "ko-KR", label: qsTr("韩语") },
                    { id: "fr-FR", label: qsTr("法语") },
                    { id: "de-DE", label: qsTr("德语") },
                    { id: "es-ES", label: qsTr("西班牙语") }
                ]
                value: {
                    const map = { "en-US": qsTr("英语"), "ja-JP": qsTr("日语"), "zh-CN": qsTr("中文"),
                                  "ko-KR": qsTr("韩语"), "fr-FR": qsTr("法语"), "de-DE": qsTr("德语"),
                                  "es-ES": qsTr("西班牙语") }
                    return map[root.srcLang] || qsTr("英语")
                }
                onPicked: picked => {
                    root.srcLang = picked
                    veyra.setPreference("translateSource", picked)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("目标语言")
            VSelect {
                options: [
                    { id: "zh-CN", label: qsTr("简体中文") },
                    { id: "zh-TW", label: qsTr("繁体中文") },
                    { id: "en", label: qsTr("英语") },
                    { id: "ja", label: qsTr("日语") },
                    { id: "ko", label: qsTr("韩语") }
                ]
                value: {
                    const map = { "zh-CN": qsTr("简体中文"), "zh-TW": qsTr("繁体中文"),
                                  "en": qsTr("英语"), "ja": qsTr("日语"), "ko": qsTr("韩语") }
                    return map[root.dstLang] || qsTr("简体中文")
                }
                onPicked: picked => {
                    root.dstLang = picked
                    veyra.setPreference("translateTarget", picked)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("显示方式")
            VSeg {
                options: [
                    { id: "both", label: qsTr("上中下英") },
                    { id: "translated", label: qsTr("仅译文") },
                    { id: "original", label: qsTr("仅原文") }
                ]
                current: root.mode
                onPicked: picked => {
                    root.mode = picked
                    veyra.setPreference("translateLayout", picked)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("字幕背景")
            hint: qsTr("关闭后只显示带描边的文字，不再有黑色底板")
            VSwitch {
                checked: root.backdrop
                onToggled: checked => {
                    root.backdrop = checked
                    veyra.setPreference("translateBackdrop", checked)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("文字动画")
            hint: qsTr("新的一句字幕淡入并轻微上浮；逐词追加时不做动画，避免整行闪")
            VSwitch {
                checked: root.textAnim
                onToggled: checked => {
                    root.textAnim = checked
                    veyra.setPreference("translateTextAnim", checked)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("文字描边")
            hint: qsTr("给文字加一圈深色边，亮画面下也看得清")
            VSwitch {
                checked: root.outline
                onToggled: checked => {
                    root.outline = checked
                    veyra.setPreference("translateOutline", checked)
                    root.pushConfig()
                }
            }
        }
        VRow {
            visible: root.outline
            label: qsTr("描边粗细")
            value: root.outlineWidth.toFixed(1) + " px"
            VSlider {
                from: 0.5
                to: 4.0
                stepSize: 0.1
                inputDecimals: 1
                value: root.outlineWidth
                onMoved: value => {
                    root.outlineWidth = Math.round(value * 10) / 10
                    veyra.setPreference("translateOutlineWidth", root.outlineWidth)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("文字阴影")
            hint: qsTr("文字下方叠一层浅阴影，和背景分得更开")
            VSwitch {
                checked: root.shadow
                onToggled: checked => {
                    root.shadow = checked
                    veyra.setPreference("translateShadow", checked)
                    root.pushConfig()
                }
            }
        }
        VRow {
            visible: root.shadow
            label: qsTr("阴影距离")
            value: root.shadowDepth.toFixed(1) + " px"
            VSlider {
                from: 1.0
                to: 8.0
                stepSize: 0.2
                inputDecimals: 1
                value: root.shadowDepth
                onMoved: value => {
                    root.shadowDepth = Math.round(value * 10) / 10
                    veyra.setPreference("translateShadowDepth", root.shadowDepth)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("垂直位置")
            hint: qsTr("只在视频画面内移动；超出画面会自动收回，不会跑到面板或其它窗口上")
            hintBelow: true
            VSeg {
                options: [
                    { id: "top", label: qsTr("顶部") },
                    { id: "center", label: qsTr("中部") },
                    { id: "bottom", label: qsTr("底部") }
                ]
                current: root.posV
                onPicked: picked => {
                    root.posV = picked
                    veyra.setPreference("translateVPos", picked)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("水平位置")
            VSeg {
                options: [
                    { id: "left", label: qsTr("左") },
                    { id: "center", label: qsTr("中") },
                    { id: "right", label: qsTr("右") }
                ]
                current: root.posH
                onPicked: picked => {
                    root.posH = picked
                    veyra.setPreference("translateHPos", picked)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("字号")
            value: root.fontSize + " px"
            VSlider {
                from: 16
                to: 44
                value: root.fontSize
                onMoved: value => {
                    root.fontSize = Math.round(value)
                    veyra.setPreference("translateSize", root.fontSize)
                    root.pushConfig()
                }
            }
        }
        VRow {
            label: qsTr("边距")
            value: root.inset + " px"
            VSlider {
                from: 40
                to: 260
                value: root.inset
                onMoved: value => {
                    root.inset = Math.round(value)
                    veyra.setPreference("translateInset", root.inset)
                    root.pushConfig()
                }
            }
        }
        VRow {
            visible: root.engine === "openai"
            label: qsTr("接口地址")
            hint: qsTr("例如 https://api.openai.com/v1")
            hintBelow: true
            DialogTextField {
                width: 200
                placeholder: "https://api.openai.com/v1"
                text: root.apiBase
                onEdited: text => root.commitField("base", text, true)
                onTyped: text => root.commitField("base", text, false)
            }
        }
        VRow {
            visible: root.engine === "openai"
            label: qsTr("密钥")
            hint: root.apiKeySet && root.apiKey.length === 0
                  ? qsTr("已保存（本机），留空则继续沿用")
                  : ""
            hintBelow: true
            DialogTextField {
                width: 200
                placeholder: root.apiKeySet ? qsTr("已保存，重新输入可覆盖") : "sk-…"
                text: root.apiKey
                onEdited: text => root.commitField("key", text, true)
                onTyped: text => root.commitField("key", text, false)
            }
        }
        VRow {
            visible: root.engine === "openai"
            label: qsTr("模型")
            DialogTextField {
                width: 200
                placeholder: "gpt-4o-mini"
                text: root.apiModel
                onEdited: text => root.commitField("model", text, true)
                onTyped: text => root.commitField("model", text, false)
            }
        }
        VRow {
            visible: root.engine === "openai"
            label: qsTr("接口测试")
            hint: root.apiTest.length > 0
                  ? root.apiTest
                  : qsTr("用当前地址/密钥/模型翻译一句示例，确认能连通")
            hintColor: /^(失败|测试失败)/.test(root.apiTest) ? Theme.warn : Theme.t3
            hintBelow: true
            VButton {
                text: root.testing ? qsTr("测试中…") : qsTr("测试")
                enabled: !root.testing
                onClicked: root.testApi()
            }
        }
        VRow {
            visible: root.engine === "openai"
            label: qsTr("参考文档")
            hint: root.docName.length > 0
                  ? qsTr("%1（%2 字）已作为术语与上下文注入提示词").arg(root.docName).arg(root.docChars)
                  : qsTr("上传 .md 术语表或设定文档，翻译时一并发给模型")
            hintBelow: true
            Row {
                spacing: 8
                VButton {
                    text: root.docName.length > 0 ? qsTr("更换") : qsTr("选择 md")
                    onClicked: root.pickDoc()
                }
                VButton {
                    visible: root.docName.length > 0
                    text: qsTr("移除")
                    ghost: true
                    onClicked: root.clearDoc()
                }
            }
        }
        VRow {
            label: qsTr("最近一条")
            hint: root.sampleOriginal.length > 0
                  ? (root.sampleTranslated.length > 0
                     ? root.sampleTranslated + "\n" + root.sampleOriginal
                     : root.sampleOriginal)
                  : qsTr("暂无")
            hintBelow: true
        }
    }

    VGroup {
        VRow {
            label: qsTr("使用前提")
            hint: qsTr("1. 按 Win+Ctrl+L 打开 Windows 实时字幕，首次需同意条款并安装对应语言的语音包。\n"
                       + "2. 源语言必须与素材语音一致，实时字幕后端才能出文本。\n"
                       + "3. 字幕层不进入 Veyra 录制/截图，HDR 素材下亮度按 SDR 显示。")
            hintBelow: true
        }
    }
}
