// QML bridge: FrameGeneration responsibilities.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
// --- frame generation --------------------------------------------------------
QString QmlPlayerBridge::fgBackendName() const {
    switch(settings().frameGenerationBackend){
    case engine::FrameGenerationBackend::Dlss:return QStringLiteral("dlss");
    case engine::FrameGenerationBackend::XeSS:return QStringLiteral("xess");
    case engine::FrameGenerationBackend::Fsr:return QStringLiteral("fsr3");
    case engine::FrameGenerationBackend::Fsr4:return QStringLiteral("fsr4");
    case engine::FrameGenerationBackend::Vfg:return QStringLiteral("vfg");
    case engine::FrameGenerationBackend::Anr:return QStringLiteral("anr");
    }
    return {};
}
QVariantList QmlPlayerBridge::fgBackendChoices() const {
    QVariantList choices = {
        QVariantMap{{"id","anr"},{"label",tr("ANR FG（极差质量） · 本地实验")},{"danger",true}},
        QVariantMap{{"id","dlss"},{"label",tr("DLSS 帧生成")}},
        QVariantMap{{"id","xess"},{"label",tr("Intel XeSS · 实验")}},
        QVariantMap{{"id","fsr3"},{"label",tr("FSR 3.1 · 2X")}},
        QVariantMap{{"id","fsr4"},{"label",tr("FSR 4 ML · RX 9000 · 实验")}},
        QVariantMap{{"id","vfg"},{"label",tr("NVIDIA VFG 👑（顶级补帧质量） · RTX 30（实验）/ 40 / 50")},{"gold",true}}
    };
    const auto caps = effectCapabilities();
    for (auto& choice : choices) {
        auto row = choice.toMap();
        const auto cap = caps.value(row.value("id").toString()).toMap();
        row["disabled"] = !cap.value("available").toBool();
        row["note"] = cap.value("reason");
        choice = row;
    }
    return choices;
}
QString QmlPlayerBridge::fgProviderText() const {
    if(fgBackendName()==QLatin1String("anr"))return tr("Apex Neural Rendering · 2X～6X · 视频估算输入，效果与速度以实际运行结果为准");
    const auto& s=impl_->snapshot;
    if(fgBackendName()==QLatin1String("vfg")){
        if(!vfgHardwarePossible())return tr("VFG 需要 NVIDIA RTX 30 / 40 / 50（30 系为实验兼容）");
        if(!effectCapabilities().value("vfg").toMap().value("available").toBool())return tr("未找到 VFG 运行组件，请在组件页核对路径");
        if(effectGpu().ampere)return tr("RTX 30 VFG 为实验兼容，复用现有模型；支持 2X～8X，实际画质与速度需实卡验证");
        return tr("VFG 支持 2X～8X 和低 / 中 / 高质量；6X～8X 为实验档，高倍高质量会增加处理时间");
    }
    if(engine::fsrFrameGeneration(s.applied.frameGenerationBackend)&&s.applied.multiplier>1&&!s.fsrProviderVersion.empty())
        return tr("实际运行：FSR %1 · 2X").arg(QString::fromStdString(s.fsrProviderVersion));
    if(fgBackendName()==QLatin1String("fsr4"))return tr("FSR 4 ML 需要支持的 RX 9000 和驱动；实卡待验");
    if(fgBackendName()==QLatin1String("fsr3"))return tr("FSR 3.1 跨厂商补帧 · 最高 2X");
    if(fgBackendName()==QLatin1String("xess"))
        return fgMaxMultiplier()<4?tr("当前 XeSS 模块未匹配 3X / 4X 解锁版本；2X 可用，请在组件页核对版本"):
            tr("2X 为原生路径，3X / 4X 为实验解锁；失败原因见诊断");
    return tr("NVIDIA RTX 补帧；倍率以运行库实际能力为准");
}
// XeSS ceiling (SettingsWindow): 4X with the audited unlock provider, else 2X,
// or a running session's reported interpolation count above 2X.
int xessCeiling(const engine::PlayerSnapshot& s) {
    static const bool audited = gfx::XessMfgUnlock::providerIsAudited(
        (runtime::localDataDirectory() / L"intel" / L"experimental" / L"libxess_fg.dll").wstring());
    int cap = audited ? 4 : 2;
    // A running context reports the count it was CREATED with (3X -> 2), not
    // what the provider allows: it may raise the ceiling, never lower it.
    // Lowering it made 4X vanish after picking 3X until the backend was reset.
    // The highest ceiling any context has reported this run stays offered:
    // a later context created at 2X reports 1 and must not hide 3X/4X again.
    static int seen = 0;
    if (s.xessMaxInterpolatedFrames > 1) seen = std::max(seen, std::clamp(s.xessMaxInterpolatedFrames + 1, 2, 4));
    return std::max(cap, seen);
}
void QmlPlayerBridge::setFgBackendName(const QString& value) {
    if(value!=QLatin1String("anr")&&value!=QLatin1String("dlss")&&value!=QLatin1String("xess")&&value!=QLatin1String("fsr3")&&value!=QLatin1String("fsr4")&&value!=QLatin1String("vfg")){emit notice(tr("未知补帧后端"),true);return;}
    const auto availability = effectCapabilities().value(value).toMap();
    if (!availability.value("available").toBool()) { emit notice(availability.value("reason").toString(), true); emit settingsChanged(); return; }
    auto s = settings();
    const auto want = value == QLatin1String("anr") ? engine::FrameGenerationBackend::Anr
                    : value == QLatin1String("xess") ? engine::FrameGenerationBackend::XeSS
                    : value == QLatin1String("fsr3") ? engine::FrameGenerationBackend::Fsr
                    : value == QLatin1String("fsr4") ? engine::FrameGenerationBackend::Fsr4
                    : value == QLatin1String("vfg") ? engine::FrameGenerationBackend::Vfg
                    : engine::FrameGenerationBackend::Dlss;
    if (s.frameGenerationBackend == want) return;
    if(want==engine::FrameGenerationBackend::Vfg&&(!vfgHardwarePossible()||!pipeline::VfgBackend::runtimeAvailable(runtime::localRuntimeDirectory().wstring()))){
        emit notice(!vfgHardwarePossible()?tr("VFG 需要 NVIDIA RTX 30 / 40 / 50（30 系为实验兼容）"):tr("未找到 VFG 运行组件，请在组件页核对路径"),true);emit settingsChanged();return;
    }
    if (want == engine::FrameGenerationBackend::Fsr4 && !fsr4Possible()) {
        // Keep the previous choice; the menu snaps back to it.
        emit notice(tr("FSR 4 ML 补帧只支持 AMD RX 9000 显卡，当前显卡不能使用，已保持原来的补帧方式"), true);
        emit settingsChanged();
        return;
    }
    s.frameGenerationBackend = want;
    // A DLSS multiplier above XeSS's ceiling used to make the switch fail
    // silently in effect (the backend stayed DLSS). Clamp to the ceiling instead.
    auto& chain = impl_->chain;
    const auto beforeMultiplier = chain.fgMultiplier;
    const int cap = want == engine::FrameGenerationBackend::XeSS ? xessCeiling(impl_->snapshot)
                  : engine::fsrFrameGeneration(want) ? 2 : want==engine::FrameGenerationBackend::Vfg?8:6;
    chain.fgMultiplier=normalizeFgMultiplier(chain.fgMultiplier,want,uint32_t(cap));
    const bool clamped = chain.fgMultiplier != beforeMultiplier;
    if(!impl_->commit(s)){
        chain.fgMultiplier = beforeMultiplier;
        emit notice(tr("补帧后端切换未被接受，原设置保留"),true);return;
    }
    if (clamped) emit notice(tr("所选补帧后端的倍率已调整为 %1X").arg(chain.fgMultiplier), false);
    emit settingsChanged();
    emitChainChanged();
}

int QmlPlayerBridge::fgMaxMultiplier() const {
    // AppShell/SettingsWindow rules. Capabilities report GENERATED frames, so the
    // multiplier ceiling is that count + 1 (DLSS MultiFrameCountMax 5 = 6X on
    // Blackwell, 1 = 2X on Ada). Unknown DLSS capability offers the full list; a
    // new context validates it. XeSS: 4X with the audited unlock provider, else
    // 2X, or what a running session already reported above 2X.
    const auto& s = impl_->snapshot;
    if(settings().frameGenerationBackend==engine::FrameGenerationBackend::Anr)
        return s.applied.frameGenerationBackend==engine::FrameGenerationBackend::Anr&&s.fgCapabilityKnown&&s.fgMultiFrameMax>0?std::clamp(s.fgMultiFrameMax+1,2,6):6;
    if(settings().frameGenerationBackend==engine::FrameGenerationBackend::Vfg)return 8;
    if(engine::fsrFrameGeneration(settings().frameGenerationBackend))return 2;
    if (settings().frameGenerationBackend == engine::FrameGenerationBackend::XeSS)
        return xessCeiling(s);
    if (s.fgCapabilityKnown && s.fgMultiFrameMax > 0) return std::clamp(s.fgMultiFrameMax + 1, 2, 6);
    return 6;
}

QVariantList QmlPlayerBridge::fgMultiplierChoices() const {
    // Only multipliers the chosen backend supports are offered: DLSS reaches 6X on
    // capable hardware, XeSS stops at 4X. Offering a value the provider would
    // refuse is the same lie as a dead control.
    const bool xess = settings().frameGenerationBackend
                      == engine::FrameGenerationBackend::XeSS;
    const int ceiling = xess ? std::min(fgMaxMultiplier(), 4) : fgMaxMultiplier();
    QVariantList out;
    const auto choices=settings().frameGenerationBackend==engine::FrameGenerationBackend::Vfg?
        std::span<const uint32_t>(engine::kVfgMultiplierChoices):settings().frameGenerationBackend==engine::FrameGenerationBackend::Anr?std::span<const uint32_t>(engine::kAnrMultiplierChoices):std::span<const uint32_t>(engine::kFgMultiplierChoices);
    for (const auto choice : choices) {
        const int m = int(choice);
        if (m < 2 || m > ceiling) continue;
        QVariantMap item;
        item["id"] = QString::number(m);
        item["label"] = QString::number(m) + QStringLiteral("X");
        out << item;
    }
    return out;
}

int QmlPlayerBridge::vfgQuality()const{return int(settings().vfgQuality);}
void QmlPlayerBridge::setVfgQuality(int value){
    if(value<0||uint32_t(value)>=engine::kVfgQualityCount)return;auto s=settings();if(s.vfgQuality==uint32_t(value))return;
    s.vfgQuality=uint32_t(value);
    if(!impl_->commit(s)){emit notice(tr("VFG 质量设置未被接受，原设置保留"),true);return;}
    emit settingsChanged();emitChainChanged();
}

bool QmlPlayerBridge::fgStrict() const { return impl_->chain.fgStrictAdmission; }
void QmlPlayerBridge::setFgStrict(bool value) {
    if (impl_->chain.fgStrictAdmission == value) return;
    impl_->chain.fgStrictAdmission = value;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        impl_->chain.fgStrictAdmission = !value; impl_->revalidate();
        emit notice(uiText(error), true);
    }
    emit settingsChanged();
    emitChainChanged();
}

// 低延迟队列 is the presentation queue (1.4.4 PresentationSettings.enabled),
// not EnhancementSettings::lowLatency, which means "NR before SR".
bool QmlPlayerBridge::fgLowQueue() const { return impl_->presentation.enabled; }
void QmlPlayerBridge::setFgLowQueue(bool value) {
    if (impl_->presentation.enabled == value) return;
    impl_->presentation.enabled = value;
    if(nvidiaLowLatencyAvailable())impl_->presentation.nvidiaLowLatency=value?engine::NvidiaLowLatency::On:engine::NvidiaLowLatency::Off;
    applyPresentation();
}
int QmlPlayerBridge::nvidiaLowLatency()const{return int(impl_->presentation.nvidiaLowLatency);}
bool QmlPlayerBridge::nvidiaLowLatencyAvailable()const{return primaryGpuVendor()==0x10de;}
void QmlPlayerBridge::setNvidiaLowLatency(int value){
    const bool nvidia=nvidiaLowLatencyAvailable();
    if(value<0||value>2||(!nvidia&&value!=0))return;
    // Legacy UI value 2 migrates to On; no UI/API request can enable Boost.
    auto p=impl_->presentation;p.nvidiaLowLatency=engine::savedNvidiaLowLatency(value);
    engine::syncNvidiaLowLatencyQueue(p,nvidia);
    if(p==impl_->presentation)return;
    impl_->presentation=p;applyPresentation();
}

// --- design gap A1-A5: flow, cadence, presentation ----------------------------
int QmlPlayerBridge::hdrOutputMode()const{return int(settings().hdrOutputMode);}
void QmlPlayerBridge::setHdrOutputMode(int value){if(value<0||value>1)return;auto s=settings();if(int(s.hdrOutputMode)==value)return;s.hdrOutputMode=engine::HdrOutputMode(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::fgMotionSource()const{return engine::motionUsesFlow(settings().fgMotion,settings().frameGenerationBackend)?1:0;}
void QmlPlayerBridge::setFgMotionSource(int value){if(value<0||value>1)return;auto s=settings();s.fgMotion=engine::MotionSource(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::srMotionSource()const{return engine::motionUsesFlow(settings().srMotion)?1:0;}
void QmlPlayerBridge::setSrMotionSource(int value){if(value<0||value>1)return;auto s=settings();s.srMotion=engine::MotionSource(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::nrMotionSource()const{return engine::motionUsesFlow(settings().nrMotion)?1:0;}
void QmlPlayerBridge::setNrMotionSource(int value){if(value<0||value>1)return;auto s=settings();s.nrMotion=engine::MotionSource(value);if(impl_->commit(std::move(s)))emit settingsChanged();}
int QmlPlayerBridge::flowQuality() const { return int(settings().flow); }
void QmlPlayerBridge::setFlowQuality(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings();
    if (int(s.flow) == value) return;
    s.flow = static_cast<engine::FlowQuality>(value);
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受运动估算质量，原设置保留"), true); return; }
    emit settingsChanged();
}
bool QmlPlayerBridge::amdFlowHalf() const { return settings().amdFlowHalfResolution; }
void QmlPlayerBridge::setAmdFlowHalf(bool value) {
    auto s = settings();
    if (s.amdFlowHalfResolution == value) return;
    s.amdFlowHalfResolution = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受 AMD 性能档，原设置保留"), true); return; }
    emit settingsChanged();
}
bool QmlPlayerBridge::edgeAegis() const { return settings().fgMotionRefine; }
void QmlPlayerBridge::setEdgeAegis(bool value) {
    auto s = settings();
    if (s.fgMotionRefine == value) return;
    s.fgMotionRefine = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受边缘神盾，原设置保留"), true); return; }
    impl_->prefs[QStringLiteral("edgeAegis")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "edge aegis not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::dlssgLite() const { return settings().dlssgLite; }
void QmlPlayerBridge::setDlssgLite(bool value) {
    auto s = settings();
    if (s.dlssgLite == value) return;
    s.dlssgLite = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受 DLSS 性能模式，原设置保留"), true); return; }
    impl_->prefs[QStringLiteral("dlssgPerformance")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "DLSS performance mode not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::vfgAutoRegulate() const { return settings().vfgAutoRegulate; }
void QmlPlayerBridge::setVfgAutoRegulate(bool value) {
    auto s = settings();
    if (s.vfgAutoRegulate == value) return;
    s.vfgAutoRegulate = value;
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受 VFG 自动调控，原设置保留"), true); return; }
    impl_->prefs[QStringLiteral("vfgAutoRegulate")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "VFG auto regulation not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::dlssgLiteAvailable() const {
    static const bool present = ngx::dlssgLiteRuntimePresent(runtime::localRuntimeDirectory());
    return present;
}
QString QmlPlayerBridge::dlssgLiteHint() const {
    if (!dlssgLiteAvailable()) return tr("此软件包没有附带性能模式所需的 DLSS 帧生成 310.9.16 运行库");
    const auto& s = impl_->snapshot;
    if (s.running && s.dlssgLiteState == 3) return tr("目前只支持 RTX 50 系列：40/30 系的多帧解锁只适用于原版运行库");
    if (s.running && s.dlssgLiteState == 1) return tr("生效中：NVIDIA DLSS 帧生成性能模式（测试版）");
    return tr("NVIDIA 新增的性能模式（测试版）：补帧更省 GPU，高倍率更稳，画质可能略降；导出仍用标准模式");
}
int QmlPlayerBridge::contentRate() const { return int(engine::currentContentRate(settings().content)); }
void QmlPlayerBridge::setContentRate(int value) {
    if (value < 0 || value > int(engine::ContentRate::Capture60To30)) return;
    value=int(engine::currentContentRate(static_cast<engine::ContentRate>(value)));
    auto s = settings();
    if (int(s.content) == value) return;
    s.content = static_cast<engine::ContentRate>(value);
    if (!impl_->commit(std::move(s))) { emit notice(tr("引擎未接受去除重复帧设置，原设置保留"), true); return; }
    // Shared cadence stays in shell preferences and can be included in presets.
    impl_->prefs[QStringLiteral("contentRate")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "content cadence not saved");
    emit settingsChanged();
}

int QmlPlayerBridge::displaySync() const { return int(impl_->presentation.display); }
bool QmlPlayerBridge::fullscreenMemorySafe() const { return !impl_->prefBool("fullscreenMemoryProtection",false)||!impl_->snapshot.vramFullscreenUnsafe; }
void QmlPlayerBridge::setPresentationFullscreen(bool value) {
    if(impl_->presentation.fullscreen==value)return;
    impl_->presentation.fullscreen=value;
    impl_->engine.requestPresentation(impl_->presentation);
    emit presentationChanged();
    // QML's visibility notification runs inside Qt's fullscreen transition.
    // Let Qt finish its activation request before applying no-focus/through
    // flags and handing the foreground to the game. One queued transition,
    // without changing polling or presentation cadence.
    if(value&&gameMode()) QMetaObject::invokeMethod(this,[this]{
        if(impl_->presentation.fullscreen)updateGameMode();
    },Qt::QueuedConnection);
    else updateGameMode();
}
double QmlPlayerBridge::previewAspect() const {
    const int mode=impl_->screenFillActive?2:impl_->aspectMode;
    auto view=impl_->engine.previewView();
    view.mode=mode;
    view.rotationDegrees=impl_->snapshot.sourceRotationDegrees;
    view.displayAspect=impl_->snapshot.sourceDisplayAspect;
    const auto e=impl_->snapshot.metrics.resolution.output;
    if(mode==1&&e.height)return view.aspect(float(e.width),float(e.height));
    return view.aspect(float(impl_->snapshot.sourceWidth),float(impl_->snapshot.sourceHeight));
}
void QmlPlayerBridge::setDisplaySync(int value) {
    if (value < 0 || value > 2 || int(impl_->presentation.display) == value) return;
    impl_->presentation.display = static_cast<engine::DisplaySync>(value);
    applyPresentation();
}
bool QmlPlayerBridge::variableRefresh() const { return impl_->presentation.variableRefresh; }
void QmlPlayerBridge::setVariableRefresh(bool value) {
    if (impl_->presentation.variableRefresh == value) return;
    impl_->presentation.variableRefresh = value;
    // Turning it on reads the monitor again: the driver setting may have changed.
    if (value) impl_->engine.displayStates().invalidate("vrr-switch-on");
    applyPresentation();
}
int QmlPlayerBridge::vrrState() const { return impl_->vrrState; }
int QmlPlayerBridge::displayHdr() const { return impl_->displayHdr; }
double QmlPlayerBridge::displayRefreshHz() const { return impl_->displayRefreshHz; }
bool QmlPlayerBridge::displayChecking() const { return impl_->displayChecking; }
void QmlPlayerBridge::recheckDisplay() {
    // Every stored display state becomes stale; the next status update (or the
    // presenter's next loop) queries the shown monitor in the background.
    auto& i = *impl_;
    i.displayRecheckGeneration = i.engine.displayStates().invalidate("manual");
    i.displayRecheckPending = true;
    i.displayRecheckSince = std::chrono::steady_clock::now();
    if (!i.displayChecking) { i.displayChecking = true; emit perfChanged(); }
}
QString QmlPlayerBridge::vrrHint() const { return vrrHintText(impl_->vrrState); }
QString QmlPlayerBridge::vrrSummary() const { return vrrSummaryText(impl_->vrrState); }
// The 信号 card's short form of vrrHintText.
QString QmlPlayerBridge::vrrSummaryText(int level) const {
    switch (level) {
    case 0: return tr("无法检测");
    case 1: return tr("不支持");
    case 2: return tr("当前不可用");
    case 3: return impl_->presentation.variableRefresh && impl_->snapshot.running ? tr("已请求") : tr("可用");
    default: return tr("生效中");
    }
}
QString QmlPlayerBridge::vrrHintText(int level) const {
    // The 可变刷新 switch is available (not grey) from level 2: the monitor
    // can do VRR even if the driver setting is still off.
    const bool on = impl_->presentation.variableRefresh, running = impl_->snapshot.running;
    switch (level) {
    case 0: return tr("无法检测屏幕是否支持（目前只支持 NVIDIA 显卡）");
    case 1: return tr("当前屏幕不支持可变刷新");
    case 2: return tr("屏幕支持可变刷新，但当前显示模式不可用；请检查连接、刷新率及驱动设置");
    case 3:
        if (!on) return tr("屏幕跟着画面刷新：不撕裂，也几乎不用等刷新");
        return running ? tr("已请求可变刷新，驱动尚未确认生效；可检查窗口/全屏 G-SYNC 设置，当前状态不代表驱动已关闭")
                       : tr("已开启，将在播放时请求可变刷新；是否生效以驱动报告为准");
    default: return on ? tr("生效中：屏幕跟着画面刷新") : tr("屏幕正处于可变刷新（驱动已开启）");
    }
}
QString QmlPlayerBridge::fgDisplayHint() const { return impl_->fgDisplayHint; }
int QmlPlayerBridge::outputRateMode() const { return int(impl_->presentation.outputRate); }
void QmlPlayerBridge::setOutputRateMode(int value) {
    if (value < 0 || value > 2 || int(impl_->presentation.outputRate) == value) return;
    impl_->presentation.outputRate = static_cast<engine::OutputRateMode>(value);
    applyPresentation();
}
double QmlPlayerBridge::outputCustomFps() const { return impl_->presentation.customFps; }
void QmlPlayerBridge::setOutputCustomFps(double value) {
    if (!std::isfinite(value) || value < 1.0 || value > 1000.0) { emit notice(tr("输出上限需在 1–1000 fps 之间"), true); return; }
    if (std::abs(impl_->presentation.customFps - value) < 1e-6) return;
    impl_->presentation.customFps = value;
    applyPresentation();
}
bool QmlPlayerBridge::presentationOwned() const { return impl_->snapshot.presentationProviderOwned; }
QString QmlPlayerBridge::presentationStatus() const { return uiText(impl_->snapshot.presentationStatus); }
void QmlPlayerBridge::applyPresentation() {
    auto& p = impl_->presentation;
    if (!p.valid()) return;
    impl_->engine.requestPresentation(p);
    impl_->prefs[QStringLiteral("presentation")] = QVariantMap{
        {"lowQueue", p.enabled}, {"nvidiaLowLatency",int(p.nvidiaLowLatency)}, {"display", int(p.display)}, {"outputRate", int(p.outputRate)}, {"customFps", p.customFps}, {"vrr", p.variableRefresh}};
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "presentation settings not saved");
    veyra::log::info("ui-presentation", std::format("lowQueue={} nvidiaLowLatency={} display={} outputRate={} customFps={:.3f} variableRefresh={}",
        p.enabled, int(p.nvidiaLowLatency), int(p.display), int(p.outputRate), p.customFps, p.variableRefresh));
    emit presentationChanged();
    emit settingsChanged();
    emit perfChanged();
}


} // namespace veyra::ui
