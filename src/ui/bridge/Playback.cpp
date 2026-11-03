// QML bridge: Playback responsibilities.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
// --- playback ---------------------------------------------------------------
QString QmlPlayerBridge::statusText() const { return uiText(impl_->snapshot.status); }
bool QmlPlayerBridge::running() const { return impl_->snapshot.running; }
bool QmlPlayerBridge::paused() const { return impl_->snapshot.transport == engine::TransportState::Paused; }
bool QmlPlayerBridge::failed() const { return impl_->snapshot.failed; }
bool QmlPlayerBridge::isImage() const { return impl_->snapshot.image; }
bool QmlPlayerBridge::isCapture() const { return impl_->snapshot.capture; }
bool QmlPlayerBridge::hasSource() const { return impl_->snapshot.running || impl_->snapshot.image; }
bool QmlPlayerBridge::openingSource() const { return impl_->openingSource; }
double QmlPlayerBridge::position() const { return impl_->snapshot.position; }
double QmlPlayerBridge::duration() const { return impl_->snapshot.duration; }
double QmlPlayerBridge::progress() const {
    const double d = impl_->snapshot.duration;
    return d > 0.01 ? std::clamp(impl_->snapshot.position / d, 0.0, 1.0) : 0.0;
}
QString QmlPlayerBridge::positionText() const { return mmss(impl_->snapshot.position); }
QString QmlPlayerBridge::durationText() const { return mmss(impl_->snapshot.duration); }

// --- source -----------------------------------------------------------------
QString QmlPlayerBridge::sourceName() const {
    if (impl_->sourceLabel.empty()) return QString();
    if(impl_->liveKind==QLatin1String("url"))return tr("网络视频 · %1").arg(impl_->liveLabel);
    return QFileInfo(utf8Of(impl_->sourceLabel)).fileName();
}
QString QmlPlayerBridge::sourceSummary() const {
    const auto& s = impl_->snapshot;
    if (!s.sourceWidth) return QString();
    // Reported values only: no upscaling claim, no invented bit depth.
    return QStringLiteral("%1x%2").arg(s.sourceWidth).arg(s.sourceHeight);
}

int QmlPlayerBridge::sourceWidth() const { return int(impl_->snapshot.sourceWidth); }
int QmlPlayerBridge::sourceHeight() const { return int(impl_->snapshot.sourceHeight); }
double QmlPlayerBridge::sourceFps() const { return impl_->snapshot.nominalSourceFps; }
int QmlPlayerBridge::sourceRotation() const { return impl_->snapshot.sourceRotationDegrees; }
int QmlPlayerBridge::previewRotation() const { return int(impl_->engine.previewView().userQuarterTurns)*90; }
void QmlPlayerBridge::rotatePictureClockwise() {
    if(!hasSource()||!impl_->engine.rotatePreviewClockwise(impl_->snapshot.sessionId))return;
    // An in-progress protection selection uses the previous coordinate system.
    beginProtectionDraw(QString());
    tickSubtitles();
    if(gameMode()){updateGameGeometry();updateGameMode();emit gameModeChanged();}
    veyra::log::info("ui-rotation",std::format("session={} clockwise={}",impl_->snapshot.sessionId,previewRotation()));
    emit compareChanged();
}
double QmlPlayerBridge::sourceAspect() const {
    const auto& s = impl_->snapshot;
    // A known DAR already includes SAR/rotation. With no DAR, rotate the coded
    // extent as the presenter does; phone files can omit SAR yet carry a matrix.
    engine::PreviewView view;
    view.displayAspect=s.sourceDisplayAspect;
    view.rotationDegrees=s.sourceRotationDegrees;
    return view.aspect(float(s.sourceWidth),float(s.sourceHeight));
}

// --- live performance -------------------------------------------------------
// XeSS generates inside the provider: the engine only submits source frames,
// so its own rate would read 60 while the SDK presents 120. Show the provider's
// reported submissions for XeSS (AppShell: "SDK提交（非屏幕实测）").
static bool xessPresenting(const engine::PlayerSnapshot& s) {
    return s.fgActive && s.applied.frameGenerationBackend == engine::FrameGenerationBackend::XeSS &&
        s.metrics.flow.xessSdkSubmitFps > 0;
}
double QmlPlayerBridge::submitFps() const {
    const auto& s = impl_->snapshot;
    return xessPresenting(s) ? s.metrics.flow.xessSdkSubmitFps : s.submissionFps.value_or(0.0);
}
bool QmlPlayerBridge::submitFpsKnown() const { return xessPresenting(impl_->snapshot) || impl_->snapshot.submissionFps.has_value(); }
QString QmlPlayerBridge::submitFpsLabel() const { return xessPresenting(impl_->snapshot) ? tr("SDK 提交") : tr("提交"); }
double QmlPlayerBridge::lateMs() const { return impl_->snapshot.lateMs; }
double QmlPlayerBridge::lateP95Ms() const { return impl_->snapshot.lateP95Ms; }
QString QmlPlayerBridge::metricsSummary() const {
    const auto& s = impl_->snapshot;
    // Submit FPS, named as such: it is what we handed to the presenter, not what
    // the panel scanned out. There is no "display FPS" because we cannot measure
    // one, and a number we did not measure must not be shown as if we had.
    if (!s.submissionFps) return tr("未测量");
    return tr("提交 %1").arg(*s.submissionFps, 0, 'f', 1);
}
bool QmlPlayerBridge::nrActive() const { return impl_->snapshot.nrActive; }
bool QmlPlayerBridge::nrAutoActive() const {return impl_->snapshot.nrAutoActive;}
int QmlPlayerBridge::nrAutoPercent() const {return int(impl_->snapshot.nrAutoPercent);}
QString QmlPlayerBridge::nrAutoStatus() const {
    const auto& s=impl_->snapshot;
    if(s.nrAutoActive)return tr("NR 自动 %1% · %2×%3").arg(s.nrAutoPercent).arg(s.nrAutoWidth).arg(s.nrAutoHeight);
    if(s.nrAutoStatus==L"自动NR实例池不可用，保留1080p NR")return tr("自动 NR 实例池不可用，保留固定 1080p 档");
    if(!s.nrAutoStatus.empty())return tr("当前配置未启用自动 NR，保留固定 1080p 档");
    return {};
}
bool QmlPlayerBridge::srActive() const { return impl_->snapshot.srActive; }
bool QmlPlayerBridge::fgActive() const { return impl_->snapshot.fgActive; }
bool QmlPlayerBridge::captureRecovering() const { return impl_->snapshot.captureRecovering; }
double QmlPlayerBridge::captureFps() const { return impl_->snapshot.captureFps; }
double QmlPlayerBridge::captureDropped() const { return double(impl_->snapshot.captureDropped); }
QString QmlPlayerBridge::backendWarning() const { return uiText(impl_->snapshot.backendWarning); }

// --- commands ---------------------------------------------------------------
void QmlPlayerBridge::openFileDialog() {
    veyra::log::info("qml-file", "openFileDialog entered");
    const QString path = QFileDialog::getOpenFileName(
        nullptr, tr("打开视频或图片"), QString(),
        tr("媒体文件 (*.mp4 *.mkv *.mov *.avi *.webm *.ts *.m2ts *.jpg *.jpeg *.png *.bmp *.webp);;所有文件 (*)"));
    veyra::log::info("qml-file", path.isEmpty() ? "openFileDialog cancelled" : "openFileDialog selected file");
    if (!path.isEmpty()) openPath(path);
}
int QmlPlayerBridge::thumbnailGeneration() const { return impl_->thumbnailGeneration; }

QObject* QmlPlayerBridge::playlist() const { return impl_->playlist; }
void QmlPlayerBridge::showPlaylist() { emit playlistRequested(); }
void QmlPlayerBridge::addPlaylistFilesDialog() {
    const auto paths = QFileDialog::getOpenFileNames(nullptr, tr("添加文件到播放列表"), {},
        tr("媒体文件 (*.mp4 *.mkv *.mov *.avi *.webm *.ts *.m2ts *.mts *.m4v *.wmv *.mpg *.mpeg *.vob *.flv *.ogv *.asf *.3gp *.jpg *.jpeg *.png *.bmp *.webp);;所有文件 (*)"));
    if (!paths.isEmpty()) { impl_->playlist->addPaths(paths); emit playlistRequested(); }
}
void QmlPlayerBridge::addPlaylistFolderDialog() {
    const auto path = QFileDialog::getExistingDirectory(nullptr, tr("导入文件夹到播放列表"));
    if (!path.isEmpty()) { impl_->playlist->addPaths({path}); emit playlistRequested(); }
}
void QmlPlayerBridge::openDroppedUrls(const QList<QUrl>& urls) {
    // A single file on the video retains drag-to-open. Folder/multi-file drops
    // append to the playlist; dropping inside PlaylistWindow always appends.
    if (urls.size() == 1) {
        const QUrl url = urls.front();
        if(!url.isLocalFile()){openUrl(url);return;}
        if (url.isLocalFile() && !QFileInfo(url.toLocalFile()).isDir()) { openUrl(url); return; }
    }
    impl_->playlist->addUrls(urls);
    emit playlistRequested();
}
void QmlPlayerBridge::tickPlaylist() {
    const auto& s = impl_->snapshot;
    using T = engine::TransportState;
    if (!impl_->playlistSession) return;
    if (!impl_->liveKind.isEmpty() || utf8Of(impl_->sourceLabel) != impl_->playlistPath) {
        impl_->playlistSession = 0; impl_->playlist->setPlayback({}, {}); return;
    }
    if (s.sessionId != impl_->playlistSession) return; // Ignore old session snapshots after open.
    if (s.capture || s.remotePlay) { impl_->playlistSession = 0; impl_->playlist->setPlayback({}, {}); return; }
    QString state = QStringLiteral("stopped");
    switch (s.transport) {
    case T::Opening: state = QStringLiteral("opening"); break;
    case T::Playing: state = s.image ? QStringLiteral("image") : QStringLiteral("playing"); break;
    case T::Paused: state = QStringLiteral("paused"); break;
    case T::Ended: state = QStringLiteral("ended"); break;
    case T::Failed: state = QStringLiteral("failed"); break;
    default: break;
    }
    impl_->playlist->setPlayback(impl_->playlistPath, state);
    if (s.transport != T::Ended || s.failed || s.image || impl_->openingSource ||
        impl_->playlistCompletedSession == s.sessionId) return;
    impl_->playlistCompletedSession = s.sessionId;
    // Wait for this GUI tick to finish, then recheck the engine session. A stop,
    // new file, or live source selected in between must cancel auto-advance.
    QTimer::singleShot(0, this, [this, session = s.sessionId] {
        const auto latest = impl_->engine.snapshot();
        if (impl_->playlistSession == session && latest.sessionId == session &&
            latest.transport == T::Ended && !latest.failed && !latest.image && impl_->liveKind.isEmpty())
            impl_->playlist->advanceAfterEnd();
    });
}

void QmlPlayerBridge::openPath(const QString& path) {
    if (path.isEmpty()) return;
    flushHdrMemory();impl_->hdrTitleApplied.clear();
    const bool network=media::networkMediaInput(wideOf(path));
    if (QFileInfo(path).isDir()) {
        impl_->playlist->addPaths({path}); emit playlistRequested(); return;
    }
    if (!network&&PlaylistModel::supported(path) && !impl_->playlist->containsPath(path)) impl_->playlist->addPaths({path});
    impl_->playlistPath = path;
    impl_->playlist->setPlayback(path, QStringLiteral("opening"));
    rememberPosition(true);
    impl_->exportQueue.setSelectedId(0);
    ++impl_->liveOpenGen; impl_->pendingLiveText.clear();   // a waiting live open is dropped
    impl_->exportTrimStartSeconds = 0.0;
    impl_->exportTrimEndSeconds = 0.0;
    emit exportChanged();
    impl_->openingSource = true;
    impl_->openingSessionId = 0;
    const std::wstring wide = wideOf(path);
    impl_->sourceLabel = wide;
    impl_->liveKind.clear();
    impl_->liveLabel.clear();
    if(network){impl_->liveKind=QStringLiteral("url");impl_->liveLabel=QUrl(path).host();}
    ++impl_->thumbnailGeneration;
    emit thumbnailSourceChanged(network?QString():path);
    emit thumbnailGenerationChanged();
    if(!network)impl_->facade.noteRecentFile(wide);
    emit recentFilesChanged();
    // Switch to a page with a video area, then settle the native window's
    // geometry, and only then open. The presenter samples the window's client
    // size once at initialisation; opening before that point is what produced a
    // 1x1 swapchain.
    // From home or the Bilibili page go to the professional page, not the minimal one:
    // after a video starts the next thing the user reaches for is FG, NR or quality.
    // Starting from the professional page keeps them where they are.
    if (impl_->currentPage.isEmpty() || impl_->currentPage == QLatin1String("home")
        || impl_->currentPage == QLatin1String("bili"))
        emit navigate(QStringLiteral("pro"));
    if(!network)rememberSource(QStringLiteral("file"), path);
    if (impl_->preOpen) impl_->preOpen();
    auto options = impl_->options;
    options.softwareDecode = impl_->prefString("decode") == QLatin1String("software");
    options.hardwareDecodeOnly = impl_->prefString("decode") == QLatin1String("hardware");
    options.deinterlace=deinterlaceAvailable()&&impl_->prefBool("deinterlace",true);
    // Network / server playback (#48): read at open, like the decode choice above.
    options.network.level=media::networkBufferLevelFromId(impl_->prefString("networkBuffer").toStdString());
    options.network.startupPrebufferSeconds=impl_->prefs.value(QStringLiteral("networkPrebuffer"),-1).toInt();
    options.network.autoReconnect=impl_->prefBool("networkAutoReconnect",true);
    options.network.corrupt=media::corruptPolicyFromId(impl_->prefString("networkCorrupt").toStdString());
    impl_->engine.open(impl_->videoWindow, wide, options);
    impl_->openingSessionId = impl_->engine.snapshot().sessionId;
    impl_->playlistSession = network?0:impl_->openingSessionId;
    impl_->resumeSession = 0;
    if ((impl_->prefBool("rememberPosition", true)||impl_->prefBool("autoResume",false))) {
        const auto saved = impl_->prefs.value("positions").toMap().value(path).toMap();
        if (!saved.isEmpty()) { impl_->resumeSession = impl_->openingSessionId; impl_->resumeAt = saved.value("at").toDouble(); }
    }
    refreshSubtitles(network?std::wstring():wide);
}

bool QmlPlayerBridge::deinterlaceAvailable() const { return media::FFmpegDeinterlacer::available(); }
void QmlPlayerBridge::openNetworkDialog(){emit navigate(QStringLiteral("network"));}
bool QmlPlayerBridge::openNetworkAddress(const QString& address){
    const QUrl url(address.trimmed(),QUrl::StrictMode);
    if(!url.isValid()||url.host().isEmpty()||!media::networkMediaInput(wideOf(url.scheme()+QStringLiteral(":")))){
        emit notice(tr("请输入有效的 HTTP、HTTPS 或 RTSP 视频地址"),true);return false;
    }
    openPath(url.adjusted(QUrl::RemoveFragment).toString(QUrl::FullyEncoded));return true;
}

void QmlPlayerBridge::togglePlayPause() {
    // AppShell's Play button: a paused session is still "running" (the worker
    // stays alive), so the decision must come from the transport state.
    const auto& s = impl_->snapshot;
    using T = engine::TransportState;
    if (s.image || s.transport == T::Opening || s.transport == T::Stopping) return;
    if(impl_->liveKind==QLatin1String("url")&&(s.transport==T::Ended||(s.transport==T::Paused&&s.duration<=0))){
        openNetworkAddress(QString::fromStdWString(impl_->sourceLabel));return;
    }
    // Capture cards and streams pause too (field request 2026-10-02): the engine stops
    // reading and enhancing, keeps the last picture and resets the source's queue, so
    // play resumes with the newest frame rather than a backlog. The connection stays up.
    if (s.capture && s.running) {
        const bool pause = s.transport == T::Playing;
        impl_->engine.pause(pause);
        veyra::log::info("ui-transport", pause ? "live pause" : "live resume");
        emit notice(pause ? tr("已暂停：画面停在当前帧，不再处理新画面；点播放继续") : tr("已继续"), false);
        return;
    }
    if (!s.running || s.transport == T::Ended) {
        // Only a real file is reopened; device/stream labels are not paths.
        const QString file = QString::fromStdWString(impl_->sourceLabel);
        if (!file.isEmpty() && QFileInfo(file).isFile()) openPath(file);
        return;
    }
    const bool pause = s.transport == T::Playing;
    impl_->engine.pause(pause);
    veyra::log::info("ui-transport", pause ? "pause" : "resume");
}
void QmlPlayerBridge::stopPlayback() {
    flushHdrMemory();
    impl_->playlistSession = 0;
    impl_->playlist->setPlayback(impl_->playlistPath, QStringLiteral("stopped"));
    rememberPosition(true);
    ++impl_->liveOpenGen; impl_->pendingLiveText.clear();
    impl_->openingSource = false;
    impl_->openingSessionId = 0;
    impl_->engine.stop();
    stopPs5Control();
}
void QmlPlayerBridge::seekTo(double seconds) { impl_->engine.seek(seconds); }
void QmlPlayerBridge::seekBy(double seconds) {
    impl_->engine.seek(std::max(0.0, impl_->snapshot.position + seconds));
}
void QmlPlayerBridge::openUrl(const QUrl& url) {
    if(!url.isLocalFile()){openNetworkAddress(url.toString(QUrl::FullyEncoded));return;}
    if (!url.isLocalFile()) {
        emit notice(tr("只能打开本地文件"), true);
        return;
    }
    // Opened inside the drop, as AppShell's WM_DROPFILES does.
    veyra::log::info("ui-drop", url.toLocalFile().toStdString());
    openPath(QDir::toNativeSeparators(url.toLocalFile()));
}
void QmlPlayerBridge::logUi(const QString& channel, const QString& text) {
    const std::string c = channel.toStdString();
    veyra::log::info(c.c_str(), text.toStdString());
}
void QmlPlayerBridge::setWindowMask(QObject* window, const QRectF& rect) {
    auto* w = qobject_cast<QWindow*>(window);
    if (!w) return;
    w->setMask(QRegion(rect.toAlignedRect()));
}
void QmlPlayerBridge::holdOriginal(bool held) {
    // Releasing the key returns to whatever the 显示 page chose (off or split).
    if (held) impl_->engine.comparison(1, false);
    else impl_->engine.comparison(impl_->compareMode, impl_->compareBase, float(impl_->compareSplit));
    veyra::log::info("ui-compare", held ? "hold original on" : "hold original off");
}
void QmlPlayerBridge::stepFrame(int direction) {
    // A frame step is a small seek. The engine owns the real frame duration, so
    // this is honest about being approximate rather than pretending to be exact.
    if (!direction) return;
    impl_->engine.seek(std::max(0.0, impl_->snapshot.position + (direction > 0 ? 1.0 / 60.0 : -1.0 / 60.0)));
}

void QmlPlayerBridge::takeScreenshot() {
    if (!hasSource()) { emit notice(tr("没有正在播放的画面，无法截图"), true); return; }
    if(!impl_->pendingScreenshot.isEmpty()||!impl_->pendingFrameExport.isEmpty()||imageBatchActive()){emit notice(tr("请等待当前图片导出完成"),true);return;}
    const QString folder = screenshotDirectory();
    if (!QDir().mkpath(folder)) { emit notice(tr("截图目录不可用：%1").arg(folder), true); return; }
    const QString name = QStringLiteral("veyra-%1.png")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    const QString path = QDir(folder).filePath(name);
    impl_->engine.saveFrame(wideOf(QDir::toNativeSeparators(path)));
    impl_->pendingScreenshot=path;
    impl_->pendingScreenshotDeadline=QDateTime::currentMSecsSinceEpoch()+30000;
    impl_->lastScreenshot.clear();
    veyra::log::info("ui-screenshot", "requested path=" + path.toStdString());
    emit preferencesChanged();
}

// ---- network / server playback (#48) ------------------------------------------------
bool QmlPlayerBridge::networkActive() const { return impl_->snapshot.network.active; }
bool QmlPlayerBridge::networkHolding() const {
    const auto& n = impl_->snapshot.network;
    return n.active && (n.holding() || n.state == media::NetworkStats::State::Reconnecting);
}
QString QmlPlayerBridge::networkSummary() const {
    const auto& n = impl_->snapshot.network;
    if (!n.active) return {};
    QStringList parts;
    parts << tr("缓冲 %1 s").arg(n.bufferedSeconds, 0, 'f', 1);
    parts << (n.downloadBytesPerSecond > 0 ? tr("下载 %1 MB/s").arg(n.downloadBytesPerSecond / 1048576.0, 0, 'f', 1)
                                           : tr("下载 —"));
    parts << tr("重连 %1").arg(n.reconnects);
    if (const uint64_t lost = std::max(n.corruptPackets, n.corruptFrames); lost > 0) parts << tr("丢包 %1").arg(qulonglong(lost));
    return parts.join(QStringLiteral(" · "));
}
QString QmlPlayerBridge::networkNotice() const {
    const auto& n = impl_->snapshot.network;
    return n.active ? uiText(n.message) : QString();
}


} // namespace veyra::ui
