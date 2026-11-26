#include "veyra/base/PortableInstallPolicy.h"
#include "veyra/base/PortablePrivileged.h"
#include <shellapi.h>
#include <winioctl.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace veyra::portable;

namespace {
int failures = 0;

void expect(bool condition, const char* message) {
    if (condition) return;
    std::cerr << "FAIL " << message << '\n';
    ++failures;
}

bool writeBytes(const fs::path& path, std::string_view bytes) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(file);
}

bool makeJunction(const fs::path& junction, const fs::path& target) {
    if (!CreateDirectoryW(junction.c_str(), nullptr)) return false;
    const HANDLE dir = CreateFileW(junction.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (dir == INVALID_HANDLE_VALUE) return false;
    const std::wstring substitute = L"\\??\\" + target.lexically_normal().wstring();
    const std::wstring print = target.lexically_normal().wstring();
    const auto substituteChars = substitute.size();
    const auto printChars = print.size();
    const auto bufferChars = substituteChars + 1 + printChars + 1;
    std::vector<unsigned char> raw(8 + 8 + bufferChars * sizeof(wchar_t));
    auto* bytes = raw.data();
    const auto write16 = [&](size_t offset, WORD value) { std::memcpy(bytes + offset, &value, 2); };
    const auto write32 = [&](size_t offset, DWORD value) { std::memcpy(bytes + offset, &value, 4); };
    write32(0, IO_REPARSE_TAG_MOUNT_POINT);
    const WORD dataLength = static_cast<WORD>(8 + bufferChars * sizeof(wchar_t));
    write16(4, dataLength);
    write16(6, 0);
    write16(8, 0);
    write16(10, static_cast<WORD>(substituteChars * sizeof(wchar_t)));
    write16(12, static_cast<WORD>((substituteChars + 1) * sizeof(wchar_t)));
    write16(14, static_cast<WORD>(printChars * sizeof(wchar_t)));
    std::memcpy(bytes + 16, substitute.data(), substituteChars * sizeof(wchar_t));
    const wchar_t nul = 0;
    std::memcpy(bytes + 16 + substituteChars * sizeof(wchar_t), &nul, sizeof(nul));
    std::memcpy(bytes + 16 + (substituteChars + 1) * sizeof(wchar_t), print.data(), printChars * sizeof(wchar_t));
    std::memcpy(bytes + 16 + (substituteChars + 1 + printChars) * sizeof(wchar_t), &nul, sizeof(nul));
    DWORD returned = 0;
    const BOOL ok = DeviceIoControl(dir, FSCTL_SET_REPARSE_POINT, raw.data(), static_cast<DWORD>(8 + dataLength),
                                    nullptr, 0, &returned, nullptr);
    CloseHandle(dir);
    return ok == TRUE;
}
}

int main() {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv || argc != 2) {
        std::cerr << "usage: veyra_portable_update_security_tests <work-directory>\n";
        if (argv) LocalFree(argv);
        return 2;
    }
    const fs::path work(argv[1]);
    LocalFree(argv);
    std::error_code error;
    fs::remove_all(work, error);
    fs::create_directories(work, error);
    if (error) {
        std::cerr << "cannot create work directory\n";
        return 2;
    }

    const PlayerExitState quitWhileElevated{0, true, false, true, true};
    const PlayerExitState quitAfterStaging{0, true, false, true, false};
    const PlayerExitState quitWritable{0, true, false, false, false};
    const PlayerExitState installButton{applyRestartExitCode, true, false, false, false};
    const PlayerExitState installWhileExporting{applyRestartExitCode, true, true, false, false};
    const PlayerExitState explicitApply{applyExitCode, true, false, false, false};
    expect(!launcherStartsWorker(launcherExitCode(quitWhileElevated)), "quit during elevated install does not start a second worker");
    expect(!launcherStartsWorker(launcherExitCode(quitAfterStaging)), "quit with an elevated package does not start the root worker");
    expect(!launcherStartsWorker(launcherExitCode(quitWritable)), "a normal quit does not install a writable package");
    expect(launcherExitCode(installButton) == applyRestartExitCode, "the install button still installs a writable package");
    expect(!launcherStartsWorker(launcherExitCode(installWhileExporting)), "an export blocks the install button");
    expect(launcherExitCode(explicitApply) == applyExitCode, "an explicit install request is preserved");
    expect(elevatedInstallMayStart(true, false, false, true), "first elevated install may start");
    expect(!elevatedInstallMayStart(true, true, false, true), "a second elevated install cannot start while one is running");
    expect(!elevatedInstallMayStart(true, false, true, true), "UAC wait counts as already started");
    expect(elevatedFileMayRun(AuthenticodeClass::Absent, true, true), "unsigned bytes may run only from a privileged extract");
    expect(!elevatedFileMayRun(AuthenticodeClass::Absent, false, true), "unsigned bytes in a user-writable directory are refused");
    expect(!elevatedFileMayRun(AuthenticodeClass::Valid, true, false), "a hash mismatch is refused");
    expect(!elevatedFileMayRun(AuthenticodeClass::Rejected, true, true), "a bad Authenticode signature is refused");

    const ApiRedirectView github{"https", "api.github.com", officialLatestApiPath, "", "", "", -1};
    const ApiRedirectView proxy{"https", "gh-proxy.com", mirrorPrefixedLatestApiPath, "", "", "", -1};
    const ApiRedirectView fast{"https", "ghfast.top", mirrorPrefixedLatestApiPath, "", "", "", -1};
    ApiRedirectView queried = github;
    queried.query = "x=1";
    const ApiRedirectView evilProxy{"https", "gh-proxy.com", "/https://evil.example/releases/latest", "", "", "", -1};
    expect(allowedApiRedirectView(github, DownloadRouteId::GitHub), "GitHub route allows the official API");
    expect(!allowedApiRedirectView(proxy, DownloadRouteId::GitHub), "GitHub route does not use gh-proxy.com");
    expect(!allowedApiRedirectView(fast, DownloadRouteId::GitHub), "GitHub route does not use ghfast.top");
    expect(allowedApiRedirectView(proxy, DownloadRouteId::GhProxy), "selected gh-proxy route allows that mirror");
    expect(!allowedApiRedirectView(fast, DownloadRouteId::GhProxy), "gh-proxy route does not allow ghfast.top");
    expect(allowedApiRedirectView(github, DownloadRouteId::GhProxy), "a mirror route still allows the official API");
    expect(!allowedApiRedirectView(evilProxy, DownloadRouteId::GhProxy), "a mirror cannot prefix an unofficial URL");
    expect(!allowedApiRedirectView(queried, DownloadRouteId::GitHub), "an API redirect with a query is refused");
    expect(!acceptableGitHubAssetName("../evil.zip"), "a traversal asset name is not acceptable");
    expect(acceptableGitHubAssetName("Veyra-2.2.1-NVIDIA-win64-portable.zip"), "a normal asset name is acceptable");

    const fs::path stageBase("C:/Users/a/AppData/Local/Veyra/update-staging");
    expect(stagingDirectoryId(stageBase, stageBase / "0123456789abcdef"), "staging id");
    expect(!stagingDirectoryId(stageBase, stageBase / "0123456789ABCDEF"), "uppercase staging id");
    expect(!stagingDirectoryId(stageBase, stageBase / "0123456789abcdef" / "extra"), "nested staging id");
    expect(!stagingDirectoryId(stageBase, stageBase / ".." / "0123456789abcdef"), "staging escape");
    expect(!stagingDirectoryId(stageBase, fs::path("D:/elsewhere/0123456789abcdef")), "staging on another drive");
    const fs::path base = stageBase;
    std::wstring folded = base.wstring();
    for (wchar_t& character : folded) {
        if (character >= L'A' && character <= L'Z') character = static_cast<wchar_t>(character - L'A' + L'a');
        else if (character >= L'a' && character <= L'z') character = static_cast<wchar_t>(character - L'a' + L'A');
    }
    expect(stagingDirectoryId(fs::path(folded), base / L"0123456789abcdef"), "staging id matches ignoring case");

    const fs::path real = work / "real";
    fs::create_directories(real);
    const fs::path archive = real / "download.zip";
    const std::string original = "veyra-update-bytes";
    expect(writeBytes(archive, original), "write sample archive");
    HANDLE reader = openPlainForRead(archive);
    expect(reader != INVALID_HANDLE_VALUE, "a normal archive opens");
    std::uint8_t digest[32]{};
    std::uint64_t size = 0;
    expect(reader != INVALID_HANDLE_VALUE && sha256OfHandle(reader, digest, &size) && size == original.size(), "sample archive hashes");
    if (reader != INVALID_HANDLE_VALUE) CloseHandle(reader);

    expect(writeBytes(archive, "VEYRA-UPDATE-BYTES"), "overwrite sample archive");
    HANDLE locked = INVALID_HANDLE_VALUE;
    expect(openAndHash(archive, digest, original.size(), &locked) == ArchiveRead::Mismatch, "tampered staging bytes are refused");
    expect(locked == INVALID_HANDLE_VALUE, "a mismatch does not keep a lock");
    expect(writeBytes(archive, original), "restore sample archive");
    expect(openAndHash(archive, digest, original.size(), &locked) == ArchiveRead::Match, "restored archive matches");
    const HANDLE writer = CreateFileW(archive.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    expect(writer == INVALID_HANDLE_VALUE, "the locked archive cannot be replaced");
    if (writer != INVALID_HANDLE_VALUE) CloseHandle(writer);

    const fs::path emptyUser = work / "empty-user";
    fs::create_directory(emptyUser);
    SealedArchive sealed;
    expect(!privilegedForElevatedUse(emptyUser), "a user-writable directory is not privileged");
    expect(sealFromLockedSource(locked, emptyUser, digest, original.size(), sealed) == SealStatus::NotPrivileged,
           "elevated copy refuses a user-writable destination");

    const fs::path junction = work / "junction";
    expect(makeJunction(junction, real), "test junction can be created");
    expect(!pathChainIsPlain(junction / "download.zip"), "a junction in the path is not a plain chain");
    HANDLE followed = INVALID_HANDLE_VALUE;
    expect(openAndHash(junction / "download.zip", digest, original.size(), &followed) == ArchiveRead::ReparseOrUnreadable,
           "a reparse-point path is not the hashed file");
    expect(followed == INVALID_HANDLE_VALUE, "a reparse open does not return a handle");

    const fs::path link = work / "download.link";
    if (CreateSymbolicLinkW(link.c_str(), archive.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        expect(openPlainForRead(link) == INVALID_HANDLE_VALUE, "a file symlink is refused");
        DeleteFileW(link.c_str());
    } else {
        std::cout << "note: file symlink was not created (GetLastError=" << GetLastError()
                  << "); the junction test covers the reparse refusal\n";
    }

    const fs::path daclOnly = work / "dacl-only";
    fs::create_directory(daclOnly);
    expect(protectForAdmins(daclOnly, false, false), "an admin-only DACL can be written without taking ownership");
    expect(daclIsAdminAndSystemOnly(daclOnly), "that DACL grants SYSTEM and Administrators only");
    expect(!ownerIsAdministrators(daclOnly), "the creating user is still the owner");
    expect(!privilegedForElevatedUse(daclOnly), "a user-owned directory is refused even with an admin DACL");
    SealedArchive ownedByUser;
    expect(sealFromLockedSource(locked, daclOnly, digest, original.size(), ownedByUser) == SealStatus::NotPrivileged,
           "elevated copy refuses a directory the user still owns");

    const fs::path admin = work / "admin-only";
    if (protectForAdmins(admin, true)) {
        expect(privilegedForElevatedUse(admin), "Administrators-only directory is an allowed elevated destination");
        expect(directoryIsEmpty(admin), "new privileged directory starts empty");
        SealedArchive copied;
        const auto status = sealFromLockedSource(locked, admin, digest, original.size(), copied);
        expect(status == SealStatus::Sealed, "sealed copy into the privileged directory succeeds");
        if (status == SealStatus::Sealed) {
            std::uint8_t again[32]{};
            std::uint64_t againSize = 0;
            expect(sha256OfHandle(copied.handle, again, &againSize) && againSize == original.size()
                       && std::memcmp(again, digest, 32) == 0,
                   "sealed copy still matches the manifest hash");
            expect(privilegedForElevatedUse(copied.path), "sealed file is not user-writable");
        }
    } else {
        std::cout << "note: Administrators-owned directory was not created (GetLastError=" << GetLastError()
                  << "). This process is not elevated. User-writable destinations were still refused.\n";
    }
    if (locked != INVALID_HANDLE_VALUE) CloseHandle(locked);

    const HANDLE first = acquireInstallMutex(work);
    const HANDLE second = acquireInstallMutex(work);
    expect(first != nullptr, "first install mutex is acquired");
    expect(second == nullptr, "second install mutex is refused while the first is held");
    if (first) {
        ReleaseMutex(first);
        CloseHandle(first);
    }
    const HANDLE third = acquireInstallMutex(work);
    expect(third != nullptr, "install mutex can be acquired after the first owner releases it");
    if (third) {
        ReleaseMutex(third);
        CloseHandle(third);
    }

    RemoveDirectoryW(junction.c_str());
    if (fs::exists(admin)) removePlainTree(admin);
    fs::remove_all(work, error);
    if (failures) {
        std::cerr << failures << " failed\n";
        return 1;
    }
    std::cout << "portable update security tests passed\n";
    return 0;
}
