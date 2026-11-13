// QML bridge: View responsibilities.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
// --- NR protection regions drawn on the picture (#8) ------------------------
namespace {
// Outline of the region being drawn, on a layered child of the video window
// (the swapchain is never read or touched), as 1.4.4's ProtectionOverlay.
void drawProtectionOutline(HWND overlay, HWND video, RECT box, bool ellipse) {
    RECT r{}; GetClientRect(video, &r);
    if (r.right < 1 || r.bottom < 1) return;
    SetWindowPos(overlay, HWND_TOP, 0, 0, r.right, r.bottom, SWP_NOACTIVATE);
    BITMAPINFO info{}; info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER); info.bmiHeader.biWidth = r.right;
    info.bmiHeader.biHeight = -r.bottom; info.bmiHeader.biPlanes = 1; info.bmiHeader.biBitCount = 32;
    HDC screen = GetDC(nullptr), memory = CreateCompatibleDC(screen); void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap || !bits) { if (bitmap) DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen); return; }
    auto previous = SelectObject(memory, bitmap);
    memset(bits, 0, size_t(r.right) * size_t(r.bottom) * 4);
    {
        Gdiplus::Bitmap canvas(r.right, r.bottom, r.right * 4, PixelFormat32bppPARGB, static_cast<BYTE*>(bits));
        Gdiplus::Graphics g(&canvas); g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(Gdiplus::Color(255, 255, 138, 61), 2.0f);
        Gdiplus::SolidBrush fill(Gdiplus::Color(40, 255, 138, 61));
        const Gdiplus::Rect rect(box.left, box.top, std::max(1L, box.right - box.left), std::max(1L, box.bottom - box.top));
        if (ellipse) { g.FillEllipse(&fill, rect); g.DrawEllipse(&pen, rect); }
        else { g.FillRectangle(&fill, rect); g.DrawRectangle(&pen, rect); }
    }
    SIZE size{r.right, r.bottom}; POINT origin{}; BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
    if (UpdateLayeredWindow(overlay, screen, nullptr, &size, memory, &origin, 0, &blend, ULW_ALPHA))
        ShowWindow(overlay, SW_SHOWNOACTIVATE);
    SelectObject(memory, previous); DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(nullptr, screen);
}
}

QVariantMap QmlPlayerBridge::maskState(bool allEffects) const {
    const auto* node=impl_->chain.firstOf(allEffects?engine::EffectType::Exclusion:engine::EffectType::Protection);
    const auto value=node?node->protection:engine::ProtectionSettings{};
    QVariantList regions;
    for(int index=0;index<int(value.regions.size());++index){
        const auto& q=value.regions[size_t(index)];
        if(!q.empty())regions<<QVariantMap{{"index",index},{"left",q.left},{"top",q.top},
            {"right",q.right},{"bottom",q.bottom},{"ellipse",q.ellipse}};
    }
    const auto& first=value.regions[0];
    return {{"enabled",node&&node->enabled},{"feather",value.featherPixels},{"regions",regions},
        {"left",first.left},{"top",first.top},{"right",first.right},{"bottom",first.bottom},
        {"drawShape",impl_->protExclusion==allEffects?impl_->protDraw:QString{}}};
}
QVariantMap QmlPlayerBridge::exclusionState() const {return maskState(true);}
QVariantList QmlPlayerBridge::protectionRegions() const {return maskState(false).value("regions").toList();}
QString QmlPlayerBridge::protectionDrawShape() const { return impl_->protDraw; }

bool QmlPlayerBridge::editProtection(const std::function<bool(engine::ChainNode&)>& edit,bool allEffects) {
    const auto type=allEffects?engine::EffectType::Exclusion:engine::EffectType::Protection;
    if(allEffects&&!impl_->chain.firstOf(type)&&addEffect(QStringLiteral("exclusion"))<0)return false;
    for (uint32_t i = 0; i < impl_->chain.nodeCount; ++i) {
        auto& node = impl_->chain.nodes[i];
        if (node.type != type) continue;
        const auto before = node; const bool wasValid = chainValid();
        if (!edit(node)) return false;
        if (node == before) return true;
        const bool accepted = impl_->revalidate(true);
        if (!accepted) { const auto error = impl_->validation.message; node = before; impl_->revalidate(); emit notice(uiText(error), true); }
        emit settingsChanged();
        if(before.enabled!=node.enabled)emitChainChanged();
        else emitParameterChanged(&QmlPlayerBridge::protectionChanged,wasValid);
        return accepted;
    }
    emit notice(tr("处理链中没有 NR 保护节点，未更改设置"), true);
    return false;
}

bool QmlPlayerBridge::beginProtectionDraw(const QString& shape) {return beginMaskDraw(false,shape);}
bool QmlPlayerBridge::beginMaskDraw(bool allEffects,const QString& shape) {
    auto& i = *impl_;
    if (shape.isEmpty()) {
        if (i.protOverlay) ShowWindow(i.protOverlay, SW_HIDE);
        if (!i.protDraw.isEmpty()) { i.protDraw.clear(); emit protectionDrawChanged();emit protectionChanged(); }
        return true;
    }
    if (shape != QLatin1String("rect") && shape != QLatin1String("ellipse")) return false;
    if (!i.snapshot.running || !i.videoWindow) { emit notice(tr("请先打开视频，再在画面上绘制保护区域"), true); return false; }
    bool space = false;
    const auto* node=i.chain.firstOf(allEffects?engine::EffectType::Exclusion:engine::EffectType::Protection);
    const auto parameters=node?node->protection:engine::ProtectionSettings{};
    for(const auto& q:parameters.regions)space|=q.empty();
    if (!space) { emit notice(tr("最多 4 个保护区域，请先删除一个"), true); return false; }
    i.protDraw = shape;i.protExclusion=allEffects;
    emit protectionDrawChanged();emit protectionChanged();
    return true;
}

void QmlPlayerBridge::updateProtectionDraw(double x0, double y0, double x1, double y1) {
    auto& i = *impl_;
    if (i.protDraw.isEmpty() || !i.videoWindow) return;
    if (!i.protOverlay) { ensureGdiplus(); i.protOverlay = createSubtitleOverlay(i.videoWindow); }
    if (!i.protOverlay) return;
    RECT client{}; GetClientRect(i.videoWindow, &client);
    const auto px = [&](double f, LONG extent) { return LONG(std::lround(std::clamp(f, 0.0, 1.0) * extent)); };
    const RECT box{px(std::min(x0, x1), client.right), px(std::min(y0, y1), client.bottom),
                   px(std::max(x0, x1), client.right), px(std::max(y0, y1), client.bottom)};
    drawProtectionOutline(i.protOverlay, i.videoWindow, box, i.protDraw == QLatin1String("ellipse"));
}

bool QmlPlayerBridge::commitProtectionDraw(double x0, double y0, double x1, double y1) {
    auto& i = *impl_;
    const bool allEffects=i.protExclusion;
    const bool ellipse = i.protDraw == QLatin1String("ellipse");
    const bool armed = !i.protDraw.isEmpty();
    beginProtectionDraw(QString());
    if (!armed || !i.videoWindow) return false;
    RECT client{}; GetClientRect(i.videoWindow, &client);
    if (client.right < 1 || client.bottom < 1) return false;
    const double w = client.right, h = client.bottom;
    // Too small to be intended (1.4.4: at least 3 px each way).
    if (std::abs(x1 - x0) * w < 3 || std::abs(y1 - y0) * h < 3) return false;
    // Host pixels -> source-normalized, through the current zoom/pan (1.4.4).
    const auto e = i.snapshot.metrics.resolution.output;
    const auto view = i.engine.previewView();
    const auto a = view.sourcePoint(float(x0 * w), float(y0 * h), float(w), float(h), float(e.width), float(e.height));
    const auto b = view.sourcePoint(float(x1 * w), float(y1 * h), float(w), float(h), float(e.width), float(e.height));
    engine::ProtectionRect q{std::clamp(std::min(a.first, b.first), 0.0f, 1.0f), std::clamp(std::min(a.second, b.second), 0.0f, 1.0f),
                             std::clamp(std::max(a.first, b.first), 0.0f, 1.0f), std::clamp(std::max(a.second, b.second), 0.0f, 1.0f), ellipse};
    if (q.empty()) return false;
    const bool ok = editProtection([&](engine::ChainNode& node) {
        for (auto& slot : node.protection.regions)
            if (slot.empty()) { slot = q; node.enabled = true; node.protection.enabled = true; return true; }
        emit notice(tr("最多 4 个保护区域，请先删除一个"), true);
        return false;
    },allEffects);
    veyra::log::info("ui-protection", std::format("{} accepted={} bounds={:.3f},{:.3f},{:.3f},{:.3f} allEffects={} source-normalized",
                                                  ellipse ? "ellipse" : "rectangle", ok, q.left, q.top, q.right, q.bottom,allEffects));
    return ok;
}

bool QmlPlayerBridge::setMaskEnabled(bool allEffects,bool enabled) {
    return editProtection([&](engine::ChainNode& node){node.enabled=enabled;node.protection.enabled=enabled;return true;},allEffects);
}
bool QmlPlayerBridge::setMaskFeather(bool allEffects,double pixels) {
    if(!std::isfinite(pixels)||pixels<0||pixels>64)return false;
    return editProtection([&](engine::ChainNode& node){node.protection.featherPixels=float(pixels);return true;},allEffects);
}
bool QmlPlayerBridge::clearMaskRegions(bool allEffects) {
    return editProtection([](engine::ChainNode& node){node.protection.regions={};return true;},allEffects);
}
bool QmlPlayerBridge::removeMaskRegion(bool allEffects,int index) {
    if(index<0||index>=int(engine::ProtectionSettings{}.regions.size()))return false;
    return editProtection([&](engine::ChainNode& node){
        auto& regions=node.protection.regions;
        for(size_t i=size_t(index);i+1<regions.size();++i)regions[i]=regions[i+1];
        regions.back()={};return true;
    },allEffects);
}

bool QmlPlayerBridge::removeProtectionRegion(int index) {
    if (index < 0 || index >= int(engine::ProtectionSettings{}.regions.size())) return false;
    return editProtection([&](engine::ChainNode& node) {
        // Keep the remaining regions packed in order.
        auto& regions = node.protection.regions;
        for (int k = index; k + 1 < int(regions.size()); ++k) regions[size_t(k)] = regions[size_t(k + 1)];
        regions.back() = {};
        return true;
    });
}
bool QmlPlayerBridge::clearProtectionRegions() {
    return editProtection([](engine::ChainNode& node) { node.protection.regions = {}; return true; });
}
bool QmlPlayerBridge::setProtectionFeather(double pixels) {
    if (!std::isfinite(pixels) || pixels < 0 || pixels > 64) return false;
    return editProtection([&](engine::ChainNode& node) { node.protection.featherPixels = float(pixels); return true; });
}


// --- design gap B8-B11: compare and aspect ------------------------------------
int QmlPlayerBridge::compareMode() const { return impl_->compareMode; }
void QmlPlayerBridge::setCompareMode(int value) {
    if (value < 0 || value > 2 || impl_->compareMode == value) return;
    impl_->compareMode = value;
    if (!impl_->holdKeyDown) impl_->engine.comparison(value, impl_->compareBase, float(impl_->compareSplit));
    veyra::log::info("ui-compare", std::format("mode={} base={} split={:.3f}", value, impl_->compareBase, impl_->compareSplit));
    // Comparison shows real frames only (a generated frame has no original to set beside
    // it), so frame generation pauses while it is on. A field user left split compare on
    // for 24 minutes and reported "补帧无效" (logs9, 2026-10-01): say so.
    if (value != 0 && impl_->snapshot.running && impl_->snapshot.applied.multiplier > 1)
        emit notice(tr("对比模式下补帧暂停（对比只能用真实帧），关闭对比后自动恢复"), false);
    emit compareChanged();
}
bool QmlPlayerBridge::compareBase() const { return impl_->compareBase; }
void QmlPlayerBridge::setCompareBase(bool value) {
    if (impl_->compareBase == value) return;
    impl_->compareBase = value;
    if (!impl_->holdKeyDown) impl_->engine.comparison(impl_->compareMode, value, float(impl_->compareSplit));
    emit compareChanged();
}
double QmlPlayerBridge::compareSplit() const { return impl_->compareSplit; }
void QmlPlayerBridge::setCompareSplitAt(double fx) {
    // 1.4.4 AppShell: the split is a fraction of the displayed picture, which is
    // letterboxed and may be zoomed/panned inside the host.
    auto& i = *impl_;
    if (!i.videoWindow || !std::isfinite(fx)) return;
    RECT r{}; GetClientRect(i.videoWindow, &r);
    if (r.right < 1 || r.bottom < 1) return;
    const auto e = i.snapshot.metrics.resolution.output;
    const auto view = i.engine.previewView();
    float contentWidth = float(r.right);
    if (e.width && e.height) contentWidth = view.renderedSize(float(r.right),float(r.bottom),float(e.width),float(e.height)).first;
    const float left = r.right * .5f - contentWidth * view.centerX;
    i.compareSplit = std::clamp((float(fx) * r.right - left) / std::max(1.0f, contentWidth), 0.0f, 1.0f);
    if (!i.holdKeyDown) i.engine.comparison(i.compareMode, i.compareBase, float(i.compareSplit));
    emit compareChanged();
}
int QmlPlayerBridge::aspectMode() const { return impl_->screenFillActive ? 2 : impl_->aspectMode; }
void QmlPlayerBridge::setAspectMode(int value) {
    if (value < 0 || value > 6 || (impl_->aspectMode == value && !impl_->screenFillActive)) return;
    impl_->aspectMode = value;
    impl_->screenFillActive = false;
    impl_->prefs[QStringLiteral("aspect")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "aspect mode not saved");
    applyAspect(true);
    if (gameMode()) { updateGameGeometry(); updateGameMode(); emit gameModeChanged(); }
    emit compareChanged();
}
void QmlPlayerBridge::applyAspect(bool force) {
    auto& i = *impl_;
    const bool aligned=gameMode()&&i.presentation.fullscreen&&!gameScaledPreview();
    const int mode = i.screenFillActive ? 2 : i.aspectMode;
    if (!i.videoWindow) return;
    RECT r{}; GetClientRect(i.videoWindow, &r);
    const auto e = i.snapshot.metrics.resolution.output;
    const QSize client(r.right, r.bottom), image(int(e.width), int(e.height));
    const double dar=sourceAspect();
    if (!force && mode == i.aspectApplied && client == i.screenFitClient && image == i.screenFitImage && dar == i.screenFitAspect) return;
    i.screenFitClient = client; i.screenFitImage = image; i.aspectApplied = mode; i.screenFitAspect = dar;
    auto view=force||aligned?engine::PreviewView{}:i.engine.previewView();
    view.mode=mode;
    view.displayAspect=dar>0&&image.height()>0&&std::abs(dar-double(image.width())/image.height())>.001?dar:0;
    i.engine.previewView(view);
    veyra::log::info("ui-aspect", std::format("mode={} zoom={:.3f} dar={:.4f} client={}x{} image={}x{}",mode,view.zoom,dar,
        client.width(),client.height(),image.width(),image.height()));
}

// --- design gap D18 / C15 / E20 / F21 / F24 ------------------------------------
double QmlPlayerBridge::audioSkewMs() const {
    const auto& skew = impl_->snapshot.captureAudio.skewMs;
    return audioSkewKnown() ? *skew : 0.0;
}
bool QmlPlayerBridge::audioSkewKnown() const {
    const auto& skew = impl_->snapshot.captureAudio.skewMs;
    return audioSyncLive() && skew.has_value() && std::isfinite(*skew) && std::abs(*skew) < 5000.0;
}

QString QmlPlayerBridge::exportSizeEstimate() const {
    const auto gb = [](double bytes) {
        return bytes >= 1024.0 * 1024 * 1024 ? QStringLiteral("%1 GB").arg(bytes / (1024.0 * 1024 * 1024), 0, 'f', 2)
                                            : QStringLiteral("%1 MB").arg(bytes / (1024.0 * 1024), 0, 'f', 0);
    };
    if (exportRunning()) {
        const double progress = exportProgress();
        const QFileInfo partial(exportTarget() + QStringLiteral(".partial"));
        if (progress > 0.02 && partial.exists() && partial.size() > 0)
            return tr("预计约 %1（按已写入 %2 推算）").arg(gb(partial.size() / progress)).arg(gb(double(partial.size())));
        return tr("开始写入后按已写入量推算");
    }
    const double end = impl_->exportTrimEndSeconds > 0 ? impl_->exportTrimEndSeconds : impl_->snapshot.duration;
    const double seconds = std::max(0.0, end - impl_->exportTrimStartSeconds);
    if (seconds <= 0) return QString();
    if (impl_->exportBitrateMbps > 0)
        return tr("约 %1（视频 %2 Mbps × %3，音轨另计）").arg(gb(impl_->exportBitrateMbps * 1e6 / 8.0 * seconds))
            .arg(impl_->exportBitrateMbps).arg(formatTime(seconds));
    return tr("恒定质量：大小取决于画面，开始后按已写入量推算");
}

QVariantMap QmlPlayerBridge::lastSource() const {
    auto saved = impl_->prefs.value(QStringLiteral("lastSource")).toMap();
    const QString kind = saved.value(QStringLiteral("kind")).toString();
    if (kind == QLatin1String("file")) {
        saved[QStringLiteral("summary")]=QFileInfo(saved.value(QStringLiteral("label")).toString()).fileName();
    } else if (kind == QLatin1String("capture")) {
        if (!hasCaptureSession()) return {};
        saved[QStringLiteral("summary")] = captureSessionSummary();
    } else if (kind == QLatin1String("ps5")) {
        saved[QStringLiteral("summary")] = tr("PS5 串流 · %1").arg(saved.value(QStringLiteral("label")).toString());
    } else if (kind == QLatin1String("moonlight")) {
        saved[QStringLiteral("summary")] = tr("PC 串流 · %1").arg(saved.value(QStringLiteral("label")).toString());
    } else if (kind == QLatin1String("xbox")) {
        saved[QStringLiteral("summary")] = tr("Xbox 串流 · %1").arg(saved.value(QStringLiteral("label")).toString());
    } else if (kind == QLatin1String("screen")) {
        saved[QStringLiteral("summary")] = tr("屏幕捕获 · %1").arg(saved.value(QStringLiteral("label")).toString());
    } else if (hasCaptureSession()) {
        // Before this was recorded, only capture sessions were remembered.
        return QVariantMap{{"kind", "capture"}, {"summary", captureSessionSummary()}};
    } else return {};
    return saved;
}
void QmlPlayerBridge::rememberSource(const QString& kind, const QString& label) {
    if(kind!=QLatin1String("file")){impl_->liveKind = kind;impl_->liveLabel = label;}
    QVariantMap value{{"kind", kind}, {"label", label}};
    if (kind == QLatin1String("screen")) value[QStringLiteral("screenKind")] = screenOptions().value(QStringLiteral("kind")).toInt();
    if (impl_->prefs.value(QStringLiteral("lastSource")).toMap() == value) return;
    impl_->prefs[QStringLiteral("lastSource")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "last source not saved");
    emit preferencesChanged();
}
void QmlPlayerBridge::autoResumeLastSource() {
    if(!impl_->prefBool("autoResume",false)||impl_->openingSource||impl_->snapshot.running)return;
    if(lastSource().isEmpty())return;
    veyra::log::info("ui-startup","automatically resuming the last source");
    resumeLastSource();
}
void QmlPlayerBridge::resumeLastSource() {
    const auto last = lastSource();
    const QString kind = last.value(QStringLiteral("kind")).toString();
    veyra::log::info("ui-resume-source", "kind=" + kind.toStdString());
    if(kind==QLatin1String("file")){
        const QString path=last.value(QStringLiteral("label")).toString();
        if(!QFileInfo(path).isFile()){emit notice(tr("上次的视频文件不存在：%1").arg(path),true);return;}
        openPath(path);return;
    }
    if (kind == QLatin1String("capture")) { resumeCaptureSession(); return; }
    if (kind == QLatin1String("ps5")) {
#ifdef VEYRA_ENABLE_REMOTEPLAY
        ps5Load();
        if (!ps5Connect()) emit notice(tr("没能继续上次的 PS5 串流：%1").arg(impl_->ps5Status), true);
#else
        emit notice(tr("此版本不含 PS5 串流"), true);
#endif
        return;
    }
    if (kind == QLatin1String("xbox")) {
#ifdef VEYRA_ENABLE_XBOX
        if (!impl_->xbox || !impl_->xbox->resumeLast()) {
            emit notice(tr("没能继续上次的 Xbox 串流，请在 Xbox 串流窗口里重新选择"), true);
            emit navigate(QStringLiteral("xbox"));
        }
#else
        emit notice(tr("此版本不含 Xbox 串流"), true);
#endif
        return;
    }
    if (kind == QLatin1String("moonlight")) {
#ifdef VEYRA_ENABLE_MOONLIGHT
        if (!impl_->moonlight || !impl_->moonlight->resumeLast()) {
            emit notice(tr("没能继续上次的 PC 串流，请在 PC 串流窗口里重新选择"), true);
            emit navigate(QStringLiteral("moonlight"));
        }
#else
        emit notice(tr("此版本不含 PC 串流"), true);
#endif
        return;
    }
    if (kind == QLatin1String("screen")) {
        const int screenKind = last.value(QStringLiteral("screenKind")).toInt();
        if (screenOptions().value(QStringLiteral("kind")).toInt() != screenKind) setScreenOption(QStringLiteral("kind"), screenKind);
        refreshCaptureTargets();
        const QString label = last.value(QStringLiteral("label")).toString();
        for (const auto& item : screenTargets()) {
            const auto map = item.toMap();
            if (map.value(QStringLiteral("label")).toString() == label) {
                setScreenTargetId(map.value(QStringLiteral("id")).toString());
                startScreenCapture();
                return;
            }
        }
        emit notice(tr("上次的捕获目标「%1」已不在，请重新选择").arg(label), true);
        emit navigate(QStringLiteral("screen"));
        return;
    }
    emit notice(tr("还没有可以继续的片源"), true);
}

bool QmlPlayerBridge::resetNrLayer(int index) {
    auto& c = impl_->chain;
    if (c.mode != engine::ChainMode::List || index < 0 || index >= int(c.nodeCount) ||
        c.nodes[size_t(index)].type != engine::EffectType::NrEnhance) return false;
    auto& node = c.nodes[size_t(index)];
    const auto before = node;
    const engine::NrLayerSettings defaults;
    // The model and residual parameters, the size and the temporal option go
    // back to their defaults; the shared runtime and the on/off state stay.
    node.nr.model = defaults.model; node.nr.residual = defaults.residual;
    node.nr.temporal = defaults.temporal; node.nr.sizePolicy = defaults.sizePolicy;
    if (node == before) return true;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message; node = before; impl_->revalidate();
        emit notice(uiText(error), true); return false;
    }
    emit settingsChanged(); emitChainChanged();
    return true;
}

QVariantList QmlPlayerBridge::frameTimes() const {
    QVariantList out;
    for (const double v : impl_->frameTimes) out << v;
    return out;
}


} // namespace veyra::ui
