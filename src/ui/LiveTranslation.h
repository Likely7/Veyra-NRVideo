// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "LiveAsr.h"
#include "LiveSentences.h"
#include <QCache>
#include <QElapsedTimer>
#include <QQueue>
#include <QSettings>
#include <QStringList>
#include <QTimer>
#include <deque>

namespace veyra::ui {
class LiveTranslation final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool windowsCaptionsSupported READ windowsCaptionsSupported CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int provider READ provider WRITE setProvider NOTIFY changed)
    Q_PROPERTY(int recognitionSource READ recognitionSource WRITE setRecognitionSource NOTIFY changed)
    Q_PROPERTY(int recognitionModel READ recognitionModel WRITE setRecognitionModel NOTIFY changed)
    Q_PROPERTY(int recognitionThreads READ recognitionThreads WRITE setRecognitionThreads NOTIFY changed)
    Q_PROPERTY(QString recognitionStatus READ recognitionStatus NOTIFY changed)
    Q_PROPERTY(bool recognitionInstalled READ recognitionInstalled NOTIFY changed)
    Q_PROPERTY(bool recognitionDownloading READ recognitionDownloading NOTIFY changed)
    Q_PROPERTY(double recognitionProgress READ recognitionProgress NOTIFY changed)
    Q_PROPERTY(qint64 recognitionBytes READ recognitionBytes NOTIFY changed)
    Q_PROPERTY(double recognitionRtf READ recognitionRtf NOTIFY changed)
    Q_PROPERTY(double averageTranslationMs READ averageTranslationMs NOTIFY changed)
    Q_PROPERTY(int skippedSentences READ skippedSentences NOTIFY changed)
    Q_PROPERTY(QString sourceLanguage READ sourceLanguage WRITE setSourceLanguage NOTIFY changed)
    Q_PROPERTY(QString targetLanguage READ targetLanguage WRITE setTargetLanguage NOTIFY changed)
    Q_PROPERTY(bool showOriginal READ showOriginal WRITE setShowOriginal NOTIFY changed)
    Q_PROPERTY(QString apiBase READ apiBase WRITE setApiBase NOTIFY changed)
    Q_PROPERTY(QString apiModel READ apiModel WRITE setApiModel NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString caption READ caption NOTIFY captionChanged)
    Q_PROPERTY(QString translated READ translated NOTIFY captionChanged)
    Q_PROPERTY(bool modelInstalled READ modelInstalled NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(double downloadProgress READ downloadProgress NOTIFY changed)
    Q_PROPERTY(qint64 downloadBytes READ downloadBytes CONSTANT)
public:
    enum Provider { Local, OpenAi, MyMemory };
    enum Source { VeyraAudio, WindowsCaptions };
    explicit LiveTranslation(const QString& directory, QObject* parent = nullptr);
    ~LiveTranslation() override;
    static bool supported() { return true; }
    static bool windowsCaptionsSupported();
    bool enabled() const { return enabled_; }
    int provider() const { return provider_; }
    int recognitionSource() const { return recognitionSource_; }
    int recognitionModel() const { return recognitionModel_; }
    int recognitionThreads() const { return recognitionThreads_; }
    QString recognitionStatus() const { return asr_.status(); }
    bool recognitionInstalled() const { return asr_.installed(); }
    bool recognitionDownloading() const { return asr_.downloading(); }
    double recognitionProgress() const { return asr_.progress(); }
    qint64 recognitionBytes() const { return asr_.downloadBytes(); }
    double recognitionRtf() const { return asr_.realTimeFactor(); }
    double averageTranslationMs() const { return averageTranslationMs_; }
    int skippedSentences() const { return skippedSentences_; }
    QString sourceLanguage() const;
    QString targetLanguage() const { return targetLanguage_; }
    bool showOriginal() const { return showOriginal_; }
    QString apiBase() const { return apiBase_; }
    QString apiModel() const { return apiModel_; }
    QString status() const;
    struct DisplayCaption {
        QString original;
        QString translated;
    };
    DisplayCaption displayCaption() const;
    QString caption() const { return displayCaption().original; }
    QString translated() const { return displayCaption().translated; }
    bool modelInstalled() const { return assets_.downloads.installed(); }
    bool downloading() const { return assets_.downloads.downloading(); }
    double downloadProgress() const { return double(assets_.downloads.received()) / double(downloadBytes()); }
    qint64 downloadBytes() const { return assets_.downloads.totalBytes(); }

    void setEnabled(bool value);
    void setProvider(int value);
    void setRecognitionSource(int value);
    void setRecognitionModel(int value);
    void setRecognitionThreads(int value);
    void setSourceLanguage(QString value);
    void setTargetLanguage(QString value);
    void setShowOriginal(bool value);
    void setApiBase(QString value);
    void setApiModel(QString value);
    Q_INVOKABLE void setApiKey(const QString& value);
    Q_INVOKABLE void openWindowsCaptions();
    Q_INVOKABLE void downloadModel();
    Q_INVOKABLE void cancelModelDownload();
    Q_INVOKABLE void downloadRecognitionModel();
    Q_INVOKABLE void cancelRecognitionDownload() { asr_.cancel(); }
    Q_INVOKABLE void deleteRecognitionModel();
    void setPlayback(quint64 session, bool active, bool discontinuity);
signals:
    void changed();
    void captionChanged();
private:
    struct Work {
        quint64 id = 0;
        LiveSentence sentence;
        QStringList context;
        QString cacheKey;
    };
    struct Row {
        quint64 id = 0;
        qint64 until = 0;
        qint64 originalUntil = 0;
        QString original;
        QString translated;
    };

    void setStatus(QString status);
    void configureAsr();
    void tick();
    void readCaptions();
    void acceptCaption(const QString& text);
    void addSentences(std::vector<LiveSentence> sentences);
    void displayTranslation(const Work& work, QString translation);
    void resetCaption();
    void cancelRequest();
    void discardHiddenWork();
    void startLocal();
    void stopLocal();
    void requestTranslation(Work work);
    void receiveTranslation(QNetworkReply* reply, const Work& work);
    void summarize(qint64 now);
    void prune(qint64 now);

    TranslationAssets assets_;
    LiveAsr asr_;
    QSettings settings_;
    QNetworkAccessManager remoteNetwork_, localNetwork_;
    QProcess reader_, server_;
    QTimer timer_;
    QElapsedTimer clock_;
    QNetworkReply* request_ = nullptr;
    HANDLE serverJob_ = nullptr;
    bool enabled_ = false, active_ = false, captionsOpen_ = false, serverFailed_ = false, readerStarted_ = false;
    bool showOriginal_ = true;
    int provider_ = Local;
    int recognitionSource_ = VeyraAudio, recognitionModel_ = asr::Parakeet, recognitionThreads_ = 0;
    quint16 serverPort_ = 0;
    quint64 session_ = 0, nextSentence_ = 0, requestSentence_ = 0;
    qint64 requestedAt_ = 0, serverStartedAt_ = 0, lastCaptionReadAt_ = 0, lastSummaryAt_ = 0;
    QByteArray captionBuffer_, serverKey_;
    QString raw_, status_;
    QString sourceLanguage_, targetLanguage_, apiBase_, apiModel_;
    QByteArray apiKey_;
    LiveSentences sentences_;
    QQueue<Work> queue_;
    std::deque<Row> rows_;
    QStringList context_;
    QCache<QString, QString> cache_{200};
    std::vector<qint64> requestTimes_, displayDelays_;
    int sentenceCount_ = 0, requestedCount_ = 0, cacheHits_ = 0, stale_ = 0, hidden_ = 0, failures_ = 0;
    int skippedSentences_ = 0;
    double averageTranslationMs_ = 0.0;
};
}
