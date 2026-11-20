#pragma once
#include <QObject>
#include <QByteArray>
#include <QUrl>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include "veyra/ui/PortableUpdate.h"

namespace veyra::ui {
// UI-only, bounded asynchronous metadata request; never touches the engine.
class UpdateChecker final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool checking READ checking NOTIFY checkingChanged)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY releaseChanged)
    Q_PROPERTY(QUrl releaseUrl READ releaseUrl NOTIFY releaseChanged)
    Q_PROPERTY(veyra::ui::PortableUpdate* portable READ portableUpdate CONSTANT)
public:
    explicit UpdateChecker(QString currentVersion, QObject* parent = nullptr,
        QUrl endpoint = veyra::portable::releaseApiUrl(),
        int timeoutMs = 12000);
    ~UpdateChecker() override;
    bool checking() const { return reply_ != nullptr; }
    QString latestVersion() const { return latestVersion_; }
    QUrl releaseUrl() const { return releaseUrl_; }
    PortableUpdate* portableUpdate() { return &portable_; }
    void setDownloadRoute(veyra::portable::DownloadRoute route);
    Q_INVOKABLE void startAutomaticCheck();
    Q_INVOKABLE void check(bool manual = true);
    Q_INVOKABLE void openRelease();
signals:
    void checkingChanged();
    void releaseChanged();
    void updateAvailable(const QString& version);
    void notice(const QString& message, bool isError);
    // Terminal diagnostic result (also used by deterministic/real HTTP tests).
    void completed(const QString& result);
private:
    void request(const QUrl& url);
    void finish();
    QNetworkAccessManager network_;
    QTimer deadline_;
    QNetworkReply* reply_ = nullptr;
    QString currentVersion_, latestVersion_;
    QUrl endpoint_, releaseUrl_;
    QByteArray body_;
    QString failure_;
    int timeoutMs_;
    bool automaticScheduled_ = false, manual_ = false;
    bool usingMirror_ = false;
    veyra::portable::DownloadRoute route_ = veyra::portable::DownloadRoute::GitHub;
    PortableUpdate portable_;
};
}
