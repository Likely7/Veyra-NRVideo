// SPDX-License-Identifier: GPL-3.0-only
#include "LiveAsr.h"
#include "AsrProtocol.h"
#include "LocalWorkerJob.h"
#include "veyra/Log.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <algorithm>
#include <format>
#include <utility>

namespace veyra::ui {
namespace {
constexpr uint32_t maxPcmBlockBytes = asr::AudioTap::blockFrames * sizeof(float);
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

void LiveAsr::configure(int model, const QString& language, int threads, int requestedThreads) {
    requestedThreads_ = requestedThreads;
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
        assets_.refreshInstalled();
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
    inFlightBytes_ = 0;
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
    speechText_.reset();
    emit draftText({});
    resampler_.reset();
    renderer_ = nullptr;
    lastAudioEndUs_ = 0;
    gapPending_ = false;
}

bool LiveAsr::sendStreamMarker() {
    if (sentEpoch_ == epoch_ && !gapPending_) return true;
    const auto type = sentEpoch_ != epoch_ ? asr::FrameType::Reset : asr::FrameType::Gap;
    if (process_.write(asr::frame(type, epoch_, 0)) <= 0) {
        fail(QStringLiteral("stream-marker"));
        return false;
    }
    sentEpoch_ = epoch_;
    gapPending_ = false;
    return true;
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
        if (sendStreamMarker()) setStatus(tr("播放或连接片源后开始翻译"));
        return;
    }
    attachTap();
    bufferDrops_ += tap_->takeDrops();
    asr::AudioTap::Block block;
    for (int i = 0; i < 12 && inFlightBytes_ + maxPcmBlockBytes <= asr::audioCreditBytes && tap_->pop(block); ++i) {
        const auto clockUs = sink::recordingClockUs();
        if (clockUs - block.playoutUs > asr::maxAudioAgeUs) {
            ++bufferDrops_;
            gapPending_ = true;
            continue;
        }
        const bool changed = renderer_ != block.renderer || block.format != format_;
        const bool gap = lastAudioEndUs_ && std::abs(block.playoutUs - lastAudioEndUs_) > 150000;
        if (renderer_ && changed) {
            resetStream();
            emit audioDiscontinuity();
        } else if (gap || gapPending_) {
            resampler_.reset();
            gapPending_ = true;
        }
        if (changed || !renderer_) {
            log::info("live-asr-audio", std::format("pcm rate={} channels={} layout=0x{:x} queuedMs={}",
                asr::inputRate, block.format.channels, block.format.mask, (block.playoutUs - clockUs) / 1000));
            renderer_ = block.renderer;
            format_ = block.format;
        }
        lastAudioEndUs_ = block.playoutUs + qint64(block.frames) * 1000000 / asr::inputRate;
        lastAudioAt_ = now;
        if (!sendStreamMarker()) return;
        if (!resampler_.convert(block, samples_)) { fail(QStringLiteral("resample")); return; }
        if (samples_.empty()) continue;
        const QByteArrayView pcm(reinterpret_cast<const char*>(samples_.data()), qsizetype(samples_.size() * sizeof(float)));
        if (pcm.size() > maxPcmBlockBytes) { fail(QStringLiteral("audio-window")); return; }
        inFlightBytes_ += uint32_t(pcm.size());
        peakInFlightBytes_ = std::max(peakInFlightBytes_, inFlightBytes_);
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
            workerCpuMs_ = previousWorkerCpuMs_ = event.value("cpuMs").toInteger();
            log::info("live-asr", std::format("ready model={} threads={} requestedThreads={} runtime={} loadMs={}", model_,
                event.value("threads").toInt(), requestedThreads_,
                event.value("version").toString().toStdString(), event.value("loadMs").toInteger()));
            continue;
        }
        if (type == QLatin1String("error")) {
            fail(event.value("reason").toString().left(64)
                + QStringLiteral("-%1").arg(event.value("win32").toInteger()));
            return;
        }
        if (type == QLatin1String("consumed")) {
            const auto bytes = event.value("bytes").toInteger();
            if (bytes <= 0 || bytes > inFlightBytes_) { fail(QStringLiteral("audio-credit")); return; }
            inFlightBytes_ -= uint32_t(bytes);
            if (event.value("stalePcm").toBool()) ++stalePcm_;
            skippedSegments_ += unsigned(event.value("skippedSegments").toInt());
            continue;
        }
        if (!active_ || !ready_ || event.value("epoch").toInteger() != qint64(epoch_)) continue;
        if (type == QLatin1String("language")) {
            log::info("live-asr", std::format("language candidate={} locked={}",
                event.value("candidate").toString().toStdString(), event.value("locked").toString().toStdString()));
            continue;
        }
        const bool final = type == QLatin1String("final");
        if (!final && type != QLatin1String("partial")) continue;
        const auto decodeMs = event.value("decodeMs").toInteger();
        workerCpuMs_ = event.value("cpuMs").toInteger();
        if (final) {
            if (decodeTimes_.size() < 256) decodeTimes_.push_back(decodeMs);
            audioMs_ += event.value("audioMs").toInteger();
        } else {
            ++partials_;
            partialDecodeMs_ += decodeMs;
        }
        const auto beginUs = event.value("t0").toInteger();
        const auto endUs = event.value("t1").toInteger();
        const auto ageUs = sink::recordingClockUs() - endUs;
        if (ageUs > asr::maxResultAgeUs) {
            if (final) { ++staleResults_; speechText_.reset(); emit draftText({}); }
            continue;
        }
        const auto text = event.value("text").toString();
        if (final && text.isEmpty()) ++emptyResults_;
        auto update = speechText_.update(event.value("segment").toInteger(), beginUs, endUs, text, final);
        emit draftText(update.draft);
        if (update.committed.isEmpty()) continue;
        if (final && endToResults_.size() < 256) endToResults_.push_back(ageUs / 1000);
        std::vector<SpeechMark> marks;
        const auto tokenEnds = event.value("tokenEnds").toArray();
        for (const auto& entry : tokenEnds) {
            const auto pair = entry.toArray();
            if (pair.size() != 2) continue;
            const auto offset = qsizetype(pair.at(0).toInt()) - update.offset;
            const auto wordEnd = beginUs + pair.at(1).toInteger() * 1000;
            if (offset > 0 && offset <= update.committed.size() && wordEnd >= beginUs && wordEnd <= endUs)
                marks.push_back({offset, wordEnd});
        }
        const auto language = event.value("lang").toString();
        if (qEnvironmentVariableIntValue("VEYRA_TEST_LIVE_TEXT_LOG") == 1)
            log::info("live-asr", std::format("test-text={} endUs={} delayMs={} early={} lang={}",
                update.committed.toStdString(), endUs, ageUs / 1000, !final, language.toStdString()));
        emit finalText(std::move(update.committed), language, endUs, marks, !final);
    }
}

void LiveAsr::summarize(qint64 now) {
    lastSummaryAt_ = now;
    if (decodeTimes_.empty() && !partials_ && !bufferDrops_ && !emptyResults_ && !staleResults_ && !skippedSegments_ && !stalePcm_) return;
    qint64 decodeMs = 0;
    for (const auto ms : decodeTimes_) decodeMs += ms;
    realTimeFactor_ = audioMs_ ? double(decodeMs) / double(audioMs_) : 0.0;
    std::ranges::sort(decodeTimes_);
    std::ranges::sort(endToResults_);
    log::info("live-asr", std::format(
        "segments={} audioMs={} decodeP50ms={} decodeP95ms={} resultP95ms={} rtf={:.3f} empty={} stale={} skippedSegments={} stalePcm={} peakPcmBytes={} partials={} partialDecodeMs={} workerCpuMs={}",
        decodeTimes_.size(), audioMs_, percentile(decodeTimes_, 50), percentile(decodeTimes_, 95),
        percentile(endToResults_, 95), realTimeFactor_, emptyResults_, staleResults_, skippedSegments_, stalePcm_,
        peakInFlightBytes_, partials_, partialDecodeMs_, workerCpuMs_ - previousWorkerCpuMs_));
    if (bufferDrops_) log::warn("live-asr-audio", std::format("discardedBlocks={}", bufferDrops_));
    decodeTimes_.clear();
    endToResults_.clear();
    bufferDrops_ = emptyResults_ = staleResults_ = skippedSegments_ = stalePcm_ = 0;
    peakInFlightBytes_ = inFlightBytes_;
    audioMs_ = partialDecodeMs_ = 0;
    partials_ = 0;
    previousWorkerCpuMs_ = workerCpuMs_;
    emit changed();
}
}
