#pragma once

#include "TranslationAssets.h"
#include <QElapsedTimer>
#include <QHash>
#include <QRectF>
#include <QSettings>
#include <QStringList>
#include <QTimer>

namespace veyra::ui {
class LiveTranslation final : public QObject {
    Q_OBJECT
    // supported: the selected caption source works on this Windows.
    Q_PROPERTY(bool supported READ sourceSupported NOTIFY changed)
    Q_PROPERTY(bool liveCaptionsSupported READ supported CONSTANT)
    Q_PROPERTY(bool screenTextSupported READ screenTextSupported CONSTANT)
    Q_PROPERTY(int captionSource READ captionSource WRITE setCaptionSource NOTIFY changed)
    Q_PROPERTY(int screenRegion READ screenRegion WRITE setScreenRegion NOTIFY changed)
    Q_PROPERTY(QRectF customRegion READ customRegion WRITE setCustomRegion NOTIFY changed)
    // The last read of the on-screen source (a preview while ScreenText is on).
    Q_PROPERTY(QString screenReading READ screenReading NOTIFY previewChanged)
    Q_PROPERTY(int screenWords READ screenWords NOTIFY previewChanged)
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
    // Where the original text comes from: what Windows Live Captions hears, or the
    // subtitles drawn on the picture (capture card, PS5/Xbox/PC streaming), read by OCR.
    enum CaptionSource { WindowsCaptions, ScreenText };
    // The part of the picture read for ScreenText.
    enum ScreenRegion { BottomSubtitles, BottomWide, TopSubtitles, WholePicture, CustomRegion };
    explicit LiveTranslation(const QString& directory, QObject* parent = nullptr);
    ~LiveTranslation() override;
    static bool supported();           // Windows Live Captions is available
    static bool screenTextSupported(); // Windows OCR is available (Windows 10 or later)
    bool sourceSupported() const;
    int captionSource() const { return captionSource_; }
    int screenRegion() const { return screenRegion_; }
    QRectF customRegion() const { return customRegion_; }
    // What the engine reads while ScreenText is on (QmlPlayerBridge polls these).
    bool screenTextWanted() const { return enabled_ && captionSource_ == ScreenText; }
    QRectF screenTextRect() const;
    bool screenTextCentered() const;
    QString screenTextLanguage() const;
    int screenTextIntervalMs() const;
    QString screenReading() const { return screenReading_; }
    int screenWords() const { return screenWords_; }
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
    void setCaptionSource(int value);
    void setScreenRegion(int value);
    void setCustomRegion(QRectF value);
    // The engine's latest settled on-screen line (empty: none), whether its OCR is
    // ready, and why not; plus the last read for the preview (what it found, words seen).
    // Ignored unless ScreenText is the enabled source.
    void acceptScreenText(const QString& text, bool ready, const QString& error,
                          const QString& reading = {}, int words = 0);
    Q_INVOKABLE void setApiKey(const QString& value);
    Q_INVOKABLE void openWindowsCaptions();
    Q_INVOKABLE void downloadModel();
    Q_INVOKABLE void cancelModelDownload();
    void setPlayback(quint64 session, bool active, bool discontinuity);
signals:
    void changed();
    void captionChanged();
    void previewChanged();
private:
    void setStatus(QString status);
    void tick();
    void readCaptions();
    void acceptCaption(const QString& text);
    // keepRequest: a translation already on its way is left to finish (on-screen source).
    void resetCaption(bool keepRequest = false);
    void cancelRequest();
    void startLocal();
    void stopLocal();
    void requestTranslation();
    void receiveTranslation(QNetworkReply* reply, const QString& text);
    QString cacheKey(const QString& text) const;
    void remember(const QString& text, const QString& translation);

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
    int captionSource_ = WindowsCaptions;
    int screenRegion_ = BottomSubtitles;
    QRectF customRegion_{0.08, 0.70, 0.84, 0.27};
    QString screenError_, screenReading_;
    int screenWords_ = 0, retries_ = 0;
    // Recent translations: game lines repeat (hints, menus, the same speaker), and a
    // repeated line is shown at once instead of being sent again. Bounded.
    QHash<QString, QString> cache_;
    QStringList cacheOrder_;
    quint16 serverPort_ = 0;
    quint64 session_ = 0;
    qint64 newTextAt_ = 0, requestedAt_ = 0, serverStartedAt_ = 0, lastCaptionReadAt_ = 0;
    QByteArray captionBuffer_, serverKey_;
    QString raw_, caption_, translated_, requestedText_, status_;
    QString sourceLanguage_, targetLanguage_, apiBase_, apiModel_;
    QByteArray apiKey_;
};
}
