// Professional-view displayCol controls; settings remain in the shared bridge.
import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Shapes
import QtQuick.Effects

ColumnLayout {
    id: displayCol
    readonly property bool hdrMeterObserved: visible && hdrCurveAccordion.open && hdrReadAccordion.open && veyra.hdrCurve
    Layout.fillWidth: true
    spacing: 6
    VGroup {
        ProPresentationRows { }
        VRow {
            label: qsTr("原画 / 增强对比")
            hint: (veyra.compareMode === 2 ? qsTr("在画面上按住左键拖动分割线：左边原画、右边增强。") : "") + (veyra.compareMode !== 0 ? qsTr("对比时补帧暂停，关闭对比后恢复") : "")
            VSeg {
                objectName: "display-compare"
                options: [{ id: "0", label: qsTr("关闭", "off") }, { id: "2", label: qsTr("分屏") }, { id: "1", label: qsTr("只看原画") }]
                current: String(veyra.compareMode)
                onPicked: id => veyra.compareMode = Number(id)
            }
        }
        VRow {
            label: qsTr("对照底图")
            hint: qsTr("低延迟 NR 先行时，“增强前底图”是原图缩放，不是独立超分对照")
            visible: veyra.compareMode !== 0
            VSeg {
                objectName: "display-compare-base"
                options: [{ id: "0", label: qsTr("输入原画") }, { id: "1", label: qsTr("增强前底图") }]
                current: veyra.compareBase ? "1" : "0"
                onPicked: id => veyra.compareBase = id === "1"
            }
        }
        VRow {
            label: qsTr("按住 V 查看原画")
            hint: qsTr("按键可在 设置 → 快捷键 里改")
            VSwitch {
                objectName: "display-hold-compare"
                checked: veyra.preferences.holdCompare !== false
                onToggled: checked => veyra.setPreference("holdCompare", checked)
            }
        }
        VRow {
            label: qsTr("画面比例")
            hint: veyra.aspectMode === 1 ? qsTr("原始 = 一个输出像素对一个屏幕像素") : veyra.aspectMode === 2 ? qsTr("填满窗口，超出部分裁掉") : ""
            VSelect {
                objectName: "display-aspect"
                options: [{ id: "0", label: qsTr("适应") }, { id: "1", label: qsTr("原始像素") }, { id: "2", label: qsTr("填充裁切") }, { id: "3", label: qsTr("拉伸铺满") }, { id: "4", label: "16:9" }, { id: "5", label: "4:3" }, { id: "6", label: "21:9" }]
                value: options[veyra.aspectMode].label
                onPicked: id => veyra.aspectMode = Number(id)
            }
        }
        VRow {
            label: qsTr("强制 SDR 预览")
            hint: qsTr("HDR 片源也按 SDR 显示；与采集卡的“转为 SDR 显示”是同一设置")
            VSwitch {
                objectName: "display-force-sdr"
                checked: veyra.captureForceSdr
                onToggled: checked => veyra.captureForceSdr = checked
            }
        }
        VRow {
            label: qsTr("HDR 输出格式")
            hint: qsTr("补帧路径固定 HDR10；需要 Windows HDR 开启")
            VSeg { objectName: "display-hdr-output"; options: [{id:"0",label:"HDR10"},{id:"1",label:qsTr("scRGB 浮点")}]; current: String(veyra.hdrOutputMode); onPicked: id => veyra.hdrOutputMode = Number(id) }
        }

    // HDR 亮度曲线: a static curve. Unlike a plain exposure the
    // highlights are protected by construction.
    VAccordion {
        id: hdrCurveAccordion
        objectName: "list-hdr-curve"
        Layout.fillWidth: true
        glyph: "sun"
        hue: "#F5C84B"
        title: qsTr("HDR 亮度曲线")
        summary: veyra.hdrCurve ? qsTr("已开启") : qsTr("已关闭")
        on: veyra.hdrCurve
        onToggled: on => veyra.hdrCurve = on
        // 曲线图（自定义）：由 C++ 用与着色器相同的公式采样，
        // 虚线是"不做任何处理"的参考线，橙线是当前曲线。

            VAccordion {
                id: hdrReadAccordion
                objectName: "list-hdr-readgroup"
                Layout.fillWidth: true
                glyph: "activity"
                hue: "#F5C84B"
                Layout.topMargin: 8
                enabledSwitch: false
                open: true
                title: qsTr("曲线图、读数与提示")
                summary: qsTr("2 项")
        // GPU 抽样统计：本卡可见且输出曲线生效时至多每秒 4 次。
        VRow {
            hintBelow: true
            objectName: "list-hdrcurve-live"
            label: qsTr("画面抽样统计")
            hint: !veyra.hdrCurveCoverage.valid
                ? qsTr("展开本卡片并播放 HDR 输出时测量，每秒最多 4 次")
                : veyra.hdrPeakWarning.capNits === 0
                    ? qsTr("曲线未设「峰值上限」，不会有像素被压平。滚降区 %1% · 暗部提升区 %2%")
                        .arg(Number(veyra.hdrCurveCoverage.rollOff).toFixed(1))
                        .arg(Number(veyra.hdrCurveCoverage.lift).toFixed(1))
                : veyra.hdrCurveCoverage.aboveCeiling <= 0.0
                    ? qsTr("上限 %1 nit 未被触及（滚降区 %2% · 暗部提升区 %3%）")
                        .arg(veyra.hdrPeakWarning.capNits)
                        .arg(Number(veyra.hdrCurveCoverage.rollOff).toFixed(1))
                        .arg(Number(veyra.hdrCurveCoverage.lift).toFixed(1))
                : qsTr("触及硬上限 %1% · 高光压缩区 %2% · 暗部提升区 %3%")
                    .arg(Number(veyra.hdrCurveCoverage.aboveCeiling).toFixed(1))
                    .arg(Number(veyra.hdrCurveCoverage.rollOff).toFixed(1))
                    .arg(Number(veyra.hdrCurveCoverage.lift).toFixed(1))
            value: !veyra.hdrCurveCoverage.valid ? qsTr("—")
                : veyra.hdrPeakWarning.capNits === 0 ? qsTr("未设上限")
                : veyra.hdrCurveCoverage.aboveCeiling > 5 ? qsTr("偏高")
                : veyra.hdrCurveCoverage.aboveCeiling > 0 ? qsTr("轻微") : qsTr("未触及")
        }
        // 高光预警（自定义）：片源峰值 vs 曲线生效后的硬上限。
        VRow {
            objectName: "list-hdrcurve-peakwarning"
            label: qsTr("高光预警")
            hint: !veyra.hdrSourceInfo.valid ? qsTr("片源没有携带亮度信息")
                : !veyra.hdrPeakWarning.valid ? qsTr("曲线未开启")
                : veyra.hdrPeakWarning.capNits === 0 ? qsTr("曲线没有设硬上限，不会裁剪片源峰值")
                : qsTr("片源最亮 %1 nit，曲线硬上限 %2 nit：%3")
                    .arg(veyra.hdrPeakWarning.sourcePeakNits)
                    .arg(veyra.hdrPeakWarning.capNits)
                    .arg(veyra.hdrPeakWarning.clipped
                         ? qsTr("超过上限的亮度需要压缩；是否触及硬上限以抽样读数为准")
                         : qsTr("上限覆盖片源标注峰值；处理后的画面以抽样读数为准"))
            value: !veyra.hdrSourceInfo.valid || !veyra.hdrPeakWarning.valid ? qsTr("—")
                : veyra.hdrPeakWarning.clipped ? qsTr("会压缩") : qsTr("安全")
        }
        Canvas {
            id: hdrCurveCanvas
            Layout.fillWidth: true
            Layout.preferredHeight: 92
            onWidthChanged: requestPaint()
            Component.onCompleted: requestPaint()
            Connections {
                target: veyra
                function onSettingsChanged() { hdrCurveCanvas.requestPaint() }
            }
            onPaint: {
                var ctx = getContext("2d");
                var w = width, h = height;
                ctx.clearRect(0, 0, w, h);
                var g = veyra.hdrCurveGraph;
                if (!g || !g.inputs || g.inputs.length < 2) return;
                var minN = g.minNits, maxN = g.maxNits;
                function px(n) {
                    var v = Math.max(n, minN) / minN;
                    return Math.log(v) / Math.log(maxN / minN);
                }
                // 参考线：未启用任何调整时曲线与它重合
                ctx.strokeStyle = Theme.t3;
                ctx.lineWidth = 1;
                ctx.beginPath();
                ctx.moveTo(0, h); ctx.lineTo(w, 0); ctx.stroke();
                // 当前曲线
                ctx.strokeStyle = g.enabled ? "#F5C84B" : Theme.t3;
                ctx.lineWidth = 2;
                ctx.beginPath();
                for (var i = 0; i < g.inputs.length; ++i) {
                    var X = px(g.inputs[i]) * w;
                    var Y = h - px(g.outputs[i]) * h;
                    if (i === 0) ctx.moveTo(X, Y); else ctx.lineTo(X, Y);
                }
                ctx.stroke();
            }
        }
        // 一键按片源选档（自定义）：用片源自带的峰值 / 平均亮度
        // 决定档位与强度，界面只解释理由，不做测量。
        Text {
            Layout.fillWidth: true
            text: qsTr("曲线作用于 HDR10 或 scRGB 输出，也适用于 RTX Video HDR 转换后的画面。低亮度画面可能不会触及高光阈值。")
            wrapMode: Text.WordWrap
            color: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: 11
        }
            }
        VAccordion {
            objectName: "list-hdr-pickgroup"
            Layout.fillWidth: true
            glyph: "wand"
            hue: "#A78BFA"
            Layout.topMargin: 8
            enabledSwitch: false
            open: true
            title: qsTr("一键选档")
            summary: qsTr("4 项")
        // 选档方式（自定义）：整档预设 / 自由组合。两套写的是同一条曲线，
        // 所以切换只影响界面，不影响存档或画面。
        VRow {
            hintBelow: true
            label: qsTr("按片源自动选档")
            hint: {
                if (!veyra.hdrRecommendation.available)
                    return veyra.sourceIsSdr
                        ? qsTr("SDR 片源没有 HDR 亮度元数据；可手动选档，HDR 输出时生效。")
                        : qsTr("片源没有携带亮度信息（抓屏 / 串流），无法自动选档");
                var r = veyra.hdrRecommendation.reason;
                var why = r === "dark-content" ? qsTr("整片偏暗、只有少量亮点")
                    : r === "bright-content" ? qsTr("高光又亮又多")
                    : r === "narrow-range" ? qsTr("母版峰值偏低（老片 / 转制片）")
                    : r === "beyond-display" ? qsTr("内容亮度远超显示设备能力")
                    : r === "dark-and-bright" ? qsTr("整片偏暗，且超出显示设备能力")
                    : qsTr("亮度分布均衡");
                var axes = veyra.hdrPresetMode === "axes";
                var pick = axes
                    ? (r === "dark-content" ? qsTr("内容：暗场多")
                        : r === "bright-content" ? qsTr("内容：高光强")
                        : r === "beyond-display" ? qsTr("内容：高光强")
                        : r === "dark-and-bright" ? qsTr("内容：两头极端")
                        : r === "narrow-range" ? qsTr("内容：老片平淡")
                        : qsTr("内容：标准"))
                    : (r === "dark-content" ? qsTr("暗场提亮")
                        : r === "bright-content" ? qsTr("高光保护")
                        : r === "beyond-display" ? qsTr("高光保护")
                        : r === "dark-and-bright" ? qsTr("暗亮双修")
                        : r === "narrow-range" ? qsTr("强化冲击") : qsTr("标准"));
                return why + qsTr(" → 建议 ") + pick
                    + qsTr("（片源最亮 %1 nit、最亮帧平均 %2 nit）")
                        .arg(veyra.hdrRecommendation.cllNits)
                        .arg(veyra.hdrRecommendation.fallNits);
            }
            VButton {


                objectName: "list-hdrcurve-auto"
                text: qsTr("一键推荐")
                ghost: true
                enabled: veyra.hdrRecommendation.available
                onClicked: veyra.applyRecommendedHdrTuning()
            }
        }

        Text {
            Layout.fillWidth: true
            visible: true
            text: qsTr("选档方式：两种选法写的是同一条曲线，切换只影响界面上怎么叫它")
            wrapMode: Text.WordWrap
            color: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: 11
        }
        Flow {
            objectName: "list-hdrcurve-mode"
            Layout.fillWidth: true
            visible: true
            Layout.preferredHeight: implicitHeight
            spacing: 6
            Repeater {
                model: [{id:"legacy",label:qsTr("整档预设")}, {id:"axes",label:qsTr("自由组合")}]
                delegate: VButton {
                    required property var modelData
                    text: modelData.label
                    ghost: true
                    primary: veyra.hdrPresetMode === modelData.id
                    onClicked: veyra.hdrPresetMode = modelData.id
                }
            }
        }

        // One-click presets: pick the one that matches the film.
        Flow {
            visible: veyra.hdrPresetMode === "legacy"
            Layout.fillWidth: true
            Layout.preferredHeight: implicitHeight
            spacing: 6
            Repeater {
                model: [{id:"standard",label:qsTr("标准")},
                        {id:"darkLift",label:qsTr("暗场提亮")},
                        {id:"highlightGuard",label:qsTr("高光保护")},
                        {id:"darkAndBright",label:qsTr("暗亮双修")},
                        {id:"brightRoom",label:qsTr("明亮环境")},
                        {id:"darkRoom",label:qsTr("暗室观影")},
                        {id:"punch",label:qsTr("强化冲击")}]
                delegate: VButton {
                    required property var modelData
                    text: modelData.label
                    ghost: true
                    // 两套预设写的是同一条曲线，所以数值上可能同时匹配（「不补偿」时自由组合就等于整档预设）。
                    // 只让当前模式那一行点亮，免得看起来像两个模式同时生效。
                    primary: veyra.hdrAppliedFrom !== "axes" && veyra.hdrTuningPreset === modelData.id
                    onClicked: { veyra.hdrPresetMode = "legacy"; veyra.applyHdrTuningPreset(modelData.id) }
                }
            }
        }
        Text {
            visible: veyra.hdrPresetMode === "legacy"
            Layout.fillWidth: true
            text: veyra.hdrTuningPreset === "darkLift" ? qsTr("提亮暗部与中间调，保留高光峰值。")
                : veyra.hdrTuningPreset === "highlightGuard" ? qsTr("高光很亮的片子（雪景/爆炸/演唱会）：压住顶部，暗部不动。")
                : veyra.hdrTuningPreset === "darkAndBright" ? qsTr("暗场很暗、亮场又很亮的片子：两头都照顾。")
                : veyra.hdrTuningPreset === "brightRoom" ? qsTr("白天或亮环境看片：整体提亮一些。")
                : veyra.hdrTuningPreset === "darkRoom" ? qsTr("夜里关灯看：柔和一点，同时保留暗部细节。")
                : veyra.hdrTuningPreset === "punch" ? qsTr("画面偏灰、平淡的老片或转制片：提高对比观感。")
                : veyra.hdrTuningPreset === "standard" ? qsTr("曲线保持原样，HDR10 元数据跟随片源。")
                : qsTr("自定义：已用下面的滑块微调过。")
            wrapMode: Text.WordWrap
            color: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: 11
        }
        Text {
            Layout.fillWidth: true
            visible: veyra.hdrPresetMode === "axes"
            text: qsTr("内容：这部片子属于哪一类")
            wrapMode: Text.WordWrap
            color: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: 11
        }
        Flow {
            objectName: "list-hdrcurve-content"
            Layout.fillWidth: true
            visible: veyra.hdrPresetMode === "axes"
            Layout.preferredHeight: implicitHeight
            spacing: 6
            Repeater {
                model: [{id:"standard",label:qsTr("标准")}, {id:"dark",label:qsTr("暗场多")}, {id:"highlight",label:qsTr("高光强")}, {id:"darkAndBright",label:qsTr("两头极端")}, {id:"punch",label:qsTr("老片平淡")}]
                delegate: VButton {
                    required property var modelData
                    text: modelData.label
                    ghost: true
                    // 只按曲线实际等于哪个组合点亮：当前是整档预设（或手动微调过）时不亮任何一项，
            // 与整档预设显示"自定义"的行为一致。
            primary: veyra.hdrAppliedFrom === "axes" && veyra.hdrAxesMatch.content === modelData.id
                    onClicked: { veyra.hdrPresetMode = "axes"; veyra.applyHdrAxesPreset(modelData.id, veyra.hdrAxes.environment !== "" ? veyra.hdrAxes.environment : "compensate"); }
                }
            }
        }
        Text {
            Layout.fillWidth: true
            visible: veyra.hdrPresetMode === "axes"
            text: qsTr("环境光补偿：屋里亮（开灯/白天）就补偿一点，屋里暗（关灯）就不补偿")
            wrapMode: Text.WordWrap
            color: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: 11
        }
        Flow {
            objectName: "list-hdrcurve-environment"
            Layout.fillWidth: true
            visible: veyra.hdrPresetMode === "axes"
            Layout.preferredHeight: implicitHeight
            spacing: 6
            Repeater {
                model: [{id:"compensate",label:qsTr("提亮补偿")}, {id:"plain",label:qsTr("不补偿")}]
                delegate: VButton {
                    required property var modelData
                    text: modelData.label
                    ghost: true
                    primary: veyra.hdrAppliedFrom === "axes" && veyra.hdrAxesMatch.environment === modelData.id
                    onClicked: { veyra.hdrPresetMode = "axes"; veyra.applyHdrAxesPreset(veyra.hdrAxesMatch.content !== "" ? veyra.hdrAxesMatch.content : (veyra.hdrAxes.content !== "" ? veyra.hdrAxes.content : "standard"), modelData.id); }
                }
            }
        }
        // 当前生效状态（自定义）：整档预设模式下说明当前是哪一档；手动微调过
        // （不匹配任何整档预设）时明确说"自定义"，免得看起来像没生效。
        Text {
            Layout.fillWidth: true
            visible: veyra.hdrPresetMode !== "axes"
            objectName: "list-hdrcurve-active-preset"
            text: veyra.hdrTuningPreset !== "custom"
                ? qsTr("当前生效：整档预设「%1」").arg(
                    veyra.hdrTuningPreset === "darkLift" ? qsTr("暗场提亮")
                    : veyra.hdrTuningPreset === "highlightGuard" ? qsTr("高光保护")
                    : veyra.hdrTuningPreset === "darkAndBright" ? qsTr("暗亮双修")
                    : veyra.hdrTuningPreset === "brightRoom" ? qsTr("明亮环境")
                    : veyra.hdrTuningPreset === "darkRoom" ? qsTr("暗室观影")
                    : veyra.hdrTuningPreset === "punch" ? qsTr("强化冲击")
                    : qsTr("标准"))
                : qsTr("当前生效：自定义微调（未匹配任何整档预设）")
            wrapMode: Text.WordWrap
            color: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: 11
        }
        // 当前组合（自定义）：自由组合模式下说清楚现在选的是哪两项 —— 手动微调
        // 过参数后没有匹配项，这一行仍然在，不会看起来像"没生效"。
        Text {
            Layout.fillWidth: true
            visible: veyra.hdrPresetMode === "axes"
            text: qsTr("当前组合：内容 %1 · 环境 %2").arg(
                    veyra.hdrAxes.content === "dark" ? qsTr("暗场多")
                    : veyra.hdrAxes.content === "highlight" ? qsTr("高光强")
                    : veyra.hdrAxes.content === "darkAndBright" ? qsTr("两头极端")
                    : veyra.hdrAxes.content === "punch" ? qsTr("老片平淡")
                    : qsTr("标准")).arg(
                    veyra.hdrAxes.environment === "compensate" ? qsTr("提亮补偿")
                    : qsTr("不补偿"))
            wrapMode: Text.WordWrap
            color: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: 11
        }
        // 等价说明（自定义）：两套预设写的是同一条曲线，「不补偿」时两轴组合与
        // 整档预设数值完全相同，所以这里把等价关系直接说出来，省得看起来像两个模式
        // 同时生效。手动微调过的曲线不匹配任何整档预设，这句话自然消失。
        Text {
            Layout.fillWidth: true
            objectName: "list-hdrcurve-equivalent"
            visible: veyra.hdrPresetMode === "axes" && veyra.hdrTuningPreset !== "custom"
            text: qsTr("这条曲线目前也等同于整档预设「%1」").arg(
                veyra.hdrTuningPreset === "darkLift" ? qsTr("暗场提亮")
                : veyra.hdrTuningPreset === "highlightGuard" ? qsTr("高光保护")
                : veyra.hdrTuningPreset === "darkAndBright" ? qsTr("暗亮双修")
                : veyra.hdrTuningPreset === "brightRoom" ? qsTr("明亮环境")
                : veyra.hdrTuningPreset === "darkRoom" ? qsTr("暗室观影")
                : veyra.hdrTuningPreset === "punch" ? qsTr("强化冲击")
                : qsTr("标准"))
            wrapMode: Text.WordWrap
            color: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: 11
        }
        }
                VAccordion {
                    objectName: "list-hdr-basegroup"
                    Layout.fillWidth: true
                    glyph: "sliders"
                    hue: "#4F7BFF"
                    Layout.topMargin: 8
                    enabledSwitch: false
                    open: true
                    title: qsTr("基础设置")
                    summary: qsTr("5 项")
        VRow {
            hintBelow: true
            label: qsTr("总体强度")
            hint: qsTr("调整力度；0% 关闭曲线")
            value: veyra.hdrCurveStrength + "%"
            VSlider {
                objectName: "list-hdrcurve-strength"
                implicitWidth: 120
                valueFromModel: true
                resettable: true; defaultValue: 100
                from: 0; to: 200
                value: veyra.hdrCurveStrength
                onMoved: value => veyra.hdrCurveStrength = Math.round(value)
            }
        }

        // 人话滑杆（自定义）：技术参数翻译成三根好懂的滑杆，
        // 精确参数仍在下面的「高级」里。
        VRow {
            hintBelow: true
            label: qsTr("暗部提亮")
            hint: qsTr("拉亮暗部")
            // 档位只是快捷方式：同时给出真实参数，避免"碰一下跳值"的困惑。
            value: (Number(veyra.hdrCurveParams.shadowLiftEv100 ?? 0) / 100).toFixed(2) + " EV"
            VSlider {
                objectName: "list-hdrcurve-dial-lift"
                implicitWidth: 120
                valueFromModel: true
                resettable: true; defaultValue: 0
                from: 0; to: 10
                value: veyra.hdrDials.shadowLift
                onMoved: value => veyra.setHdrDial("shadowLift", Math.round(value))
            }
        }
        VRow {
            hintBelow: true
            label: qsTr("整体观感")
            hint: qsTr("整体亮暗")
            value: (veyra.hdrCurveParams.midGrayNits ?? "—") + " nit"
            VSlider {
                objectName: "list-hdrcurve-dial-brightness"
                implicitWidth: 120
                valueFromModel: true
                resettable: true; defaultValue: 3
                from: 0; to: 10
                value: veyra.hdrDials.brightness
                onMoved: value => veyra.setHdrDial("brightness", Math.round(value))
            }
        }
        VRow {
            hintBelow: true
            label: qsTr("高光保护")
            hint: qsTr("压住高光")
            value: [qsTr("关"),qsTr("弱"),qsTr("中"),qsTr("强")][Math.max(0,Math.min(3,veyra.hdrDials.highlight))]
            VSlider {
                objectName: "list-hdrcurve-dial-highlight"
                implicitWidth: 120
                valueFromModel: true
                resettable: true; defaultValue: 0
                from: 0; to: 3
                value: veyra.hdrDials.highlight
                onMoved: value => veyra.setHdrDial("highlight", Math.round(value))
            }
        }
        VRow {
            hintBelow: true
            label: qsTr("显示峰值")
            hint: qsTr("我的屏能到多少 nit；0 = 自动")
            value: veyra.hdrDisplayPeakNits > 0
                ? (veyra.hdrDisplayPeakNits + " nit") : qsTr("自动")
            VSlider {
                objectName: "list-hdrcurve-displaypeak"
                implicitWidth: 120
                valueFromModel: true
                resettable: true; defaultValue: 0
                from: 0; to: 4000
                value: veyra.hdrDisplayPeakNits
                onMoved: value => veyra.hdrDisplayPeakNits = Math.round(value)
            }
        }

                    // The display's own report, kept at the card's foot like the author's section notes.
                    Text {
                        Layout.fillWidth: true
                        visible: veyra.hdrDisplayInfo.valid
                        text: qsTr("系统报告：峰值 %1、全屏 %2、黑位 %3 nit")
                                .arg(veyra.hdrDisplayInfo.peakNits)
                                .arg(veyra.hdrDisplayInfo.fullFrameNits)
                                .arg(Number(veyra.hdrDisplayInfo.minNits).toFixed(3))
                            + " · " + qsTr("系统数值不是实测；不符时可手动填写峰值")
                        wrapMode: Text.WordWrap
                        color: Theme.t3
                        font.family: Theme.fontUi; font.pixelSize: 11
                    }
                }

        // The two dials. Both scale the baseline the sliders below
        // edit, which is why moving either of them keeps the preset
        // above selected instead of falling back to "自定义".
        // Advanced technical parameters (custom): the exact values behind the three
        // dials above. Collapsed by default so the panel stays readable.
        // Advanced parameters (custom): the exact values behind the three dials above,
        // in the author's own collapsible-group style (label + chevron, no switch).
        VAccordion {
            objectName: "list-hdr-advanced"
            Layout.fillWidth: true
            glyph: "settings"
            hue: "#8A93A6"
            Layout.topMargin: 8
            enabledSwitch: false
            open: false
            title: qsTr("高级设置：技术参数")
            summary: qsTr("6 项")
            Text {
                Layout.fillWidth: true
                text: qsTr("这里是「基础设置」里 暗部提亮、整体观感、高光保护 这三项的精确数值版（强度与显示峰值不在此列，它们本身就是直接调节）。")
                wrapMode: Text.WordWrap
                color: Theme.t3
                font.family: Theme.fontUi; font.pixelSize: 11
            }
                                        Repeater {
                                            model: [{key:"shadowLiftEv100",label:qsTr("暗部抬升"),from:-200,to:200,def:0,hint:qsTr("单位 0.01 EV：正值提亮暗部，负值压暗")},
                                                    {key:"shadowRangeNits",label:qsTr("抬升范围 (nit)"),from:1,to:100,def:10,hint:qsTr("只在这个亮度以下的暗部生效")},
                                                    {key:"midGrayNits",label:qsTr("中灰 (nit)"),from:100,to:500,def:203,hint:qsTr("203 = 中性；调高整体变亮，峰值不变")},
                                                    {key:"highlightStartNits",label:qsTr("高光起点 (nit)"),from:0,to:2000,def:0,hint:qsTr("0 = 不做高光滚降")},
                                                    {key:"rollOffPercent",label:qsTr("滚降强度 (%)"),from:10,to:200,def:100,hint:qsTr("数值越大，高光过渡越平缓")},
                                                    {key:"peakCapNits",label:qsTr("峰值上限 (nit)"),from:0,to:4000,def:0,hint:qsTr("0 = 不限制")}]
                                            delegate: VRow {
                                                hintBelow: true
                                                required property var modelData
                                                label: modelData.label
                                                hint: modelData.hint || ""
                                                value: String(veyra.hdrCurveParams[modelData.key] ?? "—")
                                                VSlider {
                                                    objectName: "list-hdrcurve-" + modelData.key
                                                    implicitWidth: 120
                                                    valueFromModel: true
                                                    resettable: true; defaultValue: modelData.def
                                                    from: modelData.from; to: modelData.to
                                                    value: veyra.hdrCurveParams[modelData.key] ?? modelData.def
                                                    onMoved: value => veyra.setHdrCurveParameter(modelData.key, Math.round(value))
                                                }
                                            }
                                        }
        }
        VAccordion {
            objectName: "list-hdr-memory-group"
            Layout.fillWidth: true
            glyph: "clock"
            hue: "#4FD1A5"
            Layout.topMargin: 8
            enabledSwitch: false
            open: false
            title: qsTr("记忆与自动化")
            summary: qsTr("4 项")
        // 记忆与自动化（自定义）
        // 每台显示设备一套配置（自定义）：按显示器身份记住曲线，
        // 换设备（投影仪 / 电视）时自动套用那台设备上的设置。
        VRow {
            hintBelow: true
            label: qsTr("按显示设备记忆")
            hint: veyra.hdrDisplayProfileCount > 0
                ? qsTr("已记住 %1 台显示设备的曲线配置，切换显示器时自动套用")
                    .arg(veyra.hdrDisplayProfileCount)
                : qsTr("改动曲线后会按当前显示器记住，换设备时自动套用")
            value: veyra.hdrDisplayProfileCount > 0 ? qsTr("已启用") : qsTr("—")
            VButton {
                objectName: "list-hdrcurve-forget"
                text: qsTr("清除记忆")
                ghost: true
                enabled: veyra.hdrDisplayProfileCount > 0
                onClicked: veyra.forgetHdrDisplayProfiles()
            }
        }
        VRow {
            hintBelow: true
            label: qsTr("记住每部影片")
            hint: veyra.hdrTitleCount > 0
                ? qsTr("已记住 %1 部影片，下次播放自动套用").arg(veyra.hdrTitleCount)
                : qsTr("改动曲线后按影片记住，下次自动套用")
            value: veyra.hdrTitleMemory ? qsTr("已开启") : qsTr("已关闭")
            VSwitch {
                objectName: "list-hdrcurve-titlememory"
                checked: veyra.hdrTitleMemory
                onToggled: checked => veyra.hdrTitleMemory = checked
            }
        }
        VRow {
            hintBelow: true
            label: qsTr("开播自动选档")
            hint: qsTr("打开片子时按片源自动选一次；有影片记忆时以记忆为准")
            value: veyra.hdrAutoRecommend ? qsTr("已开启") : qsTr("已关闭")
            VSwitch {
                objectName: "list-hdrcurve-autorecommend"
                checked: veyra.hdrAutoRecommend
                onToggled: checked => veyra.hdrAutoRecommend = checked
            }
        }
        VRow {
            hintBelow: true
            label: qsTr("清除影片记忆")
            hint: qsTr("只清影片记忆，不动显示设备记忆")
            VButton {
                objectName: "list-hdrcurve-forget-titles"
                text: qsTr("清除")
                ghost: true
                enabled: veyra.hdrTitleCount > 0
                onClicked: veyra.forgetHdrTitles()
            }
        }
        }
    }

    VAccordion {
        objectName: "list-hdr-metadata"
        Layout.fillWidth: true
        Layout.topMargin: 12
        glyph: "sun"
        hue: "#7FB2F5"
        title: qsTr("HDR 元数据")
        summary: veyra.hdrMetadataEnabled ? qsTr("已启用（HDR10）") : qsTr("已关闭")
        on: veyra.hdrMetadataEnabled
        onToggled: on => veyra.hdrMetadataEnabled = on
        Text {
            Layout.fillWidth: true
            text: qsTr("HDR10 提示；系统或显示器可能忽略，让它的动态色调映射有依据。")
                + qsTr("曲线生效时会约束内容亮度提示；SDR 输入默认不发送，手动填写除外。")
            wrapMode: Text.WordWrap
            color: Theme.t3
            font.family: Theme.fontUi; font.pixelSize: 11
        }
        Repeater {
            model: [{key:"masteringPeakNits",label:qsTr("母版峰值 (nit)"),from:0,to:4000,def:0,hint:qsTr("0 = 跟随片源；实际效果取决于显示设备")},
                    {key:"maxCllNits",label:qsTr("MaxCLL (nit)"),from:0,to:4000,def:0,hint:qsTr("0 = 跟随片源")},
                    {key:"maxFallNits",label:qsTr("MaxFALL (nit)"),from:0,to:4000,def:0,hint:qsTr("0 = 跟随片源")}]
            delegate: VRow {
                required property var modelData
                label: modelData.label
                hint: modelData.hint || ""
                value: String(veyra.hdrMetadataParams[modelData.key] ?? "—")
                VSlider {
                    objectName: "list-hdrmeta-" + modelData.key
                    implicitWidth: 120
                    valueFromModel: true
                    resettable: true; defaultValue: modelData.def
                    from: modelData.from; to: modelData.to
                    value: veyra.hdrMetadataParams[modelData.key] ?? modelData.def
                    onMoved: value => veyra.setHdrMetadataParameter(modelData.key, Math.round(value))
                }
            }
        }
    }
    }
    VGroup {
        VRow {
            label: qsTr("提交帧率")
            value: veyra.submitFpsKnown ? veyra.submitFps.toFixed(2) : qsTr("未测量")
        }
        VRow {
            label: qsTr("1% low")
            hint: qsTr("最近 10 秒里最慢的 1% 提交间隔折算的帧率，和 RTSS 等叠加工具的算法相同")
            value: veyra.submitLowFpsKnown ? veyra.submitLowFps.toFixed(2) : "—"
        }
        VRow {
            label: qsTr("晚点 P95")
            value: veyra.lateP95Ms.toFixed(2) + " ms"
        }
        VRow {
            label: qsTr("调度等待 P95")
            value: veyra.scheduleP95Ms.toFixed(2) + " ms"
        }
    }
}
