// QML bridge: Recording responsibilities.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
// --- live recording of the processed picture --------------------------------
// The engine owns the encoder and the file; this side only starts, stops and
// shows the state. The 3 second countdown is an interface animation and runs
// before the request is sent.
QString QmlPlayerBridge::recordingDirectory() const {
    const QString chosen = impl_->prefString("recordDir");
    if (!chosen.isEmpty()) return chosen;
    const QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    return QDir(videos.isEmpty() ? QDir::homePath() : videos).filePath(QStringLiteral("Veyra"));
}
// 0 = automatic from the picture size and frame rate (LiveRecorder).
int QmlPlayerBridge::recordingBitrateMbps() const { return std::clamp(impl_->prefInt("recordBitrateMbps", 0), 0, 400); }
int QmlPlayerBridge::recordingFpsLimit() const { return std::clamp(impl_->prefInt("recordFps", 0), 0, 240); }
bool QmlPlayerBridge::recordingHevc() const { return impl_->prefBool("recordHevc", false); }
bool QmlPlayerBridge::recordingAudio() const { return impl_->prefBool("recordAudio", true); }
bool QmlPlayerBridge::recording() const { return impl_->recordingState.value("active").toBool(); }
QVariantMap QmlPlayerBridge::recordingState() const { return impl_->recordingState; }

void QmlPlayerBridge::startRecording() {
    if (recording()) { emit notice(tr("录制已在进行中"), true); return; }
    if (!hasSource()) { emit notice(tr("没有正在播放的画面，无法录制"), true); return; }
    engine::RecordingSettings options;
    options.directory = wideOf(recordingDirectory());
    options.bitrateMbps = uint32_t(recordingBitrateMbps());
    options.hevc = recordingHevc();
    options.fpsLimit = uint32_t(std::max(0, recordingFpsLimit()));
    options.audio = recordingAudio();
    std::wstring error;
    if (!impl_->engine.requestRecording(options, error)) { emit notice(uiText(error), true); return; }
    // Shown at once; the poll fills in the file and the counters.
    impl_->recordingState["active"] = true;
    impl_->recordingState["failed"] = false;
    emit recordingChanged();
}
void QmlPlayerBridge::stopRecording() {
    if (!recording()) return;
    impl_->engine.stopRecording();
    impl_->recordingState["active"] = false;
    emit recordingChanged();
}
void QmlPlayerBridge::toggleRecording() { if (recording()) stopRecording(); else startRecording(); }
void QmlPlayerBridge::chooseRecordingDirectory() {
    const QString dir = QFileDialog::getExistingDirectory(nullptr, tr("选择录制保存位置"), recordingDirectory());
    if (!dir.isEmpty()) setPreference(QStringLiteral("recordDir"), dir);
}
void QmlPlayerBridge::openRecordingDirectory() {
    const QString dir = recordingDirectory();
    QDir().mkpath(dir);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(dir))) emit notice(tr("无法打开目录：%1").arg(dir), true);
}

void QmlPlayerBridge::chooseExportPath() {
    if (impl_->chain.mode == engine::ChainMode::Node) {
        emit notice(tr("节点链尚未接入导出执行器，请切换列表模式后导出"), true);
        return;
    }
    const QString path = QFileDialog::getExistingDirectory(nullptr,tr("选择导出目录"),exportTarget());
    setExportPath(path);
}

void QmlPlayerBridge::setExportPath(const QString& path) {
    if (path.isEmpty()) return;
    impl_->exportOutput = wideOf(path);
    impl_->exportStatus = QFileInfo(path).fileName();
    emit exportChanged();
}

void QmlPlayerBridge::startExport() {
    QString error;auto settings=impl_->exportSettings(error);
    if(!settings){emit notice(error,true);return;}
    if(impl_->exportQueue.count()==0){
        if(!QFileInfo(utf8Of(impl_->sourceLabel)).isFile()){emit notice(tr("请先添加要导出的文件"),true);return;}
        const QString target=exportTarget();
        const bool fileTarget=!target.isEmpty()&&!QFileInfo(target).isDir();
        const auto id=impl_->exportQueue.addFile(utf8Of(impl_->sourceLabel),fileTarget?target:QString());
        impl_->exportQueue.queue().setTrim(id,impl_->exportTrimStartSeconds,impl_->exportTrimEndSeconds);
        impl_->exportQueue.setSelectedId(id);
    }
    if(impl_->exportOutput.empty())chooseExportPath();
    if(impl_->exportOutput.empty())return;
    const QFileInfo target(exportTarget());
    const QString folder=target.isDir()?target.absoluteFilePath():target.absolutePath();
    std::wstring reason;
    if(!impl_->exportQueue.queue().start(wideOf(folder),*settings,impl_->exportHevc,impl_->exportRateControl,reason)){
        emit notice(uiText(reason),true);emit exportChanged();return;
    }
    veyra::log::info("qml-export",std::format("export frozen bitrateMbps={} rateControl={} hevc={} srTarget={}",
        settings->exportBitrateMbps,int(impl_->exportRateControl),impl_->exportHevc,int(settings->srTarget)));
    impl_->exportQueue.refresh();pollExport();emit navigate(QStringLiteral("exp"));
    // Leave the GPU to the export (field request 2026-10-02): whatever plays is closed,
    // unless 导出页 → 导出时关闭正在播放的内容 is off.
    if(impl_->prefBool("exportStopsPlayback",true)&&(hasSource()||impl_->openingSource)){
        veyra::log::info("qml-export","export started: closing the playing source");
        stopPlayback();emit notice(tr("已关闭正在播放的内容，显卡全部留给导出"),false);
    }
}
void QmlPlayerBridge::cancelExport() {
    impl_->exportQueue.queue().cancel();impl_->exportQueue.refresh();pollExport();emit exportChanged();
}

void QmlPlayerBridge::restartApplication() {
    requestApplicationRestart(false);
}
void QmlPlayerBridge::restartForOverlayCompatibility() {
    requestApplicationRestart(true);
}
void QmlPlayerBridge::requestApplicationRestart(bool overlayCompatibility) {
    if (exportRunning()) {
        emit notice(tr("设置已保存，请等待导出结束后重启软件"), true);
        return;
    }
    rememberPosition(true);
    qApp->setProperty("veyraOverlayCompatRestart", overlayCompatibility);
    // main launches the replacement only after UI/engine owners unwind.
    QCoreApplication::exit(42);
}

void QmlPlayerBridge::quit() {
    rememberPosition(true);
    if (auto* app = qobject_cast<QGuiApplication*>(QCoreApplication::instance())) app->quit();
}

void QmlPlayerBridge::openCaptureDialog() { emit navigate(QStringLiteral("capture")); }
void QmlPlayerBridge::openPs5Dialog() { emit navigate(QStringLiteral("ps5")); }
void QmlPlayerBridge::openScreenCaptureDialog() { emit navigate(QStringLiteral("screen")); }
void QmlPlayerBridge::openAudioDialog() { emit navigate(QStringLiteral("audio")); }
void QmlPlayerBridge::openSubtitleDialog() { emit navigate(QStringLiteral("subtitle")); }


} // namespace veyra::ui
