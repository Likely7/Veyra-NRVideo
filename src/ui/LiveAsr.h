// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "AsrAudioTap.h"
#include "AsrModels.h"

#include <QElapsedTimer>
#include <QTimer>

namespace veyra::ui {
class LiveAsr final : public QObject {
    Q_OBJECT
public:
    explicit LiveAsr(const QString& directory);
    ~LiveAsr() override;
    void configure(int model, const QString& language, int threads);
    void setEnabled(bool enabled);
    void setPlayback(bool active, bool discontinuity);
    QString status() const;
    double realTimeFactor() const { return realTimeFactor_; }
    bool installed() const { return assets_.installed(); }
    bool downloading() const { return assets_.downloading(); }
    qint64 downloadBytes() const { return assets_.totalBytes(); }
    double progress() const { return double(assets_.received()) / double(assets_.totalBytes()); }
    void download();
    void cancel() { assets_.cancel(); }
    void removeModel();

signals:
    void changed();
    void finalText(QString text, QString language, qint64 playoutEndUs);
    void audioDiscontinuity();

private:
    void tick();
    void start();
    void stop();
    void readOutput();
    void detachTap();
    void attachTap();
    void resetStream();
    void fail(const QString& reason);
    void setStatus(QString value);
    void summarize(qint64 now);

    const QString root_;
    int model_ = asr::Parakeet;
    QString language_;
    int threads_ = 1;
    ModelAssets assets_;
    QProcess process_;
    HANDLE job_ = nullptr;
    QTimer timer_;
    QElapsedTimer clock_;
    QString status_;
    QByteArray lines_;
    bool enabled_ = false, active_ = false, ready_ = false, failed_ = false, stopping_ = false;
    qint64 startedAt_ = 0, lastAudioAt_ = 0, lastSummaryAt_ = 0, nextAssetCheckAt_ = 0;
    qint64 lastAudioEndUs_ = 0;
    uint64_t epoch_ = 1, sentEpoch_ = 0;
    const void* renderer_ = nullptr;
    sink::AudioFormat format_;
    std::shared_ptr<asr::AudioTap> tap_;
    asr::Resampler resampler_;
    std::vector<float> samples_;
    std::vector<qint64> decodeTimes_;
    std::vector<qint64> endToResults_;
    unsigned bufferDrops_ = 0, emptyResults_ = 0, staleResults_ = 0;
    qint64 audioMs_ = 0;
    double realTimeFactor_ = 0.0;
};
}
