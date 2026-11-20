#pragma once

#include "veyra/base/PortableUpdateManifest.h"

#include <QCryptographicHash>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QTimer>
#include <filesystem>
#include <memory>

namespace veyra::ui {

class PortableUpdate final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(bool scheduled READ scheduled NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
public:
    explicit PortableUpdate(const QString& version, QObject* parent = nullptr);
    ~PortableUpdate() override;

    bool available() const { return package_.has_value() && !downloading() && !scheduled_; }
    bool downloading() const { return download_ != nullptr; }
    bool scheduled() const { return scheduled_; }
    int progress() const;
    QString sizeText() const;
    QString releaseNotes() const;
    void setDownloadRoute(portable::DownloadRoute route) { route_ = route; }
    void selectRelease(const QString& version);
    Q_INVOKABLE void downloadAndSchedule();
    Q_INVOKABLE void cancel();

signals:
    void changed();
    void notice(const QString& message, bool error);

private:
    void requestManifest(portable::DownloadRoute route);
    bool requestArchive(portable::DownloadRoute route);
    void request(const QUrl& url, int buffer, int idleMs);
    bool consume();
    void finish();
    void discardDownload();
    std::filesystem::path updates_;
    QString installed_, flavor_, selectedVersion_;
    QByteArray key_, manifest_, response_;
    std::optional<portable::UpdatePackage> package_;
    QNetworkAccessManager network_;
    QTimer manifestDeadline_;
    QNetworkReply* reply_ = nullptr;
    std::unique_ptr<QSaveFile> download_;
    QCryptographicHash hash_{QCryptographicHash::Sha256};
    qint64 received_ = 0;
    portable::DownloadRoute route_ = portable::DownloadRoute::GitHub;
    portable::DownloadRoute requestRoute_ = portable::DownloadRoute::GitHub;
    bool scheduled_ = false, cancelled_ = false, failed_ = false;
};

} // namespace veyra::ui
