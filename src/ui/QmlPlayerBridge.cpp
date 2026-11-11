// QML bridge: Core responsibilities.
#include "QmlPlayerBridge_p.h"

namespace veyra::ui {
namespace {
bool prepareStartupPreset(const engine::PresetEntry& entry, engine::ChainSession& session,
                          engine::EnhancementSettings& settings, QString& error) {
    if (!session.select(entry.kind)) {
        error = QObject::tr("默认预设的编辑模式无效");
        return false;
    }
    auto& configuration = session.configurations[size_t(entry.kind)];
    configuration.apply(settings);
    if (entry.kind == engine::ChainMode::List) {
        auto chain = configuration.chain;
        const auto result = engine::PresetLibrary::applyToChain(entry, chain, settings);
        if (!result.accepted) { error = uiText(result.message); return false; }
        configuration = engine::ChainConfiguration::capture(chain, settings);
    } else {
        engine::NodeEditorDocument document;
        if (configuration.editor) document = *configuration.editor;
        else {
            document.nodes = configuration.chain;
            const auto result = document.layout.initialize(document.nodes);
            if (!result.accepted) { error = uiText(result.message); return false; }
        }
        if (!document.globals)
            document.globals = static_cast<const engine::ChainGlobalSettings&>(configuration);
        auto draftSettings = settings;
        const auto result = engine::PresetLibrary::applyToEditor(entry, document, draftSettings);
        if (!result.accepted) { error = uiText(result.message); return false; }
        auto runtime = configuration.chain;
        const auto projected = document.layout.project(document.nodes, runtime);
        if (!projected.accepted) {
            uint32_t end = document.layout.inputNext;
            while (end > engine::NodeGraphLayout::Output)
                end = document.layout.next[size_t(document.layout.indexOf(document.nodes, end))];
            if (end != engine::NodeGraphLayout::Input) { error = uiText(projected.message); return false; }
            runtime = configuration.chain;
        } else {
            document.globals->apply(settings);
            engine::fromChain(runtime, settings);
        }
        settings.audioSync = draftSettings.audioSync;
        settings.audioOffsetMs = draftSettings.audioOffsetMs;
        settings.content = draftSettings.content;
        configuration = engine::ChainConfiguration::capture(runtime, settings);
        configuration.editor = std::make_shared<engine::NodeEditorDocument>(std::move(document));
    }
    if (!session.valid() || !settings.validate().empty()) {
        error = QObject::tr("默认预设参数无效，已保留恢复会话");
        return false;
    }
    return true;
}
} // namespace
QmlPlayerBridge::QmlPlayerBridge(engine::EngineController& engine, std::filesystem::path dataDirectory,
                                QObject* parent)
    : QObject(parent), impl_(std::make_unique<Impl>(engine, std::move(dataDirectory))) {
    auto* checker = new UpdateChecker(version(), this);
    updates_ = checker;
    connect(checker, &UpdateChecker::notice, this, &QmlPlayerBridge::notice);
    impl_->playlist = new PlaylistModel(utf8Of((impl_->dataDir / L"playlist.v1.json").wstring()), this);
    connect(impl_->playlist, &PlaylistModel::playRequested, this, [this](const QString& path) {
        openPath(path);
        // Explicit playlist selection starts at the beginning; opening from
        // recent files retains the existing resume-position preference.
        impl_->resumeSession = 0;
    });
    connect(impl_->playlist, &PlaylistModel::error, this, [this](const QString& text) {
        veyra::log::warn("playlist", text.toStdString()); emit notice(text, true);
    });
    // The compute GPU decides which effects exist, so it is settled before
    // anything below samples the capabilities or restores the saved chain.
    impl_->loadPrefs();
    // The last volume (user 2026-11-08: it came back at 100% every launch).
    // Mute stays per session; profiles without the key keep 100%.
    if (impl_->prefs.contains(QStringLiteral("volume"))) {
        bool ok = false;
        const double saved = impl_->prefs.value(QStringLiteral("volume")).toDouble(&ok);
        if (ok && std::isfinite(saved) && saved >= 0.0 && saved <= 1.0) {
            impl_->engine.setVolume(float(saved), false);
            impl_->snapshot.volume = float(saved);
            veyra::log::info("ui-prefs", std::format("volume restored={:.3f}", saved));
        }
    }
    applyGameHotkeys();
    impl_->enumerateGpuAdapters();
    impl_->resolveGpuMonitor();
    impl_->applyComputeGpu();
    connect(this,&QmlPlayerBridge::snapshotChanged,this,
        [this,ceiling=fgMaxMultiplier(),text=fgProviderText()]() mutable {
            const auto nextCeiling=fgMaxMultiplier();const auto nextText=fgProviderText();
            if(nextCeiling==ceiling&&nextText==text)return;
            ceiling=nextCeiling;text=nextText;emit fgChoicesChanged();
        });
    connect(this,&QmlPlayerBridge::settingsChanged,this,&QmlPlayerBridge::fgChoicesChanged);
    auto* capabilityTimer=new QTimer(this);
    connect(capabilityTimer,&QTimer::timeout,this,[this,reported=currentEffectCapabilities(),priorityRevision=uint64_t(-1)]() mutable {
        if(gfx::gpuPriorityStatus().state==gfx::GpuPriorityState::Pending)gfx::applyRequestedGpuPriority();
        const auto priority=gfx::gpuPriorityStatus();
        if(priority.revision!=priorityRevision){priorityRevision=priority.revision;emit preferencesChanged();}
        const auto next=currentEffectCapabilities(true);
        // An action may have refreshed the cache before this tick. Compare with
        // the last reported map so that QML still learns about that change.
        if(next==reported)return;
        reported=next;emit effectCapabilitiesChanged();emit fgChoicesChanged();
    });
    capabilityTimer->start(2000);
    applyPreference(QStringLiteral("gpuPriority"));
    updateLiveCompanionCompat();
    applyPreference(QStringLiteral("prewarmEnhancement"));
    impl_->engine.setFullscreenMemoryProtection(impl_->prefBool("fullscreenMemoryProtection",false));
    connect(&impl_->exportQueue,&QmlExportQueueModel::changed,this,&QmlPlayerBridge::exportChanged);
    if (qApp) qApp->installEventFilter(this);
    // 游戏模式. The native filter also keeps windows shown during a monitor capture
    // out of that capture, so it is installed for every run.
    if (qApp) qApp->installNativeEventFilter(&impl_->game);
    impl_->displayChangeFilter.changed = &impl_->displayChanged;
    if (qApp) qApp->installNativeEventFilter(&impl_->displayChangeFilter);
    impl_->game.onEvent = [this](ui::GamePassthrough::Event event) {
        using E = ui::GamePassthrough::Event;
        switch (event) {
        case E::PanelHotkey: emit gameHotkey(0); break;
        case E::ExitHotkey:
        case E::ExitUnavailable:
            // Restore input before QML processes its fullscreen notification.
            impl_->gameRefused = true;
            impl_->gameInputShown = impl_->passthroughShown = false;
            emit gameModeChanged();
            impl_->game.apply(ui::GamePassthrough::Mode::Off);
            if (event == E::ExitHotkey) emit gameHotkey(1);
            else emit notice(tr("游戏模式键盘监听不可用，已退出鼠标穿透；请退出全屏后重试"), true);
            break;
        case E::HangReleased:
            // Publish Off before the foreground goes back to Veyra; stay out of
            // click-through until fullscreen is left.
            impl_->gameRefused = true;
            impl_->gameInputShown = impl_->passthroughShown = false;
            emit gameModeChanged();
            impl_->game.apply(ui::GamePassthrough::Mode::Off);
            emit gameModeStopped(tr("界面卡住超过 3 秒，已退出游戏模式")); break;
        // The game has the keyboard in game mode; the hook hands these over.
        case E::ScreenshotHotkey: takeScreenshot(); break;
        case E::RecordHotkey: toggleRecording(QStringLiteral("game-hotkey")); break;
        }
    };
    auto* gameBeat = new QTimer(this);
    connect(gameBeat, &QTimer::timeout, this, [this] {
        impl_->game.heartbeat();
        updateGameGeometry();
    });
    gameBeat->start(250);
#ifdef VEYRA_ENABLE_MOONLIGHT
    setupMoonlight();
#endif
#ifdef VEYRA_ENABLE_XBOX
    setupXbox();
#endif
    // Before anything opens: the renderer reads the output choice at start().
    applyPreference(QStringLiteral("language"));
    applyPreference(QStringLiteral("audioDevice"));
    applyPreference(QStringLiteral("audioForceStereo"));
    applyPreference(QStringLiteral("magewellLowLatency"));
    const bool presetsLoaded = impl_->facade.importLegacyStores();
    const auto presetNotice = uiText(impl_->facade.error());
    if (!presetNotice.isEmpty())
        QTimer::singleShot(0, this, [this, presetNotice, presetsLoaded] { emit notice(presetNotice, !presetsLoaded); });
    // Recent files, the last capture session and the shell preferences are saved
    // on every change; without this load they were written but never read back,
    // so every restart came up with an empty history. A damaged file is logged
    // and the defaults stand.
    if (!impl_->facade.loadPreferences())
        veyra::log::error("ui", "preferences not loaded: " + utf8Of(impl_->facade.error()).toStdString());
    auto restored = impl_->session;
    QString restoreError;
    if (!impl_->facade.loadChainSession(restored)) {
        restoreError = uiText(impl_->facade.error());
    } else {
        const auto migrationNotice = uiText(impl_->facade.error());
        if (!migrationNotice.isEmpty())
            QTimer::singleShot(0, this, [this, migrationNotice] { emit notice(migrationNotice, false); });
        const auto savedConfiguration = restored;
        QStringList disabled;
        for (auto& configuration : restored.configurations) {
            const auto reason = disableUnavailableEffects(configuration);
            if (!reason.isEmpty()) disabled << reason;
        }
        if (!disabled.isEmpty()) {
            const auto message = tr("已关闭当前硬件或包不支持的效果，参数仍保留：") + disabled.join("; ");
            veyra::log::warn("effect-availability", message.toStdString());
            QTimer::singleShot(0, this, [this, message] { emit notice(message, false); });
        }
        auto settings = impl_->facade.pendingSettings();
        const auto& configuration = restored.configurations[size_t(restored.active)];
        configuration.apply(settings);
        const auto previous = impl_->facade.pendingSettings();
        if (settings.validate().empty() && impl_->facade.applySettings(settings,engine::runtimeOrder(configuration.chain))) {
            impl_->session = restored;
            // Persist a repaired backend as well as the active mode. Otherwise
            // every restart reloads the unavailable DLSS value again.
            impl_->savedSession = savedConfiguration;
            impl_->restoreConfiguration(configuration);
            impl_->options = engine::PlayerOptions::from(settings,engine::runtimeOrder(impl_->activeRuntime()));
            impl_->revalidate();
        } else {
            impl_->facade.setPending(previous);
            restoreError = tr("模式会话未被引擎接受，保留初始状态");
        }
    }
    bool defaultFlowApplied = false;
    if (!restoreError.isEmpty()) {
        veyra::log::error("ui-session", restoreError.toStdString());
        QTimer::singleShot(0, this, [this, restoreError] { emit notice(restoreError, true); });
    } else if (presetsLoaded) {
        const auto defaultIndex = impl_->facade.presets().defaultIndex();
        if (defaultIndex) {
            const auto before = impl_->currentSession();
            auto candidate = before;
            auto settings = impl_->facade.pendingSettings();
            QString error;
            const auto& entry = impl_->facade.presets().entries()[*defaultIndex];
            if (!prepareStartupPreset(entry, candidate, settings, error)) {
                veyra::log::error("ui-preset", error.toStdString());
            } else {
                auto& configuration = candidate.configurations[size_t(candidate.active)];
                const auto disabled = disableUnavailableEffects(configuration);
                configuration.apply(settings);
                if (!disabled.isEmpty()) veyra::log::warn("effect-availability", "default preset disabled: " + disabled.toStdString());
            if (!impl_->facade.applySettings(settings,
                                                    engine::runtimeOrder(candidate.configurations[size_t(candidate.active)].chain))) {
                error = tr("引擎未接受默认预设，已保留恢复会话");
            } else {
                impl_->session = candidate;
                impl_->restoreConfiguration(candidate.configurations[size_t(candidate.active)]);
                impl_->revalidate();
                impl_->options = engine::PlayerOptions::from(settings, engine::runtimeOrder(impl_->activeRuntime()));
                defaultFlowApplied = entry.flow.has_value() &&
                    (entry.contents & engine::presetContentMask(engine::PresetContent::Flow));
                if (defaultFlowApplied) {
                    impl_->prefs[QStringLiteral("contentRate")] = int(settings.content);
                    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "default preset content cadence not saved");
                }
                if (candidate != before && !impl_->facade.saveChainSession(candidate))
                    error = tr("默认预设已应用，但会话保存失败：") + uiText(impl_->facade.error());
                else impl_->savedSession = candidate;
            }
            }
            if (!error.isEmpty())
                QTimer::singleShot(0, this, [this, error] { emit notice(error, true); });
        }
    }
    // These controls are global, outside either effect chain. Restore them
    // from shell preferences after the session; old profiles keep defaults.
    {
        auto s = impl_->facade.pendingSettings();
        const auto& p = impl_->prefs;
        bool changed = false;
        if (p.contains(QStringLiteral("audioSync"))) {
            const int m = p.value(QStringLiteral("audioSync")).toInt();
            if (m >= 0 && m <= 2 && int(s.audioSync) != m) { s.audioSync = static_cast<engine::AudioSyncMode>(m); changed = true; }
        }
        if (p.contains(QStringLiteral("audioOffsetMs"))) {
            const int ms = p.value(QStringLiteral("audioOffsetMs")).toInt();
            if (ms >= -250 && ms <= 250 && s.audioOffsetMs != ms) { s.audioOffsetMs = ms; changed = true; }
        }
        if (p.contains(QStringLiteral("edgeAegis"))) {
            const bool on = p.value(QStringLiteral("edgeAegis")).toBool();
            if (s.fgMotionRefine != on) { s.fgMotionRefine = on; changed = true; }
        }
        if (p.contains(QStringLiteral("dlssgPerformance"))) {
            const bool on = p.value(QStringLiteral("dlssgPerformance")).toBool();
            if (s.dlssgLite != on) { s.dlssgLite = on; changed = true; }
        }
        if (p.contains(QStringLiteral("vfgAutoRegulate"))) {
            const bool on = p.value(QStringLiteral("vfgAutoRegulate")).toBool();
            if (s.vfgAutoRegulate != on) { s.vfgAutoRegulate = on; changed = true; }
        }
        if (!defaultFlowApplied && p.contains(QStringLiteral("contentRate"))) {
            const int rate = p.value(QStringLiteral("contentRate")).toInt();
            if(rate>=0&&rate<=int(engine::ContentRate::Capture60To30)){
                const auto mode=engine::currentContentRate(static_cast<engine::ContentRate>(rate));
                if(s.content!=mode){s.content=mode;changed=true;}
                if(rate!=int(mode)){
                    impl_->prefs[QStringLiteral("contentRate")]=int(mode);
                    if(!impl_->savePrefs())veyra::log::warn("ui-prefs","duplicate-removal mode migration not saved");
                }
            }
        }
        auto restoreHold = [&](const char* key, float& target) {
            if (!p.contains(QLatin1String(key))) return;
            bool ok = false;
            const double value = p.value(QLatin1String(key)).toDouble(&ok);
            if (ok && std::isfinite(value) && value >= 0 && value <= 1 && target != float(value)) {
                target = float(value); changed = true;
            }
        };
        restoreHold("nrHoldStrength", s.nrHoldStrength);
        restoreHold("nrHoldTolerance", s.nrHoldTolerance);
        if (p.contains(QStringLiteral("nrReuse"))) {
            const int value = p.value(QStringLiteral("nrReuse")).toInt();
            const auto mode = static_cast<engine::NrReuseMode>(value);
            if (value >= 0 && engine::validNrReuseMode(mode) && s.nrReuse != mode) { s.nrReuse = mode; changed = true; }
        }
        for(const auto& pair : {std::pair{"captureFlipVertical",&s.captureFlipVertical},std::pair{"captureForceSdr",&s.forceSdrPreview}})
            if(p.contains(QLatin1String(pair.first))){const bool value=p.value(QLatin1String(pair.first)).toBool();changed|=*pair.second!=value;*pair.second=value;}
        if(p.contains("captureAudioIngress")){const int value=p.value("captureAudioIngress").toInt();if(value>=0&&value<=2){changed|=int(s.captureAudio)!=value;s.captureAudio=static_cast<engine::CaptureAudioIngress>(value);}}
        if(p.contains("captureBufferMode")){const int value=p.value("captureBufferMode").toInt();if(value>=0&&value<=2){changed|=int(s.captureBuffer)!=value;s.captureBuffer=static_cast<source::CaptureBufferMode>(value);}}
        if (changed && !impl_->commit(std::move(s))) veyra::log::warn("ui-prefs", "saved global controls not accepted");
    }
    // Presentation (低延迟队列 / 显示同步 / 输出上限) and the picture aspect.
    {
        const auto saved = impl_->prefs.value(QStringLiteral("presentation")).toMap();
        auto& pr = impl_->presentation;
        pr.hdrFgCompatibility = impl_->prefBool("hdrFgCompatibility", false);
        if (!saved.isEmpty()) {
            pr.enabled = saved.value(QStringLiteral("lowQueue")).toBool();
            // Before the three states the low queue was 开 on NVIDIA.
            pr.nvidiaLowLatency=engine::savedNvidiaLowLatency(saved.value(QStringLiteral("nvidiaLowLatency"),pr.enabled&&nvidiaLowLatencyAvailable()?1:0).toInt());
            engine::syncNvidiaLowLatencyQueue(pr,nvidiaLowLatencyAvailable());
            pr.display = static_cast<engine::DisplaySync>(std::clamp(saved.value(QStringLiteral("display")).toInt(), 0, 2));
            pr.outputRate = static_cast<engine::OutputRateMode>(std::clamp(saved.value(QStringLiteral("outputRate")).toInt(), 0, 2));
            pr.customFps = std::clamp(saved.value(QStringLiteral("customFps"), 60.0).toDouble(), 1.0, 1000.0);
            pr.variableRefresh = saved.value(QStringLiteral("vrr")).toBool();
        }
        if (pr.valid()) impl_->engine.requestPresentation(pr);
        impl_->aspectMode = std::clamp(impl_->prefs.value(QStringLiteral("aspect")).toInt(), 0, 6);
    }
    impl_->sessionSaveTimer = new QTimer(this);
    impl_->sessionSaveTimer->setSingleShot(true);
    impl_->sessionSaveTimer->setInterval(300);
    const auto scheduleSave = [this] { impl_->sessionSaveTimer->start(); };
    connect(this, &QmlPlayerBridge::chainChanged, this, scheduleSave);
    connect(this, &QmlPlayerBridge::settingsChanged, this, scheduleSave);
    connect(impl_->sessionSaveTimer, &QTimer::timeout, this, [this] {
        if (!impl_->persistSession()) {
            veyra::log::error("ui-session", utf8Of(impl_->facade.error()).toStdString());
            emit notice(tr("当前修改尚未保存：") + uiText(impl_->facade.error()), true);
        }
    });
    // 60 Hz ceiling, and it does nothing at all when the snapshot is unchanged.
    impl_->hdrMemorySaveTimer=new QTimer(this);
    impl_->hdrMemorySaveTimer->setSingleShot(true);impl_->hdrMemorySaveTimer->setInterval(400);
    connect(impl_->hdrMemorySaveTimer,&QTimer::timeout,this,[this]{flushHdrMemory();});
    impl_->volumeSaveTimer=new QTimer(this);
    impl_->volumeSaveTimer->setSingleShot(true);impl_->volumeSaveTimer->setInterval(500);
    connect(impl_->volumeSaveTimer,&QTimer::timeout,this,[this]{
        if(impl_->savePrefs())veyra::log::info("ui-prefs",std::format("volume={:.3f} saved",impl_->prefs.value(QStringLiteral("volume")).toDouble()));
        else veyra::log::warn("ui-prefs","volume not saved");
    });
    impl_->timer = new QTimer(this);
    connect(impl_->timer, &QTimer::timeout, this, [this] {
        const bool had = impl_->haveSnapshot;
        const uint64_t before = impl_->publishedRevision;
        const bool wasRunning = impl_->snapshot.running;
        const bool wasImage = impl_->snapshot.image;
        const bool wasFailed = impl_->snapshot.failed;
        const bool wasEngineHung = impl_->snapshot.status == engine::kEngineHangStatus;
        const bool wasFullscreenUnsafe = impl_->snapshot.vramFullscreenUnsafe;
        const auto wasTransport = impl_->snapshot.transport;
        const uint64_t wasSession = impl_->snapshot.sessionId;
        const auto oldSourceAspect=sourceAspect();
        if(impl_->poll()){emit settingsChanged();emitChainChanged();}
        if(wasSession!=impl_->snapshot.sessionId||oldSourceAspect!=sourceAspect())emit compareChanged();
        // Live recording state: the recorder owns the files, this side shows them.
        {
            const auto live=impl_->engine.recordingSnapshot();
            QVariantMap state;
            state["active"]=live.active;
            state["failed"]=live.failed;
            state["finishing"]=live.finishing;
            state["path"]=QString::fromStdWString(live.path);
            state["error"]=live.error.empty()?QString():uiText(live.error);
            state["note"]=live.note.empty()?QString():uiText(live.note);
            state["encoder"]=QString::fromStdWString(live.encoder);
            state["frames"]=QVariant::fromValue(qulonglong(live.frames));
            state["dropped"]=QVariant::fromValue(qulonglong(live.dropped));
            state["seconds"]=live.seconds;
            state["files"]=int(live.files);
            state["audio"]=live.audio;
            if(state!=impl_->recordingState){
                const auto previous=impl_->recordingState;
                impl_->recordingState=state;
                emit recordingChanged();
                // One notice per event: a failure, a finished file, a new file.
                const QString error=state.value("error").toString(),note=state.value("note").toString();
                if(live.failed&&!error.isEmpty()&&error!=previous.value("error").toString())emit notice(error,true);
                if(live.saved!=impl_->recordingSaved){
                    impl_->recordingSaved=live.saved;
                    emit notice(note.isEmpty()?tr("录制已保存：%1").arg(state.value("path").toString())
                                              :tr("录制已保存：%1（%2）").arg(state.value("path").toString(),note),false);
                }else if(live.active&&!note.isEmpty()&&note!=previous.value("note").toString())emit notice(note,false);
            }
        }
  // Custom: a display change re-applies that device's HDR curve. The id check is
  // a few Win32 calls, so this is safe on the polling path.
  syncHdrDisplayProfile();
  syncHdrTitleProfile();
        if (impl_->snapshot.vramRunawayMiB && (impl_->vramNoticeSession != impl_->snapshot.sessionId ||
            (!wasFullscreenUnsafe && impl_->snapshot.vramFullscreenUnsafe))) {
            impl_->vramNoticeSession = impl_->snapshot.sessionId;
            if (impl_->snapshot.vramFullscreenUnsafe)
                emit notice(tr("全屏期间显存持续增长了 %1 GB，已退出全屏保护播放；当前会话使用窗口播放。增强设置保留。"
                               "原因尚未确认，可开启设置中的 OBS 游戏采集兼容、重启后测试全屏。")
                                .arg(impl_->snapshot.vramRunawayMiB / 1024.0, 0, 'f', 1), true);
            else emit notice(tr("显存在设置未变时增加了 %1 GB，正在尝试回收；回收结果见诊断日志。原因尚未确认。")
                                 .arg(impl_->snapshot.vramRunawayMiB / 1024.0, 0, 'f', 1), true);
        }
        tickSubtitles();
        tickImageBatch();
        tickCapture();
        tickPs5();
        tickMoonlight();
        tickXbox();
        // Geometry uses the source DAR and the same transform as mouse/compare.
        if (impl_->snapshot.running && impl_->videoWindow) {
            const bool fresh = impl_->aspectSession != impl_->snapshot.sessionId;
            impl_->aspectSession = impl_->snapshot.sessionId;
            applyAspect(fresh);
        }
        {
            const auto& s = impl_->snapshot;
            if (impl_->resumeSession && s.sessionId == impl_->resumeSession && s.running && s.duration > 0) {
                const double at = impl_->resumeAt;
                impl_->resumeSession = 0;
                if (at >= 0.5 && at < s.duration - 0.5) {
                    seekTo(at);
                    veyra::log::info("ui-resume", std::format("resumed at {:.1f}s", at));
                    emit notice(tr("已从上次位置 %1 继续").arg(formatTime(at)), false);
                }
            }
            if (++impl_->positionTicks >= 120) {
                impl_->positionTicks = 0; rememberPosition(false);
                if(s.running&&!s.capture&&!s.image&&s.sourceWidth>=64&&s.sourceHeight>=64&&s.sourceWidth<=3840&&s.sourceHeight<=2160){
                    const QVariantMap extent{{"width",s.sourceWidth},{"height",s.sourceHeight}};
                    if(impl_->prefs.value(QStringLiteral("lastSourceExtent")).toMap()!=extent){
                        impl_->prefs[QStringLiteral("lastSourceExtent")]=extent;
                        if(!impl_->savePrefs())veyra::log::warn("ui-prefs","last source extent not saved");
                    }
                }
            }
        }
        // Status light: 1.4.4 sampled its dashboard history every 250 ms.
        if (++impl_->historyTicks >= 16) {
            impl_->historyTicks = 0;
            updateRunStatus();
        }
        // The renderer reopens on its own thread (device change, fallback); pick
        // up what it opened about twice a second.
        if (++impl_->audioPollTicks >= 30) {
            impl_->audioPollTicks = 0;
            const auto active = sink::activeRenderEndpoint();
            if (active.valid != impl_->audioActive.valid || active.id != impl_->audioActive.id ||
                active.fallback != impl_->audioActive.fallback || active.channels != impl_->audioActive.channels ||
                active.downmix != impl_->audioActive.downmix) {
                const bool fellBack = active.fallback && !impl_->audioActive.fallback;
                impl_->audioActive = active;
                emit audioDevicesChanged();
                if (fellBack) emit notice(tr("所选音频设备不可用，已切到系统默认设备"), true);
            }
        }
        if (!had || impl_->publishedRevision != before) {
            const auto& s = impl_->snapshot;
            if (!had || wasSession != s.sessionId || wasTransport != s.transport ||
                wasRunning != s.running || wasImage != s.image || wasFailed != s.failed)
                veyra::log::info("qml-window", std::format(
                    "source-state session={} transport={} running={} image={} failed={} opening={} revision={}",
                    s.sessionId, int(s.transport), s.running, s.image, s.failed,
                    impl_->openingSource, impl_->publishedRevision));
            impl_->power.update(s.running && !s.failed && !s.image && s.transport == engine::TransportState::Playing);
            if (had && !wasFailed && s.failed && !s.capture && !s.remotePlay &&
                impl_->prefString("decode") == QLatin1String("hardware"))
                emit notice(tr("已设为“强制硬解”，这个文件无法用硬件解码打开；可在 设置 → 播放 改回“自动”"), true);
            // A live source that fails says why, and the user is taken back to the
            // start page: the reason used to land only in the (closed) PS5 panel,
            // leaving a black cinema strip that looked like a minimised window.
            if (had && !wasFailed && s.failed && (s.capture || s.remotePlay)) {
                const QString why = veyra::ui::i18n::text(s.status);
                veyra::log::warn("ui-live-failed", std::format("remote={} status={}", s.remotePlay, utf8Of(s.status).toStdString()));
                QString text = why.isEmpty() ? (s.remotePlay ? tr("PS5 串流没有连上") : tr("采集没有开始")) : why;
                // The interface itself may be gone with the device (Qt loses it too), so
                // the hint is also left for the next start.
                const QString injected = utf8Of(gfx::riskyInjections());
                if (const QString advice = injectionAdvice(injected); !advice.isEmpty()) {
                    text += QStringLiteral("；") + advice;
                    QFile marker(lastFailurePath());
                    if (marker.open(QIODevice::WriteOnly)) marker.write((QStringLiteral("device\n") + injected + QStringLiteral("\n")).toUtf8());
                }
                const auto now = std::chrono::steady_clock::now();
                const bool reconnect = s.gpuFailed && !s.remotePlay && impl_->liveKind == QLatin1String("capture") &&
                    (impl_->gpuReconnectLast == std::chrono::steady_clock::time_point{} || now - impl_->gpuReconnectLast > std::chrono::minutes(10));
                if (s.gpuFailed) veyra::log::warn("gpu-reconnect", std::format("session={} kind={} automatic={}", s.sessionId, impl_->liveKind.toStdString(), reconnect));
                if (reconnect) { impl_->gpuReconnectSession = s.sessionId; impl_->gpuReconnectDue = {}; text += QStringLiteral("；") + tr("将自动重新连接采集卡一次"); }
                emit notice(text, true);
                if (!reconnect && impl_->currentPage == QLatin1String("min")) emit navigate(QStringLiteral("home"));
            }
            // Engine watchdog (field logs(15)): a frozen picture says why, once.
            if (had && !wasEngineHung && s.status == engine::kEngineHangStatus) emit notice(uiText(s.status), true);
            // Network sources (#48): a recovered connection and now-and-then loss summaries.
            if (s.sessionId != impl_->netNoticeSession) {
                impl_->netNoticeSession = s.sessionId;
                impl_->netLossReported = 0;
                impl_->netReconnectsReported = 0;
                impl_->netLossNoticeAt = {};
            }
            if (s.network.active) {
                const auto now = std::chrono::steady_clock::now();
                if (s.network.reconnects > impl_->netReconnectsReported) {
                    impl_->netReconnectsReported = s.network.reconnects;
                    emit notice(tr("网络已重新连接，继续播放"), false);
                }
                const uint64_t lost = std::max(s.network.corruptPackets, s.network.corruptFrames);
                if (lost > impl_->netLossReported &&
                    (impl_->netLossNoticeAt == std::chrono::steady_clock::time_point{} ||
                     now - impl_->netLossNoticeAt >= std::chrono::seconds(20))) {
                    emit notice(tr("检测到网络丢包，已自动跳过 %1 处").arg(qulonglong(lost - impl_->netLossReported)), false);
                    impl_->netLossReported = lost;
                    impl_->netLossNoticeAt = now;
                }
            }
            emit snapshotChanged();
        }
        // The reconnect waits for the failed session's teardown (the GPU reset can hold it for
        // seconds) and is dropped if anything else was opened meanwhile.
        if (impl_->gpuReconnectSession) {
            const auto& s = impl_->snapshot;
            const auto now = std::chrono::steady_clock::now();
            if (s.sessionId != impl_->gpuReconnectSession || impl_->openingSource) {
                veyra::log::info("gpu-reconnect", "cancelled: another source was opened");
                impl_->gpuReconnectSession = 0;
            } else if (!s.running) {
                if (impl_->gpuReconnectDue == std::chrono::steady_clock::time_point{}) impl_->gpuReconnectDue = now + std::chrono::seconds(3);
                else if (now >= impl_->gpuReconnectDue) {
                    impl_->gpuReconnectSession = 0; impl_->gpuReconnectLast = now;
                    veyra::log::warn("gpu-reconnect", "reconnecting the capture card after the GPU failure");
                    emit notice(tr("GPU 已重置，正在自动重新连接采集卡"), false);
                    resumeCaptureSession();
                }
            }
        }
        // An export in flight needs its own poll; it is a separate job and its
        // snapshot is not part of the player snapshot.
        if (impl_->exportQueue.queue().needsTick()) pollExport();
        tickPlaylist();
        updateGameMode();
    });
    impl_->timer->start(16);
    startGpuSampler();
    // Overlays that hook presentation get injected into this process when a D3D device
    // appears (Qt's at start, the engine's at open); see gfx/PresentationHooks.h for what
    // they broke in the field. Log each once (every 2 s for two minutes, then every 10 s).
    auto* hookTimer = new QTimer(this);
    connect(hookTimer, &QTimer::timeout, this, [this, hookTimer, started = GetTickCount64()] {
        struct Seen { const wchar_t* module; const char* product; };
        static const Seen others[] = {
            {L"graphics-hook64.dll", "OBS game capture"}, {L"DiscordHook64.dll", "Discord overlay"},
            {L"gameoverlayrenderer64.dll", "Steam overlay"}, {L"nvspcap64.dll", "NVIDIA overlay / instant replay"},
            {L"ow-graphics-hook64.dll", "Overwolf overlay"},
        };
        static bool loggedOthers[std::size(others)]{};
        for (size_t k = 0; k < std::size(others); ++k)
            if (!loggedOthers[k] && GetModuleHandleW(others[k].module)) {
                loggedOthers[k] = true;
                veyra::log::info("overlay-hooks", std::format("{} is injected into this process", others[k].product));
            }
        // RivaTuner / GamePP are only logged. The XeSS guard that switched frame generation
        // away from XeSS while they were injected was removed on request (2026-10-02): the
        // interface now draws with Direct3D 12 like 1.4.4's process, which is what they
        // coexisted with.
        static bool handled = false;
        if (const auto* hook = gfx::injectedPresentationHook(); hook && !handled) {
            handled = true;
            veyra::log::warn("overlay-hooks", std::format("{} is injected into this process ({})",
                hook->product, QString::fromWCharArray(hook->module).toStdString()));
            if (GetModuleHandleW(L"RTSSHooks64.dll")) {
                wchar_t path[32768]{};
                GetModuleFileNameW(GetModuleHandleW(L"RTSSHooks64.dll"), path, DWORD(std::size(path)));
                veyra::log::info("overlay-hooks", std::format("RTSS hook module: activeServer={} path={}",
                    gfx::rivaTunerRunning(), QString::fromWCharArray(path).toStdString()));
            }
        }
        // RivaTuner arrived after a start with the interface on the GPU (it was not running
        // then, see main.cpp). Its OSD stays out of this process (RTSSHooksProfileOverride),
        // so nothing breaks; a restart moves the interface to software drawing and puts the
        // OSD on the video.
        static bool rivaTunerLate = false;
        if (!rivaTunerLate && !qApp->property("veyraSoftwareUi").toBool() && !qApp->property("veyraUiRendererForced").toBool()
                && GetModuleHandleW(L"RTSSHooks64.dll") && gfx::rivaTunerRunning()) {
            rivaTunerLate = true;
            const bool automatic = impl_->prefString("overlayCompat") != QLatin1String("off");
            veyra::log::warn("overlay-hooks", std::format("RivaTuner injected into a run with a GPU interface; its OSD is off in this process ({})",
                automatic ? "active OSD server: compatibility restart suggested" : "monitoring compatibility off"));
            if (automatic) emit overlayRestartSuggested(QStringLiteral("rtss"));
        }
        // NVIDIA's in-game overlay switched on after a start with the interface on the GPU
        // (main.cpp checks at start). Unlike RTSS it cannot be kept out of this process,
        // and with both swapchains hooked the video and interface threads deadlocked in
        // the field (gfx/PresentationHooks.h); a restart draws the interface in software.
        static bool nvidiaOverlayLate = false;
        if (!nvidiaOverlayLate && !qApp->property("veyraSoftwareUi").toBool() && !qApp->property("veyraUiRendererForced").toBool()
                && !GetEnvironmentVariableW(L"VEYRA_TEST_IGNORE_NVIDIA_OVERLAY", nullptr, 0) && gfx::nvidiaOverlayRunning()) {
            nvidiaOverlayLate = true;
            const bool automatic = impl_->prefString("overlayCompat") != QLatin1String("off");
            veyra::log::warn("overlay-hooks", std::format("NVIDIA overlay is on in a run with a GPU interface ({})",
                automatic ? "compatibility restart suggested" : "monitoring compatibility off"));
            if (automatic) emit overlayRestartSuggested(QStringLiteral("nvidia"));
        }
        // GamePP (游戏加加) started after Veyra or injected into it: same as NVIDIA's overlay.
        static bool gamePPLate = false;
        if (!gamePPLate && !qApp->property("veyraSoftwareUi").toBool() && !qApp->property("veyraUiRendererForced").toBool()
                && !GetEnvironmentVariableW(L"VEYRA_TEST_IGNORE_GAMEPP", nullptr, 0) && (gfx::gamePPInjected() || gfx::gamePPRunning())) {
            gamePPLate = true;
            const bool automatic = impl_->prefString("overlayCompat") != QLatin1String("off");
            veyra::log::warn("overlay-hooks", std::format("GamePP is running or injected in a run with a GPU interface ({})",
                automatic ? "compatibility restart suggested" : "monitoring compatibility off"));
            if (automatic) emit overlayRestartSuggested(QStringLiteral("gamepp"));
        }
        updateLiveCompanionCompat();
        // RivaTuner's default hooking keeps OBS game capture from hooking any Direct3D 12
        // program (2026-10-03, RTSS 7.3.5.28314 + OBS 32.1.2, a bare D3D12 window included;
        // RTSS version corrected against the executable on 2026-10-04); its
        // "Use Microsoft Detours API hooking" option lets both work. Said once, ever.
        static bool obsWithRivaTuner = false;
        if (!obsWithRivaTuner && GetModuleHandleW(L"graphics-hook64.dll") && GetModuleHandleW(L"RTSSHooks64.dll")) {
            obsWithRivaTuner = true;
            veyra::log::warn("overlay-hooks", "OBS game capture and RivaTuner are both injected: OBS may not get the picture unless RTSS uses Detours hooking for Veyra");
            if (!impl_->prefs.value(QStringLiteral("obsRivaTunerHintShown")).toBool()) {
                impl_->prefs[QStringLiteral("obsRivaTunerHintShown")] = true;
                (void)impl_->savePrefs();
                emit notice(tr("检测到 OBS 游戏采集和小飞机（RTSS）同时注入 Veyra。两者默认的挂钩方式冲突，OBS 游戏采集可能抓不到画面（任何 D3D12 程序都一样）。"
                               "在 RTSS 里添加 veyra_qml_ui.exe，并在它的设置中勾选“Use Microsoft Detours API hooking”；或在 OBS 改用窗口采集（Windows 10 1903+）"), false);
            }
        }
        // Overlays inject when they decide the window is a game, e.g. on entering fullscreen
        // long after start; after the first two minutes look every 10 s instead of every 2 s.
        if (GetTickCount64() - started > 120000 && hookTimer->interval() < 10000) hookTimer->setInterval(10000);
    });
    hookTimer->start(2000);
    // The previous run crashed or lost the GPU with a known-risky component injected:
    // say so once the window is up (the crash handler cannot show anything itself).
    QTimer::singleShot(2500, this, [this] {
        QFile marker(lastFailurePath());
        if (!marker.exists()) return;
        QByteArray raw;
        if (marker.open(QIODevice::ReadOnly)) raw = marker.readAll();
        marker.close();
        marker.remove();
        // The crash handler writes UTF-16 (no allocation in the filter); the bridge UTF-8.
        const QString text = raw.size() > 1 && raw.at(1) == 0
            ? QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData()), raw.size() / 2) : QString::fromUtf8(raw);
        const QStringList lines = text.split(QLatin1Char('\n'));
        const QString kind = lines.value(0), modules = lines.value(1);
        QString advice = injectionAdvice(kind + QLatin1Char(',') + modules);
        veyra::log::info("overlay-hooks", std::format("previous run: {} injected={}", kind.toStdString(), modules.toStdString()));
        if (advice.isEmpty()) return;
        const QString head = kind.startsWith(QStringLiteral("crash"))
            ? tr("上次 Veyra 异常退出（崩溃在 %1）").arg(kind.mid(6).trimmed())
            : tr("上次采集时显卡设备丢失");
        emit notice(head + QStringLiteral("；") + advice, true);
    });
    // Unsupported saved FG choices were repaired together with both modes
    // above, using actual provider availability rather than a brand alone.
}

QmlPlayerBridge::~QmlPlayerBridge() {
    flushHdrMemory();
    if (qApp) qApp->removeEventFilter(this);
    if (qApp) qApp->removeNativeEventFilter(&impl_->displayChangeFilter);
    // rememberPosition(true) writes every preference, a pending volume included.
    if (impl_->volumeSaveTimer) impl_->volumeSaveTimer->stop();
    rememberPosition(true);
    if (impl_->sessionSaveTimer) impl_->sessionSaveTimer->stop();
    if (!impl_->persistSession())
        veyra::log::error("ui-session", utf8Of(impl_->facade.error()).toStdString());
}



} // namespace veyra::ui
