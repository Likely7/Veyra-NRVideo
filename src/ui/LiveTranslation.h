#pragma once

#include "TranslationAssets.h"
#include <QElapsedTimer>
#include <QSettings>
#include <QTimer>

namespace veyra::ui {
class LiveTranslation final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool supported READ supported CONSTANT)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int provider READ provider WRITE setProvider NOTIFY changed)
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
    explicit LiveTranslation(const QString& directory, QObject* parent = nullptr);
    ~LiveTranslation() override;
    static bool supported();
    bool enabled() const { return enabled_; }
    int provider() const { return provider_; }
    QString sourceLanguage() const { return sourceLanguage_; }
    QString targetLanguage() const { return targetLanguage_; }
    bool showOriginal() const { return showOriginal_; }
    QString apiBase() const { return apiBase_; }
    QString apiModel() const { return apiModel_; }
    QString status() const;
    QString caption() const { return caption_; }
    QString translated() const { return translated_; }
    bool modelInstalled() const { return assets_.installed(); }
    bool downloading() const { return assets_.downloading(); }
    double downloadProgress() const { return double(assets_.received()) / double(assets_.totalBytes()); }
    qint64 downloadBytes() const { return assets_.totalBytes(); }

    void setEnabled(bool value);
    void setProvider(int value);
    void setSourceLanguage(QString value);
    void setTargetLanguage(QString value);
    void setShowOriginal(bool value);
    void setApiBase(QString value);
    void setApiModel(QString value);
    Q_INVOKABLE void setApiKey(const QString& value);
    Q_INVOKABLE void openWindowsCaptions();
    Q_INVOKABLE void downloadModel();
    Q_INVOKABLE void cancelModelDownload();
    void setPlayback(quint64 session, bool active, bool discontinuity);
signals:
    void changed();
    void captionChanged();
private:
    void setStatus(QString status);
    void tick();
    void readCaptions();
    void acceptCaption(const QString& text);
    void resetCaption();
    void cancelRequest();
    void startLocal();
    void stopLocal();
    void requestTranslation();
    void receiveTranslation(QNetworkReply* reply, const QString& text);

    TranslationAssets assets_;
    QSettings settings_;
    QNetworkAccessManager remoteNetwork_, localNetwork_;
    QProcess reader_, server_;
    QTimer timer_;
    QElapsedTimer clock_;
    QNetworkReply* request_ = nullptr;
    void* serverJob_ = nullptr;
    bool enabled_ = false, active_ = false, captionsOpen_ = false, serverFailed_ = false, readerStarted_ = false;
    bool showOriginal_ = true;
    int provider_ = Local;
    quint16 serverPort_ = 0;
    quint64 session_ = 0;
    qint64 newTextAt_ = 0, requestedAt_ = 0, serverStartedAt_ = 0, lastCaptionReadAt_ = 0;
    QByteArray captionBuffer_, serverKey_;
    QString raw_, caption_, translated_, requestedText_, status_;
    QString sourceLanguage_, targetLanguage_, apiBase_, apiModel_;
    QByteArray apiKey_;
};
}
