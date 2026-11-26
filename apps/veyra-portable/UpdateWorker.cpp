#include "veyra/base/PortableFiles.h"
#include "veyra/base/PortablePrivileged.h"
#include "veyra/base/PortableUpdateManifest.h"

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QProcess>
#include <QStorageInfo>
#include <QTemporaryDir>
#include <QUuid>
#include <memory>
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

AuthenticodeClass authenticodeOf(const fs::path& path) {
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
    // A missing signature is classified as Absent and is not treated as proof.
    data.dwProvFlags = WTD_CACHE_ONLY_URL_RETRIEVAL | WTD_SAFER_FLAG;
    const LONG status = WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    const DWORD error = GetLastError();
    data.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(static_cast<HWND>(INVALID_HANDLE_VALUE), &action, &data);
    return classifyAuthenticode(status, error);
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

struct HeldHandle {
    HANDLE handle = INVALID_HANDLE_VALUE;
    ~HeldHandle() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
};

struct PrivilegedTree {
    fs::path path;
    ~PrivilegedTree() { if (!path.empty()) removePlainTree(path); }
};

bool executableAccepted(const fs::path& path, bool elevated, bool zipHashMatched) {
    const auto trust = authenticodeOf(path);
    if (elevated) return elevatedFileMayRun(trust, true, zipHashMatched);
    return trust != AuthenticodeClass::Rejected;
}

const char* applyUpdate(const fs::path& root, const fs::path& updates, bool elevated) {
    const auto state = installedVersions(root);
    const auto selfDirectory = fs::path(QCoreApplication::applicationFilePath().toStdWString()).parent_path();
    const auto versions = root / versionsFolder;
    const auto active = versions / state.current;
    std::error_code error;
    if (state.current.empty() || !plainPath(root / versionState, false) || !plainPath(root / packageMarker, false)
        || !pathChainIsPlain(root) || !pathChainIsPlain(versions) || !pathChainIsPlain(active)
        || !plainPath(versions, true) || !plainPath(active, true)
        || !fs::equivalent(selfDirectory, active, error) || error) return "invalid-layout";
    const auto flavor = QString::fromStdString(packageFlavor(root));
    const auto installed = QString::fromStdString(std::string(versionFromDirectory(state.current)));
    const DownloadCleanup files{updates / L"download.zip", updates / L"pending.json"};
    if (!pathChainIsPlain(files.archive) || !pathChainIsPlain(files.manifest)
        || !plainPath(files.archive, false) || !plainPath(files.manifest, false)) return "missing-download";

    HeldHandle manifest;
    manifest.handle = openPlainForRead(files.manifest);
    std::string manifestBytes;
    if (manifest.handle == INVALID_HANDLE_VALUE
        || !readHandleCapped(manifest.handle, static_cast<std::size_t>(maxManifestBytes), manifestBytes))
        return "invalid-signature";
    CloseHandle(manifest.handle);
    manifest.handle = INVALID_HANDLE_VALUE;
    const auto envelope = QByteArray(manifestBytes.data(), static_cast<int>(manifestBytes.size()));
    const auto package = readUpdateManifest(envelope, flavor, installed, {}, updatePublicKey());
    if (!package || package->digest.size() != 32) return "invalid-signature";
    const auto source = assetUrl(package->version, package->asset);
    if (!checkWindowsAttachment(files.archive, source)) return "windows-blocked";

    const auto* digest = reinterpret_cast<const std::uint8_t*>(package->digest.constData());
    HeldHandle sourceLock;
    const auto hashed = openAndHash(files.archive, digest, static_cast<std::uint64_t>(package->archiveBytes),
                                    &sourceLock.handle);
    if (hashed == ArchiveRead::Mismatch) return "invalid-archive";
    if (hashed != ArchiveRead::Match) return "archive-open-failed";

    PrivilegedTree privileged;
    fs::path archivePath = files.archive;
    SealedArchive sealed;
    if (elevated) {
        // The locked user zip is only a byte source. Extraction and the probe
        // run from a directory created here, owned by Administrators.
        fs::path privilegedRoot;
        if (!preparePrivilegedRoot(privilegedRoot) || !createFreshPrivilegedDirectory(privilegedRoot, privileged.path))
            return "privileged-stage-failed";
        const QStorageInfo storage(pathString(privileged.path));
        if (!storage.isReady()
            || storage.bytesAvailable() < package->archiveBytes + package->expandedBytes + (32ll << 20))
            return "disk-space";
        const auto sealedStatus = sealFromLockedSource(sourceLock.handle, privileged.path, digest,
            static_cast<std::uint64_t>(package->archiveBytes), sealed);
        CloseHandle(sourceLock.handle);
        sourceLock.handle = INVALID_HANDLE_VALUE;
        if (sealedStatus == SealStatus::Mismatch) return "invalid-archive";
        if (sealedStatus != SealStatus::Sealed) return "privileged-stage-failed";
        archivePath = sealed.path;
    } else {
        const QStorageInfo storage(pathString(updates));
        if (!storage.isReady() || storage.bytesAvailable() < package->expandedBytes + (32ll << 20))
            return "disk-space";
    }

    fs::path extractRoot;
    // Constructed only for a writable install. The default constructor would
    // create a directory in the process temp folder, which this install must not do.
    std::unique_ptr<QTemporaryDir> writableStage;
    if (elevated) {
        extractRoot = privileged.path / L"extracted";
        if (!protectForAdmins(extractRoot, true)) return "privileged-stage-failed";
    } else {
        writableStage = std::make_unique<QTemporaryDir>(pathString(updates / L"stage-XXXXXX"));
        if (!writableStage->isValid()) return "stage-create-failed";
        extractRoot = fs::path(writableStage->path().toStdWString());
    }
    wchar_t system[32768]{};
    const auto count = GetSystemDirectoryW(system, static_cast<UINT>(std::size(system)));
    if (!count || count >= std::size(system)) return "extractor-unavailable";
    const auto tar = fs::path(system) / L"tar.exe";
    if (!pathChainIsPlain(tar) || !plainPath(tar, false)) return "extractor-unavailable";
    if (!archiveMembersSafe(pathString(tar), pathString(archivePath))) return "invalid-archive";
    QProcess extractor;
    // tar has its own deadline. A hung extractor is killed and the install fails.
    if (!runChild(extractor, pathString(tar),
                  {QStringLiteral("-xf"), pathString(archivePath), QStringLiteral("-C"), pathString(extractRoot),
                   QStringLiteral("--strip-components"), QStringLiteral("1"),
                   QStringLiteral("--no-same-owner"), QStringLiteral("--no-same-permissions")}, 240000))
        return "extract-failed";

    const auto versionDir = extractRoot / versionsFolder / package->version.toStdWString();
    const auto player = versionDir / playerName;
    if (!inspectExtractedTree(extractRoot, package->expandedBytes) || !pathChainIsPlain(versionDir)
        || !plainPath(versionDir, true) || packageFlavor(extractRoot) != flavor.toStdString()
        || installedVersions(extractRoot).current != package->version.toStdString()
        || !pathChainIsPlain(player) || !plainPath(player, false)
        || !pathChainIsPlain(versionDir / workerName) || !plainPath(versionDir / workerName, false))
        return "invalid-package-layout";
    if (elevated && (!privilegedForElevatedUse(extractRoot) || !treePrivilegedForElevatedUse(versionDir)))
        return "privileged-stage-failed";

    if (!checkWindowsAttachment(player, source) || !checkWindowsAttachment(versionDir / workerName, source))
        return "windows-blocked";
    if (!executableAccepted(player, elevated, true) || !executableAccepted(versionDir / workerName, elevated, true))
        return "invalid-authenticode";

    std::unique_ptr<QTemporaryDir> writableProbe;
    fs::path probeRoot;
    if (elevated) {
        probeRoot = privileged.path / L"probe";
        if (!protectForAdmins(probeRoot, true) || !privilegedForElevatedUse(probeRoot)) return "privileged-stage-failed";
    } else {
        writableProbe = std::make_unique<QTemporaryDir>(pathString(updates / L"probe-XXXXXX"));
        if (!writableProbe->isValid()) return "stage-create-failed";
        probeRoot = fs::path(writableProbe->path().toStdWString());
    }
    QProcess probe;
    probe.setWorkingDirectory(pathString(versionDir));
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("VEYRA_LOG_FILE"), pathString(probeRoot / L"probe.log"));
    env.insert(QStringLiteral("VEYRA_XBOX_DATA"), pathString(probeRoot / L"xbox"));
    env.insert(QStringLiteral("VEYRA_MOONLIGHT_DATA"), pathString(probeRoot / L"moonlight"));
    env.insert(QStringLiteral("TEMP"), pathString(probeRoot));
    env.insert(QStringLiteral("TMP"), pathString(probeRoot));
    probe.setProcessEnvironment(env);
    if (elevated && !privilegedForElevatedUse(player)) return "privileged-stage-failed";
    if (!runChild(probe, pathString(player),
                  {QStringLiteral("--portable-update-probe"), package->version, QStringLiteral("--data-dir"),
                   pathString(probeRoot / L"profile"), QStringLiteral("--page"), QStringLiteral("set")}, 35000))
        return "startup-check-failed";

    const auto identifier = package->version + u'-'
        + QUuid::createUuid().toString(QUuid::Id128).left(12);
    const auto target = versions / identifier.toStdString();
    if (!pathChainIsPlain(versions) || (elevated && !privilegedForElevatedUse(versionDir))) return "stage-move-failed";
    if (!MoveFileExW(versionDir.c_str(), target.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH))
        return "stage-move-failed";
    if (!setInstalledVersions(root, {identifier.toStdString(), state.current})) {
        MoveFileExW(target.c_str(), versionDir.c_str(), MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH);
        return "activate-failed";
    }
    return "installed";
}

void publishResult(const fs::path& root, const fs::path& updates, const char* result) {
    const auto line = std::string(result) + "\n";
    const auto primary = root / updateFolder;
    std::error_code error;
    fs::create_directories(primary, error);
    if (!error && pathChainIsPlain(primary)) writeSmallFile(primary / L"result.txt", line);
    if (updates != primary && pathChainIsPlain(updates)) writeSmallFile(updates / L"result.txt", line);
}
} // namespace

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const auto arguments = app.arguments();
    int exitCode = 1;
    if ((arguments.size() == 2 || arguments.size() == 3) && SUCCEEDED(com)) {
        const fs::path root(arguments.at(1).toStdWString());
        fs::path updates = root / updateFolder;
        bool elevated = false;
        bool pathsOk = root.is_absolute() && pathChainIsPlain(root) && plainPath(root / packageMarker, false)
            && !packageFlavor(root).empty();
        if (pathsOk && arguments.size() == 3) {
            updates = fs::path(arguments.at(2).toStdWString());
            elevated = true;
            pathsOk = userStagingDirectory(updates);
        }
        if (pathsOk && !pathChainIsPlain(updates)) pathsOk = false;
        if (!pathsOk) {
            exitCode = 1;
        } else {
            const HANDLE mutex = acquireInstallMutex(root);
            if (!mutex) {
                publishResult(root, updates, "install-in-progress");
            } else {
                const auto result = applyUpdate(root, updates, elevated);
                publishResult(root, updates, result);
                ReleaseMutex(mutex);
                CloseHandle(mutex);
                exitCode = std::string_view(result) == "installed" ? 0 : 1;
            }
        }
    }
    if (SUCCEEDED(com)) CoUninitialize();
    return exitCode;
}
