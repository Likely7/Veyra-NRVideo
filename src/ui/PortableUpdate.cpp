#include "veyra/ui/PortableUpdate.h"
#include "veyra/base/PortableFiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QNetworkReply>
#include <QStorageInfo>

namespace veyra::ui {
using namespace veyra::portable;
namespace {
QString pathString(const std::filesystem::path& path) { return QString::fromStdWString(path.native()); }
}

PortableUpdate::PortableUpdate(const QString& version, QObject* parent)
    : QObject(parent), installed_(releaseVersion(version)), key_(updatePublicKey()) {
    if (!QCoreApplication::arguments().contains(QStringLiteral("--managed-portable"))
        || installed_.isEmpty() || key_.size() != 32) return;
    const auto directory = std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString());
    const auto root = rootFromVersionDirectory(directory);
    if (root.empty() || !plainPath(directory / workerName, false)
        || directory.filename().wstring() != std::filesystem::path(installedVersions(root).current).wstring()) return;
    flavor_ = QString::fromStdString(packageFlavor(root));
    updates_ = root / updateFolder;
    workerPath_ = pathString(directory / workerName);
    worker_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags |= CREATE_NO_WINDOW;
    });
    connect(&worker_, &QProcess::readyReadStandardOutput, this, [this] {
        while (worker_.canReadLine()) {
            bool valid = false;
            const int step = worker_.readLine().trimmed().toInt(&valid);
            if (valid && step > installStep_ && step <= 3) {
                installStep_ = step;
                qInfo() << "[portable-update-install-step]" << step;
                emit changed();
            }
        }
    });
    connect(&worker_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        const auto result = worker_.readAllStandardError().trimmed().split('\n').last().trimmed();
        finishInstallation(code == 0 && status == QProcess::NormalExit, result);
    });
    connect(&worker_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) finishInstallation(false, "worker-start-failed");
    });
    manifestDeadline_.setSingleShot(true);
    connect(&manifestDeadline_, &QTimer::timeout, this, [this] { if (reply_) reply_->abort(); });

    if (!plainPath(updates_, true)) return;
    const auto status = updates_ / L"result.txt";
    if (plainPath(status, false)) {
        const auto result = readSmallFile(status);
        DeleteFileW(status.c_str());
        if (!result.empty()) QTimer::singleShot(0, this, [this, result] {
            qInfo().noquote() << "[portable-update-result]" << QString::fromStdString(result).trimmed();
            if (result == "installed\n") emit notice(tr("Veyra 已更新"), false);
            else if (result == "windows-blocked\n") emit notice(tr("Windows 已阻止本次更新，请查看保护历史"), true);
            else emit notice(tr("更新未完成，已打开可用版本"), true);
        });
    }

    const auto pending = updates_ / L"pending.json";
    if (!plainPath(pending, false)) return;
    manifest_ = QByteArray::fromStdString(readSmallFile(pending, maxManifestBytes));
    package_ = readUpdateManifest(manifest_, flavor_, installed_, {}, key_);
    if (!package_) return;
    ready_ = hasPreparedVersion();
    if (!ready_ && !hasDownloadedArchive()) package_.reset();
}

PortableUpdate::~PortableUpdate() {
    worker_.disconnect(this);
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
}

int PortableUpdate::progress() const {
    return package_ ? int(received_ * 100 / package_->archiveBytes) : 0;
}

QString PortableUpdate::sizeText() const {
    return package_ ? QLocale().formattedDataSize(package_->archiveBytes) : QString{};
}

QString PortableUpdate::releaseNotes() const { return package_ ? package_->notes : QString{}; }

void PortableUpdate::selectRelease(const QString& version) {
    if (updates_.empty() || reply_ || installing_ || ready_ || (package_ && package_->version == version)) return;
    package_.reset();
    manifest_.clear();
    errorText_.clear();
    selectedVersion_ = version;
    if (!version.isEmpty()) requestManifest(route_);
    emit changed();
}

void PortableUpdate::requestManifest(DownloadRoute route) {
    requestRoute_ = route;
    request(assetUrl(selectedVersion_, QString::fromLatin1(manifestAsset), route), int(maxManifestBytes + 1), 12000);
    manifestDeadline_.start(12000);
}

void PortableUpdate::request(const QUrl& url, int buffer, int idleMs) {
    cancelled_ = failed_ = false;
    response_.clear();
    qInfo().noquote() << "[portable-update-request]" << (download_ ? "archive" : "manifest") << url.host();
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Veyra-Portable-Update");
    request.setRawHeader("Accept-Encoding", "identity");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::UserVerifiedRedirectPolicy);
    request.setMaximumRedirectsAllowed(3);
    request.setTransferTimeout(idleMs);
    reply_ = network_.get(request);
    reply_->setReadBufferSize(buffer);
    connect(reply_, &QNetworkReply::redirected, this, [this](const QUrl& target) {
        if (allowedRedirect(target)) reply_->redirectAllowed();
        else { failed_ = true; reply_->abort(); }
    });
    connect(reply_, &QIODevice::readyRead, this, [this] {
        if (!consume()) { failed_ = true; reply_->abort(); }
    });
    connect(reply_, &QNetworkReply::finished, this, &PortableUpdate::finish);
}

void PortableUpdate::start() {
    if (!available() || reply_) return;
    errorText_.clear();
    if (!QDir().mkpath(pathString(updates_)) || !plainPath(updates_, true)) {
        reportError(tr("无法写入更新目录"));
        return;
    }
    if (hasDownloadedArchive()) {
        prepareInstallation();
        return;
    }
    for (const auto name : {L"download.zip", L"pending.json"}) {
        const auto path = updates_ / name;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && !plainPath(path, false)) {
            reportError(tr("无法写入更新目录"));
            return;
        }
    }
    const QStorageInfo storage(pathString(updates_));
    if (!storage.isReady() || storage.bytesAvailable() < package_->archiveBytes + package_->expandedBytes + (32ll << 20)) {
        reportError(tr("磁盘空间不足，无法下载并解压更新"));
        return;
    }
    if (!requestArchive(route_)) {
        reportError(tr("无法写入更新目录"));
        return;
    }
    emit changed();
}

bool PortableUpdate::requestArchive(DownloadRoute route) {
    auto file = std::make_unique<QSaveFile>(pathString(updates_ / L"download.zip"));
    if (!file->open(QIODevice::WriteOnly)) return false;
    download_ = std::move(file);
    hash_.reset();
    received_ = 0;
    requestRoute_ = route;
    request(assetUrl(package_->version, package_->asset, route), 512 * 1024, 30000);
    return true;
}

bool PortableUpdate::consume() {
    if (!reply_->isOpen()) return !failed_;
    const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) {
        reply_->readAll(); // Ignore a redirect response body.
        return true;
    }
    if (!download_) {
        response_ += reply_->read(maxManifestBytes + 1 - response_.size());
        return response_.size() <= maxManifestBytes;
    }
    if (status != 200) return false;
    const auto oldProgress = progress();
    const auto chunk = reply_->readAll();
    if (chunk.size() > package_->archiveBytes - received_ || download_->write(chunk) != chunk.size()) return false;
    received_ += chunk.size();
    hash_.addData(chunk);
    if (progress() != oldProgress) emit changed();
    return true;
}

void PortableUpdate::finish() {
    manifestDeadline_.stop();
    const bool ok = !failed_ && !cancelled_ && reply_->error() == QNetworkReply::NoError
        && reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 && consume();
    reply_->deleteLater();
    reply_ = nullptr;
    if (!download_) {
        package_ = ok ? readUpdateManifest(response_, flavor_, installed_, selectedVersion_, key_) : std::nullopt;
        if (!package_ && requestRoute_ != DownloadRoute::GitHub) {
            qInfo().noquote() << "[portable-update-manifest] mirror fallback";
            requestManifest(DownloadRoute::GitHub);
            return;
        }
        manifest_ = package_ ? response_ : QByteArray{};
        qInfo().noquote() << "[portable-update-manifest]" << (package_ ? "verified" : "unavailable");
        emit changed();
        return;
    }

    const bool verified = ok && received_ == package_->archiveBytes && hash_.result() == package_->digest;
    if (!verified && !cancelled_ && requestRoute_ != DownloadRoute::GitHub) {
        download_->cancelWriting();
        download_.reset();
        qInfo().noquote() << "[portable-update-archive] mirror fallback";
        if (requestArchive(DownloadRoute::GitHub)) {
            emit changed();
            return;
        }
    }
    bool saved = false;
    if (verified && download_->commit()) {
        QSaveFile pending(pathString(updates_ / L"pending.json"));
        saved = pending.open(QIODevice::WriteOnly)
            && pending.write(manifest_) == manifest_.size() && pending.commit();
    }
    download_.reset();
    if (saved) {
        prepareInstallation();
    } else {
        discardDownload();
        if (!cancelled_) reportError(tr("更新下载或校验失败，请重试"));
        else emit changed();
    }
}

void PortableUpdate::discardDownload() {
    for (const auto name : {L"pending.json", L"download.zip"}) {
        const auto path = updates_ / name;
        if (plainPath(path, false)) DeleteFileW(path.c_str());
    }
    received_ = 0;
}

void PortableUpdate::cancel() {
    if (download_ && reply_) {
        cancelled_ = true;
        reply_->abort();
    }
}

bool PortableUpdate::hasDownloadedArchive() const {
    const auto pending = updates_ / L"pending.json";
    const auto archive = updates_ / L"download.zip";
    if (!package_ || !plainPath(pending, false) || !plainPath(archive, false)
        || QByteArray::fromStdString(readSmallFile(pending, maxManifestBytes)) != manifest_) return false;
    std::error_code error;
    return std::filesystem::file_size(archive, error) == static_cast<uintmax_t>(package_->archiveBytes) && !error;
}

bool PortableUpdate::hasPreparedVersion() const {
    return package_ && !preparedVersion(updates_.parent_path(), package_->version.toStdString(),
                                        package_->digest.toHex().toStdString()).empty();
}

void PortableUpdate::prepareInstallation() {
    installStep_ = 0;
    installing_ = true;
    emit changed();
    worker_.start(workerPath_, {QStringLiteral("--prepare"), pathString(updates_.parent_path())});
}

void PortableUpdate::finishInstallation(bool success, const QByteArray& result) {
    installing_ = false;
    ready_ = success && hasPreparedVersion();
    qInfo().noquote() << "[portable-update-prepare]" << targetVersion() << result << "ready=" << ready_;
    if (ready_) {
        emit changed();
        emit installationReady();
    } else {
        discardDownload();
        if (result == "windows-blocked") reportError(tr("Windows 已阻止本次更新，请查看保护历史"));
        else if (result == "disk-space") reportError(tr("磁盘空间不足，无法下载并解压更新"));
        else reportError(tr("更新安装失败，请重试"));
    }
}

void PortableUpdate::restart() {
    if (ready_ && !restarting_) emit restartRequested();
}

bool PortableUpdate::beginRestart(const QStringList& launcherArguments) {
    if (!ready_ || restarting_) return false;
    if (!hasPreparedVersion()) {
        ready_ = false;
        reportError(tr("更新安装失败，请重试"));
        return false;
    }
    const auto root = pathString(updates_.parent_path());
    QProcess reopen;
    reopen.setProgram(workerPath_);
    reopen.setArguments(QStringList{QStringLiteral("--reopen"), root} + launcherArguments);
    reopen.setWorkingDirectory(root);
    reopen.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
        arguments->flags = (arguments->flags & ~CREATE_NEW_CONSOLE) | CREATE_NO_WINDOW;
    });
    if (!reopen.startDetached()) {
        reportError(tr("无法重启 Veyra，请重试"));
        return false;
    }
    restarting_ = true;
    errorText_.clear();
    emit changed();
    return true;
}

void PortableUpdate::reportError(const QString& message) {
    errorText_ = message;
    emit changed();
    emit notice(message, true);
}

} // namespace veyra::ui
