#include "veyra/ui/PortableUpdate.h"
#include "veyra/base/PortableFiles.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QLocale>
#include <QNetworkReply>
#include <QNetworkCookie>
#include <QNetworkCookieJar>
#include <QSaveFile>
#include <QStorageInfo>
#include <QThread>
#include <algorithm>
#include <array>
#include <memory>
#include <openssl/evp.h>

namespace veyra::ui {
using namespace veyra::portable;
namespace {
constexpr int maxAssetRedirects = 3;

QString pathString(const std::filesystem::path& path) { return QString::fromStdWString(path.native()); }

QNetworkReply* requestAsset(QNetworkAccessManager& network, const QUrl& url, int buffer, int idleMs,
                            QNetworkRequest::RedirectPolicy redirectPolicy = QNetworkRequest::UserVerifiedRedirectPolicy) {
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Veyra-Portable-Update");
    request.setRawHeader("Accept-Encoding", "identity");
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, redirectPolicy);
    request.setMaximumRedirectsAllowed(maxAssetRedirects);
    request.setTransferTimeout(idleMs);
    auto* reply = network.get(request);
    reply->setReadBufferSize(buffer);
    return reply;
}

enum class ArchiveResult { Invalid, WriteFailed, Cancelled, Saved };

ArchiveResult fetchArchive(QNetworkAccessManager& network, QUrl url, const UpdatePackage& package,
                           const std::filesystem::path& directory, const QByteArray& manifest,
                           const auto& progress) {
    auto* thread = QThread::currentThread();
    if (thread->isInterruptionRequested()) return ArchiveResult::Cancelled;
    QSaveFile archive(pathString(directory / L"download.zip"));
    if (!archive.open(QIODevice::WriteOnly)) return ArchiveResult::WriteFailed;

    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> hash(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!hash || EVP_DigestInit_ex(hash.get(), EVP_sha256(), nullptr) != 1) return ArchiveResult::Invalid;
    qint64 received = 0;
    for (int redirects = 0; ; ++redirects) {
        if (thread->isInterruptionRequested()) return ArchiveResult::Cancelled;
        qInfo().noquote() << "[portable-update-request]" << "archive" << url.host();
        // A new reply reapplies the read buffer cap after each CDN redirect.
        const std::unique_ptr<QNetworkReply> reply(requestAsset(network, url, 512 * 1024, 30000,
                                                                QNetworkRequest::ManualRedirectPolicy));
        QEventLoop loop;
        QTimer watchCancellation;
        watchCancellation.setInterval(25);
        QObject::connect(&watchCancellation, &QTimer::timeout, reply.get(), [&] {
            if (thread->isInterruptionRequested()) reply->abort();
        });
        bool rejected = false;
        bool writeFailed = false;
        const auto status = [&] { return reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(); };
        const auto isRedirect = [&] { const int code = status(); return code >= 300 && code < 400; };
        const auto consume = [&] {
            if (thread->isInterruptionRequested()) return false;
            if (!reply->isOpen()) return !rejected;
            if (status() != 200) return false;
            const auto chunk = reply->readAll();
            if (chunk.size() > package.archiveBytes - received) return false;
            if (archive.write(chunk) != chunk.size()) {
                writeFailed = true;
                return false;
            }
            received += chunk.size();
            if (EVP_DigestUpdate(hash.get(), chunk.constData(), static_cast<size_t>(chunk.size())) != 1) return false;
            progress(int(std::min(qint64(99), received * 100 / package.archiveBytes)));
            return true;
        };
        QObject::connect(reply.get(), &QNetworkReply::metaDataChanged, &loop, [&] {
            if (isRedirect()) loop.quit();
        });
        QObject::connect(reply.get(), &QIODevice::readyRead, &loop, [&] {
            if (!isRedirect() && !consume()) { rejected = true; reply->abort(); }
        });
        QObject::connect(reply.get(), &QNetworkReply::finished, &loop, &QEventLoop::quit);
        watchCancellation.start();
        if (!reply->isFinished() && !isRedirect()) loop.exec();
        watchCancellation.stop();

        if (thread->isInterruptionRequested()) return ArchiveResult::Cancelled;
        if (isRedirect() && reply->error() == QNetworkReply::NoError) {
            const auto location = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            const auto target = reply->url().resolved(location);
            if (reply->isRunning()) reply->abort();
            if (redirects == maxAssetRedirects || location.isEmpty() || !location.isValid() || !allowedRedirect(target))
                return ArchiveResult::Invalid;
            url = target;
            continue;
        }
        const bool transferred = !rejected && reply->error() == QNetworkReply::NoError && status() == 200 && consume();
        if (thread->isInterruptionRequested()) return ArchiveResult::Cancelled;
        if (writeFailed) return ArchiveResult::WriteFailed;
        if (!transferred) return ArchiveResult::Invalid;
        break;
    }

    if (received != package.archiveBytes) return ArchiveResult::Invalid;
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
    unsigned int digestSize = 0;
    if (EVP_DigestFinal_ex(hash.get(), digest.data(), &digestSize) != 1
        || QByteArrayView(reinterpret_cast<const char*>(digest.data()), digestSize) != QByteArrayView(package.digest))
        return ArchiveResult::Invalid;
    if (!archive.commit()) return ArchiveResult::WriteFailed;
    if (thread->isInterruptionRequested()) return ArchiveResult::Cancelled;
    QSaveFile pending(pathString(directory / L"pending.json"));
    if (!pending.open(QIODevice::WriteOnly) || pending.write(manifest) != manifest.size() || !pending.commit())
        return ArchiveResult::WriteFailed;
    return ArchiveResult::Saved;
}
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
    if (download_) {
        download_->requestInterruption();
        download_->wait();
        discardDownload();
    }
}

int PortableUpdate::progress() const {
    return progress_;
}

QString PortableUpdate::sizeText() const {
    return package_ ? QLocale().formattedDataSize(package_->archiveBytes) : QString{};
}

QString PortableUpdate::releaseNotes() const { return package_ ? package_->notes : QString{}; }

void PortableUpdate::selectRelease(const QString& version) {
    if (updates_.empty() || reply_ || downloading() || installing_ || ready_ || (package_ && package_->version == version)) return;
    package_.reset();
    manifest_.clear();
    errorText_.clear();
    selectedVersion_ = version;
    if (!version.isEmpty()) requestManifest(route_);
    emit changed();
}

void PortableUpdate::requestManifest(DownloadRoute route) {
    requestRoute_ = route;
    failed_ = false;
    response_.clear();
    const auto url = assetUrl(selectedVersion_, QString::fromLatin1(manifestAsset), route);
    qInfo().noquote() << "[portable-update-request]" << "manifest" << url.host();
    reply_ = requestAsset(network_, url, int(maxManifestBytes + 1), 12000);
    connect(reply_, &QNetworkReply::redirected, this, [this](const QUrl& target) {
        if (allowedRedirect(target)) reply_->redirectAllowed();
        else { failed_ = true; reply_->abort(); }
    });
    connect(reply_, &QIODevice::readyRead, this, [this] {
        if (!consumeManifest()) { failed_ = true; reply_->abort(); }
    });
    connect(reply_, &QNetworkReply::finished, this, &PortableUpdate::finishManifest);
    manifestDeadline_.start(12000);
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
    requestArchive(route_);
    emit changed();
}

void PortableUpdate::requestArchive(DownloadRoute route) {
    cancelled_ = false;
    progress_ = 0;
    const auto generation = ++downloadGeneration_;
    const auto result = std::make_shared<ArchiveResult>(ArchiveResult::Invalid);
    const auto source = assetUrl(package_->version, package_->asset, route);
    const auto github = assetUrl(package_->version, package_->asset, DownloadRoute::GitHub);
    const auto sourceCookies = network_.cookieJar()->cookiesForUrl(source);
    const auto githubCookies = route == DownloadRoute::GitHub ? QList<QNetworkCookie>{}
        : network_.cookieJar()->cookiesForUrl(github);
    // Keep the reply, file and digest on the same worker thread. Only progress
    // and the terminal result cross into the GUI event loop.
    download_ = QThread::create([this, generation, result, source, github, sourceCookies, githubCookies,
                                 package = *package_, directory = updates_, manifest = manifest_] {
        QNetworkAccessManager network;
        network.cookieJar()->setCookiesFromUrl(sourceCookies, source);
        if (source != github) network.cookieJar()->setCookiesFromUrl(githubCookies, github);
        int reported = 0;
        const auto onProgress = [&](int percent) {
            if (percent <= reported) return;
            reported = percent;
            QMetaObject::invokeMethod(this, [this, generation, percent] {
                if (download_ && downloadGeneration_ == generation && !cancelled_) {
                    progress_ = percent;
                    emit changed();
                }
            }, Qt::QueuedConnection);
        };
        *result = fetchArchive(network, source, package, directory, manifest, onProgress);
        if (*result == ArchiveResult::Invalid && source != github &&
            !QThread::currentThread()->isInterruptionRequested()) {
            qInfo().noquote() << "[portable-update-archive] mirror fallback";
            *result = fetchArchive(network, github, package, directory, manifest, onProgress);
        }
    });
    auto* thread = download_;
    thread->setParent(this);
    connect(thread, &QThread::finished, this, [this, thread, generation, result] {
        thread->deleteLater();
        if (download_ != thread || downloadGeneration_ != generation) return;
        download_ = nullptr;
        if (!cancelled_ && *result == ArchiveResult::Saved) {
            progress_ = 100;
            prepareInstallation();
            return;
        }
        discardDownload();
        if (cancelled_ || *result == ArchiveResult::Cancelled) emit changed();
        else if (*result == ArchiveResult::WriteFailed) reportError(tr("无法写入更新目录"));
        else reportError(tr("更新下载或校验失败，请重试"));
    });
    thread->start();
}

bool PortableUpdate::consumeManifest() {
    if (!reply_->isOpen()) return !failed_;
    const int status = reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status >= 300 && status < 400) {
        reply_->readAll();
        return true;
    }
    response_ += reply_->read(maxManifestBytes + 1 - response_.size());
    return response_.size() <= maxManifestBytes;
}

void PortableUpdate::finishManifest() {
    manifestDeadline_.stop();
    const bool ok = !failed_ && reply_->error() == QNetworkReply::NoError
        && reply_->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200 && consumeManifest();
    reply_->deleteLater();
    reply_ = nullptr;
    package_ = ok ? readUpdateManifest(response_, flavor_, installed_, selectedVersion_, key_) : std::nullopt;
    if (!package_ && requestRoute_ != DownloadRoute::GitHub) {
        qInfo().noquote() << "[portable-update-manifest] mirror fallback";
        requestManifest(DownloadRoute::GitHub);
        return;
    }
    manifest_ = package_ ? response_ : QByteArray{};
    qInfo().noquote() << "[portable-update-manifest]" << (package_ ? "verified" : "unavailable");
    emit changed();
}

void PortableUpdate::discardDownload() {
    for (const auto name : {L"pending.json", L"download.zip"}) {
        const auto path = updates_ / name;
        if (plainPath(path, false)) DeleteFileW(path.c_str());
    }
    progress_ = 0;
}

void PortableUpdate::cancel() {
    if (!download_ || cancelled_) return;
    cancelled_ = true;
    download_->requestInterruption();
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
