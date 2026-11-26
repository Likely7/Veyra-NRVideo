#include "veyra/ui/PortableUpdate.h"
#include "veyra/base/PortableFiles.h"
#include "veyra/base/PortableLayout.h"

#include <QCoreApplication>
#include <QDir>
#include <QLocale>
#include <QNetworkReply>
#include <QStorageInfo>
#include <knownfolders.h>
#include <shellapi.h>
#include <shlobj.h>

namespace veyra::ui {
using namespace veyra::portable;
namespace {
QString pathString(const std::filesystem::path& path) { return QString::fromStdWString(path.native()); }

std::wstring quote(std::wstring_view argument) {
    std::wstring result = L"\"";
    size_t backslashes = 0;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++backslashes;
            continue;
        }
        result.append(character == L'"' ? backslashes * 2 + 1 : backslashes, L'\\');
        result.push_back(character);
        backslashes = 0;
    }
    result.append(backslashes * 2, L'\\');
    result.push_back(L'"');
    return result;
}

bool underKnownFolder(const std::filesystem::path& root, REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) || !raw) return false;
    const auto base = std::filesystem::path(raw).lexically_normal().wstring();
    CoTaskMemFree(raw);
    const auto target = root.lexically_normal().wstring();
    if (base.empty() || target.size() < base.size()) return false;
    if (_wcsnicmp(target.c_str(), base.c_str(), static_cast<unsigned>(base.size())) != 0) return false;
    return target.size() == base.size() || target[base.size()] == L'\\' || target[base.size()] == L'/';
}

bool underProgramFiles(const std::filesystem::path& root) {
    return underKnownFolder(root, FOLDERID_ProgramFiles) || underKnownFolder(root, FOLDERID_ProgramFilesX86)
        || underKnownFolder(root, FOLDERID_ProgramFilesX64);
}

bool directoryAcceptsFiles(const std::filesystem::path& dir) {
    std::error_code error;
    std::filesystem::create_directories(dir, error);
    if (error || !plainPath(dir, true)) return false;
    const auto probe = dir / (L"write-probe-" + std::to_wstring(GetCurrentProcessId()) + L"-"
                              + std::to_wstring(GetTickCount64()));
    const HANDLE file = CreateFileW(probe.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                                    FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    CloseHandle(file);
    return plainPath(dir, true);
}

std::filesystem::path userStaging(const std::filesystem::path& root) {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(sizeof(local) / sizeof(local[0])));
    if (!length || length >= std::size(local)) return {};
    const auto id = QCryptographicHash::hash(QString::fromStdWString(root.wstring()).toUtf8(),
                                             QCryptographicHash::Sha256).toHex().left(16);
    return std::filesystem::path(local) / L"Veyra" / L"update-staging" / id.toStdWString();
}
}

PortableUpdate::PortableUpdate(const QString& version, QObject* parent)
    : QObject(parent), installed_(releaseVersion(version)), key_(updatePublicKey()), elevateWait_(this) {
    connect(&elevateWait_, &QWinEventNotifier::activated, this, [this](HANDLE process) {
        DWORD code = 1;
        const bool read = process && GetExitCodeProcess(process, &code);
        elevateWait_.setEnabled(false);
        elevateWait_.setHandle(nullptr);
        if (process) CloseHandle(process);
        installing_ = false;
        scheduled_ = false;
        qInfo().noquote() << "[portable-update-elevated]" << (read ? int(code) : -1);
        if (read && code == 0) emit installFinished(true);
        else {
            discardDownload();
            emit notice(tr("更新未完成，当前版本未改变"), true);
            emit installFinished(false);
        }
        emit changed();
    });
    if (!QCoreApplication::arguments().contains(QStringLiteral("--managed-portable"))
        || installed_.isEmpty() || key_.size() != 32) return;
    const auto directory = std::filesystem::path(QCoreApplication::applicationDirPath().toStdWString());
    const auto root = rootFromVersionDirectory(directory);
    if (root.empty() || !plainPath(directory / workerName, false)
        || directory.filename().wstring() != std::filesystem::path(installedVersions(root).current).wstring()) return;
    root_ = root;
    worker_ = directory / workerName;
    flavor_ = QString::fromStdString(packageFlavor(root));
    updates_ = root / updateFolder;
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
    if (!plainPath(pending, false) || !plainPath(updates_ / L"download.zip", false)) return;
    manifest_ = QByteArray::fromStdString(readSmallFile(pending, maxManifestBytes));
    package_ = readUpdateManifest(manifest_, flavor_, installed_, {}, key_);
    if (!package_) return;
    std::error_code error;
    scheduled_ = std::filesystem::file_size(updates_ / L"download.zip", error)
        == static_cast<uintmax_t>(package_->archiveBytes) && !error;
    if (!scheduled_) package_.reset();
}

PortableUpdate::~PortableUpdate() {
    if (reply_) {
        reply_->disconnect(this);
        reply_->abort();
    }
    if (const auto process = elevateWait_.handle()) {
        elevateWait_.setEnabled(false);
        elevateWait_.setHandle(nullptr);
        CloseHandle(process);
    }
}

int PortableUpdate::progress() const {
    return package_ ? int(received_ * 100 / package_->archiveBytes) : 0;
}

QString PortableUpdate::sizeText() const {
    return package_ ? QLocale().formattedDataSize(package_->archiveBytes) : QString{};
}

QString PortableUpdate::releaseNotes() const { return package_ ? package_->notes : QString{}; }

void PortableUpdate::setReleaseInventory(bool enumerated, const QHash<QString, QByteArray>& digests) {
    assetsEnumerated_ = enumerated;
    published_ = digests;
}

bool PortableUpdate::inventoryAccepts(const QByteArray& manifestBytes, const UpdatePackage& package) const {
    const auto manifestName = QString::fromLatin1(manifestAsset);
    const auto manifestHash = QCryptographicHash::hash(manifestBytes, QCryptographicHash::Sha256);
    if (!publishedDigestAgrees(assetsEnumerated_, published_.contains(manifestName),
                               published_.value(manifestName), manifestHash)) return false;
    return publishedDigestAgrees(assetsEnumerated_, published_.contains(package.asset),
                                 published_.value(package.asset), package.digest);
}

void PortableUpdate::selectRelease(const QString& version) {
    if (reply_ || scheduled_ || installing_ || (package_ && package_->version == version)) return;
    package_.reset();
    manifest_.clear();
    selectedVersion_ = version;
    if (version.isEmpty()) {
        blockReason_.clear();
        emit changed();
        return;
    }
    if (updates_.empty()) {
        blockReason_ = QStringLiteral("not-managed");
        emit changed();
        return;
    }
    if (assetsEnumerated_ && !published_.contains(QLatin1String(manifestAsset))) {
        blockReason_ = QStringLiteral("no-package");
        emit changed();
        return;
    }
    blockReason_.clear();
    requestManifest(route_);
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

bool PortableUpdate::prepareDownloadDirectory() {
    const auto installUpdates = root_.empty() ? std::filesystem::path{} : root_ / updateFolder;
    if (!elevate_ && !installUpdates.empty() && directoryAcceptsFiles(installUpdates)) {
        updates_ = installUpdates;
        return true;
    }
    if (elevate_ && directoryAcceptsFiles(updates_) && stagingDirectoryId(updates_.parent_path(), updates_)) return true;
    if (root_.empty() || !underProgramFiles(root_)) return false;
    const auto staging = userStaging(root_);
    if (staging.empty()) return false;
    const auto base = staging.parent_path();
    if (!plainPath(base.parent_path().parent_path(), true)) return false;
    if (!directoryAcceptsFiles(base.parent_path()) || !directoryAcceptsFiles(base) || !directoryAcceptsFiles(staging))
        return false;
    if (!stagingDirectoryId(base, staging)) return false;
    updates_ = staging;
    elevate_ = true;
    qInfo().noquote() << "[portable-update] staging outside the install directory";
    return true;
}

void PortableUpdate::downloadAndSchedule() {
    if (!available() || reply_ || installing_) return;
    if (!prepareDownloadDirectory()) {
        emit notice(tr("无法写入更新目录"), true);
        return;
    }
    for (const auto name : {L"download.zip", L"pending.json"}) {
        const auto path = updates_ / name;
        if (GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES && !plainPath(path, false)) {
            emit notice(tr("无法写入更新目录"), true);
            return;
        }
    }
    const QStorageInfo storage(pathString(updates_));
    if (!storage.isReady() || storage.bytesAvailable() < package_->archiveBytes + package_->expandedBytes + (32ll << 20)) {
        emit notice(tr("磁盘空间不足，无法下载并解压更新"), true);
        return;
    }
    if (!requestArchive(route_)) {
        emit notice(tr("无法写入更新目录"), true);
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
        auto package = ok ? readUpdateManifest(response_, flavor_, installed_, selectedVersion_, key_) : std::nullopt;
        if (package && !inventoryAccepts(response_, *package)) package.reset();
        package_ = package;
        if (!package_ && !cancelled_ && requestRoute_ != DownloadRoute::GitHub) {
            qInfo().noquote() << "[portable-update-manifest] mirror fallback";
            requestManifest(DownloadRoute::GitHub);
            return;
        }
        manifest_ = package_ ? response_ : QByteArray{};
        blockReason_ = package_ || selectedVersion_.isEmpty() || cancelled_ ? QString{} : QStringLiteral("no-package");
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
    if (verified && download_->commit()) {
        QSaveFile pending(pathString(updates_ / L"pending.json"));
        scheduled_ = pending.open(QIODevice::WriteOnly)
            && pending.write(manifest_) == manifest_.size() && pending.commit();
    }
    download_.reset();
    if (scheduled_) {
        qInfo().noquote() << "[portable-update]" << package_->version << (elevate_ ? "staged" : "scheduled");
        emit notice(tr("更新已校验，重启后安装"), false);
    } else {
        discardDownload();
        if (!cancelled_) emit notice(tr("更新下载或校验失败，请重试"), true);
    }
    emit changed();
}

void PortableUpdate::discardDownload() {
    for (const auto name : {L"pending.json", L"download.zip"}) {
        const auto path = updates_ / name;
        if (plainPath(path, false)) DeleteFileW(path.c_str());
    }
    received_ = 0;
}

void PortableUpdate::cancel() {
    if (installing_) return;
    if (reply_) {
        cancelled_ = true;
        reply_->abort();
    } else if (scheduled_) {
        const auto pending = updates_ / L"pending.json";
        if (plainPath(pending, false) && !DeleteFileW(pending.c_str())) {
            emit notice(tr("无法写入更新目录"), true);
            return;
        }
        scheduled_ = false;
        elevate_ = false;
        discardDownload();
        emit changed();
    }
}

void PortableUpdate::beginElevatedInstall() {
    if (!scheduled_ || installing_ || !elevate_ || worker_.empty() || root_.empty()) return;
    if (!plainPath(worker_, false) || !plainPath(updates_, true) || !stagingDirectoryId(updates_.parent_path(), updates_)) {
        emit notice(tr("无法写入更新目录"), true);
        return;
    }
    const auto file = worker_.native();
    const auto parameters = quote(root_.native()) + L" " + quote(updates_.native());
    SHELLEXECUTEINFOW request{};
    request.cbSize = sizeof(request);
    request.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    request.lpVerb = L"runas";
    request.lpFile = file.c_str();
    request.lpParameters = parameters.c_str();
    request.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&request) || !request.hProcess) {
        const DWORD error = GetLastError();
        qInfo().noquote() << "[portable-update-elevated] start" << int(error);
        emit notice(error == ERROR_CANCELLED ? tr("已取消管理员授权") : tr("更新未完成，当前版本未改变"), true);
        return;
    }
    installing_ = true;
    elevateWait_.setHandle(request.hProcess);
    elevateWait_.setEnabled(true);
    emit changed();
}

} // namespace veyra::ui
