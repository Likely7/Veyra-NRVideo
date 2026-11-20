// SPDX-License-Identifier: GPL-3.0-only
#include "LiveAsr.h"
#include "AsrProtocol.h"
#include "LocalWorkerJob.h"
#include "veyra/Log.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <format>
#include <utility>

namespace veyra::ui {
namespace {
constexpr qint64 maxPipeBytes = 8 * 1024;
constexpr qint64 maxBacklogUs = 500000;
constexpr qint64 maxResultAgeUs = 4000000;
constexpr qint64 summaryIntervalMs = 10000;

qint64 percentile(const std::vector<qint64>& sorted, unsigned percent) {
    if (sorted.empty()) return 0;
    const size_t rank = (sorted.size() * percent + 99) / 100;
    return sorted[rank - 1];
}
}

LiveAsr::LiveAsr(const QString& directory)
    : root_(asr::root(directory)), assets_(root_, asr::assets(asr::Parakeet)) {
    clock_.start();
    timer_.setInterval(20);
    process_.setStandardErrorFile(QProcess::nullDevice());
    process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) {
        args->flags |= CREATE_NO_WINDOW | BELOW_NORMAL_PRIORITY_CLASS;
    });
    connect(&assets_, &ModelAssets::changed, this, [this] {
        if (!assets_.downloading() && assets_.installed() && process_.state() == QProcess::NotRunning)
            failed_ = false;
        emit changed();
    });
    connect(&process_, &QProcess::started, this, [this] {
        if (!enabled_ || stopping_) { process_.kill(); return; }
        job_ = attachLocalWorkerJob(DWORD(process_.processId()));
        if (!job_) { fail(QStringLiteral("job")); return; }
        sentEpoch_ = epoch_;
        const auto settings = QJsonDocument(QJsonObject{{"folder", root_}, {"model", model_},
            {"language", language_}, {"threads", threads_}}).toJson(QJsonDocument::Compact);
        if (process_.write(asr::frame(asr::FrameType::Config, sentEpoch_, 0, settings)) <= 0)
            fail(QStringLiteral("pipe"));
    });
    connect(&process_, &QProcess::readyReadStandardOutput, this, &LiveAsr::readOutput);
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (enabled_ && !stopping_ && error == QProcess::FailedToStart) {
            fail(QStringLiteral("process-start"));
            stopping_ = false;
        }
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus) {
        if (job_) CloseHandle(std::exchange(job_, nullptr));
        ready_ = false;
        if (std::exchange(stopping_, false) || !enabled_ || failed_) return;
        fail(QStringLiteral("process-exit-%1").arg(code));
        stopping_ = false;
    });
    connect(&timer_, &QTimer::timeout, this, &LiveAsr::tick);
}

LiveAsr::~LiveAsr() {
    process_.disconnect(this);
    setEnabled(false);
    if (process_.state() != QProcess::NotRunning) process_.waitForFinished(1500);
}

void LiveAsr::configure(int model, const QString& language, int threads) {
    if (asr::assets(model).empty() || (model_ == model && language_ == language && threads_ == threads)) return;
    if (model_ != model && !assets_.setAssets(asr::assets(model))) return;
    model_ = model;
    realTimeFactor_ = 0.0;
    language_ = language;
    threads_ = threads;
    stop();
    failed_ = false;
    emit changed();
}

void LiveAsr::setEnabled(bool enabled) {
    if (enabled_ == enabled) return;
    enabled_ = enabled;
    failed_ = false;
    if (enabled_) {
        nextAssetCheckAt_ = 0;
        lastSummaryAt_ = clock_.elapsed();
        timer_.start();
    } else {
        timer_.stop();
        summarize(clock_.elapsed());
        stop();
        setStatus({});
    }
}

void LiveAsr::setPlayback(bool active, bool discontinuity) {
    if (active_ == active && !discontinuity) return;
    active_ = active;
    detachTap();
    resetStream();
    if (active_ && enabled_ && ready_) attachTap();
}

QString LiveAsr::status() const {
    if (assets_.downloading() || (!assets_.installed() && !assets_.status().isEmpty())) return assets_.status();
    return status_;
}

void LiveAsr::download() {
    if (process_.state() == QProcess::NotRunning) assets_.download();
}
void LiveAsr::removeModel() {
    if (!enabled_ && process_.state() == QProcess::NotRunning)
        assets_.remove(asr::assets(model_)[2]);
}

void LiveAsr::setStatus(QString value) {
    if (value == status_) return;
    status_ = std::move(value);
    emit changed();
}

void LiveAsr::start() {
    const auto selected = asr::assets(model_);
    if (!ModelAssets::verify(root_, selected[0], true)) {
        fail(QStringLiteral("runtime-verification"));
        return;
    }
    ready_ = false;
    stopping_ = false;
    lines_.clear();
    startedAt_ = clock_.elapsed();
    process_.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--asr-worker")});
    setStatus(tr("正在加载本地语音识别模型…"));
}

void LiveAsr::stop() {
    detachTap();
    resetStream();
    ready_ = false;
    stopping_ = process_.state() != QProcess::NotRunning;
    if (job_) CloseHandle(std::exchange(job_, nullptr));
    if (stopping_) process_.kill();
    lines_.clear();
}

void LiveAsr::attachTap() {
    if (tap_) return;
    tap_ = std::make_shared<asr::AudioTap>();
    sink::addRecordingAudioSink(tap_);
    lastAudioAt_ = clock_.elapsed();
    log::info("live-asr-audio", "tap attached");
}

void LiveAsr::detachTap() {
    if (!tap_) return;
    sink::removeRecordingAudioSink(tap_.get());
    bufferDrops_ += tap_->takeDrops();
    tap_.reset();
    log::info("live-asr-audio", "tap detached");
}

void LiveAsr::resetStream() {
    ++epoch_;
    resampler_.reset();
    renderer_ = nullptr;
    lastAudioEndUs_ = 0;
}

void LiveAsr::fail(const QString& reason) {
    log::error("live-asr", std::format("worker failure reason={}", reason.toStdString()));
    failed_ = true;
    stop();
    if (reason.startsWith(QLatin1String("asset-verification")) || reason.startsWith(QLatin1String("runtime-verification")))
        setStatus(tr("语音识别文件校验失败，请重新下载"));
    else
        setStatus(tr("本地语音识别停止，请重新启用"));
    emit audioDiscontinuity();
}

void LiveAsr::tick() {
    const auto now = clock_.elapsed();
    if (now - lastSummaryAt_ >= summaryIntervalMs) summarize(now);
    if (!enabled_ || stopping_ || failed_) return;
    if (assets_.downloading()) return;
    if (process_.state() == QProcess::NotRunning) {
        if (now < nextAssetCheckAt_) return;
        nextAssetCheckAt_ = now + 500;
        if (!assets_.installed()) { setStatus(tr("请先下载语音识别模型")); return; }
        // Warm the recognizer as soon as the user enables it. This can take
        // seconds on a cold disk. PCM is attached only once playback starts.
        start();
        return;
    }
    if (!ready_) {
        if (now - startedAt_ > 45000) fail(QStringLiteral("startup-timeout"));
        return;
    }
    if (!active_) {
        setStatus(tr("播放或连接片源后开始翻译"));
        return;
    }
    attachTap();
    if (const auto dropped = tap_->takeDrops()) {
        bufferDrops_ += dropped;
        resetStream();
        emit audioDiscontinuity();
    }
    asr::AudioTap::Block block;
    for (int i = 0; i < 12 && process_.bytesToWrite() < maxPipeBytes && tap_->pop(block); ++i) {
        const auto clockUs = sink::recordingClockUs();
        if (clockUs - block.playoutUs > maxBacklogUs) {
            ++bufferDrops_;
            continue;
        }
        const bool changed = renderer_ != block.renderer || block.format != format_;
        const bool gap = lastAudioEndUs_ && std::abs(block.playoutUs - lastAudioEndUs_) > 150000;
        if (renderer_ && (changed || gap)) {
            resetStream();
            emit audioDiscontinuity();
        }
        if (changed || !renderer_) {
            log::info("live-asr-audio", std::format("pcm rate={} channels={} layout=0x{:x} queuedMs={}",
                asr::inputRate, block.format.channels, block.format.mask, (block.playoutUs - clockUs) / 1000));
            renderer_ = block.renderer;
            format_ = block.format;
        }
        lastAudioEndUs_ = block.playoutUs + qint64(block.frames) * 1000000 / asr::inputRate;
        lastAudioAt_ = now;
        if (!resampler_.convert(block, samples_)) { fail(QStringLiteral("resample")); return; }
        if (samples_.empty()) continue;
        if (sentEpoch_ != epoch_) {
            if (process_.write(asr::frame(asr::FrameType::Reset, epoch_, 0)) <= 0) {
                fail(QStringLiteral("reset-pipe"));
                return;
            }
            sentEpoch_ = epoch_;
        }
        const QByteArrayView pcm(reinterpret_cast<const char*>(samples_.data()), qsizetype(samples_.size() * sizeof(float)));
        if (process_.write(asr::frame(asr::FrameType::Audio, epoch_, block.playoutUs, pcm)) <= 0) {
            fail(QStringLiteral("audio-pipe"));
            return;
        }
    }
    setStatus(now - lastAudioAt_ > 2000 ? tr("当前片源没有可识别的 PCM 音频")
                                       : tr("正在识别 Veyra 音频…"));
}

void LiveAsr::readOutput() {
    lines_ += process_.readAllStandardOutput();
    if (lines_.size() > 256 * 1024) { fail(QStringLiteral("output-size")); return; }
    for (qsizetype end; (end = lines_.indexOf('\n')) >= 0;) {
        const auto event = QJsonDocument::fromJson(lines_.first(end)).object();
        lines_.remove(0, end + 1);
        if (!enabled_ || stopping_ || failed_) continue;
        const auto type = event.value("type").toString();
        if (type == QLatin1String("ready")) {
            ready_ = true;
            log::info("live-asr", std::format("ready model={} threads={} runtime={} loadMs={}", model_, threads_,
                event.value("version").toString().toStdString(), event.value("loadMs").toInteger()));
            continue;
        }
        if (type == QLatin1String("error")) {
            fail(event.value("reason").toString().left(64)
                + QStringLiteral("-%1").arg(event.value("win32").toInteger()));
            return;
        }
        if (!active_ || !ready_ || type != QLatin1String("final") || event.value("epoch").toInteger() != qint64(epoch_))
            continue;
        if (decodeTimes_.size() < 256) decodeTimes_.push_back(event.value("decodeMs").toInteger());
        audioMs_ += event.value("audioMs").toInteger();
        const auto endUs = event.value("t1").toInteger();
        const auto ageUs = sink::recordingClockUs() - endUs;
        if (ageUs > maxResultAgeUs) { ++staleResults_; continue; }
        const auto text = event.value("text").toString();
        if (text.isEmpty()) { ++emptyResults_; continue; }
        if (endToResults_.size() < 256) endToResults_.push_back(ageUs / 1000);
        if (qEnvironmentVariableIntValue("VEYRA_TEST_LIVE_TEXT_LOG") == 1)
            log::info("live-asr", std::format("test-text={} endUs={} delayMs={}", text.toStdString(), endUs, ageUs / 1000));
        emit finalText(text, event.value("lang").toString(), endUs);
    }
}

void LiveAsr::summarize(qint64 now) {
    lastSummaryAt_ = now;
    if (decodeTimes_.empty() && !bufferDrops_ && !emptyResults_ && !staleResults_) return;
    qint64 decodeMs = 0;
    for (const auto ms : decodeTimes_) decodeMs += ms;
    realTimeFactor_ = audioMs_ ? double(decodeMs) / double(audioMs_) : 0.0;
    std::ranges::sort(decodeTimes_);
    std::ranges::sort(endToResults_);
    log::info("live-asr", std::format("segments={} audioMs={} decodeP50ms={} decodeP95ms={} resultP95ms={} rtf={:.3f} empty={} stale={}",
        decodeTimes_.size(), audioMs_, percentile(decodeTimes_, 50), percentile(decodeTimes_, 95),
        percentile(endToResults_, 95), realTimeFactor_, emptyResults_, staleResults_));
    if (bufferDrops_) log::warn("live-asr-audio", std::format("discardedBlocks={}", bufferDrops_));
    decodeTimes_.clear();
    endToResults_.clear();
    bufferDrops_ = emptyResults_ = staleResults_ = 0;
    audioMs_ = 0;
    emit changed();
}
}
