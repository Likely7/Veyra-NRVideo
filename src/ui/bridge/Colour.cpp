// QML bridge: Colour responsibilities.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
// --- colour ------------------------------------------------------------------
double QmlPlayerBridge::colorExposure() const { return selectedColourSettings().exposure; }
void QmlPlayerBridge::setColorExposure(double v) {
    setColourParameter(QStringLiteral("exposure"),v);
}
double QmlPlayerBridge::colorContrast() const { return selectedColourSettings().contrast; }
void QmlPlayerBridge::setColorContrast(double v) {
    setColourParameter(QStringLiteral("contrast"),v);
}
double QmlPlayerBridge::colorSaturation() const { return selectedColourSettings().saturation; }
void QmlPlayerBridge::setColorSaturation(double v) {
    setColourParameter(QStringLiteral("saturation"),v);
}
double QmlPlayerBridge::colorTemperature() const { return selectedColourSettings().temperature; }
void QmlPlayerBridge::setColorTemperature(double v) {
    setColourParameter(QStringLiteral("temperature"),v);
}

void QmlPlayerBridge::resetCurrentPage() {
    // Reset only the enhancement settings this page edits. Presets, recent files,
    // the capture session and the export selection are deliberately untouched.
    auto s = settings();
    s.nr = false; s.sr = false; s.multiplier = 1;
    s.lowLatency = false; s.nrTemporal = false;
    s.nrLayers = {}; s.nrLayerCount = 0;
    s.model = engine::NrSettings{};
    s.residual = engine::ResidualSettings{};
    s.protection = engine::ProtectionSettings{};
    s.color = engine::ColorSettings{};
    s.additionalColors = {}; s.additionalColorCount = 0;
    s.antiAlias = engine::AntiAliasSettings{}; s.additionalAntiAlias = {}; s.additionalAntiAliasCount = 0;
    s.beauty = engine::BeautySettings{}; s.additionalBeauty = {}; s.additionalBeautyCount = 0; s.beautyDetection = {};
    s.videoHdr = engine::VideoHdrSettings{};
    const auto before=impl_->chain; const auto selected=impl_->selectedNr;
    const auto oldLayout = impl_->layout;
    const auto selectedColour=impl_->selectedColour;
    impl_->chain=engine::toChain(s); impl_->selectedNr=-1; impl_->selectedColour=-1;
    impl_->chain.mode = before.mode;
    if (before.mode == engine::ChainMode::Node) {
        engine::removeLegacyNodeProtection(impl_->chain);
        const auto initialized = impl_->layout.initialize(impl_->chain);
        if (!initialized.accepted) {
            impl_->chain = before; impl_->layout = oldLayout;
            impl_->selectedNr = selected; impl_->selectedColour = selectedColour;
            emit notice(uiText(initialized.message), true); return;
        }
        impl_->initializeDefaultNodePositions();
    }
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;
        impl_->chain=before;impl_->layout=oldLayout;impl_->selectedNr=selected;impl_->selectedColour=selectedColour;impl_->revalidate();
        emit notice(uiText(error),true);
    }
    emit settingsChanged();
    emitChainChanged();
}


// --- colour page -------------------------------------------------------------
// One table of the parameters the engine actually carries, so a control can only
// exist for a real field, and the UI can build itself from this catalogue.
namespace {
struct ColourParam {
    const char* name;
    const char* label;
    const char* group;
    float minimum, maximum, step;
};
// Names match ColorSettings' own members; the mixer bands are indexed.
const ColourParam kColourParams[] = {
    {"exposure",      "曝光（EV）",    "亮",       -5,     5,   0.05f},
    {"contrast",      "对比度",        "亮",      -100,   100,   1},
    {"highlights",    "高光",          "亮",      -100,   100,   1},
    {"shadows",       "阴影",          "亮",      -100,   100,   1},
    {"whites",        "白色",          "亮",      -100,   100,   1},
    {"blacks",        "黑色",          "亮",      -100,   100,   1},
    {"texture",       "纹理",          "效果",    -100,   100,   1},
    {"clarity",       "清晰度",        "效果",    -100,   100,   1},
    {"dehaze",        "去朦胧",        "效果",    -100,   100,   1},
    {"temperature",   "色温（相对）",  "颜色",    -100,   100,   1},
    {"tint",          "色调",          "颜色",    -100,   100,   1},
    {"vibrance",      "自然饱和度",    "颜色",    -100,   100,   1},
    {"saturation",    "饱和度",        "颜色",    -100,   100,   1},
    {"paramHighlights","参数化高光",   "曲线",    -100,   100,   1},
    {"paramLights",   "参数化亮调",    "曲线",    -100,   100,   1},
    {"paramDarks",    "参数化暗调",    "曲线",    -100,   100,   1},
    {"paramShadows",  "参数化阴影",    "曲线",    -100,   100,   1},
    {"splitHighlights","分离高光",     "曲线", -100,  100,   1},
    {"splitMidtones", "分离中间调",    "曲线", -100,  100,   1},
    {"splitShadows",  "分离阴影",      "曲线", -100,  100,   1},
    {"gradingBlending","混合",         "颜色分级",   0,   100,   1},
    {"gradingBalance","平衡",          "颜色分级", -100,   100,   1},
    {"calibrationShadowTint","阴影色调","校准",    -100,   100,   1},
    {"lutStrength",   "LUT 强度",      "LUT",        0,   100,   1},
};

float* colourScalar(engine::ColorSettings& c, const char* name) {
    const std::string_view n{name};
    if (n == "exposure") return &c.exposure;
    if (n == "contrast") return &c.contrast;
    if (n == "highlights") return &c.highlights;
    if (n == "shadows") return &c.shadows;
    if (n == "whites") return &c.whites;
    if (n == "blacks") return &c.blacks;
    if (n == "texture") return &c.texture;
    if (n == "clarity") return &c.clarity;
    if (n == "dehaze") return &c.dehaze;
    if (n == "temperature") return &c.temperature;
    if (n == "tint") return &c.tint;
    if (n == "vibrance") return &c.vibrance;
    if (n == "saturation") return &c.saturation;
    if (n == "paramHighlights") return &c.paramHighlights;
    if (n == "paramLights") return &c.paramLights;
    if (n == "paramDarks") return &c.paramDarks;
    if (n == "paramShadows") return &c.paramShadows;
    if (n == "splitHighlights") return &c.splitHighlights;
    if (n == "splitMidtones") return &c.splitMidtones;
    if (n == "splitShadows") return &c.splitShadows;
    if (n == "gradingBlending") return &c.gradingBlending;
    if (n == "gradingBalance") return &c.gradingBalance;
    if (n == "calibrationShadowTint") return &c.calibrationShadowTint;
    if (n == "lutStrength") return &c.lutStrength;
    // Array parameters retain the existing engine units; no UI-only mirrors.
    const auto parts=QString::fromUtf8(name).split('.');
    bool ok=false;
    const int index=parts.size()>=2?parts[1].toInt(&ok):-1;
    if(!ok||index<0)return nullptr;
    if(parts.size()==2){
        if(index<engine::kColorMixerBands){
            if(parts[0]=="mixerHue")return &c.mixerHue[index];
            if(parts[0]=="mixerSaturation")return &c.mixerSaturation[index];
            if(parts[0]=="mixerLuminance")return &c.mixerLuminance[index];
            if(parts[0]=="blackWhiteMix")return &c.blackWhiteMix[index];
        }
        if(index<3){
            if(parts[0]=="calibrationHue")return &c.calibrationHue[index];
            if(parts[0]=="calibrationSaturation")return &c.calibrationSaturation[index];
        }
    }
    if(parts.size()==3&&parts[0]=="grading"&&index<engine::kColorGradingZones){
        if(parts[2]=="hue")return &c.grading[index].hue;
        if(parts[2]=="saturation")return &c.grading[index].saturation;
        if(parts[2]=="luminance")return &c.grading[index].luminance;
    }
    return nullptr;
}
} // namespace

QVariantList QmlPlayerBridge::colourParameters() const {
    QVariantList out;
    engine::ColorSettings defaults;
    for (const auto& p : kColourParams) {
        QVariantMap item;
        item["name"] = QString::fromUtf8(p.name);
        item["label"] = uiText(std::string_view(p.label));
        item["group"] = uiText(std::string_view(p.group));
        item["min"] = double(p.minimum);
        item["max"] = double(p.maximum);
        item["step"] = double(p.step);
        if (const auto* value = colourScalar(defaults, p.name)) item["defaultValue"] = double(*value);
        // Centred controls read their fill from the middle, as the design's sliders
        // do for anything that can go either way.
        item["center"] = p.minimum < 0 && p.maximum > 0;
        out << item;
    }
    return out;
}

QVariantList QmlPlayerBridge::colourGroups() const {
    // Grouped in catalogue order, so the sections appear in the design's order
    // rather than in whatever order a hash would produce.
    QVariantList groups;
    QStringList order;
    QHash<QString, QVariantList> buckets;
    for (const auto& p : kColourParams) {
        const QString group = uiText(std::string_view(p.group));
        if (!buckets.contains(group)) order << group;
        QVariantMap item;
        item["name"] = QString::fromUtf8(p.name);
        item["label"] = uiText(std::string_view(p.label));
        item["min"] = double(p.minimum);
        item["max"] = double(p.maximum);
        item["center"] = p.minimum < 0 && p.maximum > 0;
        buckets[group] << item;
    }
    for (const auto& group : order) {
        QVariantMap entry;
        entry["group"] = group;
        entry["items"] = buckets.value(group);
        groups << entry;
    }
    return groups;
}

double QmlPlayerBridge::colourParameter(const QString& name) const {
    auto c = selectedColourSettings();
    const auto utf8 = name.toUtf8();
    if (float* field = colourScalar(c, utf8.constData())) return double(*field);
    return 0.0;
}

bool QmlPlayerBridge::setColourParameter(const QString& name, double value) {
    if(!std::isfinite(value)){emit notice(tr("调色值必须是有限数值"),true);return false;}
    auto c = selectedColourSettings();
    const auto utf8 = name.toUtf8();
    float* field = colourScalar(c, utf8.constData());
    if (field == nullptr) return false;
    const float next = float(value);
    if (*field == next) return true;
    *field = next;
    return commitColour(c);
}

bool QmlPlayerBridge::resetColourParameter(const QString& name) {
    engine::ColorSettings defaults;
    const auto utf8 = name.toUtf8();
    float* field = colourScalar(defaults, utf8.constData());
    if (field == nullptr) return false;
    return setColourParameter(name,*field);
}

int QmlPlayerBridge::selectedColourLayer() const {return impl_->colourIndex();}
void QmlPlayerBridge::setSelectedColourLayer(int index){
    if(index<0||uint32_t(index)>=impl_->chain.nodeCount||
       impl_->chain.nodes[index].type!=engine::EffectType::Color||impl_->selectedColour==index)return;
    impl_->selectedColour=index;emit settingsChanged();
}
engine::ColorSettings QmlPlayerBridge::selectedColourSettings() const {
    const int index=impl_->colourIndex();
    if(index<0)return {};
    auto colour=impl_->chain.nodes[index].color;
    colour.enabled=impl_->chain.nodes[index].enabled;
    return colour;
}
bool QmlPlayerBridge::commitColour(const engine::ColorSettings& colour){
    const int index=impl_->colourIndex();
    if(index<0){emit notice(tr("处理链中没有调色节点，未更改设置"),true);return false;}
    if(const auto error=colour.validate();!error.empty()){
        emit notice(tr("调色参数被拒绝：%1").arg(uiText(error)),true);return false;
    }
    auto& node=impl_->chain.nodes[index];
    if(node.color==colour&&node.enabled==colour.enabled)return true;
    const auto previous=node;const bool wasValid=chainValid();
    node.color=colour;node.enabled=colour.enabled;
    if(!impl_->revalidate(true)){
        const auto error=impl_->validation.message;node=previous;impl_->revalidate();
        emit notice(uiText(error),true);emit settingsChanged();return false;
    }
    if(!impl_->colourReplaying){
        const qint64 now=QDateTime::currentMSecsSinceEpoch();
        const bool sameDrag=index==impl_->colourLastIndex&&now-impl_->colourLastPushMs<600&&!impl_->colourUndo.empty();
        if(!sameDrag){
            auto before=previous.color;before.enabled=previous.enabled;
            impl_->colourUndo.push_back({index,before});
            if(impl_->colourUndo.size()>64)impl_->colourUndo.erase(impl_->colourUndo.begin());
        }
        impl_->colourLastPushMs=now;impl_->colourLastIndex=index;impl_->colourRedo.clear();
    }
    emit settingsChanged();
    if(previous.enabled!=node.enabled)emitChainChanged();else emitParameterChanged(&QmlPlayerBridge::colourParamsChanged,wasValid);
    return true;
}
QVariantMap QmlPlayerBridge::colourState() const { return colourStateAt(impl_->colourIndex()); }
// Any colour node's full state, so several node cards can show their own
// values at once; edits still go through the selected-layer setters.
QVariantMap QmlPlayerBridge::colourStateAt(int index) const {
    engine::ColorSettings c{};
    if(index>=0&&uint32_t(index)<impl_->chain.nodeCount&&impl_->chain.nodes[index].type==engine::EffectType::Color){
        c=impl_->chain.nodes[index].color;c.enabled=impl_->chain.nodes[index].enabled;
    }else index=-1;
    QVariantMap out;
    out["index"]=index;out["enabled"]=c.enabled;
    out["blackWhite"]=c.blackWhite;out["groupBypassMask"]=int(c.groupBypassMask);
    for(const auto& p:kColourParams)out[QString::fromUtf8(p.name)]=double(*colourScalar(c,p.name));
    const auto list=[](const auto& values){QVariantList out;for(const auto v:values)out<<double(v);return out;};
    out["mixerHue"]=list(c.mixerHue);out["mixerSaturation"]=list(c.mixerSaturation);
    out["mixerLuminance"]=list(c.mixerLuminance);out["blackWhiteMix"]=list(c.blackWhiteMix);
    out["calibrationHue"]=list(c.calibrationHue);out["calibrationSaturation"]=list(c.calibrationSaturation);
    QVariantList curves,wheels,samples;
    for(const auto& curve:c.curves){
        QVariantList points;
        for(int i=0;i<curve.count;++i)points<<QVariantMap{{"x",double(curve.points[i].x)},{"y",double(curve.points[i].y)}};
        curves<<QVariant(points);
        QVariantList values;
        for(int i=0;i<=128;++i)values<<double(pipeline::ColorGradeTables::curveValue(curve,float(i)/128.0f));
        samples<<QVariant(values);
    }
    for(const auto& wheel:c.grading)wheels<<QVariantMap{{"hue",double(wheel.hue)},
        {"saturation",double(wheel.saturation)},{"luminance",double(wheel.luminance)}};
    out["curves"]=curves;out["curveSamples"]=samples;out["grading"]=wheels;
    out["lutName"]=QString::fromStdWString(c.lutNameString());out["lutInputSpace"]=c.lutInputSpace;
    return out;
}
bool QmlPlayerBridge::setColourCurve(int channel,const QVariantList& points){
    if(channel<0||channel>=4||points.size()<2||points.size()>engine::kColorCurvePoints)return false;
    auto c=selectedColourSettings();auto& curve=c.curves[channel];curve.reset();curve.count=int(points.size());
    for(int i=0;i<curve.count;++i){
        const auto point=points[i].toMap();bool xok=false,yok=false;
        const double x=point.value("x").toDouble(&xok),y=point.value("y").toDouble(&yok);
        if(!xok||!yok||!std::isfinite(x)||!std::isfinite(y))return false;
        curve.points[i]={float(x),float(y)};
    }
    return commitColour(c);
}
bool QmlPlayerBridge::setColourOption(const QString& name,int value){
    auto c=selectedColourSettings();
    if(name=="blackWhite"&&(value==0||value==1))c.blackWhite=value!=0;
    else if(name=="groupBypassMask"&&value>=0&&value<128)c.groupBypassMask=uint32_t(value);
    else if(name=="lutInputSpace"&&value>=0&&value<=2)c.lutInputSpace=value;
    else return false;
    return commitColour(c);
}
bool QmlPlayerBridge::setColourWheel(int zone,double hue,double saturation,double luminance){
    if(zone<0||zone>=engine::kColorGradingZones||!std::isfinite(hue)||
       !std::isfinite(saturation)||!std::isfinite(luminance))return false;
    auto c=selectedColourSettings();c.grading[zone]={float(hue),float(saturation),float(luminance)};
    return commitColour(c);
}
bool QmlPlayerBridge::resetColourGroup(int group){
    auto c=selectedColourSettings();const engine::ColorSettings d;
    switch(group){
    case -1:{const bool enabled=c.enabled;c=d;c.enabled=enabled;break;}
    case 0:c.exposure=c.contrast=c.highlights=c.shadows=c.whites=c.blacks=0;break;
    case 1:c.temperature=c.tint=c.vibrance=c.saturation=0;break;
    case 2:c.paramHighlights=c.paramLights=c.paramDarks=c.paramShadows=0;c.curves=d.curves;
        c.splitHighlights=c.splitMidtones=c.splitShadows=0;break;
    case 3:c.mixerHue=d.mixerHue;c.mixerSaturation=d.mixerSaturation;c.mixerLuminance=d.mixerLuminance;
        c.blackWhite=d.blackWhite;c.blackWhiteMix=d.blackWhiteMix;break;
    case 4:c.grading=d.grading;c.gradingBlending=d.gradingBlending;c.gradingBalance=0;break;
    case 5:c.calibrationShadowTint=0;c.calibrationHue=d.calibrationHue;c.calibrationSaturation=d.calibrationSaturation;break;
    case 6:c.clearLut();c.lutStrength=d.lutStrength;c.lutInputSpace=d.lutInputSpace;break;
    case 7:c.texture=c.clarity=c.dehaze=0;break;
    default:return false;
    }
    if(group>=0&&group<7)c.groupBypassMask&=~(1u<<group);
    return commitColour(c);
}

// --- LUT library and colour looks (P4-g) ------------------------------------
// LUTs go to the folder the engine and the export worker resolve by name
// (runtime_local/luts); looks are user data beside the presets.
QVariantList QmlPlayerBridge::lutLibrary() const {
    QVariantList out;
    const engine::ColorLutStore store(runtime::localDataDirectory());
    for (const auto& name : store.list()) out << QString::fromStdWString(name);
    return out;
}
QVariantList QmlPlayerBridge::colourLooks() const {
    engine::ColorLookStore store(impl_->dataDir);
    QVariantList out;
    if (!store.load()) return out;
    for (size_t i = 0; i < store.entries().size(); ++i) {
        const auto& look = store.entries()[i];
        out << QVariantMap{{"index", int(i)}, {"name", QString::fromStdWString(look.name)},
                           {"lut", QString::fromStdWString(look.color.lutNameString())}};
    }
    return out;
}
void QmlPlayerBridge::importLutDialog() {
    const QString path = QFileDialog::getOpenFileName(nullptr, tr("导入 .cube LUT"), QString(),
                                                      tr("Cube LUT (*.cube);;所有文件 (*.*)"));
    if (!path.isEmpty()) importLut(path);
}
bool QmlPlayerBridge::importLut(const QString& path) {
    if (impl_->colourIndex() < 0) { emit notice(tr("处理链中没有调色节点，无法使用 LUT"), true); return false; }
    const engine::ColorLutStore store(runtime::localDataDirectory());
    std::wstring name; std::string error;
    if (!store.importFile(std::filesystem::path(path.toStdWString()), name, error) || name.empty()) {
        veyra::log::warn("ui-colour", std::format("lut import rejected path={} error={}", path.toStdString(), error));
        emit notice(tr("LUT 导入失败：%1").arg(QString::fromStdString(error)), true);
        return false;
    }
    veyra::log::info("ui-colour", std::format("lut imported name={}", QString::fromStdWString(name).toStdString()));
    emit colourLibraryChanged();
    const bool applied = setColourLut(QString::fromStdWString(name));
    if (applied) emit notice(tr("已导入并使用 LUT：%1").arg(QString::fromStdWString(name)), false);
    return applied;
}
bool QmlPlayerBridge::setColourLut(const QString& name) {
    auto c = selectedColourSettings();
    if (name.isEmpty()) { c.clearLut(); return commitColour(c); }
    const engine::ColorLutStore store(runtime::localDataDirectory());
    engine::ColorLutData data;
    if (!store.validName(name.toStdWString()) || !store.resolve(name.toStdWString(), data) || !data.valid()) {
        emit notice(tr("LUT 不可用：%1").arg(name), true);
        return false;
    }
    if (!c.setLutName(name.toStdWString())) { emit notice(tr("LUT 名称过长"), true); return false; }
    if (c.lutStrength <= 0.0f) c.lutStrength = 100.0f;
    c.enabled = true;
    return commitColour(c);
}
bool QmlPlayerBridge::saveColourLook(const QString& name, bool replace) {
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty() || trimmed.size() > 48) { emit notice(tr("颜色预设名称需为 1–48 个字符"), true); return false; }
    if (impl_->colourIndex() < 0) { emit notice(tr("处理链中没有调色节点"), true); return false; }
    engine::ColorLookStore store(impl_->dataDir);
    if (!store.load()) { emit notice(tr("颜色预设库无法读取：%1").arg(veyra::ui::i18n::text(store.error())), true); return false; }
    const bool exists = std::any_of(store.entries().begin(), store.entries().end(),
                                    [&](const auto& e) { return e.name == trimmed.toStdWString(); });
    if (exists && !replace) { emit notice(tr("已存在同名颜色预设"), true); return false; }
    auto colour = selectedColourSettings(); colour.enabled = true;
    if (!store.put(trimmed.toStdWString(), colour, replace) || !store.save()) {
        emit notice(tr("颜色预设保存失败：%1").arg(veyra::ui::i18n::text(store.error())), true); return false;
    }
    veyra::log::info("ui-colour", std::format("look saved name={} layer={}", trimmed.toStdString(), impl_->colourIndex()));
    emit colourLibraryChanged();
    emit notice(tr("已保存颜色预设：%1").arg(trimmed), false);
    return true;
}
bool QmlPlayerBridge::applyColourLook(int index) {
    engine::ColorLookStore store(impl_->dataDir);
    if (!store.load() || index < 0 || size_t(index) >= store.entries().size()) {
        emit notice(tr("颜色预设不存在"), true); return false;
    }
    auto colour = store.entries()[size_t(index)].color; colour.enabled = true;
    if (colour.hasLut()) {
        const engine::ColorLutStore luts(runtime::localDataDirectory());
        engine::ColorLutData data;
        if (!luts.resolve(colour.lutNameString(), data)) {
            emit notice(tr("预设引用的 LUT 不存在：%1，未应用").arg(QString::fromStdWString(colour.lutNameString())), true);
            return false;
        }
    }
    const bool ok = commitColour(colour);
    veyra::log::info("ui-colour", std::format("look applied index={} layer={} ok={}", index, impl_->colourIndex(), ok));
    return ok;
}
bool QmlPlayerBridge::deleteColourLook(int index) {
    engine::ColorLookStore store(impl_->dataDir);
    if (!store.load() || index < 0 || !store.erase(size_t(index)) || !store.save()) {
        emit notice(tr("删除颜色预设失败"), true); return false;
    }
    emit colourLibraryChanged();
    return true;
}
void QmlPlayerBridge::importColourLookDialog() {
    const QString path = QFileDialog::getOpenFileName(nullptr, tr("导入颜色预设"), QString(),
                                                      tr("Veyra 颜色预设 (*.vpcolor);;所有文件 (*.*)"));
    if (!path.isEmpty()) importColourLook(path);
}
bool QmlPlayerBridge::importColourLook(const QString& path) {
    engine::ColorLookStore store(impl_->dataDir);
    std::wstring name;
    if (!store.load() || !store.importFile(std::filesystem::path(path.toStdWString()), name)) {
        emit notice(tr("导入颜色预设失败：%1").arg(veyra::ui::i18n::text(store.error())), true); return false;
    }
    emit colourLibraryChanged();
    emit notice(tr("已导入颜色预设：%1").arg(QString::fromStdWString(name)), false);
    return true;
}
void QmlPlayerBridge::exportColourLookDialog(int index) {
    const QString path = QFileDialog::getSaveFileName(nullptr, tr("导出颜色预设"), QString(),
                                                      tr("Veyra 颜色预设 (*.vpcolor)"));
    if (!path.isEmpty()) exportColourLook(index, path);
}
bool QmlPlayerBridge::exportColourLook(int index, const QString& path) {
    engine::ColorLookStore store(impl_->dataDir);
    if (!store.load() || index < 0 || !store.exportFile(size_t(index), std::filesystem::path(path.toStdWString()))) {
        emit notice(tr("导出颜色预设失败：%1").arg(veyra::ui::i18n::text(store.error())), true); return false;
    }
    emit notice(tr("已导出颜色预设"), false);
    return true;
}


} // namespace veyra::ui
