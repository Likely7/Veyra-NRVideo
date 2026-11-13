// QML bridge: Enhancement responsibilities.
#include "../QmlPlayerBridge_p.h"
#include <optional>

namespace veyra::ui {
// --- settings ---------------------------------------------------------------

#define VEYRA_BOOL_PROP(getter, setter, member)                              \
    bool QmlPlayerBridge::getter() const { return settings().member; }       \
    void QmlPlayerBridge::setter(bool value) {                               \
        auto s = settings();                                                 \
        if (s.member == value) return;                                       \
        s.member = value;                                                    \
        impl_->commit(std::move(s));                                         \
        emit settingsChanged();                                              \
    }
#define VEYRA_NUM_PROP(getter, setter, member, type)                         \
    type QmlPlayerBridge::getter() const { return type(settings().member); } \
    void QmlPlayerBridge::setter(type value) {                               \
        auto s = settings();                                                 \
        if (type(s.member) == value) return;                                 \
        s.member = value;                                                    \
        impl_->commit(std::move(s));                                         \
        emit settingsChanged();                                              \
    }

int QmlPlayerBridge::videoSrQuality() const { return int(settings().videoSrQuality); }
int QmlPlayerBridge::dlssSrModel() const { return int(settings().dlssSrModel); }
QVariantList QmlPlayerBridge::dlssSrModelChoices() const {
    return {QVariantMap{{"id", "0"}, {"label", "DLSS 4 · K"}},
            QVariantMap{{"id", "1"}, {"label", "DLSS 4.5 · M"}},
            QVariantMap{{"id", "2"}, {"label", "DLSS 4.5 · L"}}};
}
void QmlPlayerBridge::setDlssSrModel(int value) {
    if (value < 0 || unsigned(value) >= engine::kDlssSrModelCount) return;
    const auto cap = effectCapabilities().value("sr0").toMap();
    if (!cap.value("available").toBool()) { emit notice(cap.value("reason").toString(), true); return; }
    auto s = settings();
    if (s.dlssSrModel == unsigned(value)) return;
    s.dlssSrModel = unsigned(value);
    if (impl_->commit(s)) emit settingsChanged();
}
int QmlPlayerBridge::srPrescale() const { return int(settings().srPrescale); }
bool QmlPlayerBridge::dualSrEnabled() const { return settings().dualSrEnabled; }
// Double SR takes every SR backend (user 2026-11-08: DLSS SR and FSR as well).
void QmlPlayerBridge::setDualSrEnabled(bool enabled){
    auto s=settings();if(s.dualSrEnabled==enabled)return;
    s.dualSrEnabled=enabled;
    if(enabled){
        // A kept second algorithm this adapter cannot run follows the base
        // (an AMD card starts FSR -> FSR instead of a dead RTX choice).
        const auto cap=effectCapabilities().value("sr"+QString::number(s.dualSrQuality)).toMap();
        if(!cap.value("available").toBool())s.dualSrQuality=s.videoSrQuality;
        // A kept supersample shrink is inactive under double SR and returns when it is off.
        if(s.srPrescale==pipeline::SrPrescale::Off)s.srPrescale=pipeline::SrPrescale::P1080;
    }
    if(impl_->commit(s))emit settingsChanged();
}
int QmlPlayerBridge::dualSrTargetIndex() const {
    switch(settings().dualSrTarget){case pipeline::SrTarget::Qhd:return 1;case pipeline::SrTarget::Uhd4K:return 2;case pipeline::SrTarget::Uhd8K:return 3;case pipeline::SrTarget::Uhd5K:return 4;case pipeline::SrTarget::Uhd6K:return 5;case pipeline::SrTarget::Uhd7K:return 6;}return 2;
}
void QmlPlayerBridge::setDualSrTargetIndex(int index){
    if(index<1||index>6)return;
    const pipeline::SrTarget targets[]={pipeline::SrTarget::Qhd,pipeline::SrTarget::Uhd4K,pipeline::SrTarget::Uhd8K,pipeline::SrTarget::Uhd5K,pipeline::SrTarget::Uhd6K,pipeline::SrTarget::Uhd7K};
    auto s=settings();s.dualSrTarget=targets[index-1];if(impl_->commit(s))emit settingsChanged();
}
int QmlPlayerBridge::dualSrQuality() const {return int(settings().dualSrQuality);}
// The second pass may use any SR backend - DLSS SR, an RTX Video SR family and
// tier, or FSR - independent of the base.
void QmlPlayerBridge::setDualSrQuality(int quality){
    if(quality<0||!engine::validVideoSrQuality(unsigned(quality)))return;
    const auto cap=effectCapabilities().value("sr"+QString::number(quality)).toMap();
    if(!cap.value("available").toBool()){emit notice(cap.value("reason").toString(),true);return;}
    auto s=settings();if(int(s.dualSrQuality)==quality)return;
    s.dualSrQuality=uint32_t(quality);if(impl_->commit(s))emit settingsChanged();
}
// Algorithm ids of the SR lists: "rtx" standard, "rtx-hb" high bitrate and
// "rtx-st" streaming RTX Video SR (each with tiers 1 low .. 4 ultra); DLSS SR
// and FSR keep their numeric ids.
QString QmlPlayerBridge::srAlgorithmOf(int quality) const {
    if(quality<0||!engine::rtxVideoSr(unsigned(quality)))return QString::number(quality);
    switch(engine::videoSrFamily(unsigned(quality))){
    case engine::VideoSrFamily::HighBitrate:return QStringLiteral("rtx-hb");
    case engine::VideoSrFamily::Streaming:return QStringLiteral("rtx-st");
    default:return QStringLiteral("rtx");
    }
}
int QmlPlayerBridge::srTierOf(int quality) const {
    return quality>=0&&engine::rtxVideoSr(unsigned(quality))?int(engine::videoSrTier(unsigned(quality))):0;
}
// The stored quality for an algorithm at a tier. Coming from DLSS SR or FSR
// there is no tier yet, so an RTX family starts at the default medium tier.
int QmlPlayerBridge::srQualityFor(const QString& algorithm,int tier) const {
    const auto family=algorithm==QLatin1String("rtx")?std::optional(engine::VideoSrFamily::Standard):
        algorithm==QLatin1String("rtx-hb")?std::optional(engine::VideoSrFamily::HighBitrate):
        algorithm==QLatin1String("rtx-st")?std::optional(engine::VideoSrFamily::Streaming):std::nullopt;
    if(family)return int(engine::videoSrQualityFor(*family,tier>=1&&tier<=4?unsigned(tier):engine::kDefaultVideoSrQuality));
    bool ok=false;const int value=algorithm.toInt(&ok);
    return ok&&value>=0&&engine::validVideoSrQuality(unsigned(value))?value:-1;
}
// Streaming has only the medium and ultra tiers.
QVariantList QmlPlayerBridge::srTierChoices(int quality) const {
    const bool streaming=quality>=0&&engine::rtxVideoSr(unsigned(quality))&&engine::videoSrFamily(unsigned(quality))==engine::VideoSrFamily::Streaming;
    const QString labels[]={tr("低"),tr("中"),tr("高"),tr("超高")};
    QVariantList out;
    for(int tier=1;tier<=4;++tier)if(!streaming||tier==2||tier==4)out<<QVariantMap{{"id",QString::number(tier)},{"label",labels[tier-1]}};
    return out;
}
// "超高", or "高码率 超高" with the family. Empty for DLSS SR and FSR.
QString QmlPlayerBridge::srQualityLabel(int quality,bool withFamily) const {
    if(quality<0||!engine::rtxVideoSr(unsigned(quality)))return {};
    const QString tiers[]={tr("低"),tr("中"),tr("高"),tr("超高")};
    const QString tier=tiers[engine::videoSrTier(unsigned(quality))-1];
    if(!withFamily)return tier;
    const auto family=engine::videoSrFamily(unsigned(quality));
    return (family==engine::VideoSrFamily::HighBitrate?tr("高码率"):family==engine::VideoSrFamily::Streaming?tr("流媒体"):tr("标准"))+QLatin1Char(' ')+tier;
}
int QmlPlayerBridge::srOutput() const { return int(settings().srOutput); }
// The double SR reduction works with every SR backend; the supersample shrink
// runs on RTX Video SR (every family) only.
void QmlPlayerBridge::setSrPrescale(int value) {
    const auto want = pipeline::SrPrescale(uint32_t(value));
    if (value < 0 || !pipeline::validSrPrescale(want)) return;
    auto s = settings();
    if (s.srPrescale == want) return;
    s.srPrescale = want;
    if (impl_->commit(s)) emit settingsChanged();
}
void QmlPlayerBridge::setSrOutput(int value) {
    const auto want = pipeline::SrOutput(uint32_t(value));
    if (value < 0 || !pipeline::validSrOutput(want)) return;
    auto s = settings();
    if (s.srOutput == want) return;
    if (!engine::rtxVideoSr(s.videoSrQuality)) { emit notice(tr("超分后缩回目前仅 RTX 视频超分支持"), true); return; }
    s.srOutput = want;
    if (impl_->commit(s)) emit settingsChanged();
}
void QmlPlayerBridge::setVideoSrQuality(int value) {
    if (value < 0 || !engine::validVideoSrQuality(unsigned(value))) return;
    const auto cap = effectCapabilities().value("sr" + QString::number(value)).toMap();
    if (!cap.value("available").toBool()) { emit notice(cap.value("reason").toString(), true); return; }
    auto s = settings();
    if (int(s.videoSrQuality) == value) return;
    s.videoSrQuality = unsigned(value);
    if (impl_->commit(s)) emit settingsChanged();
}
int QmlPlayerBridge::audioOffsetMs() const { return settings().audioOffsetMs; }
void QmlPlayerBridge::setAudioOffsetMs(int value) {
    auto s = settings();
    if (s.audioOffsetMs == value) return;
    s.audioOffsetMs = value;
    if (!impl_->commit(std::move(s))) return;
    impl_->prefs[QStringLiteral("audioOffsetMs")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "audio offset not saved");
    emit settingsChanged();
}
#undef VEYRA_BOOL_PROP
#undef VEYRA_NUM_PROP

// Stage-owned properties must change the authoritative chain first. commit()
// projects that chain back to settings, so setting only a flat mirror is a no-op.
bool QmlPlayerBridge::srEnabled() const {
    const auto* node = impl_->chain.firstOf(engine::EffectType::SuperResolution);
    return node && node->enabled;
}
void QmlPlayerBridge::setSrEnabled(bool enabled) {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::SuperResolution) {
            setEffectEnabled(int(i), enabled); return;
        }
    if (enabled) emit notice(tr("请先添加超分节点"), true);
}
bool QmlPlayerBridge::videoHdr() const {
    const auto* node = impl_->chain.firstOf(engine::EffectType::VideoHdr);
    return node && node->enabled;
}
// Custom: static HDR output tuning. Globals, not chain nodes, and every field is
// live (the curve is read by the present blit, the metadata sent to the display),
// so a change never rebuilds the graph - but it does have to reach the session
// file, which has no other trigger for a global change.
int QmlPlayerBridge::hdrCurveStrength()const{return int(settings().hdrCurve.strengthPercent);}
void QmlPlayerBridge::setHdrCurveStrength(int percent){
    auto s=settings();
    const unsigned clamped=unsigned(std::clamp(percent,0,200));
    if(s.hdrCurve.strengthPercent==clamped)return;
    s.hdrCurve.strengthPercent=clamped;
    commitHdrTuning(std::move(s));
}
int QmlPlayerBridge::hdrDisplayPeakNits()const{return int(settings().hdrCurve.displayPeakNits);}
void QmlPlayerBridge::setHdrDisplayPeakNits(int nits){
    auto s=settings();
    // 0 = ask the system; anything else has to be a plausible panel peak.
    const int clamped=nits<=0?0:std::clamp(nits,100,10000);
    if(s.hdrCurve.displayPeakNits==unsigned(clamped))return;
    s.hdrCurve.displayPeakNits=unsigned(clamped);
    commitHdrTuning(std::move(s));
}
QVariantMap QmlPlayerBridge::hdrDisplayInfo()const{
    // Read from QML bindings (the peak row's hint text), so no driver work here:
    // the values come from the shared display state, queried in the background on
    // events and 刷新显示器状态 (this used to build a DXGI factory every 5 s on the
    // UI thread).
    HWND window=impl_->videoWindow;
    if(!IsWindow(window)){
        if(auto* focused=QGuiApplication::focusWindow())window=reinterpret_cast<HWND>(focused->winId());
        else window=nullptr;
    }
    auto& displays=impl_->engine.displayStates();
    std::optional<engine::DisplayState> known;
    if(window){displays.request(window,"ui");known=displays.state(displays.monitorOf(window));}
    const engine::DisplayColorState c=known?known->color:engine::DisplayColorState{};
    return {{"valid",c.peakValid},{"plausible",c.peakValid},{"peakNits",int(c.peakNits)},
            {"fullFrameNits",int(c.fullFrameNits)},{"minNits",c.minNits}};
}
bool QmlPlayerBridge::hdrCurve()const{return settings().hdrCurve.enabled;}
void QmlPlayerBridge::setHdrCurve(bool enabled){
    auto s=settings();if(s.hdrCurve.enabled==enabled)return;s.hdrCurve.enabled=enabled;
    commitHdrTuning(std::move(s));
}
QVariantMap QmlPlayerBridge::hdrCurveParams()const{
    const auto& c=settings().hdrCurve;
    return {{"shadowLiftEv100",int(c.shadowLiftEv100)},{"shadowRangeNits",int(c.shadowRangeNits)},
            {"midGrayNits",int(c.midGrayNits)},{"highlightStartNits",int(c.highlightStartNits)},
            {"rollOffPercent",int(c.rollOffPercent)},{"peakCapNits",int(c.peakCapNits)}};
}
bool QmlPlayerBridge::setHdrCurveParameter(const QString& key,double value){
    if(!std::isfinite(value)||value<-10001||value>10001)return false;
    auto s=settings();const auto v=int(std::lround(value));
    if(key=="shadowLiftEv100"){if(v<-200||v>200)return false;s.hdrCurve.shadowLiftEv100=v;}
    else if(key=="shadowRangeNits"){if(v<1||v>100)return false;s.hdrCurve.shadowRangeNits=unsigned(v);}
    else if(key=="midGrayNits"){if(v<100||v>500)return false;s.hdrCurve.midGrayNits=unsigned(v);}
    else if(key=="highlightStartNits"){if(v<0||v>2000)return false;s.hdrCurve.highlightStartNits=unsigned(v);}
    else if(key=="rollOffPercent"){if(v<10||v>200)return false;s.hdrCurve.rollOffPercent=unsigned(v);}
    else if(key=="peakCapNits"){if(v<0||v>4000)return false;s.hdrCurve.peakCapNits=unsigned(v);}
    else return false;
    if(!s.hdrCurve.valid())return false;
    return commitHdrTuning(std::move(s));
}
bool QmlPlayerBridge::commitHdrTuning(engine::EnhancementSettings settings){
    const bool wasValid=chainValid();
    const auto curve=settings.hdrCurve;
    const auto metadata=settings.hdrMetadata;
    if(!impl_->commit(std::move(settings)))return false;
    // Re-project first, then re-assert both copies the session capture reads:
    // revalidate() rewrites the pending settings and the draft globals from the
    // runtime configuration, which does not carry this block yet.
    impl_->revalidate(true);
    auto after=impl_->facade.pendingSettings();
    after.hdrCurve=curve;after.hdrMetadata=metadata;
    impl_->facade.setPending(after);
    impl_->draftGlobals.hdrCurve=curve;impl_->draftGlobals.hdrMetadata=metadata;
    impl_->persistSession();
    if(!impl_->hdrRestoring&&impl_->hdrMemorySaveTimer){impl_->hdrMemoryDirty=true;impl_->hdrMemorySaveTimer->start();}
    emit settingsChanged();
    if(chainValid()!=wasValid)emitChainChanged();
    return true;
}
QString QmlPlayerBridge::hdrTuningPreset() const {
    const auto& s=settings();
    // hdrPresetMatches deliberately ignores the strength dial and the display peak:
    // they only scale the baseline, so turning them keeps you on the same preset.
    for(const auto& p:engine::kHdrTuningPresets)
        if(engine::hdrPresetMatches(s.hdrCurve,s.hdrMetadata,p))return QString::fromLatin1(p.id);
    return QStringLiteral("custom");
}
QString QmlPlayerBridge::hdrAppliedFrom() const {
  return impl_->prefString("hdrAppliedFrom")==QLatin1String("axes")?QStringLiteral("axes")
      :QStringLiteral("legacy");
}

void QmlPlayerBridge::setHdrAppliedFrom(const QString& from){
  const QString wanted=from==QLatin1String("axes")?QStringLiteral("axes"):QStringLiteral("legacy");
  if(impl_->prefString("hdrAppliedFrom")==wanted)return;
  impl_->prefs[QStringLiteral("hdrAppliedFrom")]=wanted;
  impl_->savePrefs();
  emit preferencesChanged();
}

QString QmlPlayerBridge::hdrPresetMode() const {
  return impl_->prefString("hdrPresetMode")==QLatin1String("axes")?QStringLiteral("axes")
                                                                :QStringLiteral("legacy");
}

void QmlPlayerBridge::setHdrPresetMode(const QString& mode){
  const QString wanted=mode==QLatin1String("axes")?QStringLiteral("axes"):QStringLiteral("legacy");
  if(impl_->prefString("hdrPresetMode")==wanted)return;
  impl_->prefs[QStringLiteral("hdrPresetMode")]=wanted;
  if(!impl_->savePrefs())veyra::log::warn("ui-prefs","hdr preset mode not saved");
  emit preferencesChanged();
}

QVariantMap QmlPlayerBridge::hdrAxes() const {
  auto environment=impl_->prefString("hdrAxesEnvironment");
  if(environment==QLatin1String("bright"))environment=QStringLiteral("compensate");
  else if(environment==QLatin1String("dark"))environment=QStringLiteral("plain");
  return {{"content",impl_->prefString("hdrAxesContent")},
          {"environment",environment}};
}

QVariantMap QmlPlayerBridge::hdrAxesMatch() const {
  // Which combination the curve currently equals. Strength and the display peak are
  // ignored, exactly like hdrPresetMatches: they only scale the baseline.
  const auto& c=settings().hdrCurve;
  for(const auto& content:engine::kHdrContentPresets)
    for(const auto* environment:engine::kHdrEnvironmentIds){
      const auto candidate=engine::hdrAxesCurve(content.id,environment);
      if(c.enabled==candidate.enabled&&c.shadowLiftEv100==candidate.shadowLiftEv100&&
         c.shadowRangeNits==candidate.shadowRangeNits&&c.midGrayNits==candidate.midGrayNits&&
         c.highlightStartNits==candidate.highlightStartNits&&c.rollOffPercent==candidate.rollOffPercent&&
         c.peakCapNits==candidate.peakCapNits)
        return {{"content",QString::fromLatin1(content.id)},{"environment",QString::fromLatin1(environment)}};
    }
  return {{"content",QString()},{"environment",QString()}};
}

QVariantMap QmlPlayerBridge::hdrCurveCoverage() const {
  const auto& snap=impl_->snapshot;
  return {{"valid",impl_->hdrCoverageObserved&&hdrCurve()&&snap.hdrCoverageValid},
          {"aboveCeiling",snap.hdrAboveCeilingPercent},
          {"rollOff",snap.hdrInRollOffPercent},
          {"lift",snap.hdrInLiftPercent}};
}

void QmlPlayerBridge::observeHdrCoverage(bool on){if(impl_->hdrCoverageObserved==on)return;impl_->hdrCoverageObserved=on;impl_->engine.observeHdrCoverage(on);emit snapshotChanged();}
QVariantMap QmlPlayerBridge::hdrCurveGraph() const {
  auto base=settings().hdrCurve;if(!base.displayPeakNits)base.displayPeakNits=effectiveHdrDisplayPeakNits();
  engine::HdrCurveSettings c;engine::HdrMetadataSettings metadata;
  engine::resolveHdrTuning(base,settings().hdrMetadata,c,metadata);
  QVariantList inputs,outputs;
  for(unsigned i=0;i<engine::kHdrCurveGraphPoints;++i){
    const double x=engine::hdrCurveGraphInputNits(i);
    inputs.append(x);
    outputs.append(c.enabled?engine::hdrCurveLuminance(c,x):x);
  }
  return {{"inputs",inputs},{"outputs",outputs},
          {"minNits",engine::kHdrCurveGraphMinNits},{"maxNits",engine::kHdrCurveGraphMaxNits},
          {"enabled",c.enabled}};
}

QVariantMap QmlPlayerBridge::hdrDials() const {
  const auto& c=settings().hdrCurve;
  return {{"shadowLift",int(engine::hdrShadowLiftLevel(c))},
          {"brightness",int(engine::hdrBrightnessLevel(c))},
          {"highlight",int(engine::hdrHighlightLevel(c))}};
}

bool QmlPlayerBridge::setHdrDial(const QString& key,int level){
  if(level<0)return false;
  auto s=settings();
  const unsigned value=unsigned(level);
  if(key==QLatin1String("shadowLift"))engine::setHdrShadowLiftLevel(s.hdrCurve,value);
  else if(key==QLatin1String("brightness"))engine::setHdrBrightnessLevel(s.hdrCurve,value);
  else if(key==QLatin1String("highlight"))engine::setHdrHighlightLevel(s.hdrCurve,value);
  else return false;
  return commitHdrTuning(std::move(s));
}

bool QmlPlayerBridge::applyHdrAxesPreset(const QString& content,const QString& environment){
  const QByteArray contentId=content.toUtf8();
  const QString normalizedEnvironment=environment==QLatin1String("bright")?QStringLiteral("compensate")
      :environment==QLatin1String("dark")?QStringLiteral("plain"):environment;
  const QByteArray environmentId=normalizedEnvironment.toUtf8();
  if(!engine::findHdrContentPreset(contentId.constData())||
      (normalizedEnvironment!=QLatin1String("compensate")&&normalizedEnvironment!=QLatin1String("plain")))return false;
  const auto curve=engine::hdrAxesCurve(contentId.constData(),environmentId.constData());
  auto s=settings();
  // The strength dial and the display peak belong to the user, not to the preset.
  const auto strength=s.hdrCurve.strengthPercent;
  const auto displayPeak=s.hdrCurve.displayPeakNits;
  s.hdrCurve=curve;
  s.hdrCurve.strengthPercent=strength;
  s.hdrCurve.displayPeakNits=displayPeak;
  if(!commitHdrTuning(std::move(s)))return false;
  setHdrAppliedFrom(QStringLiteral("axes"));
  impl_->prefs[QStringLiteral("hdrAxesContent")]=content;
  impl_->prefs[QStringLiteral("hdrAxesEnvironment")]=normalizedEnvironment;
  if(!impl_->savePrefs())veyra::log::warn("ui-prefs","hdr axes choice not saved");
  emit preferencesChanged();
  return true;
}

bool QmlPlayerBridge::hdrTitleMemory() const { return impl_->prefBool("hdrTitleMemory",true); }
bool QmlPlayerBridge::hdrAutoRecommend() const { return impl_->prefBool("hdrAutoRecommend",false); }
int QmlPlayerBridge::hdrTitleCount() const {
  return impl_->prefs.value(QStringLiteral("hdrTitles")).toList().size();
}

void QmlPlayerBridge::setHdrTitleMemory(bool on){
  if(impl_->prefBool("hdrTitleMemory",true)==on)return;
  impl_->prefs[QStringLiteral("hdrTitleMemory")]=on;
  if(!impl_->savePrefs())veyra::log::warn("ui-prefs","hdr title memory flag not saved");
  emit preferencesChanged();
}

void QmlPlayerBridge::setHdrAutoRecommend(bool on){
  if(impl_->prefBool("hdrAutoRecommend",false)==on)return;
  impl_->prefs[QStringLiteral("hdrAutoRecommend")]=on;
  if(!impl_->savePrefs())veyra::log::warn("ui-prefs","hdr auto recommend flag not saved");
  emit preferencesChanged();
}

void QmlPlayerBridge::forgetHdrTitles(){
  flushHdrMemory();
  impl_->prefs[QStringLiteral("hdrTitles")]=QVariantList{};
  if(!impl_->savePrefs())veyra::log::warn("ui-prefs","hdr title profiles not cleared");
  emit preferencesChanged();
  emit notice(tr("已清除影片记忆"),false);
}

void QmlPlayerBridge::flushHdrMemory(){
  if(impl_->hdrMemorySaveTimer)impl_->hdrMemorySaveTimer->stop();
  if(impl_->hdrRestoring||!impl_->hdrMemoryDirty)return;
  impl_->hdrMemoryDirty=false;
  if(!impl_->hdrProfileDisplay.isEmpty())storeHdrProfile(impl_->hdrProfileDisplay);
  if(hdrTitleMemory()&&!impl_->hdrTitleApplied.isEmpty())storeHdrTitleProfile(impl_->hdrTitleApplied);
}
void QmlPlayerBridge::syncHdrTitleProfile(){
  auto& i=*impl_;
  if(!i.snapshot.running||i.openingSource)return;
  const QFileInfo file(QString::fromStdWString(i.sourceLabel));
  if(!file.isFile())return; // Capture/stream labels are not film identities.
  const QString title=file.canonicalFilePath();
  if(title.isEmpty()||title==i.hdrTitleApplied)return;
  flushHdrMemory();i.hdrTitleApplied=title;
  if(hdrTitleMemory()&&loadHdrTitleProfile(title)){
    emit notice(tr("已套用这部影片上次的曲线设置"),false);return;
  }
  if(hdrAutoRecommend()&&settings().hdrCurve.enabled){QScopedValueRollback<bool> restoring(i.hdrRestoring,true);applyRecommendedHdrTuning();}
}

bool QmlPlayerBridge::loadHdrTitleProfile(const QString& title){
  const auto list=impl_->prefs.value(QStringLiteral("hdrTitles")).toList();
  for(const auto& item:list){
    const auto stored=item.toMap();
    if(stored.value(QStringLiteral("key")).toString()!=title)continue;
    auto s=settings();
    const auto number=[&](const char* key,int fallback){
      return stored.contains(QLatin1String(key))?stored.value(QLatin1String(key)).toInt():fallback;};
    s.hdrCurve.enabled=stored.value(QStringLiteral("enabled"),s.hdrCurve.enabled).toBool();
    s.hdrCurve.shadowLiftEv100=number("shadowLiftEv100",int(s.hdrCurve.shadowLiftEv100));
    s.hdrCurve.shadowRangeNits=unsigned(std::clamp(number("shadowRangeNits",int(s.hdrCurve.shadowRangeNits)),1,100));
    s.hdrCurve.midGrayNits=unsigned(std::clamp(number("midGrayNits",int(s.hdrCurve.midGrayNits)),100,500));
    s.hdrCurve.highlightStartNits=unsigned(std::clamp(number("highlightStartNits",int(s.hdrCurve.highlightStartNits)),0,2000));
    s.hdrCurve.rollOffPercent=unsigned(std::clamp(number("rollOffPercent",int(s.hdrCurve.rollOffPercent)),10,200));
    s.hdrCurve.peakCapNits=unsigned(std::clamp(number("peakCapNits",int(s.hdrCurve.peakCapNits)),0,4000));
    s.hdrCurve.strengthPercent=unsigned(std::clamp(number("strengthPercent",int(s.hdrCurve.strengthPercent)),0,200));
    QScopedValueRollback<bool> restoring(impl_->hdrRestoring,true);
    return commitHdrTuning(std::move(s));
  }
  return false;
}

bool QmlPlayerBridge::storeHdrTitleProfile(const QString& title){
  if(title.isEmpty()||!hdrTitleMemory())return false;
  auto& i=*impl_;
  const auto& c=settings().hdrCurve;
  QVariantMap entry;
  entry[QStringLiteral("key")]=title;
  entry[QStringLiteral("enabled")]=c.enabled;
  entry[QStringLiteral("shadowLiftEv100")]=int(c.shadowLiftEv100);
  entry[QStringLiteral("shadowRangeNits")]=int(c.shadowRangeNits);
  entry[QStringLiteral("midGrayNits")]=int(c.midGrayNits);
  entry[QStringLiteral("highlightStartNits")]=int(c.highlightStartNits);
  entry[QStringLiteral("rollOffPercent")]=int(c.rollOffPercent);
  entry[QStringLiteral("peakCapNits")]=int(c.peakCapNits);
  entry[QStringLiteral("strengthPercent")]=int(c.strengthPercent);
  auto list=i.prefs.value(QStringLiteral("hdrTitles")).toList();
  for(const auto& item:list)if(item.toMap().value(QStringLiteral("key")).toString()==title&&item.toMap()==entry)return false;
  for(int n=list.size()-1;n>=0;--n)
    if(list[n].toMap().value(QStringLiteral("key")).toString()==title)list.removeAt(n);
  list.append(entry);
  // Bounded list: the oldest titles fall off the front.
  while(list.size()>i.kHdrTitleProfileLimit)list.removeFirst();
  i.prefs[QStringLiteral("hdrTitles")]=list;
  if(!i.savePrefs())veyra::log::warn("ui-prefs","hdr title profile not saved");
  emit preferencesChanged();return true;
}

int QmlPlayerBridge::hdrDisplayProfileCount() const {
  return impl_->prefs.value(QStringLiteral("hdrProfiles")).toMap().size();
}

QString QmlPlayerBridge::hdrDisplayProfileId() const {
  // The EDID-based device id survives cable and port changes; the GDI device name
  // is the fallback for monitors that report none. Both are cheap Win32 calls -
  // no DXGI enumeration, so this stays safe on the snapshot cadence.
  HWND window=impl_->videoWindow;
  if(!IsWindow(window)){
    if(auto* focused=QGuiApplication::focusWindow())window=reinterpret_cast<HWND>(focused->winId());
    else window=nullptr;
  }
  const HMONITOR monitor=window?MonitorFromWindow(window,MONITOR_DEFAULTTONEAREST)
                               :MonitorFromPoint(POINT{0,0},MONITOR_DEFAULTTOPRIMARY);
  if(!monitor)return QString();
  if(monitor==impl_->hdrIdentityMonitor&&!impl_->hdrIdentityKey.isEmpty())return impl_->hdrIdentityKey;
  impl_->hdrIdentityMonitor=monitor;
  MONITORINFOEXW info{};info.cbSize=sizeof(info);
  if(!GetMonitorInfoW(monitor,reinterpret_cast<MONITORINFO*>(&info)))return QString();
  DISPLAY_DEVICEW device{};device.cb=sizeof(device);
  if(EnumDisplayDevicesW(info.szDevice,0,&device,0)&&device.DeviceID[0])
    impl_->hdrIdentityKey=QString::fromWCharArray(device.DeviceID);
  else impl_->hdrIdentityKey=QString::fromWCharArray(info.szDevice);
  return impl_->hdrIdentityKey;
}

void QmlPlayerBridge::forgetHdrDisplayProfiles(){
  flushHdrMemory();
  auto& i=*impl_;
  i.prefs[QStringLiteral("hdrProfiles")]=QVariantMap{};
  if(!i.savePrefs())veyra::log::warn("ui-prefs","hdr display profiles not cleared");
  emit preferencesChanged();
  emit notice(tr("已清除显示设备记忆"),false);
}

void QmlPlayerBridge::syncHdrDisplayProfile(){
  auto& i=*impl_;
  const QString id=hdrDisplayProfileId();
  if(id.isEmpty()||id==i.hdrProfileDisplay)return;   // the common path: nothing changed
  // Leaving a display: remember what the curve looks like on it.
  flushHdrMemory();
  i.hdrProfileDisplay=id;
  emit settingsChanged();
  if(loadHdrProfile(id)){
    emit preferencesChanged();
    emit notice(tr("已套用该显示设备的 HDR 曲线配置"),false);
  }
}

bool QmlPlayerBridge::loadHdrProfile(const QString& id){
  auto& i=*impl_;
  const auto profiles=i.prefs.value(QStringLiteral("hdrProfiles")).toMap();
  const auto stored=profiles.value(id).toMap();
  if(stored.isEmpty())return false;
  auto s=settings();
  const auto number=[&](const char* key,int fallback){
    return stored.contains(QLatin1String(key))?stored.value(QLatin1String(key)).toInt():fallback;};
  s.hdrCurve.enabled=stored.value(QStringLiteral("enabled"),s.hdrCurve.enabled).toBool();
  s.hdrCurve.shadowLiftEv100=number("shadowLiftEv100",int(s.hdrCurve.shadowLiftEv100));
  s.hdrCurve.shadowRangeNits=unsigned(std::clamp(number("shadowRangeNits",int(s.hdrCurve.shadowRangeNits)),1,100));
  s.hdrCurve.midGrayNits=unsigned(std::clamp(number("midGrayNits",int(s.hdrCurve.midGrayNits)),100,500));
  s.hdrCurve.highlightStartNits=unsigned(std::clamp(number("highlightStartNits",int(s.hdrCurve.highlightStartNits)),0,2000));
  s.hdrCurve.rollOffPercent=unsigned(std::clamp(number("rollOffPercent",int(s.hdrCurve.rollOffPercent)),10,200));
  s.hdrCurve.peakCapNits=unsigned(std::clamp(number("peakCapNits",int(s.hdrCurve.peakCapNits)),0,4000));
  s.hdrCurve.strengthPercent=unsigned(std::clamp(number("strengthPercent",int(s.hdrCurve.strengthPercent)),0,200));
  s.hdrCurve.displayPeakNits=unsigned(std::clamp(number("displayPeakNits",int(s.hdrCurve.displayPeakNits)),0,10000));
  QScopedValueRollback<bool> restoring(i.hdrRestoring,true);
  return commitHdrTuning(std::move(s));
}

bool QmlPlayerBridge::storeHdrProfile(const QString& id){
  auto& i=*impl_;
  const auto& c=settings().hdrCurve;
  QVariantMap entry;
  entry[QStringLiteral("enabled")]=c.enabled;
  entry[QStringLiteral("shadowLiftEv100")]=int(c.shadowLiftEv100);
  entry[QStringLiteral("shadowRangeNits")]=int(c.shadowRangeNits);
  entry[QStringLiteral("midGrayNits")]=int(c.midGrayNits);
  entry[QStringLiteral("highlightStartNits")]=int(c.highlightStartNits);
  entry[QStringLiteral("rollOffPercent")]=int(c.rollOffPercent);
  entry[QStringLiteral("peakCapNits")]=int(c.peakCapNits);
  entry[QStringLiteral("strengthPercent")]=int(c.strengthPercent);
  entry[QStringLiteral("displayPeakNits")]=int(c.displayPeakNits);
  auto profiles=i.prefs.value(QStringLiteral("hdrProfiles")).toMap();
  if(profiles.value(id).toMap()==entry)return false;   // nothing new to remember
  profiles[id]=entry;
  while(profiles.size()>32)profiles.erase(profiles.begin());
  i.prefs[QStringLiteral("hdrProfiles")]=profiles;
  if(!i.savePrefs())veyra::log::warn("ui-prefs","hdr display profile not saved");
  emit preferencesChanged();return true;
}

QVariantMap QmlPlayerBridge::hdrSourceInfo() const {
  const auto& snap=impl_->snapshot;
  const unsigned mastering=snap.sourceMasteringPeakNits;
  const unsigned cll=snap.sourceMaxCllNits,fall=snap.sourceMaxFallNits;
  const bool valid=snap.sourceHdr&&(mastering||cll||fall);
  return {{"valid",valid},{"masteringPeakNits",int(mastering)},
          {"maxCllNits",int(cll)},{"maxFallNits",int(fall)}};
}

bool QmlPlayerBridge::sourceIsSdr() const {
  const auto& snap=impl_->snapshot;
  return snap.running&&!snap.sourceHdr;
}

unsigned QmlPlayerBridge::effectiveHdrDisplayPeakNits() const {
  // The ceiling the panel is actually held to: the user's own value when set, else the
  // system report - but only when that report is plausible (a projector answering
  // 10000 nit with a 0.000 black is its EDID ceiling, not a measurement; the presenter
  // applies the same rule). 0 means unknown and the recommendation skips that branch.
  const unsigned setting=unsigned(std::max(0,hdrDisplayPeakNits()));
  if(setting)return setting;
  const auto info=hdrDisplayInfo();
  if(!info.value("valid").toBool()||!info.value("plausible").toBool())return 0;
  return unsigned(std::max(0,info.value("peakNits").toInt()));
}

QVariantMap QmlPlayerBridge::hdrRecommendation() const {
  const auto& snap=impl_->snapshot;
  const auto rec=engine::recommendHdrTuning(snap.sourceMasteringPeakNits,snap.sourceMaxCllNits,
      snap.sourceMaxFallNits,effectiveHdrDisplayPeakNits());
  return {{"available",rec.available},{"presetId",QString::fromLatin1(rec.presetId)},
          {"axesContent",QString::fromLatin1(engine::hdrAxesContentForPreset(rec.presetId))},
          {"reason",QString::fromLatin1(rec.reasonKey)},{"strengthPercent",int(rec.strengthPercent)},
          {"cllNits",int(rec.cllNits)},{"fallNits",int(rec.fallNits)}};
}

QVariantMap QmlPlayerBridge::hdrPeakWarning() const {
  const auto& snap=impl_->snapshot;
  const auto& s=settings();
  // The arithmetic (including the divide-by-zero guard) lives in the header so the
  // unit tests cover it; this only marshals it to QML.
  const unsigned sourcePeak=snap.sourceMaxCllNits?snap.sourceMaxCllNits:snap.sourceMasteringPeakNits;
  auto baseline=s.hdrCurve;if(!baseline.displayPeakNits)baseline.displayPeakNits=effectiveHdrDisplayPeakNits();
  const auto w=engine::hdrPeakWarning(sourcePeak,snap.sourceHdr,baseline.enabled,baseline,s.hdrMetadata);
  return {{"valid",w.valid},{"sourcePeakNits",int(w.sourcePeakNits)},{"capNits",int(w.capNits)},
          {"keptPercent",int(w.keptPercent)},{"clipped",w.clipped}};
}

bool QmlPlayerBridge::applyRecommendedHdrTuning(){
  const auto& snap=impl_->snapshot;
  const auto rec=engine::recommendHdrTuning(snap.sourceMasteringPeakNits,snap.sourceMaxCllNits,
      snap.sourceMaxFallNits,effectiveHdrDisplayPeakNits());
  if(!rec.available)return false;
  // The finding is the same in both modes; only the thing it names differs. In the
  // axes mode the content comes from the source and the environment stays the user's
  // choice, exactly like a manual pick there.
  if(hdrPresetMode()==QLatin1String("axes")){
    QString environment=impl_->prefString("hdrAxesEnvironment");
    if(environment.isEmpty())environment=QStringLiteral("compensate");
    const QString content=QString::fromLatin1(engine::hdrAxesContentForPreset(rec.presetId));
    if(!applyHdrAxesPreset(content,environment))return false;
    emit notice(tr("已按片源自动选档：%1").arg(content),false);
    return true;
  }
  const auto* preset=engine::findHdrTuningPreset(rec.presetId);
  if(!preset)return false;
  auto s=settings();
  // The strength dial and the display peak belong to the user: the axes pick
  // preserves them, so a recommendation must not reset them either.
  const auto strength=s.hdrCurve.strengthPercent;
  const auto displayPeak=s.hdrCurve.displayPeakNits;
  s.hdrCurve=preset->curve;s.hdrMetadata=preset->metadata;
  s.hdrCurve.strengthPercent=strength;
  s.hdrCurve.displayPeakNits=displayPeak;
  if(!commitHdrTuning(std::move(s)))return false;
  setHdrAppliedFrom(QStringLiteral("legacy"));
  // One line per application, for diagnostics: it records what the recommendation decided

  // and on which numbers, so an odd suggestion can be explained afterwards. Per-user

  // calibration was considered and dropped - thresholds tuned on one person film library

  // would be shipped to everyone else.
  veyra::log::info("hdr-recommend",std::format("applied preset={} reason={} cll={} fall={} mastering={} displayPeak={} mode={}",
      rec.presetId,rec.reasonKey,rec.cllNits,rec.fallNits,snap.sourceMasteringPeakNits,
      effectiveHdrDisplayPeakNits(),hdrPresetMode().toStdString()));
  emit notice(tr("已按片源自动选档：%1").arg(QString::fromLatin1(rec.presetId)),false);
  return true;
}

bool QmlPlayerBridge::applyHdrTuningPreset(const QString& id){
    for(const auto& p:engine::kHdrTuningPresets){
        if(id!=QLatin1String(p.id))continue;
        auto s=settings();
        // Picking a preset replaces the baseline, not the two dials: how hard the
        // user turned it (and which panel they told us about) must survive.
        const auto strength=s.hdrCurve.strengthPercent;
        const auto displayPeak=s.hdrCurve.displayPeakNits;
        s.hdrCurve=p.curve;s.hdrMetadata=p.metadata;
        s.hdrCurve.strengthPercent=strength;s.hdrCurve.displayPeakNits=displayPeak;
        if(!s.hdrCurve.valid()||!s.hdrMetadata.valid())return false;
        if(!commitHdrTuning(std::move(s)))return false;
        setHdrAppliedFrom(QStringLiteral("legacy"));
        return true;
    }
    return false;
}
bool QmlPlayerBridge::hdrMetadataEnabled()const{return settings().hdrMetadata.enabled;}
void QmlPlayerBridge::setHdrMetadataEnabled(bool enabled){
    auto s=settings();if(s.hdrMetadata.enabled==enabled)return;s.hdrMetadata.enabled=enabled;
    commitHdrTuning(std::move(s));
}
QVariantMap QmlPlayerBridge::hdrMetadataParams()const{
    const auto& m=settings().hdrMetadata;
    return {{"masteringPeakNits",int(m.masteringPeakNits)},{"maxCllNits",int(m.maxCllNits)},
            {"maxFallNits",int(m.maxFallNits)}};
}
bool QmlPlayerBridge::setHdrMetadataParameter(const QString& key,double value){
    if(!std::isfinite(value)||value<0||value>10000)return false;
    auto s=settings();const auto v=int(std::lround(value));
    if(v<0||v>10000)return false;
    if(key=="masteringPeakNits")s.hdrMetadata.masteringPeakNits=unsigned(v);
    else if(key=="maxCllNits")s.hdrMetadata.maxCllNits=unsigned(v);
    else if(key=="maxFallNits")s.hdrMetadata.maxFallNits=unsigned(v);
    else return false;
    if(!s.hdrMetadata.valid())return false;
    return commitHdrTuning(std::move(s));
}
void QmlPlayerBridge::setVideoHdr(bool enabled) {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::VideoHdr) {
            setEffectEnabled(int(i), enabled); return;
        }
    if (enabled) emit notice(tr("请先添加 RTX Video HDR 节点"), true);
}
// RTX Video HDR tuning lives on the chain's HDR node, like NR parameters on
// NR nodes: the same values AppShell's settings sliders edited.
namespace {
// Spatial anti-aliasing (#53): one node's values for the editors, and the edits they send.
QString antiAliasSummary(const engine::AntiAliasSettings& a) {
    using engine::AntiAliasMethod;
    if (a.method == AntiAliasMethod::Dlaa) return QStringLiteral("DLAA");
    if (a.method == AntiAliasMethod::Mlaa)
        return QStringLiteral("MLAA · ") + QmlPlayerBridge::tr("阈值 %1").arg(double(a.mlaaThreshold), 0, 'f', 2);
    const QStringList fxaa{QmlPlayerBridge::tr("中"), QmlPlayerBridge::tr("高"), QmlPlayerBridge::tr("极高")};
    const QStringList smaa{QmlPlayerBridge::tr("低"), QmlPlayerBridge::tr("中"), QmlPlayerBridge::tr("高"), QmlPlayerBridge::tr("极高")};
    QString text = a.method == AntiAliasMethod::Fxaa ? QStringLiteral("FXAA · ") + fxaa.value(int(a.quality))
                                                     : QStringLiteral("SMAA · ") + smaa.value(int(a.quality));
    if (a.usesTemporal())
        text += a.temporal == engine::AntiAliasTemporal::T2x ? QmlPlayerBridge::tr(" · 时域 T2x（实验）") : QmlPlayerBridge::tr(" · 时域 4x（实验）");
    return text;
}
QVariantMap antiAliasMap(const engine::ChainNode* node, int index) {
    const engine::AntiAliasSettings a = node ? node->aa : engine::AntiAliasSettings{};
    return {{"index", index}, {"present", node != nullptr}, {"enabled", node && node->enabled},
            {"method", int(a.method)}, {"quality", int(a.quality)},
            {"mlaaThreshold", double(a.mlaaThreshold)}, {"mlaaStrength", double(a.mlaaStrength)},
            {"temporal", int(a.temporal)}, {"historyWeight", double(a.historyWeight)},
            {"historyRejection", double(a.historyRejection)}, {"summary", antiAliasSummary(a)}};
}
// A method change starts from its high tier; the experimental modes keep Magpie's
// history defaults (T2x 0.5, 4x 0.75) unless the weight was tuned.
uint8_t defaultAntiAliasQuality(engine::AntiAliasMethod method) {
    return method == engine::AntiAliasMethod::Fxaa ? 1 : method == engine::AntiAliasMethod::Smaa ? 2 : 0;
}
float defaultHistoryWeight(engine::AntiAliasTemporal temporal) {
    return temporal == engine::AntiAliasTemporal::X4 ? 0.75f : 0.5f;
}
bool applyAntiAliasKey(engine::AntiAliasSettings& a, bool& enabled, const QString& key, double value) {
    using engine::AntiAliasMethod; using engine::AntiAliasTemporal;
    if (!std::isfinite(value)) return false;
    const int whole = int(std::lround(value));
    if (key == "enabled") { enabled = value != 0; return true; }
    if (key == "method") {
        if (whole < 0 || whole > 3) return false;
        const auto method = AntiAliasMethod(whole);
        if (method != a.method) { a.method = method; a.quality = defaultAntiAliasQuality(method); }
        if (method != AntiAliasMethod::Smaa) a.temporal = AntiAliasTemporal::Off;
        return true;
    }
    if (key == "quality") {
        if (whole < 0 || whole >= int(engine::antiAliasQualityCount(a.method))) return false;
        a.quality = uint8_t(whole); return true;
    }
    if (key == "temporal") {
        if (whole < 0 || whole > 2 || (whole && a.method != AntiAliasMethod::Smaa)) return false;
        const auto next = AntiAliasTemporal(whole);
        if (next != a.temporal && a.historyWeight == defaultHistoryWeight(a.temporal)) a.historyWeight = defaultHistoryWeight(next);
        a.temporal = next; return true;
    }
    if (key == "mlaaThreshold") { a.mlaaThreshold = float(value); return true; }
    if (key == "mlaaStrength") { a.mlaaStrength = float(value); return true; }
    if (key == "historyWeight") { a.historyWeight = float(value); return true; }
    if (key == "historyRejection") { a.historyRejection = float(value); return true; }
    return false;
}
}
// DLAA is DLSS SR at the picture's own size: available exactly where DLSS SR is,
// with the same reason shown when it is not.
bool QmlPlayerBridge::dlaaAllowed() {
    const auto dlss = effectCapabilities().value(QStringLiteral("sr0")).toMap();
    if (dlss.value(QStringLiteral("available")).toBool()) return true;
    const QString reason = dlss.value(QStringLiteral("reason")).toString();
    emit notice(reason.isEmpty() ? tr("DLAA 需要支持 DLSS 的 NVIDIA RTX 显卡") : tr("DLAA 不可用：%1").arg(reason), true);
    return false;
}
bool QmlPlayerBridge::antiAliasEnabled() const {
    const auto* node = impl_->chain.firstOf(engine::EffectType::AntiAliasing);
    return node && node->enabled;
}
void QmlPlayerBridge::setAntiAliasEnabled(bool enabled) {
    if (enabled != antiAliasEnabled()) (void)setAntiAliasParameter(QStringLiteral("enabled"), enabled ? 1 : 0);
}
QVariantMap QmlPlayerBridge::antiAliasParams() const {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::AntiAliasing) return antiAliasMap(&impl_->chain.nodes[i], int(i));
    return antiAliasMap(nullptr, -1);
}
QVariantMap QmlPlayerBridge::antiAliasParamsAt(int index) const {
    if (index < 0 || uint32_t(index) >= impl_->chain.nodeCount ||
        impl_->chain.nodes[index].type != engine::EffectType::AntiAliasing) return antiAliasMap(nullptr, -1);
    return antiAliasMap(&impl_->chain.nodes[index], index);
}
bool QmlPlayerBridge::setAntiAliasParameter(const QString& key, double value) {
    if (key == QLatin1String("method") && std::lround(value) == int(engine::AntiAliasMethod::Dlaa) && !dlaaAllowed()) return false;
    auto& c = impl_->chain;
    for (uint32_t i = 0; i < c.nodeCount; ++i)
        if (c.nodes[i].type == engine::EffectType::AntiAliasing) return setAntiAliasParameterAt(int(i), key, value);
    if (c.mode != engine::ChainMode::List) { emit notice(tr("请先添加抗锯齿节点"), true); return false; }
    // The list card's instance exists once it is used, stored after the grades
    // where toChain() puts it; the list order (compileChainExecutionPlan) runs it
    // behind the NR group when NR is on and in front of SR otherwise.
    engine::ChainNode node; node.type = engine::EffectType::AntiAliasing;
    bool enabled = false;
    if (!applyAntiAliasKey(node.aa, enabled, key, value)) return false;
    node.enabled = node.aa.enabled = enabled;
    if (!node.aa.validate().empty()) return false;
    if (c.nodeCount >= engine::kMaxChainNodes) { emit notice(tr("效果链已满"), true); return false; }
    uint32_t insertAt = 0;
    while (insertAt < c.nodeCount && c.nodes[insertAt].type == engine::EffectType::Color) ++insertAt;
    const auto before = std::make_unique<engine::EffectChain>(c);
    const int selected = impl_->selectedNr, colourSelected = impl_->selectedColour;
    for (uint32_t i = c.nodeCount; i > insertAt; --i) c.nodes[i] = c.nodes[i - 1];
    c.nodes[insertAt] = node; ++c.nodeCount;
    if (impl_->selectedNr >= int(insertAt)) ++impl_->selectedNr;
    if (impl_->selectedColour >= int(insertAt)) ++impl_->selectedColour;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        c = *before; impl_->selectedNr = selected; impl_->selectedColour = colourSelected; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged(); emitChainChanged();
    return true;
}
bool QmlPlayerBridge::setAntiAliasParameterAt(int index, const QString& key, double value) {
    if (key == QLatin1String("method") && std::lround(value) == int(engine::AntiAliasMethod::Dlaa) && !dlaaAllowed()) return false;
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::AntiAliasing) return false;
    auto& node = c.nodes[index];
    const auto before = node; const bool wasValid = chainValid();
    bool enabled = node.enabled;
    if (!applyAntiAliasKey(node.aa, enabled, key, value)) return false;
    node.enabled = node.aa.enabled = enabled;
    if (!node.aa.validate().empty()) { node = before; return false; }
    if (node == before) return true;
    const bool accepted = impl_->revalidate(true);
    if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
    emit settingsChanged();
    // The switch also strikes the chip through; parameters notify only their group.
    if (before.enabled != node.enabled) emitChainChanged();
    else emitParameterChanged(&QmlPlayerBridge::antiAliasChanged, wasValid);
    return accepted;
}
namespace {
// Beauty nodes: one node's values for the editors, the edits they send and the presets.
QString beautySummary(const engine::BeautySettings& b, bool enabled) {
    if (!enabled) return QmlPlayerBridge::tr("已关闭");
    QStringList parts;
    if (b.restore) parts << QmlPlayerBridge::tr("原画保护 %1%").arg(std::lround(b.restoreStrength * 100.0f));
    if (b.smooth) parts << QmlPlayerBridge::tr("磨皮 %1%").arg(std::lround(b.smoothAmount * 100.0f));
    return parts.isEmpty() ? QmlPlayerBridge::tr("未启用子功能") : parts.join(QStringLiteral(" · "));
}
QVariantMap beautyMap(const engine::ChainNode* node, int index, int layer) {
    const engine::BeautySettings b = node ? node->beauty : engine::BeautySettings{};
    return {{"index", index}, {"layer", layer}, {"present", node != nullptr}, {"enabled", node && node->enabled},
            {"restore", b.restore}, {"restoreStrength", double(b.restoreStrength)}, {"restoreScope", int(b.restoreScope)},
            {"regionExpand", double(b.regionExpand)}, {"feather", double(b.feather)},
            {"smooth", b.smooth}, {"strength", double(b.strength)}, {"smoothAmount", double(b.smoothAmount)},
            {"radius", double(b.radius)}, {"detail", double(b.detail)}, {"texture", double(b.texture)},
            {"brighten", double(b.brighten)}, {"scope", int(b.scope)},
            {"hueCenter", double(b.hueCenter)}, {"hueRange", double(b.hueRange)}, {"satMin", double(b.satMin)},
            {"satMax", double(b.satMax)}, {"lumaMin", double(b.lumaMin)}, {"lumaMax", double(b.lumaMax)},
            {"softness", double(b.softness)}, {"minConfidence", double(b.minConfidence)}, {"showMask", b.showMask},
            {"summary", beautySummary(b, node && node->enabled)}};
}
bool applyBeautyKey(engine::BeautySettings& b, bool& enabled, const QString& key, double value) {
    if (!std::isfinite(value)) return false;
    const int whole = int(std::lround(value));
    if (key == "enabled") { enabled = value != 0; return true; }
    if (key == "restore") { b.restore = value != 0; return true; }
    if (key == "smooth") { b.smooth = value != 0; return true; }
    if (key == "showMask") { b.showMask = value != 0; return true; }
    if (key == "restoreScope") { if (whole < 0 || whole > 1) return false; b.restoreScope = engine::BeautyRestoreScope(whole); return true; }
    if (key == "scope") { if (whole < 0 || whole > 1) return false; b.scope = engine::BeautyScope(whole); return true; }
    const std::pair<const char*, float engine::BeautySettings::*> floats[] = {
        {"restoreStrength", &engine::BeautySettings::restoreStrength}, {"regionExpand", &engine::BeautySettings::regionExpand},
        {"feather", &engine::BeautySettings::feather}, {"strength", &engine::BeautySettings::strength},
        {"smoothAmount", &engine::BeautySettings::smoothAmount}, {"radius", &engine::BeautySettings::radius},
        {"detail", &engine::BeautySettings::detail}, {"texture", &engine::BeautySettings::texture},
        {"brighten", &engine::BeautySettings::brighten}, {"hueCenter", &engine::BeautySettings::hueCenter},
        {"hueRange", &engine::BeautySettings::hueRange}, {"satMin", &engine::BeautySettings::satMin},
        {"satMax", &engine::BeautySettings::satMax}, {"lumaMin", &engine::BeautySettings::lumaMin},
        {"lumaMax", &engine::BeautySettings::lumaMax}, {"softness", &engine::BeautySettings::softness},
        {"minConfidence", &engine::BeautySettings::minConfidence}};
    for (const auto& [name, field] : floats)
        if (key == QLatin1String(name)) { b.*field = float(value); return true; }
    return false;
}
// Starting points, tuned on lit photos and dark game footage; each names the
// sub-switches it changes and leaves the skin range and the detection alone.
bool applyBeautyPresetValues(engine::BeautySettings& b, const QString& preset) {
    const auto natural = [&] { b.smooth = true; b.strength = 1.0f; b.smoothAmount = 0.6f; b.radius = 5.0f; b.detail = 0.6f; b.texture = 0.3f; };
    if (preset == "natural") { natural(); b.restore = false; return true; }
    if (preset == "light") {
        b.smooth = true; b.restore = false; b.strength = 1.0f; b.smoothAmount = 0.35f; b.radius = 3.0f; b.detail = 0.75f; b.texture = 0.4f;
        return true;
    }
    if (preset == "restore") { b.restore = true; b.restoreStrength = 1.0f; b.smooth = false; return true; }
    if (preset == "restoreNatural") { b.restore = true; b.restoreStrength = 1.0f; natural(); return true; }
    return false;
}
}
QVariantList QmlPlayerBridge::beautyLayers() const {
    QVariantList out;
    int layer = 0;
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::Beauty) out << beautyMap(&impl_->chain.nodes[i], int(i), layer++);
    return out;
}
QVariantMap QmlPlayerBridge::beautyDetection() const {
    const auto& d = impl_->chain.beautyDetection;
    return {{"quality", int(d.quality)}, {"maxFaces", int(d.maxFaces)}, {"stability", double(d.stability)}};
}
QString QmlPlayerBridge::beautyStatus() const {
    const auto& s = impl_->snapshot;
    if (!s.beautyActive) return {};
    if (!s.beautyNotice.empty()) return uiText(s.beautyNotice);
    return s.beautyFaces ? tr("检测到 %1 张人脸").arg(s.beautyFaces) : tr("未检测到人脸");
}
QVariantMap QmlPlayerBridge::beautyParamsAt(int index) const {
    const auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty) return beautyMap(nullptr, -1, -1);
    int layer = 0;
    for (int i = 0; i < index; ++i) layer += c.nodes[i].type == engine::EffectType::Beauty;
    return beautyMap(&c.nodes[index], index, layer);
}
bool QmlPlayerBridge::setBeautyParameterAt(int index, const QString& key, double value) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty) return false;
    auto& node = c.nodes[index];
    const auto before = node; const bool wasValid = chainValid();
    bool enabled = node.enabled;
    if (!applyBeautyKey(node.beauty, enabled, key, value)) return false;
    node.enabled = node.beauty.enabled = enabled;
    if (!node.beauty.validate().empty()) { node = before; return false; }
    if (node == before) return true;
    const bool accepted = impl_->revalidate(true);
    if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
    emit settingsChanged();
    // The switch also strikes the chip through; parameters notify only their group.
    if (before.enabled != node.enabled) emitChainChanged();
    else emitParameterChanged(&QmlPlayerBridge::beautyChanged, wasValid);
    return accepted;
}
int QmlPlayerBridge::duplicateBeautyLayer(int index) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty) return -1;
    if (c.mode != engine::ChainMode::List) { emit notice(tr("节点模式请用节点的复制"), true); return -1; }
    if (c.countOf(engine::EffectType::Beauty) >= engine::kMaxBeautyInstances) {
        emit notice(tr("%1 最多 %2 个").arg(tr("美颜")).arg(engine::kMaxBeautyInstances), true); return -1;
    }
    if (c.nodeCount >= engine::kMaxChainNodes) { emit notice(tr("效果链已满"), true); return -1; }
    const auto before = std::make_unique<engine::EffectChain>(c);
    const int selected = impl_->selectedNr, colourSelected = impl_->selectedColour;
    const uint32_t at = uint32_t(index) + 1;
    for (uint32_t i = c.nodeCount; i > at; --i) c.nodes[i] = c.nodes[i - 1];
    c.nodes[at] = c.nodes[index]; ++c.nodeCount;
    if (impl_->selectedNr >= int(at)) ++impl_->selectedNr;
    if (impl_->selectedColour >= int(at)) ++impl_->selectedColour;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        c = *before; impl_->selectedNr = selected; impl_->selectedColour = colourSelected; impl_->revalidate();
        emit notice(uiText(error), true); return -1;
    }
    emit settingsChanged(); emitChainChanged();
    return int(at);
}
bool QmlPlayerBridge::moveBeautyLayer(int index, int delta) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty || !delta) return false;
    int other = index;
    do other += delta < 0 ? -1 : 1;
    while (other >= 0 && uint32_t(other) < c.nodeCount && c.nodes[other].type != engine::EffectType::Beauty);
    if (other < 0 || uint32_t(other) >= c.nodeCount) return false;
    const auto before = std::make_unique<engine::EffectChain>(c);
    const auto viewA = std::pair{c.nodes[index].viewX, c.nodes[index].viewY};
    const auto viewB = std::pair{c.nodes[other].viewX, c.nodes[other].viewY};
    std::swap(c.nodes[index], c.nodes[other]);
    // Node mode keeps the canvas positions with the slot; only the parameters trade places.
    c.nodes[index].viewX = viewA.first; c.nodes[index].viewY = viewA.second;
    c.nodes[other].viewX = viewB.first; c.nodes[other].viewY = viewB.second;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; c = *before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged(); emitChainChanged();
    return true;
}
bool QmlPlayerBridge::applyBeautyPreset(int index, const QString& preset) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty) return false;
    auto& node = c.nodes[index];
    const auto before = node;
    if (!applyBeautyPresetValues(node.beauty, preset)) return false;
    node.enabled = node.beauty.enabled = true;
    if (!node.beauty.validate().empty()) { node = before; return false; }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; node = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged(); emitChainChanged();
    return true;
}
bool QmlPlayerBridge::setBeautyDetection(const QString& key, double value) {
    auto& c = impl_->chain;
    if (!std::isfinite(value)) return false;
    auto next = c.beautyDetection;
    const int whole = int(std::lround(value));
    if (key == "quality") next.quality = uint8_t(std::clamp(whole, 0, 2));
    else if (key == "maxFaces") next.maxFaces = uint8_t(std::clamp(whole, 1, 8));
    else if (key == "stability") next.stability = float(value);
    else return false;
    if (!next.validate().empty()) return false;
    if (next == c.beautyDetection) return true;
    const auto before = c.beautyDetection; const bool wasValid = chainValid();
    c.beautyDetection = next;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; c.beautyDetection = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged();
    emitParameterChanged(&QmlPlayerBridge::beautyChanged, wasValid);
    return true;
}
bool QmlPlayerBridge::beautyAnyEnabled() const {
    const auto& c = impl_->chain;
    for (uint32_t i = 0; i < c.nodeCount; ++i) if (c.nodes[i].type == engine::EffectType::Beauty && c.nodes[i].enabled) return true;
    return false;
}
bool QmlPlayerBridge::setAllBeautyEnabled(bool enabled) {
    auto& c = impl_->chain;
    const auto before = std::make_unique<engine::EffectChain>(c);
    std::vector<uint32_t> layers;
    for (uint32_t i = 0; i < c.nodeCount; ++i) if (c.nodes[i].type == engine::EffectType::Beauty) layers.push_back(i);
    if (layers.empty()) return false;
    auto& restore = impl_->beautyMasterRestore;
    if (!enabled) {
        restore.clear();
        for (const auto i : layers) { restore.push_back(c.nodes[i].enabled); c.nodes[i].enabled = c.nodes[i].beauty.enabled = false; }
    } else {
        // As the NR switch: layers added or removed meanwhile bring every layer back.
        const bool faithful = restore.size() == layers.size() && std::find(restore.begin(), restore.end(), true) != restore.end();
        for (size_t k = 0; k < layers.size(); ++k) c.nodes[layers[k]].enabled = c.nodes[layers[k]].beauty.enabled = faithful ? bool(restore[k]) : true;
    }
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; c = *before; impl_->revalidate();
        emit notice(uiText(error), true); emitChainChanged(); return false;
    }
    veyra::log::info("ui-beauty", std::format("group switch {} layers={}", enabled ? "on" : "off", layers.size()));
    emitChainChanged(); emit settingsChanged();
    return true;
}
bool QmlPlayerBridge::resetBeautyLayer(int index) {
    auto& c = impl_->chain;
    if (index < 0 || uint32_t(index) >= c.nodeCount || c.nodes[index].type != engine::EffectType::Beauty) return false;
    auto& node = c.nodes[index];
    const auto before = node;
    node.beauty = engine::BeautySettings{};
    node.beauty.enabled = node.enabled;
    if (node == before) return true;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; node = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged(); emitChainChanged();
    return true;
}
QVariantMap QmlPlayerBridge::videoHdrParams() const {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::VideoHdr) {
            const auto& v = impl_->chain.nodes[i].videoHdr;
            return {{"contrast", int(v.contrast)}, {"saturation", int(v.saturation)},
                    {"middleGray", int(v.middleGray)}, {"peakNits", int(v.peakNits)}};
        }
    return {};
}
bool QmlPlayerBridge::setVideoHdrParameter(const QString& key, double value) {
    if (!std::isfinite(value)) return false;
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i) {
        auto& node = impl_->chain.nodes[i];
        if (node.type != engine::EffectType::VideoHdr) continue;
        const auto before = node; const bool wasValid = chainValid();
        if (value < 0) return false;
        const auto v = unsigned(std::lround(value));
        if (key == "contrast") node.videoHdr.contrast = v;
        else if (key == "saturation") node.videoHdr.saturation = v;
        else if (key == "middleGray") node.videoHdr.middleGray = v;
        else if (key == "peakNits") node.videoHdr.peakNits = v;
        else return false;
        if (!node.videoHdr.valid()) { node = before; return false; }
        if (node == before) return true;
        const bool accepted = impl_->revalidate(true);
        if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
        emit settingsChanged(); emitParameterChanged(&QmlPlayerBridge::videoHdrParamsChanged, wasValid); return accepted;
    }
    emit notice(tr("请先添加 RTX Video HDR 节点"), true);
    return false;
}
bool QmlPlayerBridge::fgEnabled() const {
    const auto* node = impl_->chain.firstOf(engine::EffectType::FrameGeneration);
    return node && node->enabled;
}
void QmlPlayerBridge::setFgEnabled(bool enabled) {
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i)
        if (impl_->chain.nodes[i].type == engine::EffectType::FrameGeneration) {
            if (impl_->chain.nodes[i].enabled == enabled) return;
            // Turning on keeps the last chosen multiplier (at least 2X).
            if (enabled && impl_->chain.fgMultiplier < 2) impl_->chain.fgMultiplier = 2;
            setEffectEnabled(int(i), enabled); emit settingsChanged(); return;
        }
    if (enabled) emit notice(tr("请先添加补帧节点"), true);
}
int QmlPlayerBridge::fgMultiplier() const {
    const auto* node = impl_->chain.firstOf(engine::EffectType::FrameGeneration);
    return node && node->enabled ? int(impl_->chain.fgMultiplier) : 1;
}
void QmlPlayerBridge::setFgMultiplier(int multiplier) {
    const auto choices=settings().frameGenerationBackend==engine::FrameGenerationBackend::Vfg?
        std::span<const uint32_t>(engine::kVfgMultiplierChoices):settings().frameGenerationBackend==engine::FrameGenerationBackend::Anr?std::span<const uint32_t>(engine::kAnrMultiplierChoices):std::span<const uint32_t>(engine::kFgMultiplierChoices);
    const bool legal = std::find(choices.begin(),choices.end(),uint32_t(std::max(multiplier,0)))!=choices.end();
    if (!legal || multiplier > fgMaxMultiplier()) {
        emit notice(tr("补帧倍率超出当前后端范围"), true); return;
    }
    auto& chain = impl_->chain;
    auto* node = chain.firstOf(engine::EffectType::FrameGeneration);
    if (!node) { if (multiplier > 1) emit notice(tr("请先添加补帧节点"), true); return; }
    const bool enabled = multiplier > 1;
    if (node->enabled == enabled && (!enabled || chain.fgMultiplier == uint32_t(multiplier))) return;
    const auto beforeMultiplier = chain.fgMultiplier; const auto beforeEnabled = node->enabled;
    node->enabled = enabled; if (enabled) chain.fgMultiplier = uint32_t(multiplier);
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        node->enabled = beforeEnabled; chain.fgMultiplier = beforeMultiplier; impl_->revalidate();
        emit notice(uiText(error), true);
    }
    emit settingsChanged(); emitChainChanged();
}

bool QmlPlayerBridge::colorEnabled() const {return selectedColourSettings().enabled;}
void QmlPlayerBridge::setColorEnabled(bool enabled){
    auto c=selectedColourSettings();c.enabled=enabled;commitColour(c);
}

bool QmlPlayerBridge::protectionEnabled() const {return maskState(false).value("enabled").toBool();}
void QmlPlayerBridge::setProtectionEnabled(bool enabled) {setMaskEnabled(false,enabled);}
QVariantMap QmlPlayerBridge::protectionState() const {return maskState(false);}

bool QmlPlayerBridge::setProtectionRegion(double left,double top,double right,double bottom,double feather) {
    for (double v : {left,top,right,bottom,feather}) if (!std::isfinite(v)) return false;
    if (left<0 || top<0 || right>1 || bottom>1 || left>right || top>bottom || feather<0 || feather>64) return false;
    for (uint32_t i=0;i<impl_->chain.nodeCount;++i) {
        auto& node=impl_->chain.nodes[i];
        if (node.type!=engine::EffectType::Protection) continue;
        const auto before=node;const bool wasValid=chainValid();
        // Explicit edits replace legacy multi-rectangle settings with the one global region.
        node.protection.regions={};
        node.protection.regions[0]={float(left),float(top),float(right),float(bottom)};
        node.protection.featherPixels=float(feather);
        if(node==before)return true;
        const bool accepted=impl_->revalidate(true);
        if(!accepted){const auto error=impl_->validation.message;node=before;impl_->revalidate();emit notice(uiText(error),true);}
        emit settingsChanged();emitParameterChanged(&QmlPlayerBridge::protectionChanged,wasValid);return accepted;
    }
    return false;
}

// The inspector edits the selected list node, not the first flat NR mirror.
#define VEYRA_NR_PROP(getter,setter,member,type)                              \
    type QmlPlayerBridge::getter() const {                                  \
        const auto* n=impl_->nrNode(); const engine::ChainNode fallback{};    \
        return type((n?*n:fallback).member);                                 \
    }                                                                      \
    void QmlPlayerBridge::setter(type value){                               \
        auto* n=impl_->nrNode(); if(!n||type(n->member)==value)return;        \
        const auto before=*n; const bool wasValid=chainValid();             \
        n->member=decltype(n->member)(value);                                \
        if(!impl_->revalidate(true)){                                       \
            const auto error=impl_->validation.message; *n=before;          \
            impl_->revalidate();emit notice(uiText(error),true);             \
        }                                                                  \
        emit settingsChanged();                                              \
        if(before.enabled!=n->enabled)emitChainChanged();                    \
        else emitParameterChanged(&QmlPlayerBridge::nrLayersChanged,wasValid); \
    }
VEYRA_NR_PROP(nrEnabled,setNrEnabled,enabled,bool)
VEYRA_NR_PROP(nrTemporal,setNrTemporal,nr.temporal,bool)
VEYRA_NR_PROP(nrIntensity,setNrIntensity,nr.model.intensity,double)
VEYRA_NR_PROP(nrTone,setNrTone,nr.model.tone,double)
VEYRA_NR_PROP(nrStructure,setNrStructure,nr.model.structure,double)
VEYRA_NR_PROP(nrSkin,setNrSkin,nr.model.skin,double)
VEYRA_NR_PROP(nrAutoMask,setNrAutoMask,nr.model.autoMask,bool)
VEYRA_NR_PROP(nrUiCorrection,setNrUiCorrection,nr.model.uiCorrection,bool)
VEYRA_NR_PROP(nrStyle,setNrStyle,nr.model.style,int)
#undef VEYRA_NR_PROP
int QmlPlayerBridge::selectedNrLayer() const{return impl_->nrIndex();}
void QmlPlayerBridge::setSelectedNrLayer(int index){
    if(index<0||uint32_t(index)>=impl_->chain.nodeCount||
       impl_->chain.nodes[index].type!=engine::EffectType::NrEnhance||index==impl_->selectedNr)return;
    impl_->selectedNr=index;emit settingsChanged();
}
bool QmlPlayerBridge::lowLatency() const{return settings().lowLatency;}
void QmlPlayerBridge::setLowLatency(bool value){
    const auto before=impl_->chain;
    for(uint32_t i=0;i<impl_->chain.nodeCount;++i)if(impl_->chain.nodes[i].type==engine::EffectType::NrEnhance)
        impl_->chain.nodes[i].nr.lowLatencyPairing=value;
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;impl_->chain=before;
        impl_->revalidate();emit notice(uiText(error),true);
    }
    emit settingsChanged();emitChainChanged();
}

// Stage-5 output stabiliser (anti-flicker). One independent node controls the
// existing pass after the whole NR stack and residual composite. Strength 0 is the
// default and the graph then skips the dispatch entirely, so committing it
// never costs anything until the user turns it on.
double QmlPlayerBridge::nrHoldStrength() const { return double(settings().nrHoldStrength); }
void QmlPlayerBridge::setNrHoldStrength(double value) {
    if(!std::isfinite(value)||value<0||value>1)return;
    auto* node=impl_->chain.firstOf(engine::EffectType::NrStabilizer);
    if(!node){
        if(value==0)return;
        const int index=addEffect(QStringLiteral("nr-stabilizer"));
        if(index<0)return;
        node=&impl_->chain.nodes[uint32_t(index)];
    }
    const auto previous=*node;
    const bool wasValid=chainValid();
    node->enabled=value>0;
    if(value>0)node->holdStrength=float(value);
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;*node=previous;impl_->revalidate();
        emit notice(uiText(error),true);return;
    }
    impl_->prefs[QStringLiteral("nrHoldStrength")]=value;
    if(!impl_->savePrefs())veyra::log::warn("ui-prefs","output stabiliser strength not saved");
    emit settingsChanged();
    if(previous.enabled!=node->enabled)emitChainChanged();
    else emitParameterChanged(&QmlPlayerBridge::nrStabilizerChanged,wasValid);
}
// NR inference reuse (pipeline/NrReuse.h), realtime preview only. 0 off,
// 1 Auto, 2/3/4 reuse one frame in every 2/3/4. Its existing list-only admission stays.
int QmlPlayerBridge::nrReuseMode() const { return int(settings().nrReuse); }
void QmlPlayerBridge::setNrReuseMode(int value) {
    const auto mode = static_cast<engine::NrReuseMode>(value);
    if (value < 0 || !engine::validNrReuseMode(mode)) return;
    auto s = settings();
    if (s.nrReuse == mode) return;
    s.nrReuse = mode;
    if (!impl_->commit(std::move(s))) return;
    impl_->prefs[QStringLiteral("nrReuse")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "NR inference reuse mode not saved");
    emit settingsChanged();
}
double QmlPlayerBridge::nrHoldTolerance() const { return double(settings().nrHoldTolerance); }
void QmlPlayerBridge::setNrHoldTolerance(double value) {
    if(!std::isfinite(value)||value<0||value>1)return;
    auto* node=impl_->chain.firstOf(engine::EffectType::NrStabilizer);
    if(node){
        const int index=int(node-impl_->chain.nodes.data());
        if(!setStabilizerParameter(index,QStringLiteral("tolerance"),value))return;
    }else{
        auto settings=this->settings();settings.nrHoldTolerance=float(value);
        if(!impl_->commit(settings))return;
    }
    impl_->prefs[QStringLiteral("nrHoldTolerance")]=value;
    if(!impl_->savePrefs())veyra::log::warn("ui-prefs","output stabiliser tolerance not saved");
    emit settingsChanged();
}

bool QmlPlayerBridge::applying() const { return impl_->facade.applying(); }

// Volume and mute are engine state (the audio clock owns them), so they go
// straight to the engine and come back through the snapshot: the UI shows what
// the engine did, not what it asked for.
double QmlPlayerBridge::volume() const { return double(impl_->snapshot.volume); }
void QmlPlayerBridge::setVolume(double value) {
    if (!std::isfinite(value)) return;
    const double volume = std::clamp(value, 0.0, 1.0);
    impl_->engine.setVolume(float(volume), impl_->snapshot.muted);
    // Kept for the next launch (user 2026-11-08). A slider drag calls this for
    // every move, so the file is written 0.5 s after the last change and on exit.
    const QString key = QStringLiteral("volume");
    if (impl_->prefs.contains(key) && impl_->prefs.value(key).toDouble() == volume) return;
    impl_->prefs[key] = volume;
    if (impl_->volumeSaveTimer) impl_->volumeSaveTimer->start();
}
bool QmlPlayerBridge::muted() const { return impl_->snapshot.muted; }
void QmlPlayerBridge::setMuted(bool value) {
    impl_->engine.setVolume(impl_->snapshot.volume, value);
}
double QmlPlayerBridge::playbackSpeed() const { return impl_->snapshot.playbackSpeed; }
double QmlPlayerBridge::playbackRate() const { return impl_->snapshot.playbackRate; }
double QmlPlayerBridge::minimumPlaybackRate() const { return engine::kMinPlaybackRate; }
double QmlPlayerBridge::maximumPlaybackRate() const { return engine::kMaxPlaybackRate; }
void QmlPlayerBridge::setPlaybackRate(double rate) {
    if(!impl_->engine.setPlaybackRate(rate))return;
    impl_->snapshot.playbackRate=rate;emit snapshotChanged();
}


} // namespace veyra::ui
