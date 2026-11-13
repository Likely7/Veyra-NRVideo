// QML bridge: Preferences responsibilities.
#include "../QmlPlayerBridge_p.h"
#include "veyra/ngx/AnrProvider.h"

namespace veyra::ui {
// --- shell preferences -------------------------------------------------------
bool QmlPlayerBridge::reducedMotion() const { return impl_->facade.reducedMotion(); }
int QmlPlayerBridge::uiDeviceGeneration() const { return impl_->uiDeviceGeneration; }
void QmlPlayerBridge::noteUiDeviceRecreated(const QString& window) {
    ++impl_->uiDeviceGeneration;
    veyra::log::warn("ui-device", std::format("{} scene graph recreated after a lost graphics device; icons rebuilt generation={}",
                                              window.toStdString(), impl_->uiDeviceGeneration));
    emit uiDeviceGenerationChanged();
}
void QmlPlayerBridge::setReducedMotion(bool value) {
    if (impl_->facade.reducedMotion() == value) return;
    impl_->facade.setReducedMotion(value);
    emit settingsChanged();
}
QString QmlPlayerBridge::defaultPage() const { return utf8Of(impl_->facade.defaultPage()); }
void QmlPlayerBridge::setDefaultPage(const QString& value) {
    if (utf8Of(impl_->facade.defaultPage()) == value) return;
    impl_->facade.setDefaultPage(wideOf(value));
    emit settingsChanged();
}
QString QmlPlayerBridge::defaultPageLabel() const {
    const QString page = defaultPage();
    if (page == QLatin1String("min")) return tr("极简模式");
    if (page == QLatin1String("pro")) return tr("专业模式");
    if (page == QLatin1String("last")) return tr("上次");
    return tr("首页");
}

QString QmlPlayerBridge::initialPage() const {
    // The saved preference unless a test override was set; "last" reopens the
    // page the previous session ended on.
    if (!impl_->initialPageOverride.isEmpty()) return impl_->initialPageOverride;
    if (defaultPage() == QLatin1String("last")) {
        const QString last = impl_->prefString("lastPage");
        return last.isEmpty() ? QStringLiteral("home") : last;
    }
    return defaultPage();
}
void QmlPlayerBridge::setInitialPage(const QString& value) {
    impl_->initialPageOverride = value;
    emit settingsChanged();
}

QString QmlPlayerBridge::currentPage() const { return impl_->currentPage; }
void QmlPlayerBridge::setCurrentPage(const QString& value) {
    impl_->currentPage = value;
    static const QStringList remembered{"home", "min", "pro", "node", "set", "exp"};
    if (remembered.contains(value) && impl_->prefString("lastPage") != value) {
        impl_->prefs["lastPage"] = value == QLatin1String("node") ? QStringLiteral("pro") : value;
        impl_->savePrefs();
    }
}

QString QmlPlayerBridge::remotePlayState() const {
    // The engine's own remote-play state word, or empty when there is none. No
    // invented "connected" wording.
    return uiText(impl_->snapshot.remoteRecoveryMessage);
}

QVariantList QmlPlayerBridge::componentList() const {
    if (!impl_->componentListRead) { impl_->componentList = readComponentList(); impl_->componentListRead = true; }
    return impl_->componentList;
}
void QmlPlayerBridge::refreshComponentList() {
    auto list = readComponentList();
    impl_->componentListRead = true;
    if (list == impl_->componentList) return;
    impl_->componentList = std::move(list);
    emit componentListChanged();
}
QVariantList QmlPlayerBridge::readComponentList() const {
    // Read from the runtime manifest the build produces rather than a list written
    // by hand here, so the page cannot claim a component the package lacks.
    QVariantList out;
    const auto anrDirectory=ngx::AnrProvider::directory(runtime::localRuntimeDirectory().wstring());
    out<<QVariantMap{{"name",QStringLiteral("ANR FG")},{"detail",QString::fromStdWString(anrDirectory)},
        {"loaded",ngx::AnrProvider::available(runtime::localRuntimeDirectory().wstring())},{"experimental",true}};
    const auto vfgDirectory=pipeline::VfgBackend::runtimeDirectory(runtime::localRuntimeDirectory().wstring());
    const bool vfgAvailable=pipeline::VfgBackend::runtimeAvailable(runtime::localRuntimeDirectory().wstring());
    out<<QVariantMap{{"name",QStringLiteral("NVIDIA VFG")},{"detail",QString::fromStdWString(vfgDirectory)},
        {"loaded",vfgAvailable},{"experimental",true}};
    const auto manifest = runtime::localRuntimeDirectory() / L"release-runtime-manifest.json";
    QFile file(QString::fromWCharArray(manifest.c_str()));
    if (!file.open(QIODevice::ReadOnly)) {
        // No manifest is a reportable fact, not a reason to invent entries.
        return out;
    }
    const auto document = QJsonDocument::fromJson(file.readAll());
    const auto entries = document.isArray() ? document.array()
                                            : document.object().value(QStringLiteral("files")).toArray();
    for (const auto& value : entries) {
        const auto object = value.toObject();
        QVariantMap item;
        item["name"] = object.value(QStringLiteral("name")).toString();
        // Release manifests write fileVersion/authenticode; keep the short keys too.
        auto field = [&](const char* primary, const char* fallback) {
            const QString text = object.value(QLatin1String(primary)).toString();
            return text.isEmpty() ? object.value(QLatin1String(fallback)).toString() : text;
        };
        const QString version = field("version", "fileVersion");
        const QString signature = field("signature", "authenticode");
        const bool experimental = object.value(QStringLiteral("experimental")).toBool();
        // Several runtimes share a file name (the NR slots); the folder tells them apart.
        const QString path = object.value(QStringLiteral("path")).toString();
        const QString folder = path.section(QLatin1Char('/'), 0, -2);
        QStringList detail;
        for (const QString& part : {version, signature, folder})
            if (!part.isEmpty()) detail << part;
        item["detail"] = detail.join(QStringLiteral(" · "));
        item["loaded"] = object.value(QStringLiteral("loaded")).toBool(true);
        item["experimental"] = experimental;
        if (!item["name"].toString().isEmpty()) out << item;
    }
    return out;
}

void QmlPlayerBridge::openProjectPage() {
    // 关于 → 交流群: the group QR codes are kept current in the project README.
    if (!QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Likely7/Veyra-NRVideo#readme"))))
        emit notice(tr("无法打开浏览器"), true);
}

void QmlPlayerBridge::copyDiagnostics() {
    if (auto* clipboard = QGuiApplication::clipboard()) {
        clipboard->setText(diagnosticsReport());
        emit notice(tr("诊断信息已复制"), false);
    }
}


// --- shell preferences (P4-f / P4-c / P4-d) --------------------------------
QVariantMap QmlPlayerBridge::preferences() const { return impl_->prefs; }
QString QmlPlayerBridge::gpuPriorityStatus() const {
    const auto status=gfx::gpuPriorityStatus();
    const auto name=[](int priority){return priority==5?tr("实时"):priority==4?tr("高"):tr("普通");};
    if(status.state==gfx::GpuPriorityState::Pending)return tr("创建显卡会话后生效");
    if(status.state==gfx::GpuPriorityState::Unavailable)return tr("系统不支持调整 GPU 优先级");
    if(status.state==gfx::GpuPriorityState::Rejected)return tr("系统未接受设置，当前：%1").arg(status.actual<0?tr("未知"):name(status.actual));
    if(status.capped)return tr("已生效：%1（抖音直播伴侣运行中，暂不使用实时）").arg(name(status.actual));
    return tr("已生效：%1").arg(name(status.actual));
}
bool QmlPlayerBridge::enhancementPrewarmAvailable() const {
    // PreviewGpuSession::prepare refuses RTX 30/40 (patched FG sessions keep the
    // full NGX lifetime), so the switch must not look usable there either.
    const auto gpu=effectGpu();
    return gpu.vendor==0x10DE&&!ngx::FgCompatibilitySession::requested(gpu.vendor,gpu.deviceId);
}
void QmlPlayerBridge::scheduleEnhancementPrewarm(){
    QTimer::singleShot(2000,this,[this]{
        if(!impl_->prefBool("prewarmEnhancement",false)||!enhancementPrewarmAvailable()||impl_->openingSource||impl_->snapshot.running||!impl_->engine.idle())return;
        if(engine::runtimeOrder(impl_->activeRuntime()))return;
        const auto saved=impl_->prefs.value(QStringLiteral("lastSourceExtent")).toMap();
        const auto width=saved.value("width",1920).toUInt(),height=saved.value("height",1080).toUInt();
        const bool valid=width>=64&&height>=64&&width<=3840&&height<=2160;
        impl_->engine.prewarmEnhancement(impl_->facade.pendingSettings(),valid?width:1920,valid?height:1080);
    });
}
void QmlPlayerBridge::rememberWindowSize(int width, int height) {
    // Only kept when the user chose "记住上次"; the key is internal.
    if (width < 400 || height < 200 || width > 16384 || height > 16384) return;
    const QString v = QStringLiteral("%1x%2").arg(width).arg(height);
    if (impl_->prefs.value(QStringLiteral("lastWindow")).toString() == v) return;
    impl_->prefs[QStringLiteral("lastWindow")] = v;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "last window size not saved");
}
QRect QmlPlayerBridge::screenAvailableAt(int x, int y) const {
    QScreen* screen = QGuiApplication::screenAt(QPoint(x, y));
    if (!screen) screen = QGuiApplication::primaryScreen();
    return screen ? screen->availableGeometry() : QRect(0, 0, 1920, 1040);
}
QRect QmlPlayerBridge::launchScreenAvailable() const {
    const QPoint at = QCursor::pos();
    return screenAvailableAt(at.x(), at.y());
}

bool QmlPlayerBridge::setPreference(const QString& key, const QVariant& value) {
    if(key==QLatin1String("blackmagicNativeCapture")){
        if(captureQueryBusy()){emit notice(tr("请等待采集设备查询完成后再切换接口"),true);return false;}
        if(captureBlackmagicDevice())impl_->prefs[source::decklink::isPath(impl_->captureDevice)?"blackmagicNativeLastDevice":"blackmagicDirectShowLastDevice"]=utf8Of(impl_->captureDevice);
    }
    static const QHash<QString, std::pair<int, int>> ranges{
        {"subtitleSize", {16, 56}}, {"subtitleFont", {0, 5}}, {"subtitleOutline", {0, 3}},
        {"subtitleMargin", {0, 240}}, {"subtitleLines", {0, 8}},
        // 列表模式 parameter panel width (ProPage splitter); QML clamps it to the window.
        {"listInspectorWidth", {376, 4000}},
        // Network sources (#48): seconds to collect before the first picture; -1 follows the buffer level.
        {"networkPrebuffer", {-1, 60}},
        // Live recording: the target bitrate for the file, and the frame rate
        // cap (0 keeps the presented rate).
        {"recordBitrateMbps", {0, 400}},
        {"recordFps", {0, 240}}};
    static const QStringList flags{"recordHevc", "recordAudio", "subtitleEnabled", "subtitleBackground", "subtitleFit",
                                   "subtitleSecondLanguage", "audioForceStereo", "holdCompare",
                                   "magewellLowLatency", "blackmagicNativeCapture", "captureShowAllFrameRates", "cinePillHidden", "exportStopsPlayback", "obsGameCapture", "autoResume", "fullscreenMemoryProtection", "prewarmEnhancement", "hdrFgCompatibility", "computeOnMonitorGpu", "deinterlace", "networkAutoReconnect"};
    QVariant stored;
    if (ranges.contains(key)) {
        bool ok = false; const int n = value.toInt(&ok);
        const auto [low, high] = ranges.value(key);
        if (!ok || n < low || n > high) return false;
        stored = n;
    } else if (flags.contains(key)) {
        stored = value.toBool();
    } else if (key == QLatin1String("nrResidualView") || key == QLatin1String("nrCorrectionView")) {
        const auto view = value.toString();
        if (view != QLatin1String("web") && view != QLatin1String("list")) return false;
        stored = view;
    } else if (key == QLatin1String("screenshotDir")) {
        const QString dir = value.toString();
        if (!dir.isEmpty() && !QFileInfo(dir).isDir()) { emit notice(tr("截图目录不存在：%1").arg(dir), true); return false; }
        stored = dir;
    } else if (key == QLatin1String("recordDir")) {
        const QString dir = value.toString();
        if (!dir.isEmpty() && !QFileInfo(dir).isDir()) { emit notice(tr("录制目录不存在：%1").arg(dir), true); return false; }
        stored = dir;
    } else if (key == QLatin1String("audioDevice")) {
        stored = value.toString();
    } else if (key == QLatin1String("sliderKeyStep")) {
        // Arrow-key step of a focused slider (设置 → 通用): 1, 0.1 or 0.01.
        bool ok = false; const double v = value.toDouble(&ok);
        if (!ok || !(qFuzzyCompare(v, 1.0) || qFuzzyCompare(v, 0.1) || qFuzzyCompare(v, 0.01))) return false;
        stored = v;
    } else if (key == QLatin1String("accent")) {
        const QString v = value.toString();
        if (v != QLatin1String("orange") && v != QLatin1String("white")) return false;
        stored = v;
    } else if (key == QLatin1String("backdrop")) {
        bool ok = false; const int n = value.toInt(&ok);
        if (!ok || n < 0 || n > 2) return false;
        stored = n;
    } else if (key == QLatin1String("uiScale")) {
        bool ok = false; const int n = value.toInt(&ok);
        if (!ok || (n != 0 && n != 100 && n != 125 && n != 150)) return false;
        stored = n;
    } else if (key == QLatin1String("overlayCompat")) {
        // 设置 → 监控软件兼容; read by main.cpp before Qt starts.
        const QString v = value.toString();
        if (v != QLatin1String("auto") && v != QLatin1String("off")) return false;
        stored = v;
    } else if (key == QLatin1String("uiRenderer")) {
        // 设置 → 界面绘制 (veyra/ui/InterfaceRenderer.h); read by main.cpp before Qt starts.
        const QString v = value.toString();
        if (v != QLatin1String("software") && v != QLatin1String("gpu")) return false;
        stored = v;
    } else if (key == QLatin1String("gpuPriority")) {
        const QString v=value.toString();
        if(v!=QLatin1String("normal")&&v!=QLatin1String("high")&&v!=QLatin1String("realtime"))return false;
        stored=v;
    } else if (key == QLatin1String("decode")) {
        const QString v = value.toString();
        if (v != QLatin1String("auto") && v != QLatin1String("software") && v != QLatin1String("hardware")) return false;
        stored = v;
    } else if (key == QLatin1String("networkBuffer")) {
        // Network buffer level (#48): the ids of media::NetworkBufferLevel.
        const QString v = value.toString();
        static const QStringList levels{"auto", "off", "small", "medium", "large"};
        if (!levels.contains(v)) return false;
        stored = v;
    } else if (key == QLatin1String("networkCorrupt")) {
        // What to do with pictures damaged by packet loss (network sources only).
        const QString v = value.toString();
        if (v != QLatin1String("conceal") && v != QLatin1String("keyframe")) return false;
        stored = v;
    } else if (key == QLatin1String("rememberPosition") || key == QLatin1String("dockPinned")) {
        stored = value.toBool();
    } else if (key == QLatin1String("language")) {
        const QString v = value.toString();
        static const QStringList languages{"auto", "zh-CN", "zh-TW", "en", "ja"};
        if (!languages.contains(v)) return false;
        stored = v;
    } else if (key == QLatin1String("monitorGpu")) {
        const QString v = value.toString();
        if (!v.isEmpty() && std::none_of(impl_->gpuAdapters.begin(), impl_->gpuAdapters.end(), [&](const auto& a) { return a.id == v; })) return false;
        stored = v;
    } else if (key == QLatin1String("windowSize")) {
        const QString v = value.toString();
        static const QStringList sizes{"1280x800", "1600x1000", "1920x1200", "last"};
        if (!sizes.contains(v)) return false;
        stored = v;
    } else {
        return false;
    }
    if (impl_->prefs.value(key) == stored) return true;
    const auto previous = impl_->prefs;
    impl_->prefs[key] = stored;
    if (!impl_->savePrefs()) {
        impl_->prefs = previous;
        emit notice(tr("偏好保存失败，已保留原设置"), true);
        return false;
    }
    veyra::log::info("ui-prefs", std::format("{}={}", key.toStdString(), stored.toString().toStdString()));
    applyPreference(key);
    emit preferencesChanged();
    return true;
}
void QmlPlayerBridge::applyPreference(const QString& key) {
    if(key==QLatin1String("hdrFgCompatibility")){
        impl_->presentation.hdrFgCompatibility=impl_->prefBool("hdrFgCompatibility",false);
        impl_->engine.requestPresentation(impl_->presentation);
        emit presentationChanged();
    }
    if(key==QLatin1String("prewarmEnhancement")){
        impl_->engine.setEnhancementPrewarmEnabled(impl_->prefBool("prewarmEnhancement",false));
        if(impl_->prefBool("prewarmEnhancement",false))scheduleEnhancementPrewarm();
    }
    if(key==QLatin1String("gpuPriority")){
        const auto value=impl_->prefString("gpuPriority");
        // Unset means High (user default, 2026-11-13); saved choices stay explicit.
        gfx::requestGpuPriority(value==QLatin1String("normal")?gfx::GpuPriority::Normal:
                                value==QLatin1String("realtime")?gfx::GpuPriority::Realtime:gfx::GpuPriority::High);
    }
    if(key==QLatin1String("overlayCompat"))updateLiveCompanionCompat();
    if(key==QLatin1String("fullscreenMemoryProtection")){impl_->engine.setFullscreenMemoryProtection(impl_->prefBool("fullscreenMemoryProtection",false));emit snapshotChanged();}
    if(key==QLatin1String("captureShowAllFrameRates"))emit captureChanged();
    if (key == QLatin1String("audioDevice")) sink::setPreferredRenderEndpoint(impl_->prefString("audioDevice").toStdWString());
    else if (key == QLatin1String("audioForceStereo")) sink::setForceStereoDownmix(impl_->prefBool("audioForceStereo", false));
    else if (key == QLatin1String("magewellLowLatency")) source::magewell::setLowLatencyPreference(impl_->prefBool("magewellLowLatency", false));
    else if (key == QLatin1String("blackmagicNativeCapture")) { if(impl_->captureListed)refreshCaptureDevices();emit captureChanged(); }
    else if (key == QLatin1String("language")) {
        const QString code = i18n::resolve(impl_->prefString("language"));
        if (!i18n::apply(code)) emit notice(tr("这个语言的翻译没有找到，界面保持简体中文"), true);
        if (impl_->qmlEngine) {
            impl_->qmlEngine->setUiLanguage(i18n::current());
            impl_->qmlEngine->retranslate();
            // Strings the bridge builds itself (tr() in getters) follow once their
            // properties notify: every argument-free notify signal fires once.
            const QMetaObject* meta = metaObject();
            QSet<int> fired;
            for (int i = meta->propertyOffset(); i < meta->propertyCount(); ++i) {
                const QMetaMethod notify = meta->property(i).notifySignal();
                if (notify.isValid() && notify.parameterCount() == 0 && !fired.contains(notify.methodIndex())) {
                    fired.insert(notify.methodIndex());
                    notify.invoke(this, Qt::DirectConnection);
                }
            }
            impl_->exportQueue.refresh();
        }
    }
    else if (key == QLatin1String("monitorGpu") || key == QLatin1String("computeOnMonitorGpu")) {
        if (key == QLatin1String("monitorGpu")) { impl_->resolveGpuMonitor(); impl_->gpuUtil = -1.0; emit perfChanged(); }
        const auto before = impl_->computeLuid;
        impl_->applyComputeGpu();
        if (impl_->computeLuid != before) {
            // Effect availability follows the compute GPU (NVIDIA-only effects on another card).
            (void)currentEffectCapabilities(true);
            // The chain is checked against the new card the way a restart checks it:
            // DLSS moves to an available backend, NVOF to FidelityFX flow, and stages
            // the card cannot run are switched off with their parameters kept.
            auto candidate = impl_->currentSession();
            const auto previous = candidate;
            // Switching back gives the list mode its frame generation again (DLSS 4X
            // moved to FSR 2X by the last switch returns as DLSS 4X) once the card runs it.
            const auto caps = currentEffectCapabilities();
            const auto runs = [&](const QString& id) { return caps.value(id).toMap().value("available").toBool(); };
            auto& active = candidate.configurations[size_t(candidate.active)];
            const bool listMode = !active.editor;
            const auto memory = impl_->prefs.value(QStringLiteral("computeFgMemory")).toMap();
            bool restored = false;
            if (listMode && !memory.isEmpty()) {
                const auto backend = engine::FrameGenerationBackend(memory.value(QStringLiteral("backend")).toInt());
                const auto flow = engine::OpticalFlowBackend(memory.value(QStringLiteral("flow")).toInt());
                if (runs(fgEffectId(backend))) {
                    active.fgBackend = backend;
                    active.chain.fgMultiplier = memory.value(QStringLiteral("multiplier")).toUInt();
                    if (flow != engine::OpticalFlowBackend::Nvidia || runs(QStringLiteral("flow0"))) active.opticalFlowBackend = flow;
                    impl_->prefs.remove(QStringLiteral("computeFgMemory"));
                    restored = true;
                    veyra::log::info("effect-availability", std::format("compute gpu switch restored backend={} multiplier={}",
                        engine::frameGenerationBackendName(backend), active.chain.fgMultiplier));
                }
            }
            QStringList disabled;
            for (auto& configuration : candidate.configurations) {
                const auto reason = disableUnavailableEffects(configuration);
                if (!reason.isEmpty()) disabled << reason;
            }
            const auto& before = previous.configurations[size_t(previous.active)];
            if (listMode && !restored && (before.fgBackend != active.fgBackend || before.opticalFlowBackend != active.opticalFlowBackend))
                impl_->prefs[QStringLiteral("computeFgMemory")] = QVariantMap{{QStringLiteral("backend"), int(before.fgBackend)},
                    {QStringLiteral("multiplier"), int(before.chain.fgMultiplier)}, {QStringLiteral("flow"), int(before.opticalFlowBackend)}};
            if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "compute gpu frame generation memory not saved");
            if (candidate != previous) {
                auto next = impl_->facade.pendingSettings();
                const auto& configuration = candidate.configurations[size_t(candidate.active)];
                configuration.apply(next);
                if (next.validate().empty() && impl_->facade.applySettings(next, engine::runtimeOrder(configuration.chain))) {
                    impl_->session = candidate;
                    impl_->restoreConfiguration(configuration);
                    impl_->revalidate();
                    impl_->options = engine::PlayerOptions::from(next, engine::runtimeOrder(impl_->activeRuntime()));
                    if (!impl_->persistSession()) veyra::log::warn("effect-availability", "compute gpu refit not saved");
                    emitChainChanged(); emit settingsChanged();
                } else veyra::log::warn("effect-availability", "compute gpu refit not accepted; chain unchanged");
                if (!disabled.isEmpty()) {
                    const auto message = tr("已关闭当前硬件或包不支持的效果，参数仍保留：") + disabled.join("; ");
                    veyra::log::warn("effect-availability", message.toStdString());
                    emit notice(message, false);
                }
            }
            emit effectCapabilitiesChanged(); emit fgChoicesChanged();
            // The low queue follows the new card's own control: the NVIDIA states
            // there, the plain switch elsewhere (the NVIDIA choice is kept).
            if (engine::syncNvidiaLowLatencyQueue(impl_->presentation, nvidiaLowLatencyAvailable())) applyPresentation();
            if (impl_->snapshot.running)
                emit notice(impl_->computeLuid ? tr("运算显卡已切换为「%1」，重新打开视频后生效").arg(impl_->computeGpuName)
                                               : tr("运算显卡已恢复默认（性能最强的显卡），重新打开视频后生效"), false);
        }
    }
}
QVariantList QmlPlayerBridge::gpuMonitorChoices() const {
    QVariantList out;
    out << QVariantMap{{"id", QString()}, {"label", tr("自动（独立显卡）")}};
    for (const auto& a : impl_->gpuAdapters) out << QVariantMap{{"id", a.id}, {"label", a.name}};
    return out;
}
QString QmlPlayerBridge::gpuMonitorName() const { return impl_->gpuMonitorName; }
QString QmlPlayerBridge::computeGpuHint() const {
    if (!impl_->prefBool("computeOnMonitorGpu", false)) return tr("关闭时增强、补帧和导出用性能最强的显卡");
    if (impl_->computeLuid) return tr("增强、补帧和导出都在「%1」上运行；切换后重新打开视频生效").arg(impl_->computeGpuName);
    return tr("上面选“自动”时仍用性能最强的显卡；选一块显卡后它也负责运算");
}
QString QmlPlayerBridge::uiLanguage() const { return i18n::current(); }
void QmlPlayerBridge::setQmlEngine(QQmlEngine* engine) {
    impl_->qmlEngine = engine;
    if (engine) engine->setUiLanguage(i18n::current());
}
QVariantList QmlPlayerBridge::audioDevices() const {
    QVariantList out;
    out << QVariantMap{{"id", QString()}, {"label", tr("跟随系统默认")}, {"isDefault", false}};
    const QString chosen = impl_->prefString("audioDevice");
    bool listed = chosen.isEmpty();
    for (const auto& e : impl_->audioEndpoints) {
        const QString id = QString::fromStdWString(e.id);
        listed = listed || id == chosen;
        out << QVariantMap{{"id", id}, {"label", QString::fromStdWString(e.name) + (e.isDefault ? tr(" · 当前默认") : QString())},
                           {"isDefault", e.isDefault}};
    }
    // A saved device that is unplugged stays visible, marked, instead of vanishing.
    if (!listed) out << QVariantMap{{"id", chosen}, {"label", tr("已保存的设备（未连接）")}, {"isDefault", false}, {"missing", true}};
    return out;
}
QString QmlPlayerBridge::audioOutputStatus() const {
    const auto& a = impl_->audioActive;
    if (!a.valid) return tr("尚未打开音频输出");
    QString text = QString::fromStdWString(a.name.empty() ? L"(未命名设备)" : a.name) + tr(" · %1 声道").arg(a.channels);
    if (a.downmix) text += tr(" · 下混");
    if (a.fallback) text = tr("所选设备不可用，已回落到系统默认：") + text;
    return text;
}
bool QmlPlayerBridge::audioOutputFallback() const { return impl_->audioActive.valid && impl_->audioActive.fallback; }
void QmlPlayerBridge::refreshAudioDevices() {
    impl_->audioEndpoints = sink::enumerateRenderEndpoints();
    impl_->audioActive = sink::activeRenderEndpoint();
    veyra::log::info("ui-audio", std::format("render endpoints={}", impl_->audioEndpoints.size()));
    emit audioDevicesChanged();
}
// --- shortcuts (P4-f) --------------------------------------------------------
namespace {
const QVariantMap& shortcutDefaults() {
    // Screenshot was Ctrl+S before 2026-10-27; saved custom keys are kept.
    static const QVariantMap d{{"playPause", "Space"}, {"fullscreen", "F"}, {"lock", "Ctrl+L"},
                               {"hold", "V"}, {"screenshot", "F7"}, {"record", "F8"}, {"toggleMode", "Tab"},
                               {"gamePanel", "Home"}, {"gameExit", "Ctrl+Alt+Home"}};
    return d;
}
}
QVariantMap QmlPlayerBridge::shortcuts() const {
    QVariantMap out = shortcutDefaults();
    const auto custom = impl_->prefs.value("shortcuts").toMap();
    QStringList taken;
    for (auto it = custom.begin(); it != custom.end(); ++it)
        if (out.contains(it.key())) {
            if ((it.key()=="gamePanel"||it.key()=="gameExit") && !ui::gameKey(QKeySequence(it.value().toString(),QKeySequence::PortableText)).vk) continue;
            out[it.key()] = it.value();
            taken << it.value().toString();
        }
    // A default added later (F7/F8) may be a key the user already gave another
    // action: that action keeps it and the new one stays unbound until set.
    for (auto it = out.begin(); it != out.end(); ++it)
        if (!custom.contains(it.key()) && taken.contains(it.value().toString())) it.value() = QString();
    return out;
}
void QmlPlayerBridge::applyGameHotkeys() {
    const auto keys = shortcuts();
    const auto key = [&keys](const char* action) { return ui::gameKey(QKeySequence(keys.value(action).toString(), QKeySequence::PortableText)); };
    impl_->game.setHotkeys(key("gamePanel"), key("gameExit"), key("screenshot"), key("record"));
    // At start and after every change: a field log then says which key each
    // action answers to (the 2026-11-08 recording report had no way to tell).
    const auto custom = impl_->prefs.value("shortcuts").toMap();
    std::string line, changed;
    for (auto it = keys.begin(); it != keys.end(); ++it) {
        line += std::format("{}{}={}", line.empty() ? "" : " ", it.key().toStdString(),
                            it.value().toString().isEmpty() ? std::string("none") : it.value().toString().toStdString());
        if (custom.contains(it.key())) changed += (changed.empty() ? "" : ",") + it.key().toStdString();
    }
    veyra::log::info("ui-shortcuts", std::format("{} custom={}", line, changed.empty() ? std::string("none") : changed));
}
bool QmlPlayerBridge::setShortcut(const QString& action, const QString& sequence) {
    if (!shortcutDefaults().contains(action)) return false;
    const QKeySequence seq(sequence, QKeySequence::PortableText);
    if (seq.isEmpty() || seq.count() != 1) { emit notice(tr("无效的按键"), true); return false; }
    const QString text = seq.toString(QKeySequence::PortableText);
    if ((action=="gamePanel"||action=="gameExit") && !ui::gameKey(seq).vk) {
        emit notice(tr("无效的按键"),true); return false;
    }
    if ((action=="gamePanel"||action=="gameExit") &&
        (text=="Ctrl+Alt+Del"||text=="Alt+Tab"||text=="Alt+F4"||text=="Ctrl+Shift+Esc"||text=="Alt+Enter")) {
        emit notice(tr("%1 已被固定功能占用").arg(text),true); return false;
    }
    // Keys the shell already owns cannot be taken over.
    static const QStringList reserved{"Esc", "F11", "Left", "Right", "Up", "Down", "Ctrl+O", "Ctrl+E",
                                      "B", "Z", "X", "T", "Y", "Shift+Z", "Shift+X", "Alt+Return"};
    if (reserved.contains(text)) { emit notice(tr("%1 已被固定功能占用").arg(text), true); return false; }
    const auto current = shortcuts();
    for (auto it = current.begin(); it != current.end(); ++it)
        if (it.key() != action && it.value().toString() == text) {
            emit notice(tr("%1 已用于其他操作").arg(text), true); return false;
        }
    auto custom = impl_->prefs.value("shortcuts").toMap();
    custom[action] = text;
    const auto previous = impl_->prefs;
    impl_->prefs["shortcuts"] = custom;
    if (!impl_->savePrefs()) { impl_->prefs = previous; emit notice(tr("快捷键保存失败"), true); return false; }
    veyra::log::info("ui-prefs", std::format("shortcut {}={}", action.toStdString(), text.toStdString()));
    applyGameHotkeys();
    emit preferencesChanged();
    return true;
}
void QmlPlayerBridge::resetShortcuts() {
    const auto previous = impl_->prefs;
    impl_->prefs.remove("shortcuts");
    if (!impl_->savePrefs()) { impl_->prefs = previous; return; }
    applyGameHotkeys();
    emit preferencesChanged();
}
bool QmlPlayerBridge::obsGameCaptureActive() const { return qApp && qApp->property("veyraObsGameCapture").toBool(); }
bool QmlPlayerBridge::overlayCompatActive() const { return qApp && qApp->property("veyraOverlayCompat").toBool(); }
bool QmlPlayerBridge::softwareUiActive() const { return qApp && qApp->property("veyraSoftwareUi").toBool(); }
bool QmlPlayerBridge::rivaTunerRunning() const { return gfx::rivaTunerRunning(); }
bool QmlPlayerBridge::presentationOverlayRunning() const {
    return gfx::rivaTunerRunning() || gfx::nvidiaOverlayRunning() || gfx::gamePPRunning() || gfx::douyinLiveCompanionRunning();
}
// The field hangs with 抖音直播伴侣 (2026-10-15) came with this process at Realtime GPU
// priority. OBS itself never asks for Realtime when hardware-accelerated GPU scheduling
// is on (High instead, libobs-d3d11 set_priority); the same class is used here while
// the companion runs, so neither program can hold the GPU off from the other.
void QmlPlayerBridge::updateLiveCompanionCompat() {
    const bool automatic = impl_->prefString("overlayCompat") != QLatin1String("off");
    const bool running = automatic && !GetEnvironmentVariableW(L"VEYRA_TEST_IGNORE_DOUYIN", nullptr, 0)
                         && gfx::douyinLiveCompanionRunning();
    if (running == impl_->liveCompanionCap) return;
    impl_->liveCompanionCap = running;
    gfx::setGpuPriorityCap(running ? gfx::GpuPriority::High : gfx::GpuPriority::Realtime);
    const auto status = gfx::gpuPriorityStatus();
    veyra::log::warn("overlay-hooks", std::format("Douyin live companion {}: GPU priority cap={} requested={} actual={}",
        running ? "running" : "gone", running ? "high" : "none", int(status.requested), status.actual));
    if (running && status.capped)
        emit notice(tr("检测到抖音直播伴侣：GPU 优先级暂时从“实时”降为“高”，避免两个程序争用显卡时卡死；直播伴侣退出后自动恢复"), false);
    emit preferencesChanged();
}

int QmlPlayerBridge::uiScaleActive() const { return qApp ? qApp->property("veyraUiScale").toInt() : 0; }
void QmlPlayerBridge::openFeedbackPage() {
    copyDiagnostics();
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Likely7/Veyra-NRVideo/issues")));
    emit notice(tr("诊断信息已复制，可直接粘贴到反馈里"), false);
}
void QmlPlayerBridge::openReleasesPage() {
    QDesktopServices::openUrl(QUrl(QStringLiteral("https://github.com/Likely7/Veyra-NRVideo/releases")));
}
bool QmlPlayerBridge::openSponsorLink(const QString& id) {
    // The author's pages as the author gave them (2026-10-19); the page shows buttons,
    // never these addresses.
    static constexpr std::pair<const char*, const char*> links[] = {
        {"bilibili", "https://space.bilibili.com/19500532?spm_id_from=333.1007.0.0"},
        {"douyin", "https://v.douyin.com/9s06Bf2Inqc/"},
        {"x", "https://x.com/likely2002"},
        {"github", "https://github.com/Likely7"},
        {"youtube", "https://www.youtube.com/@Likely7Ai"},
        {"patreon", "https://www.patreon.com/cw/Likely7"},
        {"patreonCoffee", "https://www.patreon.com/Likely7/posts/buy-me-coffee-175018110"},
    };
    for (const auto& [name, url] : links) {
        if (id != QLatin1StringView(name)) continue;
        veyra::log::info("ui-sponsor", std::format("open {}", name));
        // Acceptance runs click every card: they read the address from the log
        // instead of opening a browser on the test machine.
        if (qEnvironmentVariableIsSet("VEYRA_TEST_SPONSOR_NO_BROWSER")) {
            veyra::log::info("ui-sponsor", std::format("test url {}", url));
            return true;
        }
        if (QDesktopServices::openUrl(QUrl(QString::fromLatin1(url)))) return true;
        emit notice(tr("无法打开浏览器"), true);
        return false;
    }
    return false;
}
QObject* QmlPlayerBridge::updates() const { return updates_; }
bool QmlPlayerBridge::eventFilter(QObject* watched, QEvent* event) {
    const auto type = event->type();
    if (watched == impl_->gameWindow && (type == QEvent::WinIdChange ||
        (type == QEvent::PlatformSurface && static_cast<QPlatformSurfaceEvent*>(event)->surfaceEventType() ==
                                          QPlatformSurfaceEvent::SurfaceCreated))) {
        // Rebind after Qt's platform-window transition, never during teardown.
        QMetaObject::invokeMethod(this, [this] {
            if (!impl_->gameWindow || !impl_->gameWindow->handle()) return;
            impl_->game.attach(reinterpret_cast<HWND>(impl_->gameWindow->winId()));
            updateGameGeometry(); updateGameMode();
        }, Qt::QueuedConnection);
    }
    // Remembered for the record trigger log only; the event goes on unchanged.
    if (type == QEvent::ShortcutOverride) {
        const auto* key = static_cast<QKeyEvent*>(event);
        auto& seen = impl_->lastShortcutKey;
        seen.key = key->key(); seen.modifiers = int(key->modifiers());
        seen.virtualKey = key->nativeVirtualKey(); seen.scanCode = key->nativeScanCode();
        seen.autoRepeat = key->isAutoRepeat(); seen.atMs = QDateTime::currentMSecsSinceEpoch();
        seen.window = watched->objectName().isEmpty() ? QString::fromLatin1(watched->metaObject()->className()) : watched->objectName();
    }
    if (type == QEvent::KeyPress || type == QEvent::KeyRelease) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (!key->isAutoRepeat() && qobject_cast<QWindow*>(watched)) {
            const QKeySequence hold(shortcuts().value("hold").toString(), QKeySequence::PortableText);
            const int pressed = key->key() | int(key->modifiers() & ~Qt::KeypadModifier);
            // Typing into a text field never compares.
            const QObject* focus = QGuiApplication::focusObject();
            const bool typing = focus && (focus->inherits("QQuickTextInput") || focus->inherits("QQuickTextEdit"));
            if (!hold.isEmpty() && hold[0].toCombined() == pressed && !typing && impl_->prefBool("holdCompare", true)) {
                const bool down = type == QEvent::KeyPress;
                if (down != impl_->holdKeyDown) { impl_->holdKeyDown = down; holdOriginal(down); }
                return true;
            }
            if (type == QEvent::KeyRelease && impl_->holdKeyDown && key->key() == QKeySequence(hold)[0].key()) {
                impl_->holdKeyDown = false; holdOriginal(false);
            }
        }
    } else if (type == QEvent::ApplicationDeactivate && impl_->holdKeyDown) {
        impl_->holdKeyDown = false; holdOriginal(false);
    }
    return QObject::eventFilter(watched, event);
}
// Remembered playback positions: files only, the last 40, saved on switch/stop/exit.
void QmlPlayerBridge::rememberPosition(bool flush) {
    const auto& s = impl_->snapshot;
    const QString file = QString::fromStdWString(impl_->sourceLabel);
    if (s.running && !s.image && !s.capture && s.duration > 0 && !file.isEmpty() && QFileInfo(file).isFile()) {
        auto positions = impl_->prefs.value("positions").toMap();
        const double at = s.position;
        if (at >= 0.5 && at < s.duration - 0.5) positions[file] = QVariantMap{{"at", at}, {"t", QDateTime::currentSecsSinceEpoch()}};
        else positions.remove(file);   // finished or barely started: next time from the top
        while (positions.size() > 40) {
            auto oldest = positions.begin();
            for (auto it = positions.begin(); it != positions.end(); ++it)
                if (it.value().toMap().value("t").toLongLong() < oldest.value().toMap().value("t").toLongLong()) oldest = it;
            positions.erase(oldest);
        }
        impl_->prefs["positions"] = positions;
    }
    if (flush) impl_->savePrefs();
}

QString QmlPlayerBridge::screenshotDirectory() const {
    const QString chosen = impl_->prefString("screenshotDir");
    if (!chosen.isEmpty()) return chosen;
    const QString pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    return QDir(pictures.isEmpty() ? QDir::homePath() : pictures).filePath(QStringLiteral("Veyra"));
}
QString QmlPlayerBridge::lastScreenshot() const { return impl_->lastScreenshot; }
QString QmlPlayerBridge::dataDirectory() const { return QString::fromStdWString(impl_->dataDir.wstring()); }
// main() records the file it actually opened (it falls back to a per-process name).
QString QmlPlayerBridge::logFile() const { return qApp ? qApp->property("veyraLogFile").toString() : QString(); }
void QmlPlayerBridge::chooseScreenshotDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(nullptr, tr("选择截图目录"), screenshotDirectory());
    if (!dir.isEmpty()) setPreference(QStringLiteral("screenshotDir"), dir);
}
void QmlPlayerBridge::openScreenshotDirectory() {
    const QString dir = screenshotDirectory();
    QDir().mkpath(dir);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) emit notice(tr("无法打开目录：%1").arg(dir), true);
}
void QmlPlayerBridge::openLogFolder() {
    const QString file = logFile();
    const QString dir = file.isEmpty() ? QString() : QFileInfo(file).absolutePath();
    if (dir.isEmpty() || !QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) emit notice(tr("日志目录不可用"), true);
}
void QmlPlayerBridge::openDataFolder() {
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dataDirectory()))) emit notice(tr("无法打开数据目录"), true);
}

// --- P5 (2026-09-29): headers, colour history, export rows, presets -------
QString QmlPlayerBridge::sourceKind() const {
    const auto& s = impl_->snapshot;
    if (!hasSource()) return {};
    if (s.image) return QStringLiteral("image");
    if (s.remotePlay) return QStringLiteral("ps5");
    if (!impl_->liveKind.isEmpty()) return impl_->liveKind;
    if (s.capture) return QStringLiteral("capture");
    return QStringLiteral("file");
}
QString QmlPlayerBridge::sourceTitle() const {
    const QString kind = sourceKind();
    if (kind.isEmpty()) return {};
    if(kind==QLatin1String("url"))return tr("网络视频 · %1").arg(impl_->liveLabel);
    if (kind == QLatin1String("file") || kind == QLatin1String("image")) return sourceName();
    if (kind == QLatin1String("capture")) {
        const QString name = impl_->prefString("captureDeviceName");
        return tr("采集卡 · %1").arg(name.isEmpty() ? impl_->liveLabel : name);
    }
    if (kind == QLatin1String("ps5")) return tr("PS5 串流 · %1").arg(impl_->liveLabel);
    if (kind == QLatin1String("moonlight")) return tr("PC 串流 · %1").arg(impl_->liveLabel);
    if (kind == QLatin1String("xbox")) return tr("Xbox 串流 · %1").arg(impl_->liveLabel);
    return tr("屏幕捕获 · %1").arg(impl_->liveLabel);
}
// "1080p60" / "1080p59.94": the reported height and nominal rate only.
QString QmlPlayerBridge::sourceRateText() const {
    const auto& s = impl_->snapshot;
    if (!s.sourceHeight) return {};
    if (s.image) return QStringLiteral("%1×%2").arg(s.sourceWidth).arg(s.sourceHeight);
    const double fps = s.nominalSourceFps;
    if (fps <= 0.0) return QStringLiteral("%1p").arg(s.sourceHeight);
    const bool whole = std::abs(fps - std::round(fps)) < 0.01;
    return QStringLiteral("%1p%2").arg(s.sourceHeight)
        .arg(whole ? QString::number(int(std::round(fps))) : QString::number(fps, 'f', 2));
}
QString QmlPlayerBridge::sourceFormatText() const {
    QStringList parts;
    const QString rate = sourceRateText();
    if (rate.isEmpty()) return {};
    parts << rate;
    if (sourceKind() == QLatin1String("capture")) {
        for (const auto& f : impl_->captureFormatList)
            if (f.key == impl_->captureFormatKey) {
                const QString label = uiText(f.label);
                const int dot = label.lastIndexOf(QString::fromUtf8("·"));
                if (dot >= 0) parts << label.mid(dot + 1).trimmed();
                break;
            }
    }
    const QString colour = uiText(impl_->snapshot.colorStatus);
    if (!colour.isEmpty()) parts << colour.section(QString::fromUtf8(" → "), 0, 0).trimmed();
    return parts.join(QString::fromUtf8(" · "));
}

bool QmlPlayerBridge::colourCanUndo() const { return !impl_->colourUndo.empty(); }
bool QmlPlayerBridge::colourCanRedo() const { return !impl_->colourRedo.empty(); }
bool QmlPlayerBridge::colourCanPaste() const { return impl_->colourClip.has_value(); }
namespace {
bool isColourNode(const engine::EffectChain& chain, int index) {
    return index >= 0 && uint32_t(index) < chain.nodeCount && chain.nodes[index].type == engine::EffectType::Color;
}
}
// Undo and redo re-commit a stored state through the normal path (validation,
// chain rebuild), marked as a replay so it does not push history of its own.
bool QmlPlayerBridge::colourUndo() {
    while (!impl_->colourUndo.empty()) {
        const auto step = impl_->colourUndo.back();
        impl_->colourUndo.pop_back();
        if (!isColourNode(impl_->chain, step.index)) continue;
        impl_->selectedColour = step.index;
        const auto current = selectedColourSettings();
        impl_->colourReplaying = true;
        const bool ok = commitColour(step.colour);
        impl_->colourReplaying = false;
        if (ok) impl_->colourRedo.push_back({step.index, current});
        impl_->colourLastIndex = -1;
        veyra::log::info("ui-colour", std::format("undo layer={} ok={} left={}", step.index, ok, impl_->colourUndo.size()));
        emit settingsChanged();
        return ok;
    }
    emit settingsChanged();
    return false;
}
bool QmlPlayerBridge::colourRedo() {
    while (!impl_->colourRedo.empty()) {
        const auto step = impl_->colourRedo.back();
        impl_->colourRedo.pop_back();
        if (!isColourNode(impl_->chain, step.index)) continue;
        impl_->selectedColour = step.index;
        const auto current = selectedColourSettings();
        impl_->colourReplaying = true;
        const bool ok = commitColour(step.colour);
        impl_->colourReplaying = false;
        if (ok) impl_->colourUndo.push_back({step.index, current});
        impl_->colourLastIndex = -1;
        veyra::log::info("ui-colour", std::format("redo layer={} ok={}", step.index, ok));
        emit settingsChanged();
        return ok;
    }
    emit settingsChanged();
    return false;
}
void QmlPlayerBridge::colourCopy() {
    if (impl_->colourIndex() < 0) { emit notice(tr("处理链中没有调色节点"), true); return; }
    impl_->colourClip = selectedColourSettings();
    veyra::log::info("ui-colour", "copy");
    emit settingsChanged();
    emit notice(tr("已复制调色参数"), false);
}
bool QmlPlayerBridge::colourPaste() {
    if (!impl_->colourClip) return false;
    auto next = *impl_->colourClip;
    next.enabled = selectedColourSettings().enabled;   // paste the grade, not the switch
    impl_->colourLastIndex = -1;                        // a paste is its own step
    const bool ok = commitColour(next);
    veyra::log::info("ui-colour", std::format("paste ok={}", ok));
    return ok;
}

QVariantList QmlPlayerBridge::exportItems() const {
    QVariantList result;for(int i=0;i<impl_->exportQueue.count();++i)result<<impl_->exportQueue.get(i);return result;
}
void QmlPlayerBridge::addCurrentExportFile(){
    if(!QFileInfo(utf8Of(impl_->sourceLabel)).isFile())return;
    const auto id=impl_->exportQueue.addFile(utf8Of(impl_->sourceLabel));
    impl_->exportQueue.queue().setTrim(id,impl_->exportTrimStartSeconds,impl_->exportTrimEndSeconds);
    impl_->exportQueue.setSelectedId(id);emit exportChanged();
}
QObject* QmlPlayerBridge::exportQueueModel() const{return &impl_->exportQueue;}
int QmlPlayerBridge::exportContainer()const{return impl_->exportContainer;}
void QmlPlayerBridge::setExportContainer(int value){
    if(value<0||value>1||value==impl_->exportContainer)return;
    impl_->exportContainer=value;impl_->exportQueue.queue().setContainer(static_cast<engine::ExportContainer>(value));
    impl_->exportQueue.refresh();
}
int QmlPlayerBridge::exportReadyCount()const{return int(impl_->exportQueue.queue().readyCount());}
bool QmlPlayerBridge::exportSelectionEditable()const{
    const auto* item=impl_->exportQueue.queue().find(impl_->exportQueue.selectedId());
    return item&&item->state!=engine::ExportItemState::Running&&item->state!=engine::ExportItemState::Queued;
}
int QmlPlayerBridge::exportAudioPolicy()const{
    const auto* item=impl_->exportQueue.queue().find(impl_->exportQueue.selectedId());return item?int(item->media.audio.policy):1;
}
int QmlPlayerBridge::exportSubtitlePolicy()const{
    const auto* item=impl_->exportQueue.queue().find(impl_->exportQueue.selectedId());return item?int(item->media.subtitles.policy):1;
}
QVariantList QmlPlayerBridge::exportTracks()const{
    QVariantList result;const auto* item=impl_->exportQueue.queue().find(impl_->exportQueue.selectedId());if(!item)return result;
    for(const auto& track:item->info.tracks){const auto& selection=track.audio?item->media.audio:item->media.subtitles;
        const bool selected=selection.policy==engine::ExportTrackPolicy::All||selection.contains(track.index);
        result<<QVariantMap{{"index",track.index},{"audio",track.audio},{"selected",selected},{"compatible",track.compatible},
            {"label",QStringLiteral("%1 · %2 · %3%4").arg(track.index).arg(utf8Of(track.codec)).arg(utf8Of(track.language)).arg(track.channels?QStringLiteral(" · %1 声道").arg(track.channels):QString())},
            {"title",utf8Of(track.title)},{"action",utf8Of(track.action)}};
    }
    return result;
}
void QmlPlayerBridge::setExportTrackPolicy(bool audio,int policy){
    if(policy<1||policy>3)return;
    auto& queue=impl_->exportQueue.queue();const auto id=impl_->exportQueue.selectedId();const auto* item=queue.find(id);if(!item)return;
    engine::ExportTrackSelection selection;selection.policy=static_cast<engine::ExportTrackPolicy>(policy);
    if(selection.policy==engine::ExportTrackPolicy::Selected){
        for(const auto& track:item->info.tracks)if(track.audio==audio){selection.indices[0]=track.index;selection.count=1;break;}
        if(!selection.count){emit notice(tr("该文件没有可选轨道"),true);return;}
    }
    if(queue.setTracks(id,audio,selection))impl_->exportQueue.refresh();
}
void QmlPlayerBridge::toggleExportTrack(bool audio,int index,bool keep){
    auto& queue=impl_->exportQueue.queue();const auto id=impl_->exportQueue.selectedId();const auto* item=queue.find(id);if(!item)return;
    const auto& previous=audio?item->media.audio:item->media.subtitles;
    engine::ExportTrackSelection selection;selection.policy=engine::ExportTrackPolicy::Selected;
    for(const auto& track:item->info.tracks)if(track.audio==audio){
        // Leaving "all" keeps what "all" actually carried: tracks it skipped for
        // this container stay out instead of turning into a hard failure.
        const bool selected=track.index==index?keep:previous.policy==engine::ExportTrackPolicy::All?track.compatible:previous.contains(track.index);
        if(selected){if(selection.count>=selection.indices.size()){emit notice(tr("手动选择最多 64 条；保留全部不限制轨道数量"),true);return;}selection.indices[selection.count++]=track.index;}
    }
    if(!selection.count)selection.policy=engine::ExportTrackPolicy::None;
    if(queue.setTracks(id,audio,selection))impl_->exportQueue.refresh();
}
void QmlPlayerBridge::previewExportItem(qulonglong id){
    const auto* item=impl_->exportQueue.queue().find(id);if(!item)return;
    const QString input=utf8Of(item->input);const double start=item->start,end=item->end;
    if(input!=utf8Of(impl_->sourceLabel))openPath(input);
    impl_->exportQueue.setSelectedId(id);impl_->exportTrimStartSeconds=start;impl_->exportTrimEndSeconds=end;
    emit exportChanged();
}
bool QmlPlayerBridge::exportCompletionSound()const{return impl_->prefs.value(QStringLiteral("exportCompletionSound"),true).toBool();}
void QmlPlayerBridge::setExportCompletionSound(bool enabled){
    const auto previous=impl_->prefs;impl_->prefs[QStringLiteral("exportCompletionSound")]=enabled;
    if(!impl_->savePrefs()){impl_->prefs=previous;emit notice(tr("提示音设置保存失败"),true);}
    emit exportChanged();
}
void QmlPlayerBridge::clearFinishedExportItems() {
    for(int row=impl_->exportQueue.count()-1;row>=0;--row){const auto& item=impl_->exportQueue.queue().items()[row];
        if(item.state==engine::ExportItemState::Done||item.state==engine::ExportItemState::Failed||item.state==engine::ExportItemState::Cancelled)impl_->exportQueue.removeItem(item.id);
    }
    emit exportChanged();
}
// A thumbnail of any file (export queue rows), through the same provider.
QString QmlPlayerBridge::fileThumbnailId(const QString& path, double seconds) const {
    if (path.isEmpty()) return {};
    return QStringLiteral("image://veyra-thumb/file/%1/%2/%3")
        .arg(impl_->thumbnailGeneration)
        .arg(QString::fromLatin1(path.toUtf8().toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals)))
        .arg(qint64(std::max(0.0, seconds) * 1000.0));
}

QString QmlPlayerBridge::capturePreviewUrl() const {
    const auto& s = impl_->snapshot;
    if (!s.running || s.posterRgba.empty() || !s.posterWidth || !s.posterHeight) return {};
    if (impl_->posterSession != s.sessionId || impl_->posterUrl.isEmpty()) {
        const QImage image(s.posterRgba.data(), int(s.posterWidth), int(s.posterHeight), int(s.posterWidth * 4),
                           QImage::Format_RGBA8888);
        QByteArray png;
        QBuffer buffer(&png);
        buffer.open(QIODevice::WriteOnly);
        image.save(&buffer, "PNG");
        impl_->posterSession = s.sessionId;
        impl_->posterUrl = png.isEmpty() ? QString()
                                         : QStringLiteral("data:image/png;base64,") + QString::fromLatin1(png.toBase64());
    }
    return impl_->posterUrl;
}
QString QmlPlayerBridge::captureSignalLevel() const {
    const auto& s = impl_->snapshot;
    if (!s.running || !s.capture) return QStringLiteral("idle");
    if (s.captureRecovering) return QStringLiteral("warn");
    if (s.captureSampledBlack) return QStringLiteral("warn");
    return s.captureFps > 0.5 ? QStringLiteral("ok") : QStringLiteral("warn");
}
QString QmlPlayerBridge::captureSignalText() const {
    const auto& s = impl_->snapshot;
    if (captureSignalLevel() == QLatin1String("idle")) return tr("未连接");
    if (s.captureRecovering) return tr("正在恢复");
    if (s.captureFps > 0.5 && s.captureSampledBlack) return tr("收到黑帧 · 检查输入制式");
    return s.captureFps > 0.5 ? tr("正在收帧") : tr("等待信号");
}

void QmlPlayerBridge::importPresetDialog() {
    const QString path = QFileDialog::getOpenFileName(nullptr, tr("导入预设"), QString(),
                                                      tr("Veyra 预设 (*.vpreset);;所有文件 (*)"));
    if (!path.isEmpty()) importPreset(path);
}
bool QmlPlayerBridge::importPreset(const QString& path) {
    std::wstring name;
    auto& library = impl_->facade.presets();
    const bool ok = library.importFile(std::filesystem::path(wideOf(path)), name);
    veyra::log::info("ui-preset", std::format("import ok={} name={}", ok, utf8Of(name).toStdString()));
    if (!ok) { emit notice(tr("导入失败：%1").arg(uiText(library.error())), true); return false; }
    emit presetsChanged();
    emit notice(tr("已导入预设：%1").arg(utf8Of(name)), false);
    return true;
}
void QmlPlayerBridge::exportPresetDialog(int index) {
    const auto& entries = impl_->facade.presets().entries();
    if (index < 0 || size_t(index) >= entries.size()) return;
    const QString path = QFileDialog::getSaveFileName(nullptr, tr("导出预设"),
                                                      utf8Of(entries[size_t(index)].name) + QStringLiteral(".vpreset"),
                                                      tr("Veyra 预设 (*.vpreset)"));
    if (!path.isEmpty()) exportPreset(index, path);
}
bool QmlPlayerBridge::exportPreset(int index, const QString& path) {
    auto& library = impl_->facade.presets();
    if (index < 0 || size_t(index) >= library.entries().size()) return false;
    const bool ok = library.exportEntry(size_t(index), std::filesystem::path(wideOf(path)));
    veyra::log::info("ui-preset", std::format("export index={} ok={}", index, ok));
    if (!ok) { emit notice(tr("导出失败：%1").arg(uiText(library.error())), true); return false; }
    emit notice(tr("预设已导出到：%1").arg(QDir::toNativeSeparators(path)), false);
    return true;
}

QString QmlPlayerBridge::liveOpeningText() const {
    if (!impl_->pendingLiveText.isEmpty()) return impl_->pendingLiveText;
    const auto& s = impl_->snapshot;
    if (s.running || s.failed || s.transport != engine::TransportState::Opening) return {};
    const QString status = veyra::ui::i18n::text(s.status);
    if (s.remotePlay)
        return status.contains(QStringLiteral("PIN")) ? status : tr("正在连接 PS5 · %1").arg(impl_->liveLabel);
    if (impl_->liveKind == QLatin1String("screen")) return tr("正在开始屏幕捕获…");
    if (s.capture) return tr("正在打开采集设备…");
    return {};
}


} // namespace veyra::ui
