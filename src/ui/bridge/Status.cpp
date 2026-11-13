// QML bridge: Status responsibilities.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
// --- professional-page readouts ---------------------------------------------
// These read fields the engine genuinely publishes. Where it publishes nothing,
// the getter says so and the UI shows an explicit "unmeasured" rather than a
// zero or a stand-in.
QString QmlPlayerBridge::outputSummary() const {
    const auto& s = impl_->snapshot;
    const auto& plan = s.metrics.resolution;
    if (!s.running || !plan.output.valid() || plan.settingsRevision != s.applied.revision) return {};
    return QStringLiteral("%1x%2").arg(plan.output.width).arg(plan.output.height);
}
// Staged SR as the running plan has it. Double SR: source -> first result ->
// reduction -> second result; supersampling: source -> SR result -> shrunk
// output. Empty for single-step SR.
QString QmlPlayerBridge::srStageSummary() const {
    const auto& s = impl_->snapshot;
    const auto& plan = s.metrics.resolution;
    if (!s.running || !plan.output.valid() || plan.settingsRevision != s.applied.revision || !plan.srApplied) return {};
    if (!plan.dualSrApplied && plan.base == plan.srOutput) return {};
    const auto size = [](pipeline::Extent e) { return QStringLiteral("%1x%2").arg(e.width).arg(e.height); };
    QStringList parts{size(plan.source)};
    if (plan.dualSrApplied) parts << size(plan.firstSrOutput) << size(plan.srInput);
    parts << size(plan.srOutput);
    if (plan.base != plan.srOutput) parts << size(plan.base);
    return parts.join(QStringLiteral(" → "));
}
QString QmlPlayerBridge::srStageNote() const {
    const auto& s = impl_->snapshot;
    const auto& plan = s.metrics.resolution;
    if (!s.running || !plan.output.valid() || plan.settingsRevision != s.applied.revision) return {};
    QStringList notes;
    if (plan.srPrescaleIneffective) notes << tr("第二次超分未执行：再次降低后的尺寸须小于基础超分结果及第二次目标。");
    if (plan.srOutputIneffective) notes << tr("超分后缩回未生效：超分须先放大画面，且缩回尺寸要小于超分结果。");
    // Only the standard family restores a source already at the target size.
    if (s.applied.sr && engine::vfxVideoSr(s.applied.videoSrQuality) && !plan.srApplied)
        notes << tr("高码率和流媒体只做放大：片源已达到目标尺寸，所以没有运行；同尺寸修复请用标准。");
    return notes.join(QStringLiteral("；"));
}

double QmlPlayerBridge::queuedFrames() const {
    // Valid output opportunities held by presenter jobs, not cumulative skips
    // and not the OS/display queue. Zero is meaningful only with a live window.
    return queuedFramesKnown() ? double(impl_->snapshot.metrics.flow.pendingOutputFrames) : 0.0;
}
bool QmlPlayerBridge::queuedFramesKnown() const {
    const auto& s = impl_->snapshot;
    const auto& flow = s.metrics.flow;
    return s.running && !s.image && s.sessionId != 0 &&
        flow.latest.sessionId == s.sessionId && flow.lastReady100ns > 0 &&
        flow.latest.frame.settingsRevision == s.applied.revision;
}

int QmlPlayerBridge::skippedFrames() const { return int(impl_->snapshot.previewSkipped); }

QString QmlPlayerBridge::flowBackend() const {
    // The snapshot carries a performance figure and a content rate, not a backend
    // name; report the fact we do have and let it read as unmeasured otherwise.
    const auto& s = impl_->snapshot;
    if (s.flowPerf == 0) return {};
    return QStringLiteral("GPU 光流");
}

int QmlPlayerBridge::opticalFlowChoice() const { return int(settings().opticalFlowBackend); }
bool QmlPlayerBridge::setOpticalFlowChoice(int backend) {
    if(backend<0||backend>2)return false;
    const auto cap = effectCapabilities().value("flow" + QString::number(backend)).toMap();
    if (!cap.value("available").toBool()) { emit notice(cap.value("reason").toString(), true); return false; }
    const auto before=settings();auto next=before;
    next.opticalFlowBackend=engine::OpticalFlowBackend(backend);
    if(next.opticalFlowBackend==before.opticalFlowBackend)return true;
    if(!impl_->commit(next)){
        emit notice(tr("引擎未接受光流配置，原设置保留"),true);return false;
    }
    emit settingsChanged();return true;
}

QVariantList QmlPlayerBridge::stageTimings() const {
    // Consume the existing GPU timestamp aggregates. Repeated NR layers share
    // a timestamp slot: this is stage sampling, NOT per-node attribution.
    // XeSS-internal FG is not measured by the graph's DLSS FG batch query.
    const auto& s = impl_->snapshot;
    QVariantList out;
    using Stage = diagnostics::GpuStage;
    struct Row { const char* label; Stage stage; const char* color; };
    const Row rows[] = {
        {"输入", Stage::Color, "#A8A8B8"},
        {"抗锯齿", Stage::AaNode0, "#7FD4FF"},
        {"抗锯齿 2", Stage::AaNode1, "#A9E2FF"},
        {"人脸识别", Stage::BeautyDetect, "#FFB3D1"},
        {"美颜", Stage::BeautyNode0, "#FF8FBF"},
        {"美颜 2", Stage::BeautyNode1, "#FF9DC7"},
        {"美颜 3", Stage::BeautyNode2, "#FFABCF"},
        {"美颜 4", Stage::BeautyNode3, "#FFB9D7"},
        {"美颜 5", Stage::BeautyNode4, "#FFC7DF"},
        {"美颜 6", Stage::BeautyNode5, "#FFD5E7"},
        {"超分", Stage::Sr, "#6EA8FF"},
        {"光流", Stage::Flow, "#9A85FF"},
        {"NR采样", Stage::Nr, "#FF8A3D"},
        {"残差", Stage::Residual, "#E7B868"},
        {"FG", Stage::FgBatch, "#3DDC84"},
        {"HDR", Stage::VideoHdr, "#E58BD9"},
        {"呈现", Stage::Blit, "#58CAD4"},
    };
    const double budget = stageBudgetMs();
    // Several NR layers: every layer marks the shared NR slot, so it only ever held the last
    // layer's time (field report 2026-10-01). Each layer also has its own slot; with two or
    // more layers measured, show those instead of the shared row.
    std::vector<Row> shown;
    unsigned measuredLayers = 0;
    for (unsigned i = 0; i < diagnostics::kTimedNrLayers; ++i)
        if (s.metrics.flow.gpuTiming[size_t(diagnostics::nrLayerStage(i))].samples > 0) ++measuredLayers;
    static const char* const layerLabels[] = {"NR 第1层", "NR 第2层", "NR 第3层", "NR 第4层"};
    static const char* const layerColors[] = {"#FF8A3D", "#FFA866", "#FFC28F", "#FFD9B8"};
    for (const auto& r : rows) {
        // Anti-aliasing and beauty rows appear only while those nodes run.
        const bool optional = r.stage == Stage::AaNode0 || r.stage == Stage::AaNode1 ||
            (r.stage >= Stage::BeautyNode0 && r.stage <= Stage::BeautyDetect);
        if (optional && s.metrics.flow.gpuTiming[size_t(r.stage)].samples == 0) continue;
        if (r.stage == Stage::Nr && measuredLayers >= 2) {
            for (unsigned i = 0; i < diagnostics::kTimedNrLayers; ++i)
                if (s.metrics.flow.gpuTiming[size_t(diagnostics::nrLayerStage(i))].samples > 0)
                    shown.push_back({layerLabels[i], diagnostics::nrLayerStage(i), layerColors[i]});
        } else shown.push_back(r);
    }
    for (const auto& r : shown) {
        const auto& sample = s.metrics.flow.gpuTiming[size_t(r.stage)];
        // The mean of the last second, as 1.4.4's dashboard showed; P95 rides along for the
        // tooltip. Showing P95 alone read as "slower than 1.4.4" for the same work.
        const bool measured = queuedFramesKnown() && sample.samples > 0 &&
            sample.mean && std::isfinite(*sample.mean) && *sample.mean >= 0.0;
        const double ms = measured ? *sample.mean : 0.0;
        QVariantMap item;
        item["label"] = uiText(std::string_view(r.label));
        item["ms"] = ms;
        item["p95"] = measured && sample.p95 ? *sample.p95 : 0.0;
        item["measured"] = measured;
        item["samples"] = measured ? QVariant::fromValue<qulonglong>(sample.samples) : QVariant::fromValue<qulonglong>(0);
        item["domain"] = QStringLiteral("GPU timestamp P95");
        item["color"] = QString::fromUtf8(r.color);
        item["fraction"] = (measured && budget > 0.01) ? ms / budget : 0.0;
        out << item;
    }
    return out;
}

QVariantMap QmlPlayerBridge::nodeTimings() const {
    // Per-node GPU timestamp P95, keyed by the card id chain() publishes. An
    // editor node borrows a number only when the wired path it sits on is the
    // chain actually running; drafts, detached and disabled nodes never do.
    using Stage = diagnostics::GpuStage;
    const auto& c = impl_->chain;
    const auto& flow = impl_->snapshot.metrics.flow;
    const bool node = c.mode == engine::ChainMode::Node;
    std::vector<int> path;
    bool draft = false;
    if (!node) {
        for (uint32_t i = 0; i < c.nodeCount; ++i) path.push_back(int(i));
    } else {
        uint32_t id = impl_->layout.inputNext;
        while (id > engine::NodeGraphLayout::Output && path.size() <= c.nodeCount) {
            const int index = impl_->layout.indexOf(c, id);
            if (index < 0) break;
            path.push_back(index);
            id = impl_->layout.next[size_t(index)];
        }
        engine::EffectChain projected;
        draft = id != engine::NodeGraphLayout::Output || !impl_->layout.project(c, projected).accepted ||
            engine::runtimeOrder(projected) != engine::runtimeOrder(impl_->acceptedChain);
        if (draft) path.clear();
    }
    const bool live = queuedFramesKnown();
    const auto& snapshot = impl_->snapshot;
    // A failed optical-flow/NR backend can leave the requested editor layer
    // enabled while the engine has deliberately resumed playback without NR.
    // Show that confirmed recovery, not an endless timing warmup or stale ms.
    const bool nrRejected = live && !snapshot.applying && snapshot.rejectedRevision != 0 &&
        snapshot.desired.revision == snapshot.applied.revision &&
        !snapshot.desired.nr && !snapshot.applied.nr && !snapshot.backendWarning.empty();
    const bool xess = fgBackendName() == QLatin1String("xess");
    QVariantMap out;
    auto publish = [&](uint32_t i, const QString& state, std::optional<Stage> stage) {
        QVariantMap item{{"state", state}, {"ms", 0.0}, {"samples", QVariant::fromValue<qulonglong>(0)}};
        if (state == QLatin1String("failed")) item["reason"] = uiText(snapshot.backendWarning);
        if (stage) {
            const auto& sample = flow.gpuTiming[size_t(*stage)];
            if (live && sample.samples > 0 && sample.mean && std::isfinite(*sample.mean) && *sample.mean >= 0.0) {
                item["state"] = QStringLiteral("measured");
                item["ms"] = *sample.mean;
                item["samples"] = QVariant::fromValue<qulonglong>(sample.samples);
            }
        }
        const QVariant key = node ? QVariant(impl_->layout.ids[i]) : QVariant(i + 2);
        out[key.toString()] = item;
    };
    std::vector<bool> onPath(c.nodeCount, false);
    for (const int i : path) onPath[size_t(i)] = true;
    for (uint32_t i = 0; i < c.nodeCount; ++i)
        if (!onPath[i]) publish(i, draft ? QStringLiteral("draft") : QStringLiteral("idle"), std::nullopt);
    unsigned enabledNr = 0, colour = 0, antiAlias = 0, beauty = 0;
    for (const int index : path) {
        const auto i = uint32_t(index);
        const auto& n = c.nodes[i];
        std::optional<Stage> stage;
        QString waiting = live ? QStringLiteral("pending") : QStringLiteral("stopped");
        switch (n.type) {
        case engine::EffectType::NrEnhance:
            if (nrRejected) waiting = QStringLiteral("failed");
            else if (n.enabled && enabledNr < diagnostics::kTimedNrLayers) stage = diagnostics::nrLayerStage(enabledNr);
            if (n.enabled) ++enabledNr;
            break;
        case engine::EffectType::Color:
            // Parameter 0 before any SR/NR is fused into the input conversion
            // shader: it has no separable GPU span of its own.
            if (colour < diagnostics::kTimedColorNodes) stage = diagnostics::colorNodeStage(colour);
            if (colour == 0) waiting = live ? QStringLiteral("fused") : waiting;
            ++colour;
            break;
        case engine::EffectType::AntiAliasing:
            // Indexed by parameter ordinal, like Color: every anti-aliasing node on the path counts.
            if (antiAlias < diagnostics::kTimedAaNodes) stage = diagnostics::aaNodeStage(antiAlias);
            ++antiAlias;
            break;
        case engine::EffectType::Beauty:
            if (beauty < diagnostics::kTimedBeautyNodes) stage = diagnostics::beautyNodeStage(beauty);
            ++beauty;
            break;
        case engine::EffectType::SuperResolution: stage = Stage::Sr; break;
        case engine::EffectType::VideoHdr:
            stage = Stage::VideoHdr;
            // SDR display path: the node is wired but TrueHDR is not built.
            if (live && !impl_->snapshot.videoHdrActive) waiting = QStringLiteral("hdr-sdr");
            break;
        case engine::EffectType::FrameGeneration:
            if (xess) waiting = QStringLiteral("sdk"); else stage = Stage::FgBatch;
            // Realtime admission declined the pairs inside this window.
            if (!xess && live && flow.counters.fgSkippedBeforeEval > 0 && flow.counters.fgEvaluated < flow.counters.fgCandidate)
                waiting = QStringLiteral("fg-skipped");
            break;
        case engine::EffectType::NrStabilizer:
        case engine::EffectType::Exclusion:
        default: waiting = QStringLiteral("untimed"); break;
        }
        if (!n.enabled) { publish(i, QStringLiteral("disabled"), std::nullopt); continue; }
        publish(i, waiting, stage);
    }
    return out;
}

double QmlPlayerBridge::stageBudgetMs() const {
    // Budget per content frame entering enhancement. Intentional dedup and
    // playback speed change that interval; a presentation cap alone does not.
    // Reuse the bounded status sample, without sampling or repainting faster.
    const auto& s=impl_->snapshot;const auto& h=impl_->history;
    const double fps=h.session==s.sessionId&&h.revision==s.applied.revision&&h.output.contentFps>0
        ?h.output.contentFps:s.nominalSourceFps*(s.captureHalfRate?.5:1.0);
    return fps > 0.01 ? 1000.0 / fps : 0.0;
}

double QmlPlayerBridge::scheduleP95Ms() const { return impl_->snapshot.schedulingWaitP95Ms; }

double QmlPlayerBridge::chainTotalMs() const {
    // Engine merges same-frame measured intervals before aggregation. Never
    // sum unrelated percentiles. This excludes uninstrumented stages/nodes.
    // Mean of the last second, the same statistic as the stage rows (see stageTimings).
    return chainTotalMsKnown() ? *impl_->snapshot.metrics.flow.enhancementProcessing.mean : 0.0;
}
bool QmlPlayerBridge::chainTotalMsKnown() const {
    const auto& sample = impl_->snapshot.metrics.flow.enhancementProcessing;
    return queuedFramesKnown() && sample.samples > 0 && sample.mean &&
        std::isfinite(*sample.mean) && *sample.mean >= 0.0;
}


// --- performance orbs and status light (#13) --------------------------------
double QmlPlayerBridge::gpuUtilization() const { return std::max(0.0, impl_->gpuUtil.load()); }
bool QmlPlayerBridge::gpuUtilizationKnown() const { return impl_->gpuUtil.load() >= 0.0; }
QString QmlPlayerBridge::runStatus() const { return impl_->runStatus; }
QString QmlPlayerBridge::runStatusLevel() const { return impl_->runLevel; }
QString QmlPlayerBridge::runStatusDetail() const { return impl_->runDetail; }
QStringList QmlPlayerBridge::runOptions() const { return impl_->runOptions; }
double QmlPlayerBridge::effectiveOutputFps() const { return impl_->effectiveOutputFps; }
double QmlPlayerBridge::outputRateRatio() const { return impl_->rateRatio; }

void QmlPlayerBridge::startGpuSampler() {
    // Task Manager's source: the "GPU Engine" utilization counters, summed per
    // engine type across processes, busiest type wins. Sampled once a second on
    // its own thread; -1 means the counters are unavailable.
    auto* impl = impl_.get();
    impl->gpuSampler = std::jthread([impl](std::stop_token stop) {
        PDH_HQUERY query = nullptr; PDH_HCOUNTER counter = nullptr;
        if (PdhOpenQueryW(nullptr, 0, &query) != ERROR_SUCCESS) return;
        if (PdhAddEnglishCounterW(query, L"\\GPU Engine(*)\\Utilization Percentage", 0, &counter) != ERROR_SUCCESS) {
            PdhCloseQuery(query); veyra::log::warn("perf", "GPU engine counters unavailable"); return;
        }
        PdhCollectQueryData(query);
        std::vector<unsigned char> buffer;
        while (!stop.stop_requested()) {
            for (int i = 0; i < 10 && !stop.stop_requested(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            if (stop.stop_requested()) break;
            if (PdhCollectQueryData(query) != ERROR_SUCCESS) continue;
            DWORD bytes = 0, count = 0;
            if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, nullptr) != PDH_MORE_DATA) continue;
            buffer.resize(bytes);
            auto* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());
            if (PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &bytes, &count, items) != ERROR_SUCCESS) continue;
            std::map<std::wstring, double> byType;
            // Instance names carry the adapter: pid_<n>_luid_0x<high>_0x<low>_phys_..._engtype_<type>.
            const uint64_t luid = impl->gpuMonitorLuid.load();
            const std::wstring luidKey = luid ? std::format(L"luid_0x{:08x}_0x{:08x}_", uint32_t(luid >> 32), uint32_t(luid)) : std::wstring{};
            for (DWORD i = 0; i < count; ++i) {
                if (items[i].FmtValue.CStatus != PDH_CSTATUS_VALID_DATA && items[i].FmtValue.CStatus != PDH_CSTATUS_NEW_DATA) continue;
                std::wstring name = items[i].szName ? items[i].szName : L"";
                std::transform(name.begin(), name.end(), name.begin(), [](wchar_t ch) { return wchar_t(towlower(ch)); });
                if (!luidKey.empty() && name.find(luidKey) == std::wstring::npos) continue;
                const auto at = name.find(L"engtype_");
                byType[at == std::wstring::npos ? std::wstring{} : name.substr(at + 8)] += items[i].FmtValue.doubleValue;
            }
            double busiest = 0;
            for (const auto& [type, value] : byType) busiest = std::max(busiest, value);
            impl->gpuUtil = std::clamp(busiest, 0.0, 100.0);
        }
        PdhCloseQuery(query);
    });
}

void QmlPlayerBridge::updateRunStatus() {
    auto& i = *impl_;
    const auto& s = i.snapshot;
    // QScreen's cached rate for the actual video host, not an arbitrary popup
    // or another monitor. This uses the existing bounded status cadence.
    double refresh=0;
    const HWND owner=i.videoWindow?GetAncestor(i.videoWindow,GA_ROOT):nullptr;
    for(auto* window:QGuiApplication::topLevelWindows())
        if(window->isVisible()&&window->screen()&&reinterpret_cast<HWND>(window->winId())==owner){refresh=window->screen()->refreshRate();break;}
    i.history.sample(s,{refresh,i.compareMode!=0});
    const auto& rate=i.history.output;
    const auto& f = s.metrics.flow;
    const bool active = s.running && !s.image && s.transport == engine::TransportState::Playing;
    QString status = tr("待机"), level = QStringLiteral("idle");
    if (s.failed) { status = tr("错误"); level = QStringLiteral("err"); }
    else if (s.remoteRecovering) { status = tr("恢复中"); level = QStringLiteral("warn"); }
    // Paused to buffer a network source (#48): no pictures are due, so the output rate is not judged.
    else if (s.running && s.network.active && (s.network.holding() || s.network.state == media::NetworkStats::State::Reconnecting)) {
        status = tr("缓冲中"); level = QStringLiteral("warn");
    }
    else if (active && !s.applying && f.rateWindowReady) {
        // Judged on the engine's own words, shown in the interface language.
        const std::wstring rate = i.history.rateStatus(s);
        status = uiText(rate);
        level = rate == L"正常" ? QStringLiteral("ok") : QStringLiteral("warn");
    }
    else if (s.applying && s.running) { status = tr("调整中"); level = QStringLiteral("warn"); }
    else if (active) status = tr("采样中");
    else if (s.transport == engine::TransportState::Paused) status = tr("已暂停");
    QStringList options;
    const auto chosen=settings();
    const auto content=engine::currentContentRate(chosen.content);
    if(content!=engine::ContentRate::Transport){
        const auto cadence=content==engine::ContentRate::Capture60To30?QStringLiteral("60→30"):tr("自动去除");
        if(s.running&&rate.halfRate)options<<tr("%1 · 当前约 %2 fps").arg(cadence).arg(rate.contentFps,0,'f',0);
        else if(s.running&&rate.deduplicating)options<<tr("%1 · 去重后约 %2 fps").arg(cadence).arg(rate.contentFps,0,'f',0);
        else if(s.running&&!s.capture)options<<tr("%1 · 当前来源未去重").arg(cadence);
        else if(s.running)options<<tr("%1 · 尚未观察到去重").arg(cadence);
        else options<<cadence;
    }
    if(i.presentation.display!=engine::DisplaySync::Tearing){
        const auto sync=i.presentation.display==engine::DisplaySync::Vsync?tr("垂直同步"):tr("自动显示同步");
        options<<((refresh>0)?tr("%1 · 屏幕 %2 Hz").arg(sync).arg(refresh,0,'f',1):sync);
    }
    // Variable refresh (NVIDIA G-SYNC / G-SYNC Compatible), Windows HDR and the
    // refresh rate: the presenting monitor while playing, otherwise the main
    // window's monitor. They come from the engine's DisplayStateService,
    // queried in the background on events only (startup, another monitor,
    // WM_DISPLAYCHANGE, the switch turned on, 刷新显示器状态), never on a timer.
    // The driver and Windows settings are the user's; the 可变刷新 switch decides
    // how Veyra presents when it is possible.
    auto& displays=i.engine.displayStates();
    if(i.displayChanged){i.displayChanged=false;displays.invalidate("display-change");}
    HWND host=s.running&&i.videoWindow?i.videoWindow:owner;
    if(!host)if(auto* window=QGuiApplication::focusWindow())host=reinterpret_cast<HWND>(window->winId());
    // request() starts a query only when this monitor's state is stale or
    // missing (the presenter asks for the same monitor while playing).
    if(host)displays.request(host,i.displayRecheckPending?"manual":"ui");
    const auto display=host?displays.state(displays.monitorOf(host)):std::nullopt;
    const int vrrLevel=s.running?int(s.displayVrr):display?int(gfx::vrrLevel(display->vrr)):0;
    const int hdr=display&&display->color.hdr?int(*display->color.hdr):-1;
    const double refreshHz=display?display->refreshHz:0;
    QString recheckNotice;bool recheckFailed=false;
    if(i.displayRecheckPending){
        // Answered once the shown monitor has a state of the re-check's generation.
        if(display&&display->generation>=i.displayRecheckGeneration){
            i.displayRecheckPending=false;
            recheckNotice=tr("已刷新显示器状态：系统 HDR %1 · 可变刷新 %2")
                .arg(hdr>0?tr("开"):hdr==0?tr("关"):tr("未知"),vrrSummaryText(int(gfx::vrrLevel(display->vrr))));
        }else if(std::chrono::steady_clock::now()-i.displayRecheckSince>std::chrono::seconds(5)){
            i.displayRecheckPending=false;recheckNotice=tr("刷新显示器状态没有完成，请稍后再试");recheckFailed=true;
        }
    }
    const bool displayChecking=i.displayRecheckPending||displays.busy();
    if(i.presentation.variableRefresh){
        if(vrrLevel>=4)options<<tr("可变刷新 · 生效中");
        else if(vrrLevel==3)options<<tr("可变刷新 · 等待驱动确认");
        else options<<tr("可变刷新 · 当前屏幕不可用");
    }else if(s.running&&vrrLevel>=4)options<<tr("屏幕可变刷新生效中");
    if(i.presentation.outputRate!=engine::OutputRateMode::Off){
        if(s.running&&s.presentationEffective.outputRate==engine::OutputRateMode::Off)
            options<<tr("输出限帧 · 当前未生效");
        else if(i.presentation.outputRate==engine::OutputRateMode::FollowDisplay)
            options<<tr("输出限帧 · 跟随屏幕");
        else options<<tr("输出限帧 · %1 fps").arg(i.presentation.customFps,0,'f',1);
    }
    // On NVIDIA the low-latency choice owns the low queue.
    if(nvidiaLowLatencyAvailable()&&i.presentation.nvidiaLowLatency!=engine::NvidiaLowLatency::Off){
        if(s.running&&s.presentationEffective.nvidiaLowLatency==engine::NvidiaLowLatency::Off)options<<tr("英伟达低延迟 · 当前未生效");
        else options<<tr("英伟达低延迟 · 开");
    }
    else if(i.presentation.enabled)options<<(s.running&&!s.presentationEffective.enabled?tr("低延迟队列 · 当前未生效"):tr("低延迟队列"));
    if(!s.capture&&std::abs(s.playbackRate-1.0)>.001)options<<tr("播放速度 %1×").arg(s.playbackRate,0,'f',2);
    if(i.compareMode!=0)options<<tr("画面对比 · 补帧暂停");
    QStringList detail;
    // Live output hint: also shown under the 倍率 control (fgDisplayHint).
    QString fgHint;
    if (s.running && !s.image && s.applied.multiplier > 1 && i.compareMode == 0 && s.displayFgSuggestion > 0 &&
        s.displayFgContentFps > 0 && s.displayFgRefreshHz > 0) {
        const double output = s.displayFgContentFps * s.applied.multiplier;
        fgHint = s.displayFgSuggestion == 1
            ? tr("当前帧率 %1 fps 超出屏幕刷新率 %2 Hz，建议关闭补帧来获得更好的延迟体验")
                  .arg(output, 0, 'f', 0).arg(s.displayFgRefreshHz, 0, 'f', 0)
            : tr("当前帧率 %1 fps 超出屏幕刷新率 %2 Hz，建议降至 %3X 来获得更好的延迟体验")
                  .arg(output, 0, 'f', 0).arg(s.displayFgRefreshHz, 0, 'f', 0).arg(s.displayFgSuggestion);
    }
    if (s.running && !s.image) {
        if (s.applied.multiplier > 1) {
            QString backend;
            switch (s.applied.frameGenerationBackend) {
            case engine::FrameGenerationBackend::Dlss: backend = tr("DLSS"); break;
            case engine::FrameGenerationBackend::XeSS: backend = tr("XeSS"); break;
            case engine::FrameGenerationBackend::Fsr: backend = tr("FSR 3.1"); break;
            case engine::FrameGenerationBackend::Fsr4: backend = tr("FSR 4 ML"); break;
            case engine::FrameGenerationBackend::Anr: backend = QStringLiteral("ANR FG"); break;
            case engine::FrameGenerationBackend::Vfg: backend = QStringLiteral("NVIDIA VFG"); break;
            }
            detail << tr("补帧 %1X · %2").arg(s.applied.multiplier)
                          .arg(backend);
        }
        else detail << tr("未补帧");
        if (s.applied.multiplier > 1 && impl_->compareMode != 0) detail << tr("补帧暂停");
        // Live output hint (DisplayFgBudget.h): a suggestion only, the
        // multiplier stays the user's choice.
        else if (!fgHint.isEmpty()) detail << fgHint;
        else if (s.applied.multiplier > 1 && (s.fgBudgetLimited || s.xessGenerationSuppressed) &&
                 (!rate.policyLimited()||i.history.overloaded)) detail << tr("调度降档");
        // Why VFG cannot hold the requested multiplier. One VFG interpolation
        // costs about the same at any resolution, so what fits follows from its
        // measured cost, the rest of the chain and the source interval; the
        // display refresh caps what can be shown. A hint only: no setting changes.
        if (s.applied.multiplier > 1 && s.applied.frameGenerationBackend == engine::FrameGenerationBackend::Vfg && i.compareMode == 0) {
            const double source = rate.contentFps;
            const auto perRun = f.gpuTiming[size_t(diagnostics::GpuStage::Fg1)].p95;
            const auto batch = f.gpuTiming[size_t(diagnostics::GpuStage::FgBatch)].p95;
            const auto whole = f.enhancementProcessing.p95;
            // A mixed tier runs its first model once per pair (the middle
            // subframe) and the second model for the others; Fg1 is then a
            // second-model subframe whenever the group has more than one.
            const bool mixed = engine::vfgMixedQuality(s.applied.vfgQuality) && s.applied.multiplier > 2;
            const auto middleStage = diagnostics::GpuStage(unsigned(diagnostics::GpuStage::Fg1) + engine::vfgPrimarySubframe(s.applied.multiplier) - 1);
            const auto middle = mixed ? f.gpuTiming[size_t(middleStage)].p95 : perRun;
            if (s.fgBudgetLimited && (!rate.policyLimited()||i.history.overloaded) && source > 1 && perRun && *perRun > 0 && middle && *middle > 0 && batch && whole) {
                const double base = std::max(0.0, *whole - *batch), budget = 1000.0 / source - base;
                // generated = 1 middle + k others; fits = generated + the real frame.
                const int generated = budget < *middle ? 0 : 1 + int(std::floor((budget - *middle) / *perRun));
                const int fits = std::clamp(1 + generated, 1, int(s.applied.multiplier));
                if (fits < int(s.applied.multiplier)) {
                    if (mixed) detail << tr("VFG 中间帧约 %1 ms、其余每张约 %2 ms，当前负载约可稳定 %3X").arg(*middle, 0, 'f', 1).arg(*perRun, 0, 'f', 1).arg(fits);
                    else detail << tr("VFG 每张约 %1 ms，当前负载约可稳定 %2X").arg(*perRun, 0, 'f', 1).arg(fits);
                }
            }
            if (fgHint.isEmpty() && refresh > 1 && source > 1 && source * s.applied.multiplier > refresh * 1.02)
                detail << tr("屏幕 %1 Hz 最多显示约 %2X").arg(refresh, 0, 'f', 0).arg(std::max(1, int(refresh / source)));
        }
        if (s.nominalSourceFps > 0.01) detail << tr("源 %1 fps").arg(s.nominalSourceFps, 0, 'f', 2);
        if(active&&f.rateWindowReady&&rate.targetFps>0)detail<<tr("有效目标 %1 fps").arg(rate.targetFps,0,'f',1);
        const auto autoState=nrAutoStatus();if(!autoState.isEmpty())detail<<autoState;
    }
    double ratio = -1.0;
    if (active && f.rateWindowReady && rate.targetFps>0)ratio=rate.actualFps/rate.targetFps;
    const bool ratioChanged = std::abs(ratio - i.rateRatio) > 0.005;
    i.rateRatio = ratio;
    // Frame-time line: the submit interval while playing (a real display rate is
    // not available here), 48 samples = 12 s at the 250 ms cadence.
    if (active && submitFpsKnown() && submitFps() > 0.5) {
        i.frameTimes.push_back(1000.0 / submitFps());
        while (i.frameTimes.size() > 48) i.frameTimes.pop_front();
    } else if (!s.running && !i.frameTimes.empty()) i.frameTimes.clear();
    const QString text = detail.join(QStringLiteral(" · "));
    const double gpu = i.gpuUtil.load();
    const double target=active&&f.rateWindowReady&&rate.targetFps>0?rate.targetFps:-1;
    const bool changed = ratioChanged || active || status != i.runStatus || level != i.runLevel || text != i.runDetail || options!=i.runOptions || std::abs(target-i.effectiveOutputFps)>.05 || std::abs(gpu - i.gpuPublished) > 0.05 ||
                         fgHint != i.fgDisplayHint || vrrLevel != i.vrrState || displayChecking != i.displayChecking ||
                         hdr != i.displayHdr || std::abs(refreshHz - i.displayRefreshHz) > .01;
    i.runStatus = status; i.runLevel = level; i.runDetail = text; i.gpuPublished = gpu;
    i.runOptions=options;i.effectiveOutputFps=target;i.fgDisplayHint=fgHint;i.vrrState=vrrLevel;i.displayChecking=displayChecking;
    i.displayHdr=hdr;i.displayRefreshHz=refreshHz;
    if (changed) emit perfChanged();
    if (!recheckNotice.isEmpty()) emit notice(recheckNotice, recheckFailed);
}


} // namespace veyra::ui
