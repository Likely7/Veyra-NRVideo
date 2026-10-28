// QML bridge: Export responsibilities.
#include "../QmlPlayerBridge_p.h"

namespace veyra::ui {
// --- image export (P4-b) -----------------------------------------------------
bool QmlPlayerBridge::imageBatchActive() const { return impl_->imagePhase != Impl::ImagePhase::Idle; }
int QmlPlayerBridge::imageBatchDone() const { return impl_->imageDone; }
int QmlPlayerBridge::imageBatchTotal() const { return int(impl_->imageFiles.size()); }
int QmlPlayerBridge::imageBatchFailures() const { return impl_->imageFailures; }
QString QmlPlayerBridge::imageBatchStatus() const { return impl_->imageStatus; }
void QmlPlayerBridge::saveFrameDialog() {
    if(!impl_->pendingFrameExport.isEmpty()||!impl_->pendingScreenshot.isEmpty()||imageBatchActive()){emit notice(tr("请等待当前图片导出完成"),true);return;}
    if (!hasSource()) { emit notice(tr("没有可保存的画面"), true); return; }
    if (impl_->chain.mode == engine::ChainMode::Node) {
        emit notice(tr("图片导出只接受列表模式；请切回列表模式再导出"), true); return;
    }
    const QString suggested = QDir(screenshotDirectory()).filePath(
        QFileInfo(QString::fromStdWString(impl_->sourceLabel)).completeBaseName() + QStringLiteral("-veyra.png"));
    const QString path = QFileDialog::getSaveFileName(nullptr, tr("导出当前画面"), suggested,
                                                      tr("PNG 图片 (*.png);;JPEG 图片 (*.jpg)"));
    if (path.isEmpty()) return;
    if (QFileInfo::exists(path)) { emit notice(tr("目标文件已存在，请换一个文件名"), true); return; }
    impl_->engine.saveFrame(wideOf(QDir::toNativeSeparators(path)));
    impl_->pendingFrameExport=path;impl_->pendingFrameDeadline=QDateTime::currentMSecsSinceEpoch()+30000;
    emit notice(tr("正在保存：%1").arg(QDir::toNativeSeparators(path)), false);
}
void QmlPlayerBridge::exportImagesDialog() {
    const auto files = QFileDialog::getOpenFileNames(nullptr, tr("选择要处理的图片"), QString(),
        tr("图片 (*.png *.jpg *.jpeg)"));
    if (files.isEmpty()) return;
    const QString folder = QFileDialog::getExistingDirectory(nullptr, tr("保存到"), QFileInfo(files.front()).absolutePath());
    if (!folder.isEmpty()) startImageBatch(files, folder);
}
bool QmlPlayerBridge::startImageBatch(const QStringList& files, const QString& folder) {
    auto& i = *impl_;
    if (i.imagePhase != Impl::ImagePhase::Idle) { emit notice(tr("已有一批图片在处理"), true); return false; }
    if (i.chain.mode == engine::ChainMode::Node) {
        emit notice(tr("图片导出只接受列表模式；请切回列表模式再导出"), true); return false;
    }
    if (files.isEmpty() || !QFileInfo(folder).isDir()) { emit notice(tr("请选择图片和已存在的输出目录"), true); return false; }
    i.imageFiles = files; i.imageFolder = folder;
    i.imageIndex = -1; i.imageDone = 0; i.imageFailures = 0;
    i.imagePhase = Impl::ImagePhase::Opening; i.imageWaits = 0;
    veyra::log::info("image-batch", std::format("start files={} folder={}", files.size(), folder.toStdString()));
    emit imageBatchChanged();
    return true;
}
void QmlPlayerBridge::cancelImageBatch() {
    if (impl_->imagePhase == Impl::ImagePhase::Idle) return;
    impl_->imagePhase = Impl::ImagePhase::Idle;
    impl_->imageStatus = tr("已取消：完成 %1 / %2").arg(impl_->imageDone).arg(impl_->imageFiles.size());
    veyra::log::info("image-batch", "cancelled");
    emit imageBatchChanged();
}
void QmlPlayerBridge::tickImageBatch() {
    auto& i = *impl_;
    if(!i.pendingScreenshot.isEmpty()){
        const QString written=QDir::fromNativeSeparators(QString::fromStdWString(i.snapshot.savedFramePath));
        const QString hdr=QFileInfo(i.pendingScreenshot).path()+QLatin1Char('/')+QFileInfo(i.pendingScreenshot).completeBaseName()+QStringLiteral(".jxr");
        if(written==i.pendingScreenshot||written==hdr){
            i.lastScreenshot=written;i.pendingScreenshot.clear();
            veyra::log::info("ui-screenshot","saved path="+written.toStdString());
            emit preferencesChanged();
            emit notice(tr("截图保存到：%1").arg(QDir::toNativeSeparators(written)),false);
        }
        if(!i.pendingScreenshot.isEmpty()&&QDateTime::currentMSecsSinceEpoch()>i.pendingScreenshotDeadline){
            i.pendingScreenshot.clear();emit notice(tr("图片保存未确认完成，请查看诊断"),true);
        }
    }
    if(!i.pendingFrameExport.isEmpty()){
        QString path=i.pendingFrameExport;
        if(!QFileInfo(path).isFile())path=QFileInfo(path).path()+QLatin1Char('/')+QFileInfo(path).completeBaseName()+QStringLiteral(".jxr");
        const bool reported=i.snapshot.status.find(L"已保存")!=std::wstring::npos;
        if(reported&&QFileInfo(path).size()>0&&exportFileClosed(path)){
            const bool valid=path.endsWith(QStringLiteral(".jxr"),Qt::CaseInsensitive)||!QImageReader(path).read().isNull();
            if(valid){i.pendingFrameExport.clear();exportCompletionNotice(exportCompletionSound(),++i.imageCompletionEvent,"image");emit notice(tr("图片导出完成：%1").arg(path),false);}
        }
        if(!i.pendingFrameExport.isEmpty()&&QDateTime::currentMSecsSinceEpoch()>i.pendingFrameDeadline){i.pendingFrameExport.clear();emit notice(tr("图片保存未确认完成，请查看诊断"),true);}
    }
    if (i.imagePhase == Impl::ImagePhase::Idle) return;
    auto fail = [&](const QString& why) {
        ++i.imageFailures;
        veyra::log::warn("image-batch", std::format("item failed index={} reason={}", i.imageIndex, why.toStdString()));
        emit notice(tr("图片未导出：%1（%2）").arg(QFileInfo(i.imageFiles[i.imageIndex]).fileName(), why), true);
        i.imagePhase = Impl::ImagePhase::Opening; i.imageSession = 0;
    };
    const auto& s = i.snapshot;
    if (i.imagePhase == Impl::ImagePhase::Opening && i.imageSession == 0) {
        // Next item.
        if (++i.imageIndex >= i.imageFiles.size()) {
            i.imagePhase = Impl::ImagePhase::Idle;
            i.imageStatus = tr("完成 %1 张，失败 %2 张").arg(i.imageDone).arg(i.imageFailures);
            veyra::log::info("image-batch", std::format("finished done={} failed={}", i.imageDone, i.imageFailures));
            emit imageBatchChanged();
            emit notice(i.imageStatus, i.imageFailures > 0);
            if(i.imageDone>0&&i.imageFailures==0)exportCompletionNotice(exportCompletionSound(),++i.imageCompletionEvent,"image-batch");
            return;
        }
        const QString input = i.imageFiles[i.imageIndex];
        i.imageTarget = QDir(i.imageFolder).filePath(QFileInfo(input).completeBaseName() + QStringLiteral("-veyra.png"));
        i.imageStatus = tr("正在处理 %1 / %2：%3").arg(i.imageIndex + 1).arg(i.imageFiles.size()).arg(QFileInfo(input).fileName());
        emit imageBatchChanged();
        if (!QFileInfo(input).isFile()) { fail(tr("文件不存在")); return; }
        if (QFileInfo::exists(i.imageTarget)) { fail(tr("输出已存在，不覆盖")); return; }
        openPath(input);
        i.imageSession = i.openingSessionId ? i.openingSessionId : UINT64_MAX;
        i.imageWaits = 0;
        return;
    }
    ++i.imageWaits;
    if (i.imagePhase == Impl::ImagePhase::Opening) {
        if (s.sessionId == i.imageSession && s.failed) { fail(tr("无法打开")); return; }
        if (s.sessionId == i.imageSession && s.image && s.frames > 0 && !applying()) {
            impl_->engine.saveFrame(wideOf(QDir::toNativeSeparators(i.imageTarget)));
            i.imagePhase = Impl::ImagePhase::Saving; i.imageWaits = 0;
        } else if (i.imageWaits > 60 * 20) { fail(tr("打开超时")); }
        return;
    }
    // Saving: the engine writes on its own thread; HDR output becomes .jxr.
    QString written = i.imageTarget;
    if (!QFileInfo(written).isFile()) written = QFileInfo(i.imageTarget).path() + QLatin1Char('/') + QFileInfo(i.imageTarget).completeBaseName() + QStringLiteral(".jxr");
    if (QFileInfo(written).isFile() && QFileInfo(written).size() > 0 && s.status.find(L"已保存")!=std::wstring::npos && exportFileClosed(written)) {
        ++i.imageDone;
        veyra::log::info("image-batch", std::format("saved index={} path={}", i.imageIndex, written.toStdString()));
        i.imagePhase = Impl::ImagePhase::Opening; i.imageSession = 0;
        emit imageBatchChanged();
    } else if (i.imageWaits > 60 * 15) {
        fail(tr("保存超时"));
    }
}

// --- export -----------------------------------------------------------------
// Poll the export job once per UI tick. Its snapshot is the source of the
// numbers below; nothing here keeps a parallel counter that could drift.
void QmlPlayerBridge::pollExport() {
    const auto& queue=impl_->exportQueue.queue();
    const bool changed=impl_->exportQueue.tick();
    impl_->exportSnapshot=queue.snapshot();
    const auto event=queue.successEvent();
    if(event&&event!=impl_->exportNotifiedBatch){
        impl_->exportNotifiedBatch=event;
        exportCompletionNotice(exportCompletionSound(),event,"video-batch");
        emit notice(uiText(queue.status()),false);
    }
    if(changed)emit exportChanged();
}

bool QmlPlayerBridge::exportRunning() const { return impl_->exportQueue.queue().busy(); }
bool QmlPlayerBridge::exportPaused() const {
    return impl_->exportSnapshot.state == engine::ExportState::Paused;
}
double QmlPlayerBridge::exportProgress() const { return impl_->exportSnapshot.progress; }
QString QmlPlayerBridge::exportStatus() const {
    if(impl_->exportSnapshot.active())return uiText(impl_->exportSnapshot.message);
    return uiText(impl_->exportQueue.queue().status());
}
QString QmlPlayerBridge::exportTarget() const { return utf8Of(impl_->exportOutput); }
int QmlPlayerBridge::exportEncoded() const { return int(impl_->exportSnapshot.encoded); }
int QmlPlayerBridge::exportGenerated() const { return int(impl_->exportSnapshot.generated); }
double QmlPlayerBridge::exportEtaSeconds() const { return impl_->exportSnapshot.etaSeconds; }
int QmlPlayerBridge::exportQueueCount() const { return int(std::count_if(impl_->exportQueue.queue().items().begin(),impl_->exportQueue.queue().items().end(),[](const auto& i){return i.state==engine::ExportItemState::Queued;})); }
int QmlPlayerBridge::exportQueueFailures() const { return int(impl_->exportQueue.queue().failures()); }
QString QmlPlayerBridge::exportQueueFailure() const { return uiText(impl_->exportSnapshot.lastQueueFailure); }

// HEVC or H.264: the engine's export entry takes this as a flag, so it is a real
// choice with a real effect rather than a label.
bool QmlPlayerBridge::exportHevc() const { return impl_->exportHevc; }
void QmlPlayerBridge::setExportHevc(bool value) {
    if (impl_->exportHevc == value) return;
    impl_->exportHevc = value;
    emit exportChanged();
}

// Export bitrate: 0 means the encoder's constant-quality default, which is what
// the engine's exportBitrateMbps field documents.
int QmlPlayerBridge::exportBitrateMbps() const { return int(impl_->exportBitrateMbps); }
void QmlPlayerBridge::setExportBitrateMbps(int value) {
    const uint32_t want = uint32_t(std::clamp(value, 0, 300));
    if (impl_->exportBitrateMbps == want) return;
    impl_->exportBitrateMbps = want;
    emit exportChanged();
}

int QmlPlayerBridge::exportRateControl() const { return int(impl_->exportRateControl); }
void QmlPlayerBridge::setExportRateControl(int value) {
    if (value < 0 || value > 2) return;
    const auto want = static_cast<sink::ExportRateControl>(value);
    if (impl_->exportRateControl == want) return;
    impl_->exportRateControl = want;
    emit exportChanged();
}

QString QmlPlayerBridge::exportPresetName() const {
    return impl_->exportPresetName.empty() ? tr("当前播放设置") : utf8Of(impl_->exportPresetName);
}

int QmlPlayerBridge::exportSrTargetIndex() const {
    return impl_->exportResolutionIndex;
}
void QmlPlayerBridge::setExportSrTargetIndex(int index) {
    if (index < -1 || index > 6 || impl_->exportResolutionIndex == index) return;
    impl_->exportResolutionIndex = index;
    emit exportChanged();
}

double QmlPlayerBridge::exportTrimStart() const { return impl_->exportTrimStartSeconds; }
void QmlPlayerBridge::setExportTrimStart(double seconds) {
    if (!std::isfinite(seconds)) return;
    const double duration = impl_->snapshot.duration;
    const double end = impl_->exportTrimEndSeconds > 0.0 ? impl_->exportTrimEndSeconds : duration;
    const double maxStart = end > 0.05 ? end - 0.05 : 0.0;
    const double value = std::clamp(seconds, 0.0, std::max(0.0, maxStart));
    if (std::abs(impl_->exportTrimStartSeconds - value) < 0.0005) return;
    const auto selected=impl_->exportQueue.selectedId();
    if(selected&&!impl_->exportQueue.queue().setTrim(selected,value,impl_->exportTrimEndSeconds))return;
    impl_->exportTrimStartSeconds = value;
    emit exportChanged();
}

double QmlPlayerBridge::exportTrimEnd() const { return impl_->exportTrimEndSeconds; }
void QmlPlayerBridge::setExportTrimEnd(double seconds) {
    if (!std::isfinite(seconds)) return;
    const double duration = impl_->snapshot.duration;
    const double sourceEnd = duration > 0.0 ? duration : seconds;
    const double value = std::clamp(seconds, 0.0, std::max(0.0, sourceEnd));
    const double start = impl_->exportTrimStartSeconds;
    if (value > 0.0 && value <= start + 0.05) return;
    if (std::abs(impl_->exportTrimEndSeconds - value) < 0.0005) return;
    const auto selected=impl_->exportQueue.selectedId();
    if(selected&&!impl_->exportQueue.queue().setTrim(selected,impl_->exportTrimStartSeconds,value))return;
    impl_->exportTrimEndSeconds = value;
    emit exportChanged();
}

QString QmlPlayerBridge::srTargetLabel() const {
    // The label follows the SR target, because that is what sets the output size.
    switch (srTargetIndex()) {
    case 1: return QStringLiteral("2K");
    case 2: return QStringLiteral("4K");
    case 3: return QStringLiteral("8K");
    case 4: return QStringLiteral("5K");
    case 5: return QStringLiteral("6K");
    case 6: return QStringLiteral("7K");
    default: return tr("源尺寸");
    }
}

int QmlPlayerBridge::srTargetIndex() const {
    // UI IDs 1/2/3 remain 2K/4K/8K; append 4/5/6 for 5K/6K/7K.
    switch (settings().srTarget) {
    case pipeline::SrTarget::Qhd: return 1;
    case pipeline::SrTarget::Uhd4K: return 2;
    case pipeline::SrTarget::Uhd8K: return 3;
    case pipeline::SrTarget::Uhd5K: return 4;
    case pipeline::SrTarget::Uhd6K: return 5;
    case pipeline::SrTarget::Uhd7K: return 6;
    }
    return 0;
}
void QmlPlayerBridge::setSrTargetIndex(int index) {
    if(index<1||index>6)return;
    auto s = settings();
    pipeline::SrTarget want = pipeline::SrTarget::Uhd4K;
    switch (index) {
    case 1: want = pipeline::SrTarget::Qhd; break;
    case 2: want = pipeline::SrTarget::Uhd4K; break;
    case 3: want = pipeline::SrTarget::Uhd8K; break;
    case 4: want = pipeline::SrTarget::Uhd5K; break;
    case 5: want = pipeline::SrTarget::Uhd6K; break;
    case 6: want = pipeline::SrTarget::Uhd7K; break;
    default: break;
    }
    if (s.srTarget == want) return;
    s.srTarget = want;
    const auto oldOutput=s.srOutput;
    if(!impl_->commit(std::move(s)))return;
    if(oldOutput!=settings().srOutput&&settings().srOutput==pipeline::SrOutput::Uhd4K)
        emit notice(tr("已保留内部超分目标，并将三段式超分的 NR/补帧最终处理尺寸设为 4K，避免恢复高负载组合。"),false);
    emit settingsChanged();
}

QVariantList QmlPlayerBridge::presetChoices() const {
    // One list for both kinds; the label carries the distinction so the export page
    // can offer "any preset" without a second control.
    QVariantList out;
    out << QVariantMap{{"id", QStringLiteral("-1")}, {"label", tr("当前播放设置")}};
    const auto& entries = impl_->facade.presets().entries();
    for (size_t i = 0; i < entries.size(); ++i) {
        QVariantMap item;
        item["id"] = QString::number(i);
        item["label"] = utf8Of(entries[i].name)
                        + (entries[i].kind == engine::ChainMode::Node ? tr(" · 节点") : QString());
        item["disabled"] = entries[i].kind == engine::ChainMode::Node;
        out << item;
    }
    return out;
}

void QmlPlayerBridge::pauseExport(bool paused) {
    impl_->exportJob.pause(paused);
    pollExport();
}

QString QmlPlayerBridge::formatTime(double seconds) const { return mmss(seconds); }

QString QmlPlayerBridge::diagnosticsReport() const {
    // Engine-reported values only. A diagnostic that guesses is worse than none.
    const auto& s = impl_->snapshot;
    QStringList lines;
    lines << tr("状态: %1").arg(uiText(s.status));
    lines << tr("源: %1x%2 旋转 %3° 标称 %4 fps")
                 .arg(s.sourceWidth).arg(s.sourceHeight).arg(s.sourceRotationDegrees)
                 .arg(s.nominalSourceFps, 0, 'f', 2);
    lines << tr("提交 FPS: %1").arg(s.submissionFps ? QString::number(*s.submissionFps, 'f', 2) : tr("未测量"));
    lines << tr("晚点: 当前 %1 ms / P95 %2 ms").arg(s.lateMs, 0, 'f', 2).arg(s.lateP95Ms, 0, 'f', 2);
    lines << tr("调度等待 P95: %1 ms").arg(s.schedulingWaitP95Ms, 0, 'f', 2);
    lines << tr("处理 CPU P95: %1 ms").arg(s.processCpuP95Ms, 0, 'f', 2);
    lines << tr("呈现 CPU P95: %1 ms").arg(s.presentCpuP95Ms, 0, 'f', 2);
    lines << tr("帧: 已处理 %1 生成 %2").arg(s.frames).arg(s.generated);
    lines << tr("NR 求值 %1 / NVOF %2").arg(s.nrEvaluated).arg(s.nvofExecuted);
    if (s.capture) {
        lines << tr("采集: 接收 %1 丢弃 %2 速率 %3 fps")
                     .arg(s.captureReceived).arg(s.captureDropped).arg(s.captureFps, 0, 'f', 2);
    }
    if (!s.backendWarning.empty()) lines << tr("后端告警: %1").arg(uiText(s.backendWarning));
    if (!s.colorStatus.empty()) lines << tr("色彩: %1").arg(uiText(s.colorStatus));
    if (!s.videoHdrStatus.empty()) lines << tr("Video HDR: %1").arg(uiText(s.videoHdrStatus));
    return lines.join(QLatin1Char('\n'));
}


} // namespace veyra::ui
