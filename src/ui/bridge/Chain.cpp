// QML bridge: Chain responsibilities.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
// --- chain ------------------------------------------------------------------
// The settings the UI is showing: the facade's pending copy, which the engine
// has been asked to apply. One accessor so every property reads the same value.
engine::EnhancementSettings QmlPlayerBridge::settings() const {
    auto result = impl_->facade.pendingSettings();
    if (impl_->chain.mode == engine::ChainMode::Node) impl_->draftGlobals.apply(result);
    return result;
}

QVariantList QmlPlayerBridge::chain() const {
    QVariantList out;
    int nrOrdinal=0;
    const auto& c = impl_->chain;
    for (uint32_t i = 0; i < c.nodeCount; ++i) {
        const auto& node = c.nodes[i];
        const auto& info = engine::effectInfo(node.type);
        QVariantMap item;
        item["index"] = int(i);
        item["id"] = c.mode == engine::ChainMode::Node ? impl_->layout.ids[i] : i + 2;
        item["type"] = utf8Of(info.id);
        item["label"] = uiText(info.label);
        if(node.type==engine::EffectType::FrameGeneration)
            for(const auto& choice:fgBackendChoices()){
                const auto option=choice.toMap();
                if(option.value("id").toString()==fgBackendName())item["label"]=option.value("label");
            }
        if(node.type==engine::EffectType::NrEnhance)item["label"]=tr("NR 画面增强 · 第 %1 层").arg(++nrOrdinal);
        item["enabled"] = node.enabled;
        item["x"] = double(node.viewX);
        item["y"] = double(node.viewY);
        item["mustBeLast"] = info.mustBeLast;
        item["justBeforeLast"] = info.justBeforeLast;
        item["experimental"] = info.experimental;
        item["changesResolution"] = info.changesResolution;
        out << item;
    }
    return out;
}

QVariantList QmlPlayerBridge::nrLayers() const {
    QVariantList out;
    for(uint32_t i=0;i<impl_->chain.nodeCount;++i){const auto& node=impl_->chain.nodes[i];
        if(node.type!=engine::EffectType::NrEnhance)continue;
        const auto& n=node.nr;
        out<<QVariantMap{{"index",int(i)},{"enabled",node.enabled},{"sizePolicy",int(n.sizePolicy)},{"runtime",int(engine::currentNrRuntime(n.runtime))},
            {"manualSize",pipeline::manualNrSizePolicy(n.sizePolicy)},{"sizePercent",int(pipeline::nrSizePercent(n.sizePolicy))},
            {"intensity",n.model.intensity},{"tone",n.model.tone},{"structure",n.model.structure},
            {"skin",n.model.skin},{"style",n.model.style},{"autoMask",bool(n.model.autoMask)},
            {"uiCorrection",bool(n.model.uiCorrection)},{"performance",n.model.performance},{"total",n.residual.total},
            {"darken",n.residual.darken},{"brighten",n.residual.brighten},
            {"color",n.residual.color},{"luminance",n.residual.luminance},{"temporal",n.temporal},
            {"antiFlicker",int(n.antiFlicker)},
            {"correctionEnabled",n.residual.correction.enabled},{"correctionAuto",n.residual.correction.automatic},
            {"hueProtection",n.residual.correction.hue},{"chromaProtection",n.residual.correction.chroma},
            {"highlightProtection",n.residual.correction.highlight},{"localCompression",n.residual.correction.compression},
            {"temporalStability",n.residual.correction.stability},
            {"neutralProtection",n.residual.correction.neutral},{"colorRetention",n.residual.correction.colorKeep},
            {"luminanceRetention",n.residual.correction.lumaKeep},{"shadowProtection",n.residual.correction.shadow},
            {"correctionAmount",n.residual.correction.autoAmount}};
    }
    return out;
}

QString QmlPlayerBridge::nrAutoSelectionReason(int index) const {
    const auto cap=effectCapabilities().value("nrAuto").toMap();
    if(!cap.value("available").toBool())return cap.value("reason").toString();
    const auto& s=settings();const auto& snapshot=impl_->snapshot;
    int first=-1;unsigned count=0;bool temporal=false,otherAuto=false;
    for(uint32_t i=0;i<impl_->chain.nodeCount;++i){const auto& node=impl_->chain.nodes[i];
        if(node.type!=engine::EffectType::NrEnhance)continue;
        if(first<0)first=int(i);
        ++count;temporal|=node.enabled&&(node.nr.residual.correction.enabled?
            node.nr.residual.correction.usesHistory():node.nr.temporal);
        otherAuto|=int(i)!=index&&node.enabled&&node.nr.sizePolicy==pipeline::NrSizePolicy::Auto;
    }
    if(first!=index||count>2||otherAuto)return tr("自动 NR 仅支持一至两层列表的第一层");
    if(impl_->chain.mode!=engine::ChainMode::List||engine::currentNrRuntime(s.nrRuntime)!=engine::NrRuntime::Ampere||s.sr||s.multiplier>1||temporal||
       s.nrHoldStrength>0||s.color.enabled||s.additionalColorCount||s.antiAlias.enabled||s.additionalAntiAliasCount||s.beautyEnabled()||s.videoHdr.enabled||
       s.opticalFlowBackend!=engine::OpticalFlowBackend::Nvidia||s.nrMotion!=engine::MotionSource::OpticalFlow||snapshot.capture||snapshot.image||snapshot.sourceHdr||
       (snapshot.sourceWidth&& (snapshot.sourceWidth!=1920||snapshot.sourceHeight!=1080)))
        return tr("需 1080p SDR 文件、SF 和 NVIDIA 光流；关闭超分、补帧、抗闪烁、调色、美颜及 HDR");
    return {};
}

bool QmlPlayerBridge::setNrLayerParameter(int index,const QString& key,double value){
    if(index<0||uint32_t(index)>=impl_->chain.nodeCount||
       impl_->chain.nodes[index].type!=engine::EffectType::NrEnhance||!std::isfinite(value))return false;
    if(key=="runtime") {
        if(value!=0&&value!=1&&value!=2&&value!=3&&value!=4)return false;
        value=int(engine::currentNrRuntime(static_cast<engine::NrRuntime>(int(value))));
        const auto cap = effectCapabilities().value("nr" + QString::number(int(value))).toMap();
        if (!cap.value("available").toBool()) { emit notice(cap.value("reason").toString(), true); return false; }
        const auto before=impl_->chain;
        for(uint32_t i=0;i<impl_->chain.nodeCount;++i)
            if(impl_->chain.nodes[i].type==engine::EffectType::NrEnhance)
                impl_->chain.nodes[i].nr.runtime=static_cast<engine::NrRuntime>(int(value));
        const bool accepted=impl_->revalidate(true);
        if(!accepted){const auto error=impl_->validation.message;impl_->chain=before;impl_->revalidate();emit notice(uiText(error),true);}
        emit settingsChanged();emitChainChanged();return accepted;
    }
    auto& node=impl_->chain.nodes[index];const auto before=node;auto& n=node.nr;const bool wasValid=chainValid();
    if(key=="intensity")n.model.intensity=float(value);
    else if(key=="tone")n.model.tone=float(value);
    else if(key=="structure")n.model.structure=float(value);
    else if(key=="skin")n.model.skin=float(value);
    else if(key=="total")n.residual.total=float(value);
    else if(key=="darken")n.residual.darken=float(value);
    else if(key=="brighten")n.residual.brighten=float(value);
    else if(key=="color")n.residual.color=float(value);
    else if(key=="luminance")n.residual.luminance=float(value);
    else if(key=="hueProtection")n.residual.correction.hue=float(value);
    else if(key=="chromaProtection")n.residual.correction.chroma=float(value);
    else if(key=="highlightProtection")n.residual.correction.highlight=float(value);
    else if(key=="localCompression")n.residual.correction.compression=float(value);
    else if(key=="temporalStability")n.residual.correction.stability=float(value);
    else if(key=="neutralProtection")n.residual.correction.neutral=float(value);
    else if(key=="colorRetention")n.residual.correction.colorKeep=float(value);
    else if(key=="luminanceRetention")n.residual.correction.lumaKeep=float(value);
    else if(key=="shadowProtection")n.residual.correction.shadow=float(value);
    else if(key=="correctionAmount")n.residual.correction.autoAmount=float(value);
    else if(key=="sizePercent"){
        if(value!=std::floor(value)||value<pipeline::kNrMinimumPercent||value>pipeline::kNrMaximumPercent)return false;
        n.sizePolicy=pipeline::nrSizePolicyForPercent(unsigned(value));
    }
    else if(key=="sizePolicy"){
        if(value!=std::floor(value)||value<0||value>int(pipeline::NrSizePolicy::Manual))return false;
        const auto policy=static_cast<pipeline::NrSizePolicy>(int(value));
        if(!pipeline::validNrSizePolicy(policy))return false;
        if(policy==pipeline::NrSizePolicy::Auto){const auto reason=nrAutoSelectionReason(index);if(!reason.isEmpty()){emit notice(reason,true);return false;}}
        n.sizePolicy=policy;
    }
    else {
        if(value!=std::floor(value)||value<0||value>6)return false;
        if(key=="style"&&value<=2)n.model.style=int(value);
        else if(key=="autoMask"&&value<=1)n.model.autoMask=int(value);
        else if(key=="uiCorrection"&&value<=1)n.model.uiCorrection=int(value);
        else if(key=="performance"&&value<=1&&engine::nrUsesPerformanceModel(n.runtime))n.model.performance=value!=0;
        else if(key=="temporal"&&value<=1)n.temporal=value!=0;
        else if(key=="correctionEnabled"&&value<=1)n.residual.correction.enabled=value!=0;
        else if(key=="correctionAuto"&&value<=1)n.residual.correction.automatic=value!=0;
        else if(key=="correctionUseAuto"&&value==1)n.residual.correction.useAutomaticValues(n.model.style);
        else if(key=="antiFlicker"&&validNrAntiFlicker(engine::NrAntiFlicker(int(value))))n.antiFlicker=static_cast<engine::NrAntiFlicker>(int(value));
        else return false;
    }
    if(node==before)return true;
    const bool accepted=impl_->revalidate(true);
    if(!accepted){const auto error=impl_->validation.message;node=before;impl_->revalidate();emit notice(uiText(error),true);}
    emit settingsChanged();emitParameterChanged(&QmlPlayerBridge::nrLayersChanged,wasValid);return accepted;
}

QVariantMap QmlPlayerBridge::nrStabilizerState() const {
    const auto* node=impl_->chain.firstOf(engine::EffectType::NrStabilizer);
    return {{"enabled",node&&node->enabled},{"strength",node?node->holdStrength:.8f},
        {"tolerance",node?node->holdTolerance:.02f}};
}
bool QmlPlayerBridge::setStabilizerParameter(int index,const QString& key,double value) {
    if(index<0||uint32_t(index)>=impl_->chain.nodeCount||!std::isfinite(value)||value<0||value>1)return false;
    auto& node=impl_->chain.nodes[uint32_t(index)];
    if(node.type!=engine::EffectType::NrStabilizer)return false;
    const auto previous=node;
    const bool wasValid=chainValid();
    if(key==QLatin1String("strength"))node.holdStrength=float(value);
    else if(key==QLatin1String("tolerance"))node.holdTolerance=float(value);
    else return false;
    if(node==previous)return true;
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;node=previous;impl_->revalidate();
        emit notice(uiText(error),true);return false;
    }
    emit settingsChanged();emitParameterChanged(&QmlPlayerBridge::nrStabilizerChanged,wasValid);return true;
}

QVariantList QmlPlayerBridge::effectCatalog() const {
    QVariantList out;
    for (const auto& info : engine::effectCatalog()) {
        if (info.hidden||(impl_->chain.mode==engine::ChainMode::List&&info.type==engine::EffectType::NrStabilizer))continue;
        if (impl_->chain.mode == engine::ChainMode::Node && info.type == engine::EffectType::FrameGeneration) {
            for(const auto& choice : fgBackendChoices()) {
                const auto option=choice.toMap();const auto backend=option.value("id").toString();
                QVariantMap fg;
                fg["id"]=backend+QStringLiteral("-fg");
                fg["label"]=option.value("label");
                fg["maxInstances"]=1;fg["repeatable"]=false;fg["mustBeLast"]=true;
                fg["justBeforeLast"]=false;fg["experimental"]=backend!=QLatin1String("dlss");
                fg["disabled"]=option.value("disabled");fg["note"]=option.value("note");
                out<<fg;
            }
            continue;
        }
        QVariantMap item;
        item["id"] = utf8Of(info.id);
        item["label"] = uiText(info.label);
        item["maxInstances"] = int(info.maxInstances);
        item["repeatable"] = info.repeatable;
        item["mustBeLast"] = info.mustBeLast;
        item["justBeforeLast"] = info.justBeforeLast;
        item["experimental"] = info.experimental;
        auto cap = effectAvailability(utf8Of(info.id));
        if(info.type==engine::EffectType::FrameGeneration)
            for(const auto& choice:fgBackendChoices())if(!choice.toMap().value("disabled").toBool()){
                cap=QVariantMap{{"available",true},{"reason",QString{}}};break;
            }
        item["disabled"] = !cap.value("available").toBool();
        item["note"] = cap.value("reason");
        out << item;
    }
    return out;
}

QString QmlPlayerBridge::chainError() const { return uiText(impl_->validation.message); }
void QmlPlayerBridge::emitChainChanged() {
    // A pending drawing belongs to the old document. A mode/preset change or
    // node deletion must not commit it into another mask or recreate that node.
    if(!impl_->protDraw.isEmpty())beginProtectionDraw(QString{});
    emit chainChanged(); emit nrLayersChanged(); emit videoHdrParamsChanged();
    emit antiAliasChanged(); emit beautyChanged(); emit colourParamsChanged(); emit protectionChanged();
    emit nrStabilizerChanged();
}
// Every chainChanged re-evaluated all chain-bound bindings on every page (the pro
// and node pages stay loaded) and repainted the node wires: 65-80 ms of interface
// thread per 40 ms slider step with six effects (audit 2026-10-15).
void QmlPlayerBridge::emitParameterChanged(void (QmlPlayerBridge::*group)(), bool wasValid) {
    if (chainValid() != wasValid) { emitChainChanged(); return; }
    (this->*group)();
}
bool QmlPlayerBridge::chainValid() const { return impl_->validation.accepted; }
int QmlPlayerBridge::nodeMode() const { return impl_->chain.mode == engine::ChainMode::Node ? 1 : 0; }
void QmlPlayerBridge::setNodeMode(int mode) {
    if (mode < 0 || mode > 1) { emit notice(tr("无效编辑模式"), true); return; }
    const auto want = mode ? engine::ChainMode::Node : engine::ChainMode::List;
    if (impl_->chain.mode == want) return;
    const auto previousSession = impl_->currentSession();
    auto candidate = previousSession;
    const bool freshNode = want == engine::ChainMode::Node && !candidate.initialized[size_t(want)];
    if (!candidate.select(want)) { emit notice(tr("模式配置无效，未切换"), true); return; }
    if (freshNode) {
        // The first visit to the node editor starts from an empty graph (input
        // straight to output), not a copy of the list's effects.
        auto& fresh = candidate.configurations[size_t(want)];
        auto empty = std::make_unique<engine::EffectChain>();
        empty->mode = engine::ChainMode::Node;
        empty->fgMultiplier = fresh.chain.fgMultiplier;
        fresh.chain = *empty;
        fresh.editor.reset();
        fresh.selectedNr = fresh.selectedColour = -1;
    }
    const auto previousSettings = impl_->facade.pendingSettings();
    auto next = previousSettings;
    auto& configuration = candidate.configurations[size_t(want)];
    disableUnavailableEffects(configuration);
    configuration.apply(next);
    if (!next.validate().empty()) { emit notice(tr("模式参数无效，未切换"), true); return; }
    // Save before changing the live mode; an unwritable/corrupt file cannot
    // silently discard the configuration the user is about to leave.
    if (!impl_->facade.saveChainSession(candidate)) { emit notice(uiText(impl_->facade.error()), true); return; }
    if (!impl_->facade.applySettings(next,engine::runtimeOrder(configuration.chain))) {
        impl_->facade.setPending(previousSettings);
        if (!impl_->facade.saveChainSession(previousSession))
            emit notice(tr("引擎拒绝切换，存档回退失败：") + uiText(impl_->facade.error()), true);
        emit notice(tr("引擎未接受模式设置，当前配置不变"), true); return;
    }
    impl_->session = impl_->savedSession = candidate;
    impl_->restoreConfiguration(configuration);
    impl_->revalidate();
    impl_->options = engine::PlayerOptions::from(next,engine::runtimeOrder(impl_->activeRuntime()));
    emitChainChanged();
    emit settingsChanged();
}

QVariantList QmlPlayerBridge::recentFiles() const {
    QVariantList out;
    for (const auto& entry : impl_->facade.recentFiles()) {
        QVariantMap item;
        item["path"] = utf8Of(entry.path);
        item["label"] = utf8Of(entry.label);
        item["exists"] = entry.exists;
        out << item;
    }
    return out;
}

QVariantList QmlPlayerBridge::presets() const {
    QVariantList out;
    const auto& entries = impl_->facade.presets().entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        const auto& e = entries[i];
        QVariantMap item;
        item["index"] = int(i);
        item["name"] = utf8Of(e.name);
        item["note"] = uiText(e.note);
        item["builtin"] = e.builtin;
        item["nodeMode"] = e.kind == engine::ChainMode::Node;
        item["contents"] = int(e.contents);
        out << item;
    }
    return out;
}

QString QmlPlayerBridge::currentPresetName() const {
    // Matching is by name of the stored entry whose chain equals what the UI is
    // showing; anything else is honestly "自定义" rather than the nearest guess.
    const auto& entries = impl_->facade.presets().entries();
    for (const auto& e : entries) {
        if (e.chain == impl_->chain) return utf8Of(e.name);
    }
    return tr("自定义");
}

QVariantList QmlPlayerBridge::audioTracks() const {
    QVariantList out;
    for (const auto& track : impl_->snapshot.audioTracks) {
        // The track struct carries raw fields; the UI shows the ones that are
        // actually present rather than a synthesized name.
        QStringList parts;
        if (!track.language.empty()) parts << utf8Of(track.language);
        if (!track.title.empty()) parts << utf8Of(track.title);
        if (!track.codec.empty()) parts << utf8Of(track.codec);
        QVariantMap item;
        item["index"] = track.streamIndex;
        item["label"] = parts.isEmpty() ? tr("音轨 %1").arg(track.streamIndex) : parts.join(QStringLiteral(" · "));
        item["channels"] = int(track.channels);
        out << item;
    }
    return out;
}
int QmlPlayerBridge::selectedAudioTrack() const { return impl_->snapshot.selectedAudioTrack; }
void QmlPlayerBridge::setSelectedAudioTrack(int index) {
    impl_->engine.selectAudioTrack(impl_->snapshot.sessionId, index);
}
bool QmlPlayerBridge::hasCaptureSession() const { return impl_->facade.hasCaptureSession(); }

QString QmlPlayerBridge::captureSessionSummary() const {
    // Built from the recorded session only: the device, the format and the preset
    // that were actually used. A summary assembled from defaults would read as a
    // history that never happened.
    const auto& session = impl_->facade.lastCaptureSession();
    QStringList parts;
    // The device's friendly name (stored when the session started); the moniker
    // path itself is not something a person can read.
    const QString name = impl_->prefString("captureDeviceName");
    if (!name.isEmpty()) parts << name;
    else if (!session.devicePath.empty()) parts << QFileInfo(utf8Of(session.devicePath)).fileName();
    const QString formatLabel = impl_->prefString("captureFormatLabel");
    if (!formatLabel.isEmpty()) parts << formatLabel.section(QStringLiteral(" · "), 0, 1);
    else if (!session.formatKey.empty()) parts << utf8Of(session.formatKey);
    if (formatLabel.isEmpty() && session.fps > 0.01) parts << QStringLiteral("%1 fps").arg(session.fps, 0, 'f', 2);
    if (!session.presetName.empty()) parts << tr("预设「%1」").arg(utf8Of(session.presetName));
    return parts.join(QStringLiteral(" · "));
}

void QmlPlayerBridge::resumeCaptureSession() {
    // "Continue": the remembered device, format, audio and colour, re-checked
    // against what the machine reports now; nothing else is picked for the user.
    impl_->capturePrefs = ui::CapturePreferenceStore(impl_->dataDir).load();
    if (impl_->capturePrefs.videoPath.empty()) { emit notice(tr("还没有采集记录"), true); return; }
    impl_->captureDevice = impl_->capturePrefs.videoPath;
    impl_->captureColor = impl_->capturePrefs.colorForDevice(impl_->captureDevice);
    impl_->captureResumePending = true;
    queryCapture(2, impl_->captureDevice);
}

QString QmlPlayerBridge::colorStatus() const { return uiText(impl_->snapshot.colorStatus); }
QString QmlPlayerBridge::videoHdrStatus() const { return uiText(impl_->snapshot.videoHdrStatus); }

// --- chain editing ----------------------------------------------------------
// The validator is the authority: an edit that would put frame generation or
// RTX Video HDR where the pipeline cannot honour them is refused with a reason,
// not silently dropped.
int QmlPlayerBridge::addEffect(const QString& type) {
    // The generic list entry means "add an available FG stage". A saved,
    // disabled NVIDIA choice must not make FSR/XeSS impossible to add on AMD.
    if(type==QLatin1String("frame-generation")&&
       !effectAvailability(type).value("available").toBool()){
        for(const auto& choice:fgBackendChoices()){
            const auto row=choice.toMap();if(row.value("disabled").toBool())continue;
            return addEffect(row.value("id").toString()+QStringLiteral("-fg"));
        }
    }
    const auto cap = effectAvailability(type);
    if (!cap.value("available").toBool()) { emit notice(cap.value("reason").toString(), true); return -1; }
    const bool fgType=type==QLatin1String("dlss-fg")||type==QLatin1String("xess-fg")||
        type==QLatin1String("fsr3-fg")||type==QLatin1String("fsr4-fg")||type==QLatin1String("vfg-fg")||type==QLatin1String("anr-fg");
    const auto fgBackend=type==QLatin1String("anr-fg")?engine::FrameGenerationBackend::Anr:type==QLatin1String("vfg-fg")?engine::FrameGenerationBackend::Vfg:type==QLatin1String("fsr4-fg")?engine::FrameGenerationBackend::Fsr4:
        type==QLatin1String("fsr3-fg")?engine::FrameGenerationBackend::Fsr:
        type==QLatin1String("xess-fg")?engine::FrameGenerationBackend::XeSS:engine::FrameGenerationBackend::Dlss;
    const auto fgCeiling=engine::fsrFrameGeneration(fgBackend)?2u:
        fgBackend==engine::FrameGenerationBackend::XeSS?uint32_t(xessCeiling(impl_->snapshot)):fgBackend==engine::FrameGenerationBackend::Vfg?8u:6u;
    if(fgType&&fgBackend==engine::FrameGenerationBackend::Vfg&&(!vfgHardwarePossible()||!pipeline::VfgBackend::runtimeAvailable(runtime::localRuntimeDirectory().wstring()))){
        emit notice(!vfgHardwarePossible()?tr("VFG 需要 NVIDIA RTX 30 / 40 / 50（30 系为实验兼容）"):tr("未找到 VFG 运行组件，请在组件页核对路径"),true);return -1;
    }
    if (impl_->chain.mode == engine::ChainMode::Node) {
        const bool fg = fgType;
        const engine::EffectInfo* info = nullptr;
        for (const auto& item : engine::effectCatalog())
            if ((fg && item.type == engine::EffectType::FrameGeneration) || utf8Of(item.id) == type) info = &item;
        if (!info || info->hidden) {
            emit notice(tr("节点模式不支持该效果类型"), true); return -1;
        }
        auto& c = impl_->chain;
        auto before = std::make_unique<engine::EffectChain>(c);
        const auto oldLayout = impl_->layout; const auto previous = impl_->facade.pendingSettings();
        const auto oldGlobals = impl_->draftGlobals;
        auto pending = settings();
        if (fg){pending.frameGenerationBackend=fgBackend;c.fgMultiplier=normalizeFgMultiplier(c.fgMultiplier,fgBackend,fgCeiling);}
        int index = -1;
        if (fg) for (uint32_t i = 0; i < c.nodeCount; ++i)
            if (c.nodes[i].type == engine::EffectType::FrameGeneration) index = int(i);
        if (index < 0) {
            if (c.nodeCount >= engine::kMaxChainNodes || c.countOf(info->type) >= info->maxInstances ||
                impl_->layout.nextId == std::numeric_limits<uint32_t>::max()) {
                emit notice(tr("该类型节点已达上限（含禁用和未连接节点）"), true); return -1;
            }
            index = int(c.nodeCount++);
            auto& node = c.nodes[index]; node = engine::ChainNode{}; node.type = info->type;
            node.enabled = info->type != engine::EffectType::Beauty;   // beauty starts switched off
            node.viewX = float(index % 4) * 260; node.viewY = float(index / 4) * 300;
            if (info->type == engine::EffectType::NrEnhance) {
                node.nr.runtime = previous.nrRuntime; node.nr.lowLatencyPairing = previous.lowLatency;
            }
            const uint32_t id = impl_->layout.nextId++; impl_->layout.ids[index] = id; impl_->layout.next[index] = 0;
            auto projected = std::make_unique<engine::EffectChain>();
            if (oldLayout.project(*before, *projected).accepted) {
                uint32_t predecessor = 0, next = impl_->layout.inputNext;
                while (next > 1) {
                    const int at = impl_->layout.indexOf(c, next);
                    const auto t = c.nodes[at].type;
                    if (t == engine::EffectType::FrameGeneration ||
                        (info->type==engine::EffectType::NrEnhance&&
                         (t==engine::EffectType::Protection||t==engine::EffectType::NrStabilizer))||
                        (info->type!=engine::EffectType::FrameGeneration&&info->type!=engine::EffectType::Exclusion&&
                         (t==engine::EffectType::VideoHdr||t==engine::EffectType::Exclusion)))break;
                    predecessor = next; next = impl_->layout.next[at];
                }
                if(info->type==engine::EffectType::NrStabilizer||info->type==engine::EffectType::Protection){
                    uint32_t current=impl_->layout.inputNext;
                    while(current>1){
                        const auto at=impl_->layout.indexOf(c,current);
                        const auto type=c.nodes[size_t(at)].type;
                        if(type==engine::EffectType::NrEnhance||
                           (info->type==engine::EffectType::NrStabilizer&&type==engine::EffectType::Protection)){
                            predecessor=current;next=impl_->layout.next[size_t(at)];
                        }
                        current=impl_->layout.next[size_t(at)];
                    }
                }
                if (predecessor == 0) impl_->layout.inputNext = id;
                else impl_->layout.next[size_t(impl_->layout.indexOf(c, predecessor))] = id;
                impl_->layout.next[index] = next;
            }
        } else c.nodes[index].enabled = true;
        impl_->draftGlobals = engine::ChainGlobalSettings::capture(pending);
        if (!impl_->revalidate(true, &previous)) {
            const auto error = impl_->validation.message; c = *before; impl_->layout = oldLayout;
            impl_->draftGlobals = oldGlobals;
            impl_->facade.setPending(previous); impl_->revalidate(); emit notice(uiText(error), true); return -1;
        }
        if (info->type == engine::EffectType::NrEnhance) impl_->selectedNr = index;
        if (info->type == engine::EffectType::Color) impl_->selectedColour = index;
        emitChainChanged(); emit settingsChanged(); return index;
    }
    if(fgType) {
        const auto before=impl_->chain;const auto previous=settings();
        auto& c=impl_->chain;
        int slot=-1;
        for(uint32_t i=0;i<c.nodeCount;++i)if(c.nodes[i].type==engine::EffectType::FrameGeneration)slot=int(i);
        if(slot<0) {
            if(c.nodeCount>=engine::kMaxChainNodes){emit notice(tr("效果链已满"),true);return -1;}
            slot=int(c.nodeCount++);c.nodes[slot]={};c.nodes[slot].type=engine::EffectType::FrameGeneration;
            c.nodes[slot].viewX=float(slot%4)*260;c.nodes[slot].viewY=float(slot/4)*300;
        }
        c.nodes[slot].enabled=true;
        c.fgMultiplier=normalizeFgMultiplier(c.fgMultiplier,fgBackend,fgCeiling);
        auto next=previous;engine::fromChain(c,next);
        next.frameGenerationBackend=fgBackend;
        impl_->validation=engine::validateChain(c);
        if(!impl_->validation.accepted||!next.validate().empty()||
           !unavailableEffects(next).isEmpty()||
           !impl_->facade.applySettings(next,engine::runtimeOrder(c))) {
            c=before;impl_->facade.setPending(previous);impl_->validation=engine::validateChain(c);
            emit notice(tr("补帧选择未被接受，保留原设置；请检查倍率和后端能力"),true);return -1;
        }
        impl_->options=engine::PlayerOptions::from(next,engine::runtimeOrder(c));
        emit settingsChanged();emitChainChanged();return slot;
    }
    engine::EffectType wanted = engine::EffectType::NrEnhance;
    bool found = false;
    for (const auto& info : engine::effectCatalog()) {
        if (!info.hidden && utf8Of(info.id) == type) {
            wanted = info.type;
            found = true;
            break;
        }
    }
    if (!found) { emit notice(tr("未知的效果: %1").arg(type), true); return -1; }

    auto& c = impl_->chain;
    if (c.nodeCount >= engine::kMaxChainNodes) { emit notice(tr("效果链已满"), true); return -1; }
    const auto& info = engine::effectInfo(wanted);
    if (c.countOf(wanted) >= info.maxInstances) {
        emit notice(tr("%1 最多 %2 个").arg(uiText(info.label)).arg(info.maxInstances), true);
        return -1;
    }

    const auto before=c; const int selected=impl_->selectedNr,colourSelected=impl_->selectedColour;
    // Insert before a pinned tail so the validator never has to undo the edit:
    // frame generation stays last and RTX Video HDR stays in front of it.
    uint32_t insertAt = c.nodeCount;
    if (wanted != engine::EffectType::FrameGeneration) {
        for (uint32_t i = 0; i < c.nodeCount; ++i) {
            const auto& nodeInfo = engine::effectInfo(c.nodes[i].type);
            if (nodeInfo.mustBeLast||(wanted!=engine::EffectType::Exclusion&&c.nodes[i].type==engine::EffectType::Exclusion)||((wanted==engine::EffectType::Color||wanted==engine::EffectType::Beauty)&&c.nodes[i].type==engine::EffectType::VideoHdr)||
                (wanted==engine::EffectType::NrEnhance&&
                (c.nodes[i].type==engine::EffectType::Protection||c.nodes[i].type==engine::EffectType::NrStabilizer||c.nodes[i].type==engine::EffectType::VideoHdr))) { insertAt = i; break; }
        }
    }
    if(wanted==engine::EffectType::NrStabilizer)
        for(uint32_t i=0;i<c.nodeCount;++i)
            if(c.nodes[i].type==engine::EffectType::NrEnhance||c.nodes[i].type==engine::EffectType::Protection)insertAt=i+1;
    for (uint32_t i = c.nodeCount; i > insertAt; --i) c.nodes[i] = c.nodes[i - 1];
    if(impl_->selectedNr>=int(insertAt))++impl_->selectedNr;
    if(impl_->selectedColour>=int(insertAt))++impl_->selectedColour;
    c.nodes[insertAt] = engine::ChainNode{};
    c.nodes[insertAt].type = wanted;
    // A new list-mode NR layer arrives switched off: adding a layer must not
    // change the picture until the user turns it on.
    c.nodes[insertAt].enabled = !(wanted == engine::EffectType::NrEnhance && c.mode == engine::ChainMode::List);
    // A beauty layer arrives with its switch and both sub-functions off: adding one
    // never changes the picture (its presets turn it on).
    if (wanted == engine::EffectType::Beauty) c.nodes[insertAt].enabled = false;
    if(wanted==engine::EffectType::Color)impl_->selectedColour=int(insertAt);
    if(wanted==engine::EffectType::NrEnhance){
        c.nodes[insertAt].nr.runtime=settings().nrRuntime;
        c.nodes[insertAt].nr.lowLatencyPairing=settings().lowLatency;
        impl_->selectedNr=int(insertAt);
    }
    // A new node lands below the others in node mode, in a stable column.
    c.nodes[insertAt].viewX = 40.0f + float(insertAt % 4) * 260.0f;
    c.nodes[insertAt].viewY = 40.0f + float(insertAt / 4) * 200.0f;
    ++c.nodeCount;

    const bool live=c.mode==engine::ChainMode::Node||wanted==engine::EffectType::NrEnhance||wanted==engine::EffectType::Color;
    if(!impl_->revalidate(live)){
        const auto error=impl_->validation.message;c=before;impl_->selectedNr=selected;
        impl_->selectedColour=colourSelected;
        impl_->revalidate();emit notice(uiText(error),true);return -1;
    }
    emitChainChanged();
    if (!impl_->validation.accepted) {
        emit notice(uiText(impl_->validation.message), true);
    } else {
        emit settingsChanged();
    }
    return int(insertAt);
}

QVariantList QmlPlayerBridge::nodeConnections() const {
    QVariantList out;
    if (impl_->chain.mode != engine::ChainMode::Node) return out;
    const auto add = [&](uint32_t from, uint32_t to) {
        if (to != engine::NodeGraphLayout::Input)
            out << QVariantMap{{"from", from}, {"to", to}};
    };
    add(0, impl_->layout.inputNext);
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        add(impl_->layout.ids[i], impl_->layout.next[i]);
    return out;
}

int QmlPlayerBridge::nodeIndexForId(uint id) const {
    return impl_->chain.mode == engine::ChainMode::Node ? impl_->layout.indexOf(impl_->chain, id) : -1;
}

int QmlPlayerBridge::duplicateNode(uint id) {
    if (impl_->chain.mode != engine::ChainMode::Node) return -1;
    auto before = std::make_unique<engine::EffectChain>(impl_->chain);
    const auto layout = impl_->layout;
    uint32_t created = 0;
    const auto result = impl_->layout.duplicate(impl_->chain, id, created);
    if (!result.accepted) { emit notice(uiText(result.message), true); return -1; }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        impl_->chain = *before; impl_->layout = layout; impl_->revalidate();
        emit notice(uiText(error), true); return -1;
    }
    const int index = nodeIndexForId(created);
    if (impl_->chain.nodes[index].type == engine::EffectType::NrEnhance) impl_->selectedNr = index;
    if (impl_->chain.nodes[index].type == engine::EffectType::Color) impl_->selectedColour = index;
    emitChainChanged(); emit settingsChanged(); return index;
}

bool QmlPlayerBridge::resetNode(uint id) {
    const int index = nodeIndexForId(id);
    if (index < 0) return false;
    auto& node = impl_->chain.nodes[index];
    const auto before = node; const auto multiplier = impl_->chain.fgMultiplier;
    const auto strict = impl_->chain.fgStrictAdmission;
    const auto previous = impl_->facade.pendingSettings();
    const auto oldGlobals = impl_->draftGlobals;
    node = engine::ChainNode{};
    node.type = before.type; node.enabled = before.enabled;
    node.viewX = before.viewX; node.viewY = before.viewY;
    if (node.type == engine::EffectType::FrameGeneration) {
        impl_->chain.fgMultiplier = 2; impl_->chain.fgStrictAdmission = false;
    }
    if (node.type == engine::EffectType::SuperResolution) {
        const engine::EnhancementSettings defaults;
        impl_->draftGlobals.srTarget = defaults.srTarget;
        impl_->draftGlobals.videoSrQuality = defaults.videoSrQuality;
        impl_->draftGlobals.dlssSrModel = defaults.dlssSrModel;
        impl_->draftGlobals.srPrescale = defaults.srPrescale;
        impl_->draftGlobals.srOutput = defaults.srOutput;
        impl_->draftGlobals.dualSrEnabled=false;impl_->draftGlobals.dualSrTarget=defaults.dualSrTarget;impl_->draftGlobals.dualSrQuality=defaults.dualSrQuality;
    }
    if (!impl_->revalidate(true, &previous)) {
        const auto error = impl_->validation.message; node = before;
        impl_->draftGlobals = oldGlobals;
        impl_->facade.setPending(previous);
        impl_->chain.fgMultiplier = multiplier; impl_->chain.fgStrictAdmission = strict; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emitChainChanged(); emit settingsChanged(); return true;
}

bool QmlPlayerBridge::connectNodes(uint from, uint to) {
    if (impl_->chain.mode != engine::ChainMode::Node) return false;
    const auto before = impl_->layout;
    const auto result = impl_->layout.connect(impl_->chain, from, to);
    if (!result.accepted) { emit notice(uiText(result.message), true); return false; }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; impl_->layout = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emitChainChanged(); emit settingsChanged(); return true;
}

bool QmlPlayerBridge::disconnectNode(uint from) {
    if (impl_->chain.mode != engine::ChainMode::Node) return false;
    const auto before = impl_->layout;
    const auto result = impl_->layout.disconnect(impl_->chain, from);
    if (!result.accepted) { emit notice(uiText(result.message), true); return false; }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; impl_->layout = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emitChainChanged(); emit settingsChanged(); return true;
}

bool QmlPlayerBridge::insertNodeAfter(uint id, uint after) {
    if (impl_->chain.mode != engine::ChainMode::Node) return false;
    const auto before = impl_->layout;
    const auto result = impl_->layout.insertAfter(impl_->chain, id, after);
    if (!result.accepted) { emit notice(uiText(result.message), true); return false; }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; impl_->layout = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emitChainChanged(); emit settingsChanged(); return true;
}

int QmlPlayerBridge::duplicateNrLayer(int index){
    if (impl_->chain.mode == engine::ChainMode::Node)
        return index >= 0 && uint32_t(index) < impl_->chain.nodeCount ? duplicateNode(impl_->layout.ids[index]) : -1;
    auto& c=impl_->chain;
    if(index<0||uint32_t(index)>=c.nodeCount||c.nodes[index].type!=engine::EffectType::NrEnhance||
       c.countOf(engine::EffectType::NrEnhance)>=engine::kMaxNrInstances||c.nodeCount>=engine::kMaxChainNodes)return -1;
    const auto before=c; const int selected=impl_->selectedNr,colourSelected=impl_->selectedColour;
    const auto original=c.nodes[index];
    for(uint32_t i=c.nodeCount;i>uint32_t(index+1);--i)c.nodes[i]=c.nodes[i-1];
    c.nodes[index+1]=original;++c.nodeCount;impl_->selectedNr=index+1;
    if(impl_->selectedColour>index)++impl_->selectedColour;
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;c=before;impl_->selectedNr=selected;
        impl_->selectedColour=colourSelected;
        impl_->revalidate();emit notice(uiText(error),true);return -1;
    }
    emitChainChanged();emit settingsChanged();return index+1;
}

bool QmlPlayerBridge::removeEffect(int index) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount) return false;
    const auto& info = engine::effectInfo(c.nodes[index].type);
    if (info.mustBeLast) { emit notice(tr("补帧固定为最后一步，不能删除"), true); return false; }
    const auto before=c; const int selected=impl_->selectedNr,colourSelected=impl_->selectedColour;
    const auto previousLayout = impl_->layout;
    if (c.mode == engine::ChainMode::Node) {
        const auto result = impl_->layout.remove(c, impl_->layout.ids[index]);
        if (!result.accepted) { emit notice(uiText(result.message), true); return false; }
    } else {
        for (uint32_t i = uint32_t(index); i + 1 < c.nodeCount; ++i) c.nodes[i] = c.nodes[i + 1];
        --c.nodeCount;
    }
    if(impl_->selectedNr==index)impl_->selectedNr=-1;
    else if(impl_->selectedNr>index)--impl_->selectedNr;
    if(impl_->selectedColour==index)impl_->selectedColour=-1;
    else if(impl_->selectedColour>index)--impl_->selectedColour;
    // All removals must update the running graph, including list-mode beauty.
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;c=before;impl_->layout=previousLayout;impl_->selectedNr=selected;
        impl_->selectedColour=colourSelected;
        impl_->revalidate();emit notice(uiText(error),true);return false;
    }
    emitChainChanged();
    emit settingsChanged();
    return true;
}

bool QmlPlayerBridge::moveEffect(int from, int to) {
    auto& c = impl_->chain;
    if (from < 0 || to < 0 || uint32_t(from) >= c.nodeCount || uint32_t(to) >= c.nodeCount) return false;
    if (c.mode == engine::ChainMode::Node) {
        if (from == to) return true;
        return insertNodeAfter(impl_->layout.ids[from], impl_->layout.ids[to]);
    }
    // The two pinned stages cannot be dragged; the UI greys them, and the
    // engine refuses them too so a script cannot route around the UI.
    if (engine::effectInfo(c.nodes[from].type).mustBeLast ||
        engine::effectInfo(c.nodes[to].type).mustBeLast) {
        emit notice(tr("补帧固定为最后一步，不能拖动"), true);
        return false;
    }
    const auto before = c;
    const auto moving = c.nodes[from];
    if (from < to) for (int i = from; i < to; ++i) c.nodes[i] = c.nodes[i + 1];
    else for (int i = from; i > to; --i) c.nodes[i] = c.nodes[i - 1];
    c.nodes[to] = moving;

    impl_->revalidate(true);
    if (!impl_->validation.accepted) {
        const auto error=impl_->validation.message;
        impl_->chain = before;
        impl_->revalidate();
        emit notice(uiText(error), true);
        return false;
    }
    const auto remap=[&](int i){
        if(i==from)return to;
        if(from<to&&i>from&&i<=to)return i-1;
        if(from>to&&i>=to&&i<from)return i+1;
        return i;
    };
    impl_->selectedNr=remap(impl_->selectedNr);
    impl_->selectedColour=remap(impl_->selectedColour);
    emitChainChanged();
    emit settingsChanged();
    return true;
}

bool QmlPlayerBridge::setEffectEnabled(int index, bool enabled) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount) return false;
    if (c.nodes[index].enabled == enabled) return true;
    c.nodes[index].enabled = enabled;
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;c.nodes[index].enabled=!enabled;
        impl_->revalidate();emit notice(uiText(error),true);return false;
    }
    emitChainChanged();
    emit settingsChanged();
    return true;
}

bool QmlPlayerBridge::nrAnyEnabled() const {
    const auto& c=impl_->chain;
    for(uint32_t i=0;i<c.nodeCount;++i)if(c.nodes[i].type==engine::EffectType::NrEnhance&&c.nodes[i].enabled)return true;
    return false;
}
bool QmlPlayerBridge::setAllNrEnabled(bool enabled) {
    auto& c=impl_->chain;const auto before=c;
    std::vector<uint32_t> layers;
    for(uint32_t i=0;i<c.nodeCount;++i)if(c.nodes[i].type==engine::EffectType::NrEnhance)layers.push_back(i);
    if(layers.empty())return false;
    auto& restore=impl_->nrMasterRestore;
    if(!enabled){
        restore.clear();
        for(const auto i:layers){restore.push_back(c.nodes[i].enabled);c.nodes[i].enabled=false;}
    }else{
        // Layers added or removed since the switch went off: no faithful restore,
        // so every layer comes back on. Same when nothing was on to begin with.
        const bool faithful=restore.size()==layers.size()&&std::find(restore.begin(),restore.end(),true)!=restore.end();
        for(size_t k=0;k<layers.size();++k)c.nodes[layers[k]].enabled=faithful?bool(restore[k]):true;
    }
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;c=before;impl_->revalidate();
        emit notice(uiText(error),true);emitChainChanged();return false;
    }
    veyra::log::info("ui-nr",std::format("master switch {} layers={}",enabled?"on":"off",layers.size()));
    emitChainChanged();emit settingsChanged();
    return true;
}

QVariantMap QmlPlayerBridge::nodeAnchors() const { return impl_->anchors; }
bool QmlPlayerBridge::setNodeAnchor(const QString& key, double x, double y) {
    if ((key != QLatin1String("input") && key != QLatin1String("flow") && key != QLatin1String("output")) ||
        !std::isfinite(x) || !std::isfinite(y) || std::abs(x) > 1000000 || std::abs(y) > 1000000) return false;
    impl_->anchors[key] = QVariantMap{{"x", x}, {"y", y}};
    if (!impl_->anchorsFile.empty()) {
        QFile file(QString::fromStdWString(impl_->anchorsFile.wstring()));
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
            file.write(QJsonDocument(QJsonObject::fromVariantMap(impl_->anchors)).toJson(QJsonDocument::Compact));
        else veyra::log::warn("ui-node", "node anchor positions not saved");
    }
    emit nodeAnchorsChanged();
    return true;
}
bool QmlPlayerBridge::setEffectPosition(int index, double x, double y) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || !std::isfinite(x) || !std::isfinite(y) ||
        std::abs(x) > 1000000 || std::abs(y) > 1000000) return false;
    c.nodes[index].viewX = float(x);
    c.nodes[index].viewY = float(y);
    // Positions never affect the picture, so this deliberately does not
    // revalidate or touch the engine: dragging a node must not cost a rebuild.
    emitChainChanged();
    return true;
}

QVariantMap QmlPlayerBridge::effectDescriptor(const QString& type) const {
    for (const auto& info : engine::effectCatalog()) {
        if (utf8Of(info.id) != type) continue;
        QVariantMap out;
        out["id"] = utf8Of(info.id);
        out["label"] = uiText(info.label);
        out["maxInstances"] = int(info.maxInstances);
        out["repeatable"] = info.repeatable;
        out["mustBeLast"] = info.mustBeLast;
        out["justBeforeLast"] = info.justBeforeLast;
        out["changesResolution"] = info.changesResolution;
        out["experimental"] = info.experimental;
        return out;
    }
    return {};
}

// --- presets ----------------------------------------------------------------
bool QmlPlayerBridge::applyPresetIndex(int index) {
    if (index < 0 || size_t(index) >= impl_->facade.presets().entries().size()) return false;
    const auto mode = impl_->chain.mode;
    const auto& entry = impl_->facade.presets().entries()[size_t(index)];
    if (entry.kind != mode) { emit notice(tr("请先切换到预设对应的编辑模式"), true); return false; }
    const auto previous = impl_->facade.pendingSettings();
    const auto oldGlobals = impl_->draftGlobals;
    auto nextGlobals = oldGlobals;
    auto settings = previous;
    auto candidate = impl_->chain;
    const auto oldLayout = impl_->layout;
    auto nextLayout = oldLayout;
    engine::ChainValidation validation;
    if (mode == engine::ChainMode::Node) {
        auto document = std::make_unique<engine::NodeEditorDocument>();
        document->nodes = candidate; document->layout = nextLayout; document->globals = oldGlobals;
        validation = engine::PresetLibrary::applyToEditor(entry, *document, settings);
        candidate = document->nodes; nextLayout = document->layout;
        nextGlobals = document->globals.value_or(engine::ChainGlobalSettings::capture(settings));
        // Keep the facade on accepted values until revalidate projects the new
        // document. Audio/non-chain preset fields are retained in this copy.
        engine::fromChain(impl_->activeRuntime(), settings);
        // Custom: the static HDR output tuning rides in the globals block this call
        // re-derives, so preserve the values this commit is carrying.
        const auto keepCurve = settings.hdrCurve;
        const auto keepMetadata = settings.hdrMetadata;
        engine::ChainGlobalSettings::capture(previous).apply(settings);
        settings.hdrCurve = keepCurve; settings.hdrMetadata = keepMetadata;
    } else validation = engine::PresetLibrary::applyToChain(entry, candidate, settings);
    if (!validation.accepted) { emit notice(uiText(validation.message), true); return false; }
    if (const auto error = settings.validate(); !error.empty()) {
        veyra::log::error("qml-preset", error);
        emit notice(tr("预设参数无效，已保留原状态"), true); return false;
    }
    // Partial presets do not change the current editing target. Colour-only
    // insertion/removal can shift NR indices, so retain the instance ordinal.
    const auto remapSelection = [&](int selected, engine::EffectType type) {
        if (entry.contents & engine::presetContentMask(engine::PresetContent::Chain)) return -1;
        if (selected < 0) return -1;
        uint32_t ordinal = 0;
        for (int i = 0; i < selected; ++i) if (impl_->chain.nodes[i].type == type) ++ordinal;
        for (uint32_t i = 0; i < candidate.nodeCount; ++i) {
            if (candidate.nodes[i].type != type) continue;
            if (ordinal == 0) return int(i);
            --ordinal;
        }
        return -1;
    };
    const int selectedNr = remapSelection(impl_->nrIndex(), engine::EffectType::NrEnhance);
    const int selectedColour = remapSelection(impl_->colourIndex(), engine::EffectType::Color);
    auto oldChain = std::make_unique<engine::EffectChain>(impl_->chain);
    impl_->chain = candidate;
    impl_->layout = nextLayout;
    if (mode == engine::ChainMode::Node) {
        impl_->draftGlobals = nextGlobals;
        engine::ChainGlobalSettings::capture(previous).apply(settings);
    }
    impl_->facade.setPending(settings);
    if (!impl_->revalidate(true, &previous)) {
        const auto error = impl_->validation.message; impl_->chain = *oldChain; impl_->layout = oldLayout;
        impl_->draftGlobals = oldGlobals;
        impl_->facade.setPending(previous); impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    impl_->selectedNr = selectedNr;
    impl_->selectedColour = selectedColour;
    if (entry.flow && (entry.contents & engine::presetContentMask(engine::PresetContent::Flow))) {
        impl_->prefs[QStringLiteral("contentRate")] = int(this->settings().content);
        if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "preset content cadence not saved");
    }
    emit settingsChanged();
    emitChainChanged();
    return true;
}

void QmlPlayerBridge::enqueueExportFile(const QString& input,const QString& output) {
    if(!QFileInfo(input).isFile()){emit notice(tr("队列输入文件不存在"),true);return;}
    impl_->exportQueue.addFile(input,output);emit exportChanged();
}
void QmlPlayerBridge::addExportFiles(const QStringList& paths) {
    for(const auto& path:paths){const auto url=QUrl(path);const QString file=url.isLocalFile()?url.toLocalFile():path;
        if(QFileInfo(file).isFile())impl_->exportQueue.addFile(file);
        else emit notice(tr("无法添加文件：%1").arg(file),true);
    }
    emit exportChanged();
}
void QmlPlayerBridge::addExportFilesDialog() {
    addExportFiles(QFileDialog::getOpenFileNames(nullptr,tr("加入导出队列"),QString(),
        tr("视频文件 (*.mp4 *.mkv *.mov *.avi *.webm *.ts *.m2ts);;所有文件 (*)")));
}
bool QmlPlayerBridge::selectExportPreset(int index) {
    if (index == -1) {
        impl_->exportPresetName.clear();
    } else {
        const auto& entries = impl_->facade.presets().entries();
        if (index < 0 || size_t(index) >= entries.size() || entries[size_t(index)].kind == engine::ChainMode::Node)
            return false;
        impl_->exportPresetName = entries[size_t(index)].name;
    }
    emit exportChanged();
    return true;
}

bool QmlPlayerBridge::savePresetAs(const QString& name, int contentsMask, bool nodeMode) {
    if (name.isEmpty()) { emit notice(tr("预设需要一个名字"), true); return false; }
    if (nodeMode != (impl_->chain.mode == engine::ChainMode::Node)) {
        emit notice(tr("请先切换到要保存的编辑模式"), true); return false;
    }
    engine::PresetEntry entry;
    entry.name = wideOf(name);
    entry.kind = nodeMode ? engine::ChainMode::Node : engine::ChainMode::List;
    entry.contents = uint32_t(contentsMask);
    entry.chain = impl_->chain;
    entry.globals=engine::ChainGlobalSettings::capture(impl_->facade.pendingSettings());
    entry.color = impl_->facade.pendingSettings().color;
    entry.fg.multiplier = impl_->facade.pendingSettings().multiplier;
    entry.fg.backend = impl_->facade.pendingSettings().frameGenerationBackend;
    entry.fg.vfgQuality = impl_->facade.pendingSettings().vfgQuality;
    entry.audioSync = impl_->facade.pendingSettings().audioSync;
    entry.audioOffsetMs = impl_->facade.pendingSettings().audioOffsetMs;
    if (nodeMode) {
        entry.nodeConfiguration = impl_->currentSession().configurations[1];
        entry.chain = entry.nodeConfiguration->chain;
        if (const auto* color = impl_->chain.firstOf(engine::EffectType::Color)) {
            entry.color = color->color; entry.color.enabled = color->enabled;
        }
        entry.fg.multiplier = uint32_t(fgMultiplier());
        entry.fg.backend = impl_->draftGlobals.fgBackend;
        entry.fg.vfgQuality = impl_->draftGlobals.vfgQuality;
        entry.globals=impl_->draftGlobals;
    }
    entry.flow = engine::PresetFlowSettings::capture(settings());
    const bool ok = impl_->facade.savePreset(entry, false);
    if (ok) emit presetsChanged();
    else emit notice(tr("保存预设失败: %1").arg(uiText(impl_->facade.error())), true);
    return ok;
}

bool QmlPlayerBridge::deletePreset(int index) {
    if (index < 0 || size_t(index) >= impl_->facade.presets().entries().size()) return false;
    const auto& entry = impl_->facade.presets().entries()[size_t(index)];
    if (entry.builtin) { emit notice(tr("内置预设不能删除，可以另存一份"), true); return false; }
    const bool selected = impl_->exportPresetName == entry.name;
    const bool ok = impl_->facade.presets().erase(size_t(index));
    if (ok) {
        if (selected) { impl_->exportPresetName.clear(); emit exportChanged(); }
        emit presetsChanged();
    }
    return ok;
}

bool QmlPlayerBridge::renamePreset(int index, const QString& name) {
    if (index < 0 || size_t(index) >= impl_->facade.presets().entries().size()) return false;
    const bool selected = impl_->exportPresetName == impl_->facade.presets().entries()[size_t(index)].name;
    const bool ok = impl_->facade.presets().rename(size_t(index), wideOf(name));
    if (ok) {
        if (selected) { impl_->exportPresetName = wideOf(name); emit exportChanged(); }
        emit presetsChanged();
    }
    return ok;
}

QVariantList QmlPlayerBridge::presetSaveParts() const {
    // What a preset would contain right now, part by part, so the save dialog can
    // list it before the user names anything. `meaningful` marks the parts that are
    // not at defaults, which is what makes "will be saved" honest.
    const auto s = settings();
    QVariantList out;
    auto add = [&](const char* id, const QString& label, const QString& summary, bool meaningful) {
        QVariantMap item;
        item["id"] = QString::fromUtf8(id);
        item["label"] = label;
        item["summary"] = summary;
        item["meaningful"] = meaningful;
        out << item;
    };
    QStringList chainParts;
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        chainParts << uiText(engine::effectInfo(impl_->chain.nodes[i].type).label);
    add("chain", tr("效果链"),
        chainParts.isEmpty() ? tr("空") : chainParts.join(QStringLiteral(" → ")),
        impl_->chain.nodeCount > 0);
    add("color", tr("色彩"), s.color.enabled ? tr("已启用") : tr("未启用"), s.color.enabled);
    add("fg", tr("补帧"),
        s.multiplier > 1 ? QStringLiteral("%1X").arg(s.multiplier) : tr("关闭", "off"),
        s.multiplier > 1);
    add("audio", tr("声音"),
        s.audioOffsetMs != 0 ? tr("偏移 %1 ms").arg(s.audioOffsetMs) : tr("默认"),
        s.audioOffsetMs != 0);
    const QStringList flowNames{QStringLiteral("NVIDIA NVOF"), tr("AMD 光流"), QStringLiteral("GPU DIS")};
    const QStringList qualityNames{tr("性能"), tr("平衡"), tr("质量")};
    const auto content=engine::currentContentRate(s.content);
    const auto cadenceName=content==engine::ContentRate::Transport?tr("去重关闭"):
        content==engine::ContentRate::Capture60To30?QStringLiteral("60→30"):tr("自动去除");
    QString flowSummary = flowNames.value(int(s.opticalFlowBackend)) + QStringLiteral(" · ") +
        qualityNames.value(int(s.flow)) + QStringLiteral(" · ") + cadenceName;
    if (s.opticalFlowBackend == engine::OpticalFlowBackend::AmdFidelityFx && s.amdFlowHalfResolution)
        flowSummary += QStringLiteral(" · ") + tr("半分辨率");
    add("flow", tr("光流与去除重复帧"), flowSummary,
        engine::PresetFlowSettings::capture(s) != engine::PresetFlowSettings{});
    return out;
}

int QmlPlayerBridge::defaultPresetIndex() const {
    const auto index = impl_->facade.presets().defaultIndex();
    return index.has_value() ? int(*index) : -1;
}

bool QmlPlayerBridge::setDefaultPreset(int index) {
    if (index < -1 || (index >= 0 && size_t(index) >= impl_->facade.presets().entries().size())) return false;
    const bool ok = index == -1 ? impl_->facade.presets().clearDefault()
                                : impl_->facade.presets().setDefault(size_t(index));
    if (ok) emit presetsChanged();
    else emit notice(tr("默认预设保存失败：") + uiText(impl_->facade.presets().error()), true);
    return ok;
}

bool QmlPlayerBridge::duplicatePreset(int index) {
    if (index < 0 || size_t(index) >= impl_->facade.presets().entries().size()) return false;
    const bool ok = impl_->facade.presets().duplicate(size_t(index));
    if (ok) emit presetsChanged();
    return ok;
}

void QmlPlayerBridge::refreshRecentFiles() {
    impl_->facade.refreshRecentFiles();
    emit recentFilesChanged();
}
void QmlPlayerBridge::clearRecentFiles() {
    // The facade owns the list; clearing it means dropping the entries it holds
    // and re-persisting, which refreshRecentFiles already does after the fact.
    impl_->facade.clearRecentFiles();
    emit recentFilesChanged();
}



} // namespace veyra::ui
