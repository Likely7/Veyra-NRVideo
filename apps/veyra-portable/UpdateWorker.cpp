#include "veyra/base/PortableFiles.h"
#include "veyra/base/PortableUpdateManifest.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDeadlineTimer>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QUuid>
#include <shobjidl.h>
#include <softpub.h>
#include <wintrust.h>

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

bool boundedOutput(QProcess& child, QByteArray& output, int timeoutMs) {
    QDeadlineTimer deadline(timeoutMs);
    while (child.state() != QProcess::NotRunning) {
        if (deadline.hasExpired()) {
            child.kill();
            child.waitForFinished(10000);
            return false;
        }
        child.waitForReadyRead(200);
        output += child.readAllStandardOutput();
        child.readAllStandardError();
        if (output.size() > 4 * 1024 * 1024) {
            child.kill();
            child.waitForFinished(10000);
            return false;
        }
    }
    output += child.readAllStandardOutput();
    return child.exitStatus() == QProcess::NormalExit && child.exitCode() == 0;
}

bool namesAreSafe(const QByteArray& output) {
    int lines = 0;
    int start = 0;
    for (int i = 0; i <= output.size(); ++i) {
        if (i != output.size() && output[i] != '\n') continue;
        auto line = output.mid(start, i - start);
        if (line.endsWith('\r')) line.chop(1);
        start = i + 1;
        if (line.isEmpty()) continue;
        if (++lines > 30000 || !safeArchiveEntry(std::string_view(line.constData(), static_cast<size_t>(line.size()))))
            return false;
    }
    return lines > 0;
}

bool modesAreSafe(const QByteArray& output) {
    int start = 0;
    bool any = false;
    for (int i = 0; i <= output.size(); ++i) {
        if (i != output.size() && output[i] != '\n') continue;
        auto line = output.mid(start, i - start);
        if (line.endsWith('\r')) line.chop(1);
        start = i + 1;
        if (line.isEmpty()) continue;
        any = true;
        const char kind = line.front();
        if (kind != '-' && kind != 'd') return false;
    }
    return any;
}

bool archiveMembersSafe(const QString& tar, const QString& archive) {
    const QStringList modes{QStringLiteral("-tf"), QStringLiteral("-tvf")};
    for (const auto& flag : modes) {
        QProcess child;
        child.setProgram(tar);
        child.setArguments({flag, archive});
        child.start();
        if (!child.waitForStarted(10000)) return false;
        QByteArray output;
        if (!boundedOutput(child, output, 60000)) return false;
        if (!(flag == QLatin1String("-tvf") ? modesAreSafe(output) : namesAreSafe(output))) return false;
    }
    return true;
}

bool userStagingDirectory(const fs::path& staging) {
    wchar_t local[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"LOCALAPPDATA", local, static_cast<DWORD>(sizeof(local) / sizeof(local[0])));
    if (!length || length >= std::size(local)) return false;
    const fs::path base = fs::path(local) / L"Veyra" / L"update-staging";
    return plainPath(fs::path(local), true) && plainPath(base.parent_path(), true) && plainPath(base, true)
        && plainPath(staging, true) && stagingDirectoryId(base, staging);
}

bool acceptExecutable(const fs::path& path) {
    WINTRUST_FILE_INFO file{};
    file.cbStruct = sizeof(file);
    file.pcwszFilePath = path.c_str();
    GUID action = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    WINTRUST_DATA data{};
    data.cbStruct = sizeof(data);
    data.dwUIChoice = WTD_UI_NONE;
    data.fdwRevocationChecks = WTD_REVOKE_NONE;
    data.dwUnionChoice = WTD_CHOICE_FILE;
    data.pFile = &file;
    data.dwStateAction = WTD_STATEACTION_VERIFY;
    // Cache only: a revoked certificate is not rechecked online during install.
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_SAFER_FLAG;
    const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    const DWORD error = GetLastError();
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return acceptAuthenticode(status, error);
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
    if (!archiveMembersSafe(pathString(tar), pathString(files.archive))) return "invalid-archive";
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
    if (!acceptExecutable(player) || !acceptExecutable(versionDir / workerName)) return "invalid-authenticode";
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
    const auto arguments = app.arguments();
    if (arguments.size() != 2 && arguments.size() != 3) return 1;
    const fs::path root(arguments.at(1).toStdWString());
    fs::path updates = root / updateFolder;
    if (arguments.size() == 3) {
        updates = fs::path(arguments.at(2).toStdWString());
        if (!userStagingDirectory(updates)) return 1;
    }
    if (!root.is_absolute() || !plainPath(updates, true) || packageFlavor(root).empty()) return 1;
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto result = SUCCEEDED(com) ? applyUpdate(root, updates) : "windows-check-unavailable";
    if (SUCCEEDED(com)) CoUninitialize();
    writeSmallFile(updates / L"result.txt", std::string(result) + "\n");
    return std::string_view(result) == "installed" ? 0 : 1;
}
