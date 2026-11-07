// QML bridge: Subtitles responsibilities.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
// --- subtitles (P4-c) --------------------------------------------------------
namespace {
const wchar_t* const kSubtitleFonts[] = {L"Microsoft YaHei UI", L"SimHei", L"SimSun", L"DengXian", L"Arial", L"Segoe UI"};
bool subtitleIsChinese(const engine::SubtitleTrack& t) {
    const auto has = [](const std::wstring& s, const wchar_t* k) { return s.find(k) != std::wstring::npos; };
    return has(t.language, L"chi") || has(t.language, L"zho") || has(t.language, L"zh") || has(t.name, L"中") ||
           has(t.name, L"简") || has(t.name, L"繁");
}
} // namespace
void ensureGdiplus() {
    static ULONG_PTR token = 0;
    if (token) return;
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&token, &input, nullptr) != Gdiplus::Ok) token = 0;
}
void QmlPlayerBridge::refreshSubtitles(const std::wstring& media) {
    auto& i = *impl_;
    i.subTracks.clear(); i.subExternal = i.subLoaded = 0;
    i.subPrimary = i.subSecondary = -1; i.subPrimaryChosen = i.subSecondaryChosen = false;
    i.subStatus=QFileInfo(QString::fromStdWString(media)).isFile()?tr("正在读取字幕"):QString{}; i.subMedia = media;
    if(i.subFileLoader)i.subFileGeneration=i.subFileLoader->requestFile(L"");
    i.subFilePath.clear();
    if (i.subAlignWorker.joinable()) { i.subAlignWorker.request_stop(); i.subAlignWorker = {}; }
    i.subAligning = false; i.subAlignReady = false;
    if (!i.subLoader) i.subLoader = std::make_unique<engine::SubtitleLoader>();
    // A file on a server is scanned only while playback has its buffer (#48): a second reader
    // that takes the same share's bandwidth would starve the picture it is meant to subtitle.
    const bool server = !media.empty() && engine::subtitleServerFile(media);
    i.subScanGate = server ? std::make_shared<std::atomic<bool>>(false) : nullptr;
    i.subScanOpen = false;
    i.subLoader->setReadGate(i.subScanGate);
    if (server) i.subStatus = tr("字幕将在视频缓冲好后读取");
    i.subGeneration = i.subLoader->request(media);
    emit subtitlesChanged();
}
void QmlPlayerBridge::tickSubtitles() {
    auto& i = *impl_;
    // The only place that sees playback while it happens: remember where the Bilibili
    // video got to, so the report at flush time describes what was actually watched.
    // 这里是唯一逐帧看到播放进度的地方：记下 B 站视频播到哪，切走或播完时才能把
    // 真实位置写回历史。2.1.2→2.1.3 拆桥接层时这段丢过，续播因此失效。
    if (!i.biliWatchBvid.empty() && i.biliWatchCid > 0) {
        const auto& cur = i.snapshot;
        using Tr = engine::TransportState;
        const bool playing = cur.running && !cur.failed &&
                             (cur.transport == Tr::Playing || cur.transport == Tr::Paused);
        if (playing && cur.position > 0.0) {
            i.biliWatchLastPos = cur.position;
            if (cur.duration > 0.0) i.biliWatchDuration = cur.duration;
        } else if (cur.transport == Tr::Ended && !cur.failed) {
            reportBiliWatchProgressAsync();
        }
    }
    bool changed = false;
    if (i.subScanGate) {
        // Opens once playback holds a few seconds ahead (or has read the whole file), closes when
        // it runs short, and never opens for a server too slow for the stream itself.
        const auto& n = i.snapshot.network;
        const bool ended = n.active && n.state == media::NetworkStats::State::Ended;
        const bool healthy = n.active && !n.bandwidthLow && n.state == media::NetworkStats::State::Playing;
        const bool open = i.subScanOpen ? (ended || (healthy && n.bufferedSeconds >= 1.0))
                                        : (ended || (healthy && n.bufferedSeconds >= 3.0));
        if (open != i.subScanOpen) {
            i.subScanOpen = open;
            i.subScanGate->store(open);
            veyra::log::info("subtitle", std::format("server scan gate {} (buffer {:.1f}s)", open ? "open" : "closed", n.bufferedSeconds));
            if (open && i.subStatus == tr("字幕将在视频缓冲好后读取")) { i.subStatus = tr("正在读取字幕"); changed = true; }
        }
    }
    if (i.subLoader) {
        if (auto result = i.subLoader->poll(); result && result->generation == i.subGeneration) {
            // Replace the loader's tracks, keep user-loaded files and per-track offsets.
            const size_t count = result->tracks.size();
            for (size_t k = 0; k < std::min(i.subLoaded, count); ++k)
                result->tracks[k].offsetMs = i.subTracks[i.subExternal + k].offsetMs;
            i.subTracks.erase(i.subTracks.begin() + std::ptrdiff_t(i.subExternal),
                              i.subTracks.begin() + std::ptrdiff_t(i.subExternal + i.subLoaded));
            i.subTracks.insert(i.subTracks.begin() + std::ptrdiff_t(i.subExternal),
                               std::make_move_iterator(result->tracks.begin()), std::make_move_iterator(result->tracks.end()));
            i.subLoaded = count;
            if(result->complete&&(i.subStatus==tr("正在读取字幕")||i.subStatus==tr("字幕将在视频缓冲好后读取")))i.subStatus.clear();
            int firstUsable = -1, firstChinese = -1;
            for (size_t k = 0; k < i.subTracks.size(); ++k) {
                if (!i.subTracks[k].usable()) continue;
                if (firstUsable < 0) firstUsable = int(k);
                if (firstChinese < 0 && subtitleIsChinese(i.subTracks[k])) firstChinese = int(k);
            }
            if (!i.subPrimaryChosen) i.subPrimary = firstChinese >= 0 ? firstChinese : firstUsable;
            if (!i.subSecondaryChosen && i.prefBool("subtitleSecondLanguage", false)) {
                int candidate = -1;
                for (size_t k = 0; k < i.subTracks.size(); ++k)
                    if (i.subTracks[k].usable() && int(k) != i.subPrimary) { candidate = int(k); break; }
                i.subSecondary = candidate;
            }
            if (result->complete)
                for (const auto& t : i.subTracks)
                    veyra::log::info("subtitle", std::format("track name={} codec={} embedded={} cues={} usable={}",
                        QString::fromStdWString(t.name).toStdString(), QString::fromStdWString(t.codec).toStdString(),
                        t.embedded ? 1 : 0, t.cues.size(), t.usable() ? 1 : 0));
            veyra::log::info("subtitle", std::format("tracks={} primary={} secondary={}", i.subTracks.size(), i.subPrimary, i.subSecondary));
            changed = true;
        }
    }
    if(i.subFileLoader){
        if(auto result=i.subFileLoader->poll();result&&result->generation==i.subFileGeneration&&result->complete&&!i.subFilePath.empty()){
            const auto file=QFileInfo(QString::fromStdWString(i.subFilePath)).fileName();
            i.subFilePath.clear();
            const bool usable=std::any_of(result->tracks.begin(),result->tracks.end(),[](const auto& t){return t.usable();});
            if(!usable){
                const auto detail=result->tracks.empty()?tr("文件格式不支持、数据损坏或没有字幕内容"):uiText(result->tracks.front().note);
                i.subStatus=tr("外部字幕加载失败：%1 · %2").arg(file,detail);
                emit notice(i.subStatus,true);
            }else{
                const int added=int(result->tracks.size());
                int first=0;
                while(!result->tracks[size_t(first)].usable())++first;
                i.subTracks.insert(i.subTracks.begin(),std::make_move_iterator(result->tracks.begin()),std::make_move_iterator(result->tracks.end()));
                i.subExternal+=size_t(added);
                if(i.subSecondary>=0)i.subSecondary+=added;
                if(i.subAlignTrack>=0)i.subAlignTrack+=added;
                i.subPrimary=first;i.subPrimaryChosen=true;
                i.subStatus=tr("已加载外部字幕：%1").arg(file);
                veyra::log::info("subtitle",std::format("external loaded tracks={} file={}",added,file.toStdString()));
            }
            changed=true;
        }
    }
    if (i.subAlignReady.exchange(false) && i.subAlignGeneration == i.subGeneration &&
        i.subAlignTrack >= 0 && size_t(i.subAlignTrack) < i.subTracks.size()) {
        i.subTracks[size_t(i.subAlignTrack)].offsetMs = i.subAlignOffset.load();
        std::lock_guard lock(i.subAlignMutex);
        i.subStatus = tr("自动对齐：%1（Z/X 可微调）").arg(QString::fromStdWString(i.subAlignDetail));
        veyra::log::info("subtitle", std::format("auto align applied offsetMs={}", i.subAlignOffset.load()));
        changed = true;
    }
    if (i.subAligning.load() != (i.subStatus.startsWith(tr("正在自动对齐")))) changed = true;
    if (changed) emit subtitlesChanged();

    // Draw. The overlay is a layered child of the video window: it moves with the
    // picture and is clipped by the same window region as the video itself.
    if (!i.videoWindow || !IsWindow(i.videoWindow)) return;
    if (!i.subOverlay) {
        // A refused layered child (missing compatibility manifest, old OS) is
        // reported once; retrying every tick would only flood the log.
        if (i.subOverlayFailed) return;
        ensureGdiplus();
        i.subOverlay = createSubtitleOverlay(i.videoWindow);
        const DWORD error = i.subOverlay ? 0 : GetLastError();
        veyra::log::info("subtitle", std::format("overlay created ok={} err={}", i.subOverlay != nullptr, error));
        if (!i.subOverlay) { i.subOverlayFailed = true; return; }
    }
    RECT client{}; GetClientRect(i.videoWindow, &client);
    RECT current{}; GetClientRect(i.subOverlay, &current);
    if (current.right != client.right || current.bottom != client.bottom)
        SetWindowPos(i.subOverlay, HWND_TOP, 0, 0, client.right, client.bottom, SWP_NOACTIVATE);
    const auto& s = i.snapshot;
    std::vector<SubtitleLine> draw;
    const bool show = i.prefBool("subtitleEnabled", true) && (s.running && !s.image && !s.capture) && !i.subTracks.empty();
    if (show) {
        auto append = [&](int index, bool secondary) {
            if (index < 0 || size_t(index) >= i.subTracks.size()) return;
            const auto& track = i.subTracks[size_t(index)];
            if (!track.usable()) return;
            const double at = s.position + double(track.offsetMs) / 1000.0;
#ifdef VEYRA_HAS_LIBASS
            if (track.ass) {
                // ASS/SSA: libass draws the whole script (effects, karaoke,
                // container fonts). The parsed text only feeds diagnostics.
                SubtitleLine line;
                line.ass = track.ass; line.assTimeMs = at * 1000.0; line.secondary = secondary;
                line.text = engine::textAt(track, at);
                draw.push_back(std::move(line));
                return;
            }
#endif
            for (const auto* cue : engine::cuesAt(track, at, 3)) {
                SubtitleLine line;
                line.text = cue->text; line.bitmap = cue->bitmap;
                line.style = track.styles.empty() ? engine::SubtitleStyle{}
                           : track.styles[size_t(std::clamp(cue->style, 0, int(track.styles.size()) - 1))];
                if (cue->alignOverride) line.style.alignment = cue->alignOverride;
                line.alignOverride = cue->alignOverride;
                line.posX = cue->posX >= 0 ? cue->posX / std::max(1.0, track.scriptWidth) : -1.0;
                line.posY = cue->posY >= 0 ? cue->posY / std::max(1.0, track.scriptHeight) : -1.0;
                line.secondary = secondary;
                // Outline level 0..3: none / thin / normal / thick, over the track's own width.
                const int outline = i.prefInt("subtitleOutline", 2);
                if (outline == 0) line.style.outlineWidth = 0;
                else if (line.style.outlineWidth <= 0) line.style.outlineWidth = outline == 1 ? 1.0 : outline == 2 ? 2.0 : 3.5;
                else line.style.outlineWidth *= outline == 1 ? 0.6 : outline == 3 ? 1.6 : 1.0;
                draw.push_back(std::move(line));
            }
        };
        append(i.subSecondary, true);   // the painter stacks bottom-up
        append(i.subPrimary, false);
    }
    SubtitleView view;
    view.scale = double(i.prefInt("subtitleSize", 22)) / 22.0;
    const int font = i.prefInt("subtitleFont", 0);
    view.fontOverride = font > 0 ? std::wstring(kSubtitleFonts[std::clamp(font, 0, 5)]) : std::wstring{};
    view.outline = i.prefInt("subtitleOutline", 2) > 0;
    view.background = i.prefBool("subtitleBackground", false);
    view.bottomMargin = i.prefInt("subtitleMargin", 0) + i.subBottomInset;
    view.targetLines = i.prefInt("subtitleLines", 2);
    view.fitToLines = i.prefBool("subtitleFit", false);
    view.preview = i.engine.previewView();
    view.videoWidth = s.metrics.resolution.output.width;
    view.videoHeight = s.metrics.resolution.output.height;
    updateSubtitleOverlay(i.subOverlay, draw, view);
    QString joined;
    for (const auto& line : draw) { if (line.text.empty()) continue; if (!joined.isEmpty()) joined += QStringLiteral(" | "); joined += QString::fromStdWString(line.text); }
    if (joined != i.subText) {
        i.subText = joined;
        if (!joined.isEmpty()) veyra::log::info("subtitle-overlay", "text=" + joined.toStdString());
        emit subtitleTextChanged();
    }
    tickDanmaku();
}
QVariantList QmlPlayerBridge::subtitleTracks() const {
    QVariantList out;
    for (size_t k = 0; k < impl_->subTracks.size(); ++k) {
        const auto& t = impl_->subTracks[k];
        QString label = QString::fromStdWString(t.name);
        if (label.isEmpty()) label = QString::fromStdWString(t.language);
        if (label.isEmpty()) label = tr("字幕 %1").arg(k + 1);
        QString note = t.embedded ? tr("内嵌") : tr("外挂");
        if (!t.codec.empty()) note += QStringLiteral(" · ") + QString::fromStdWString(t.codec).toUpper();
        if (!t.note.empty()) note += QStringLiteral(" · ") + uiText(t.note);
        else if(!t.usable()) note += QStringLiteral(" · ") + tr("未解码出可显示的字幕");
        out << QVariantMap{{"index", int(k)}, {"label", label}, {"note", note}, {"usable", t.usable()},
                           {"embedded", t.embedded}, {"cues", int(t.cues.size())}, {"offsetMs", t.offsetMs}};
    }
    return out;
}
int QmlPlayerBridge::subtitlePrimary() const { return impl_->subPrimary; }
void QmlPlayerBridge::setSubtitlePrimary(int index) {
    if (index < -1 || index >= int(impl_->subTracks.size())) return;
    if (index >= 0 && !impl_->subTracks[size_t(index)].usable()) { emit notice(tr("该字幕轨没有可显示的内容"), true); return; }
    impl_->subPrimaryChosen = true;
    if (impl_->subPrimary == index) return;
    impl_->subPrimary = index;
    if (impl_->subSecondary == index) impl_->subSecondary = -1;
    veyra::log::info("subtitle", std::format("primary={} secondary={}", impl_->subPrimary, impl_->subSecondary));
    emit subtitlesChanged();
}
int QmlPlayerBridge::subtitleSecondary() const { return impl_->subSecondary; }
void QmlPlayerBridge::setSubtitleSecondary(int index) {
    if (index < -1 || index >= int(impl_->subTracks.size())) return;
    if (index >= 0 && (!impl_->subTracks[size_t(index)].usable() || index == impl_->subPrimary)) return;
    impl_->subSecondaryChosen = true;
    if (impl_->subSecondary == index) return;
    impl_->subSecondary = index;
    veyra::log::info("subtitle", std::format("primary={} secondary={}", impl_->subPrimary, impl_->subSecondary));
    emit subtitlesChanged();
}
int QmlPlayerBridge::subtitleOffsetMs() const {
    const int p = impl_->subPrimary;
    return p >= 0 && size_t(p) < impl_->subTracks.size() ? impl_->subTracks[size_t(p)].offsetMs : 0;
}
void QmlPlayerBridge::setSubtitleOffsetMs(int ms) {
    const int p = impl_->subPrimary;
    if (p < 0 || size_t(p) >= impl_->subTracks.size()) return;
    auto& track = impl_->subTracks[size_t(p)];
    ms = std::clamp(ms, -30000, 30000);
    if (track.offsetMs == ms) return;
    track.offsetMs = ms;
    impl_->subStatus = tr("字幕延时 %1 ms").arg(ms);
    veyra::log::info("subtitle", std::format("offset track={} offsetMs={}", p, ms));
    emit subtitlesChanged();
}
QString QmlPlayerBridge::subtitleStatus() const { return impl_->subStatus; }
bool QmlPlayerBridge::subtitleAligning() const { return impl_->subAligning.load(); }
QString QmlPlayerBridge::subtitleText() const { return impl_->subText; }
int QmlPlayerBridge::subtitleBottomInset() const { return impl_->subBottomInset; }
void QmlPlayerBridge::setSubtitleBottomInset(int value) {
    value = std::clamp(value, 0, 400);
    if (impl_->subBottomInset == value) return;
    impl_->subBottomInset = value;
    emit subtitlesChanged();
}
void QmlPlayerBridge::loadSubtitleDialog() {
    const QString start = impl_->subMedia.empty() ? QString() : QFileInfo(QString::fromStdWString(impl_->subMedia)).absolutePath();
    const QString path = QFileDialog::getOpenFileName(nullptr, tr("加载外部字幕"), start,
        tr("字幕文件 (*.srt *.ass *.ssa *.vtt *.sup *.pgs *.idx *.sub *.smi *.sami *.sbv *.mpl *.stl *.txt);;所有文件 (*.*)"));
    if (!path.isEmpty()) loadSubtitleFile(path);
}
bool QmlPlayerBridge::loadSubtitleFile(const QString& path) {
    if(!QFileInfo(path).isFile()){
        emit notice(tr("字幕文件不存在或无法读取"),true);
        return false;
    }
    if(!impl_->subFileLoader)impl_->subFileLoader=std::make_unique<engine::SubtitleLoader>();
    impl_->subFilePath=wideOf(path);
    impl_->subFileGeneration=impl_->subFileLoader->requestFile(impl_->subFilePath);
    impl_->subStatus=tr("正在加载外部字幕：%1").arg(QFileInfo(path).fileName());
    emit subtitlesChanged();
    return true;
}
void QmlPlayerBridge::nudgeSubtitle(int deltaMs) {
    if (impl_->subPrimary < 0) { emit notice(tr("没有正在显示的字幕"), true); return; }
    setSubtitleOffsetMs(subtitleOffsetMs() + deltaMs);
    emit notice(impl_->subStatus, false);
}
void QmlPlayerBridge::cycleSubtitle(bool secondary) {
    auto& i = *impl_;
    if (i.subTracks.empty()) { emit notice(tr("当前片源没有字幕"), true); return; }
    int slot = secondary ? i.subSecondary : i.subPrimary;
    for (size_t n = 0; n <= i.subTracks.size(); ++n) {   // -1 -> 0 -> ... -> -1, skipping unusable
        slot = slot + 1 >= int(i.subTracks.size()) ? -1 : slot + 1;
        if (slot < 0 || (i.subTracks[size_t(slot)].usable() && !(secondary && slot == i.subPrimary))) break;
    }
    if (secondary) setSubtitleSecondary(slot); else setSubtitlePrimary(slot);
    const int now = secondary ? i.subSecondary : i.subPrimary;
    emit notice(now < 0 ? (secondary ? tr("副字幕已关闭") : tr("主字幕已关闭"))
                        : (secondary ? tr("副字幕：%1") : tr("主字幕：%1")).arg(subtitleTracks()[now].toMap().value("label").toString()), false);
}
void QmlPlayerBridge::toggleSubtitles() {
    const bool on = !impl_->prefBool("subtitleEnabled", true);
    setPreference(QStringLiteral("subtitleEnabled"), on);
    emit notice(on ? tr("字幕：开") : tr("字幕：关"), false);
}
void QmlPlayerBridge::autoAlignSubtitle() {
    auto& i = *impl_;
    if (i.subAligning.load()) { emit notice(tr("已有一个自动对齐在进行"), true); return; }
    if (i.subMedia.empty() || i.subPrimary < 0 || !i.subTracks[size_t(i.subPrimary)].usable()) {
        emit notice(tr("没有可用的主字幕，无法自动对齐"), true); return;
    }
    const std::wstring media = i.subMedia;
    const auto track = i.subTracks[size_t(i.subPrimary)];
    i.subAlignGeneration = i.subGeneration; i.subAlignTrack = i.subPrimary;
    i.subAligning = true; i.subAlignReady = false;
    i.subStatus = tr("正在自动对齐：分析音轨（最多前 30 分钟）…");
    emit subtitlesChanged();
    i.subAlignWorker = std::jthread([&i, media, track](std::stop_token stop) {
        const auto result = engine::alignSubtitleToAudio(media, track, 30, stop);
        if (stop.stop_requested()) { i.subAligning = false; return; }
        { std::lock_guard lock(i.subAlignMutex); i.subAlignDetail = result.detail; }
        i.subAlignOffset = result.ok ? result.offsetMs : 0;
        i.subAlignReady = result.ok;
        i.subAligning = false;
    });
}

void QmlPlayerBridge::attachVideoWindow(qulonglong nativeHandle) {
    impl_->videoWindow = reinterpret_cast<HWND>(nativeHandle);
}

void QmlPlayerBridge::setPreOpenHook(std::function<void()> hook) {
    impl_->preOpen = std::move(hook);
}

QString QmlPlayerBridge::appName() const { return QStringLiteral("Veyra"); }
QString QmlPlayerBridge::version() const { return QStringLiteral(VEYRA_DISPLAY_VERSION); }

// --- audio sync (1.4.4 声音补偿, #12) ------------------------------------------
int QmlPlayerBridge::audioSyncMode() const { return int(settings().audioSync); }
void QmlPlayerBridge::setAudioSyncMode(int value) {
    if (value < 0 || value > 2) return;
    auto s = settings();
    if (int(s.audioSync) == value) return;
    s.audioSync = static_cast<engine::AudioSyncMode>(value);
    if (!impl_->commit(std::move(s))) return;
    impl_->prefs[QStringLiteral("audioSync")] = value;
    if (!impl_->savePrefs()) veyra::log::warn("ui-prefs", "sound sync mode not saved");
    emit settingsChanged();
}
bool QmlPlayerBridge::audioSyncLive() const {
    const auto& s = impl_->snapshot;
    return s.running && (s.capture || s.remotePlay);
}
double QmlPlayerBridge::audioCompensationMs() const {
    return audioSyncLive() ? impl_->snapshot.captureAudio.compensationMs : 0.0;
}


} // namespace veyra::ui
