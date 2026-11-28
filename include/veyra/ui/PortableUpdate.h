#pragma once

#include "veyra/base/PortableUpdateManifest.h"

#include <QHash>
#include <QNetworkAccessManager>
#include <QSaveFile>
#include <QTimer>
#include <QWinEventNotifier>
#include <filesystem>
#include <memory>

namespace veyra::ui {

struct PortableArchiveHasher;

class PortableUpdate final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(bool scheduled READ scheduled NOTIFY changed)
    Q_PROPERTY(bool installing READ installing NOTIFY changed)
    Q_PROPERTY(bool authorizing READ authorizing NOTIFY changed)
    Q_PROPERTY(bool needsElevation READ needsElevation NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
    Q_PROPERTY(QString blockReason READ blockReason NOTIFY changed)
public:
    explicit PortableUpdate(const QString& version, QObject* parent = nullptr);
    ~PortableUpdate() override;

    bool available() const { return package_.has_value() && !downloading() && !scheduled_ && !installing_ && !authorizing_; }
    bool downloading() const { return download_ != nullptr; }
    bool scheduled() const { return scheduled_; }
    bool installing() const { return installing_; }
    bool authorizing() const { return authorizing_; }
    bool needsElevation() const { return elevate_; }
    // Blocks until the elevated worker exits. The launcher lock stays held for
    // that whole interval, so a second install cannot start beside it.
    void waitForElevatedInstall();
    int progress() const;
    QString sizeText() const;
    QString releaseNotes() const;
    QString blockReason() const { return blockReason_; }
    void setDownloadRoute(portable::DownloadRoute route) { route_ = route; }
    void setReleaseInventory(bool enumerated, const QHash<QString, QByteArray>& digests);
    void selectRelease(const QString& version);
    // Runs the already-verified worker elevated. The caller must invoke this
    // only from an explicit user action. UAC is Windows' own prompt.
    void beginElevatedInstall();
    Q_INVOKABLE void downloadAndSchedule();
    Q_INVOKABLE void cancel();

signals:
    void changed();
    void notice(const QString& message, bool error);
    void installFinished(bool ok);

private:
    void requestManifest(portable::DownloadRoute route);
    bool requestArchive(portable::DownloadRoute route);
    void request(const QUrl& url, int buffer, int idleMs);
    bool consume();
    void finish();
    void discardDownload();
    bool inventoryAccepts(const QByteArray& manifestBytes, const portable::UpdatePackage& package) const;
    bool prepareDownloadDirectory();
    std::filesystem::path root_, worker_, updates_;
    QString installed_, flavor_, selectedVersion_, blockReason_;
    QByteArray key_, manifest_, response_;
    QHash<QString, QByteArray> published_;
    std::optional<portable::UpdatePackage> package_;
    QNetworkAccessManager network_;
    QTimer manifestDeadline_;
    QWinEventNotifier elevateWait_;
    QNetworkReply* reply_ = nullptr;
    std::unique_ptr<QSaveFile> download_;
    qint64 received_ = 0;
    portable::DownloadRoute route_ = portable::DownloadRoute::GitHub;
    portable::DownloadRoute requestRoute_ = portable::DownloadRoute::GitHub;
    bool scheduled_ = false, cancelled_ = false, failed_ = false;
    bool assetsEnumerated_ = false, elevate_ = false, installing_ = false, authorizing_ = false;
    std::unique_ptr<PortableArchiveHasher> hasher_;
};

} // namespace veyra::ui
