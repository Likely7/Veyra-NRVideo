#pragma once

#include "veyra/base/PortableUpdateManifest.h"

#include <QNetworkAccessManager>
#include <QProcess>
#include <QTimer>
#include <filesystem>

class QThread;

namespace veyra::ui {

class PortableUpdate final : public QObject {
    Q_OBJECT
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool downloading READ downloading NOTIFY changed)
    Q_PROPERTY(bool installing READ installing NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool restarting READ restarting NOTIFY changed)
    Q_PROPERTY(int progress READ progress NOTIFY changed)
    Q_PROPERTY(int installStep READ installStep NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(QString targetVersion READ targetVersion NOTIFY changed)
    Q_PROPERTY(QString sizeText READ sizeText NOTIFY changed)
    Q_PROPERTY(QString releaseNotes READ releaseNotes NOTIFY changed)
public:
    explicit PortableUpdate(const QString& version, QObject* parent = nullptr);
    ~PortableUpdate() override;

    bool available() const { return package_.has_value() && !downloading() && !installing_ && !ready_; }
    bool downloading() const { return download_ != nullptr; }
    bool installing() const { return installing_; }
    bool ready() const { return ready_; }
    bool restarting() const { return restarting_; }
    int progress() const;
    int installStep() const { return installStep_; }
    QString errorText() const { return errorText_; }
    QString targetVersion() const { return package_ ? package_->version : QString{}; }
    QString sizeText() const;
    QString releaseNotes() const;
    void setDownloadRoute(portable::DownloadRoute route) { route_ = route; }
    void selectRelease(const QString& version);
    Q_INVOKABLE void start();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void restart();
    bool beginRestart(const QStringList& launcherArguments);

signals:
    void changed();
    void installationReady();
    void restartRequested();
    void notice(const QString& message, bool error);

private:
    void requestManifest(portable::DownloadRoute route);
    void requestArchive(portable::DownloadRoute route);
    bool consumeManifest();
    void finishManifest();
    void discardDownload();
    bool hasDownloadedArchive() const;
    bool hasPreparedVersion() const;
    void prepareInstallation();
    void finishInstallation(bool success, const QByteArray& result);
    void reportError(const QString& message);
    std::filesystem::path updates_;
    QString workerPath_;
    QString installed_, flavor_, selectedVersion_, errorText_;
    QByteArray key_, manifest_, response_;
    std::optional<portable::UpdatePackage> package_;
    QNetworkAccessManager network_;
    QProcess worker_;
    QTimer manifestDeadline_;
    QNetworkReply* reply_ = nullptr;
    QThread* download_ = nullptr;
    quint64 downloadGeneration_ = 0;
    int progress_ = 0;
    portable::DownloadRoute route_ = portable::DownloadRoute::GitHub;
    portable::DownloadRoute requestRoute_ = portable::DownloadRoute::GitHub;
    int installStep_ = 0;
    bool installing_ = false, ready_ = false, restarting_ = false;
    bool cancelled_ = false, failed_ = false;
};

} // namespace veyra::ui
