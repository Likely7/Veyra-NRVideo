#include "veyra/base/PortableFiles.h"
#include "veyra/base/PortableUpdateManifest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QUuid>
#include <shobjidl.h>

namespace {
using namespace veyra::portable;
namespace fs = std::filesystem;

QString pathString(const fs::path& path) { return QString::fromStdWString(path.native()); }

bool runChild(QProcess& child, const QString& program, const QStringList& arguments, int timeout) {
    child.setProgram(program);
    child.setArguments(arguments);
    child.setStandardOutputFile(QProcess::nullDevice());
    child.setStandardErrorFile(QProcess::nullDevice());
    child.start();
    if (!child.waitForStarted(10000)) return false;
    if (!child.waitForFinished(timeout)) {
        child.kill();
        child.waitForFinished(10000);
        return false;
    }
    return child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
}

bool checkWindowsAttachment(const fs::path& path, const QUrl& source) {
    IAttachmentExecute* attachment = nullptr;
    HRESULT result = CoCreateInstance(CLSID_AttachmentServices, nullptr, CLSCTX_INPROC_SERVER,
                                      IID_PPV_ARGS(&attachment));
    if (FAILED(result)) return false;
    const auto url = source.toString(QUrl::FullyEncoded).toStdWString();
    result = attachment->SetLocalPath(path.c_str());
    if (SUCCEEDED(result)) result = attachment->SetSource(url.c_str());
    if (SUCCEEDED(result)) result = attachment->Save();
    attachment->Release();
    return result == S_OK;
}

bool inspectExtractedTree(const fs::path& root, qint64 expectedSize) {
    // The staging directory adds a level to the installation path. Qt assets
    // can pass MAX_PATH here even when the same package runs after installation.
    // Use an extended-length path for both enumeration and attribute checks.
    auto name = root.lexically_normal().make_preferred().native();
    if (!name.starts_with(LR"(\\?\)")) {
        if (name.starts_with(LR"(\\)")) name = LR"(\\?\UNC\)" + name.substr(2);
        else name = LR"(\\?\)" + name;
    }
    qint64 bytes = 0;
    unsigned files = 0;
    std::error_code error;
    fs::recursive_directory_iterator it(fs::path(name), error), end;
    for (; !error && it != end; it.increment(error)) {
        const auto& path = it->path();
        if (plainPath(path, true)) continue;
        if (!plainPath(path, false) || ++files > 30000) return false;
        const auto size = it->file_size(error);
        if (error || size > static_cast<uint64_t>(expectedSize - bytes)) return false;
        bytes += static_cast<qint64>(size);
    }
    return !error && bytes == expectedSize;
}

struct DownloadCleanup {
    fs::path archive, manifest;
    ~DownloadCleanup() {
        if (plainPath(archive, false)) DeleteFileW(archive.c_str());
        if (plainPath(manifest, false)) DeleteFileW(manifest.c_str());
    }
};

const char* applyUpdate(const fs::path& root, const fs::path& updates) {
    const auto state = installedVersions(root);
    const auto selfDirectory = fs::path(QCoreApplication::applicationFilePath().toStdWString()).parent_path();
    const auto versions = root / versionsFolder;
    const auto active = versions / state.current;
    std::error_code error;
    if (state.current.empty() || !plainPath(versions, true) || !plainPath(active, true)
        || !fs::equivalent(selfDirectory, active, error) || error) return "invalid-layout";
    const auto flavor = QString::fromStdString(packageFlavor(root));
    const auto installed = QString::fromStdString(std::string(versionFromDirectory(state.current)));
    const DownloadCleanup files{updates / L"download.zip", updates / L"pending.json"};
    if (!plainPath(files.archive, false) || !plainPath(files.manifest, false)) return "missing-download";

    const auto envelope = QByteArray::fromStdString(readSmallFile(files.manifest, maxManifestBytes));
    const auto package = readUpdateManifest(envelope, flavor, installed, {}, updatePublicKey());
    if (!package) return "invalid-signature";
    const auto source = assetUrl(package->version, package->asset);
    if (!checkWindowsAttachment(files.archive, source)) return "windows-blocked";
    // The archive is rechecked after Attachment Services and kept read-only
    // for the entire verification/extraction interval.
    const HANDLE lock = CreateFileW(files.archive.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                                    OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (lock == INVALID_HANDLE_VALUE) return "archive-open-failed";
    struct ArchiveLock { HANDLE handle; ~ArchiveLock() { CloseHandle(handle); } } held{lock};
    QFile archive(pathString(files.archive));
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!archive.open(QIODevice::ReadOnly) || archive.size() != package->archiveBytes
        || !hash.addData(&archive) || hash.result() != package->digest) return "invalid-archive";
    archive.close();
    const QStorageInfo storage(pathString(updates));
    if (!storage.isReady() || storage.bytesAvailable() < package->expandedBytes + (32ll << 20))
        return "disk-space";

    QTemporaryDir staging(pathString(updates / L"stage-XXXXXX"));
    if (!staging.isValid()) return "stage-create-failed";
    wchar_t system[32768]{};
    const auto count = GetSystemDirectoryW(system, std::size(system));
    if (!count || count >= std::size(system)) return "extractor-unavailable";
    const auto tar = fs::path(system) / L"tar.exe";
    if (!plainPath(tar, false)) return "extractor-unavailable";
    QProcess extractor;
    if (!runChild(extractor, pathString(tar),
                  {QStringLiteral("-xf"), pathString(files.archive), QStringLiteral("-C"), staging.path(),
                   QStringLiteral("--strip-components"), QStringLiteral("1"),
                   QStringLiteral("--no-same-owner"), QStringLiteral("--no-same-permissions")}, 240000))
        return "extract-failed";

    const auto packageRoot = fs::path(staging.path().toStdWString());
    const auto versionDir = packageRoot / versionsFolder / package->version.toStdWString();
    const auto player = versionDir / playerName;
    if (!inspectExtractedTree(packageRoot, package->expandedBytes) || !plainPath(versionDir, true)
        || packageFlavor(packageRoot) != flavor.toStdString()
        || installedVersions(packageRoot).current != package->version.toStdString()
        || !plainPath(player, false) || !plainPath(versionDir / workerName, false))
        return "invalid-package-layout";

    if (!checkWindowsAttachment(player, source) || !checkWindowsAttachment(versionDir / workerName, source))
        return "windows-blocked";
    QTemporaryDir probeData(pathString(updates / L"probe-XXXXXX"));
    if (!probeData.isValid()) return "stage-create-failed";
    QProcess probe;
    probe.setWorkingDirectory(pathString(versionDir));
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("VEYRA_LOG_FILE"), probeData.filePath(QStringLiteral("probe.log")));
    env.insert(QStringLiteral("VEYRA_XBOX_DATA"), probeData.filePath(QStringLiteral("xbox")));
    env.insert(QStringLiteral("VEYRA_MOONLIGHT_DATA"), probeData.filePath(QStringLiteral("moonlight")));
    env.insert(QStringLiteral("TEMP"), probeData.path());
    env.insert(QStringLiteral("TMP"), probeData.path());
    probe.setProcessEnvironment(env);
    if (!runChild(probe, pathString(player),
                  {QStringLiteral("--portable-update-probe"), package->version, QStringLiteral("--data-dir"),
                   probeData.filePath(QStringLiteral("profile")), QStringLiteral("--page"),
                   QStringLiteral("set")}, 35000))
        return "startup-check-failed";

    const auto identifier = package->version + u'-'
        + QUuid::createUuid().toString(QUuid::Id128).left(12);
    const auto target = versions / identifier.toStdString();
    if (!MoveFileExW(versionDir.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) return "stage-move-failed";
    if (!setInstalledVersions(root, {identifier.toStdString(), state.current})) {
        // A failed pointer write leaves the validated old version selected.
        MoveFileExW(target.c_str(), versionDir.c_str(), MOVEFILE_WRITE_THROUGH);
        return "activate-failed";
    }
    return "installed";
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2) return 1;
    const fs::path root(app.arguments().at(1).toStdWString());
    const auto updates = root / updateFolder;
    if (!root.is_absolute() || !plainPath(updates, true) || packageFlavor(root).empty()) return 1;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto result = SUCCEEDED(com) ? applyUpdate(root, updates) : "windows-check-unavailable";
    if (SUCCEEDED(com)) CoUninitialize();
    writeSmallFile(updates / L"result.txt", std::string(result) + "\n");
    return std::string_view(result) == "installed" ? 0 : 1;
}
