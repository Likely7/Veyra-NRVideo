#pragma once

// Elevated update install. The unelevated user can write the LocalAppData
// staging directory after the UI checks the zip, so that directory is not a
// place to extract or run anything. The elevated worker re-reads the zip
// through a handle that refuses reparse points, copies those bytes into a new
// directory owned by Administrators whose DACL grants write only to SYSTEM and
// Administrators, and only then extracts. Authenticode absence is not the
// trust decision; the signed manifest plus this copy is.

#include "veyra/base/PortableInstallPolicy.h"

#include <windows.h>
#include <aclapi.h>
#include <bcrypt.h>
#include <objbase.h>
#include <shlobj.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "uuid.lib")

namespace veyra::portable {
namespace {

struct SidPair {
    PSID system = nullptr;
    PSID admins = nullptr;
    SidPair() {
        SID_IDENTIFIER_AUTHORITY authority = SECURITY_NT_AUTHORITY;
        AllocateAndInitializeSid(&authority, 1, SECURITY_LOCAL_SYSTEM_RID,
            0, 0, 0, 0, 0, 0, 0, &system);
        AllocateAndInitializeSid(&authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS,
            0, 0, 0, 0, 0, 0, &admins);
    }
    ~SidPair() {
        if (system) FreeSid(system);
        if (admins) FreeSid(admins);
    }
    SidPair(const SidPair&) = delete;
    SidPair& operator=(const SidPair&) = delete;
    bool ok() const { return system && admins; }
};

struct LocalSecurity {
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    ~LocalSecurity() { if (descriptor) LocalFree(descriptor); }
};

inline std::wstring extendedPath(const std::filesystem::path& path) {
    wchar_t full[32768];
    const DWORD length = GetFullPathNameW(path.c_str(), static_cast<DWORD>(std::size(full)), full, nullptr);
    if (!length || length >= std::size(full)) return {};
    std::wstring want(full, length);
    if (want.rfind(L"\\\\?\\", 0) == 0) return want;
    if (want.rfind(L"\\\\", 0) == 0) return L"\\\\?\\UNC\\" + want.substr(2);
    return L"\\\\?\\" + want;
}

inline bool sameExtendedPath(const std::wstring& left, const std::wstring& right) {
    if (left.empty() || right.empty()) return false;
    return CompareStringOrdinal(left.c_str(), -1, right.c_str(), -1, TRUE) == CSTR_EQUAL;
}

inline bool finalPathMatches(HANDLE handle, const std::filesystem::path& expected) {
    wchar_t buffer[32768];
    const DWORD length = GetFinalPathNameByHandleW(handle, buffer, static_cast<DWORD>(std::size(buffer)), FILE_NAME_NORMALIZED);
    if (!length || length >= std::size(buffer)) return false;
    return sameExtendedPath(std::wstring(buffer, length), extendedPath(expected));
}

} // namespace

// Every component, including the leaf. A junction anywhere in the chain is refused.
// The volume root is checked as "C:\", never as the drive-relative path "C:".
inline bool pathChainIsPlain(const std::filesystem::path& path) {
    const auto normal = path.lexically_normal();
    if (!normal.is_absolute() || normal.empty()) return false;
    std::filesystem::path current;
    bool first = true;
    for (const auto& part : normal) {
        if (first) {
            current = part;
            first = false;
            continue;
        }
        current /= part;
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    return true;
}

inline HANDLE openPlainForRead(const std::filesystem::path& path) {
    if (!pathChainIsPlain(path)) return INVALID_HANDLE_VALUE;
    const HANDLE handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return handle;
    FILE_ATTRIBUTE_TAG_INFO info{};
    const bool tagged = GetFileInformationByHandleEx(handle, FileAttributeTagInfo, &info, sizeof(info)) != 0;
    if (!tagged || (info.FileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) || info.ReparseTag != 0
        || (info.FileAttributes & FILE_ATTRIBUTE_DIRECTORY) || !finalPathMatches(handle, path)) {
        CloseHandle(handle);
        return INVALID_HANDLE_VALUE;
    }
    return handle;
}

inline bool sha256OfHandle(HANDLE handle, std::uint8_t out[32], std::uint64_t* bytes) {
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN)) return false;
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return false;
    struct Algorithm {
        BCRYPT_ALG_HANDLE handle;
        ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
    } algorithmHold{algorithm};
    DWORD objectSize = 0, ignored = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
            sizeof(objectSize), &ignored, 0) != 0 || objectSize == 0 || objectSize > 4096) return false;
    std::vector<UCHAR> object(objectSize);
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) != 0) return false;
    struct Hash {
        BCRYPT_HASH_HANDLE handle;
        ~Hash() { if (handle) BCryptDestroyHash(handle); }
    } hashHold{hash};
    std::vector<unsigned char> buffer(1u << 20);
    std::uint64_t total = 0;
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(handle, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) return false;
        if (read == 0) break;
        if (total > maxSealedArchiveBytes || read > maxSealedArchiveBytes - total) return false;
        if (BCryptHashData(hash, buffer.data(), read, 0) != 0) return false;
        total += read;
    }
    if (BCryptFinishHash(hash, out, 32, 0) != 0) return false;
    if (bytes) *bytes = total;
    return true;
}

inline bool readHandleCapped(HANDLE handle, std::size_t cap, std::string& out) {
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(handle, &size) || size.QuadPart <= 0
        || static_cast<std::uint64_t>(size.QuadPart) > cap) return false;
    out.assign(static_cast<std::size_t>(size.QuadPart), '\0');
    std::size_t total = 0;
    while (total < out.size()) {
        DWORD read = 0;
        const auto chunk = static_cast<DWORD>(out.size() - total);
        if (!ReadFile(handle, out.data() + total, chunk, &read, nullptr) || read == 0) return false;
        total += read;
    }
    return true;
}

enum class ArchiveRead { Match, Mismatch, ReparseOrUnreadable };

// Opens the path without following a reparse point, hashes the handle, and on
// a match leaves *locked held with FILE_SHARE_READ only so the file cannot be
// replaced until the caller closes it. A different hash is Mismatch. A reparse
// point, junction in the path, or unreadable file is ReparseOrUnreadable.
inline ArchiveRead openAndHash(const std::filesystem::path& path, const std::uint8_t expected[32],
    std::uint64_t expectedSize, HANDLE* locked) {
    if (locked) *locked = INVALID_HANDLE_VALUE;
    if (!expected || expectedSize == 0 || expectedSize > maxSealedArchiveBytes) return ArchiveRead::ReparseOrUnreadable;
    const HANDLE handle = openPlainForRead(path);
    if (handle == INVALID_HANDLE_VALUE) return ArchiveRead::ReparseOrUnreadable;
    std::uint8_t digest[32]{};
    std::uint64_t size = 0;
    const bool hashed = sha256OfHandle(handle, digest, &size);
    if (!hashed || size != expectedSize || std::memcmp(digest, expected, 32) != 0) {
        CloseHandle(handle);
        return hashed ? ArchiveRead::Mismatch : ArchiveRead::ReparseOrUnreadable;
    }
    if (locked) *locked = handle;
    else CloseHandle(handle);
    return ArchiveRead::Match;
}

inline bool daclIsAdminAndSystemOnly(const std::filesystem::path& path) {
    SidPair sids;
    if (!sids.ok()) return false;
    PACL dacl = nullptr;
    LocalSecurity security;
    if (GetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
            DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &security.descriptor) != ERROR_SUCCESS) {
        return false;
    }
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (!security.descriptor || !GetSecurityDescriptorControl(security.descriptor, &control, &revision)) return false;
    if ((control & SE_DACL_PROTECTED) == 0 || !dacl) return false;
    bool sawSystem = false, sawAdmins = false;
    for (DWORD i = 0; i < dacl->AceCount; ++i) {
        LPVOID raw = nullptr;
        if (!GetAce(dacl, i, &raw) || !raw) return false;
        const auto* header = static_cast<ACE_HEADER*>(raw);
        const bool deny = header->AceType == ACCESS_DENIED_ACE_TYPE;
        const bool allow = header->AceType == ACCESS_ALLOWED_ACE_TYPE;
        if (!deny && !allow) return false;
        auto* ace = static_cast<ACCESS_ALLOWED_ACE*>(raw);
        const PSID sid = reinterpret_cast<PSID>(&ace->SidStart);
        if (!IsValidSid(sid)) return false;
        const bool system = EqualSid(sid, sids.system) == TRUE;
        const bool admins = EqualSid(sid, sids.admins) == TRUE;
        if (deny) {
            if (system || admins) return false;
            continue;
        }
        if (!system && !admins) return false;
        if (system) sawSystem = true;
        if (admins) sawAdmins = true;
    }
    return sawSystem && sawAdmins;
}

inline bool ownerIsAdministrators(const std::filesystem::path& path) {
    SidPair sids;
    if (!sids.ok()) return false;
    PSID owner = nullptr;
    LocalSecurity security;
    if (GetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION, &owner, nullptr, nullptr, nullptr, &security.descriptor) != ERROR_SUCCESS) {
        return false;
    }
    return owner && EqualSid(owner, sids.admins) == TRUE;
}

// True only for an object the unelevated user cannot write: plain path, owner
// is Administrators, and the protected DACL allows SYSTEM and Administrators only.
inline bool privilegedForElevatedUse(const std::filesystem::path& path) {
    return pathChainIsPlain(path) && daclIsAdminAndSystemOnly(path) && ownerIsAdministrators(path);
}

inline bool protectForAdmins(const std::filesystem::path& path, bool createDirectory, bool takeOwner = true) {
    SidPair sids;
    if (!sids.ok()) return false;
    EXPLICIT_ACCESSW entries[2]{};
    for (EXPLICIT_ACCESSW& entry : entries) {
        entry.grfAccessPermissions = GENERIC_ALL;
        entry.grfAccessMode = SET_ACCESS;
        entry.grfInheritance = SUB_CONTAINERS_AND_OBJECTS_INHERIT;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    }
    entries[0].Trustee.ptstrName = static_cast<LPWSTR>(sids.system);
    entries[1].Trustee.ptstrName = static_cast<LPWSTR>(sids.admins);
    PACL acl = nullptr;
    if (SetEntriesInAclW(2, entries, nullptr, &acl) != ERROR_SUCCESS || !acl) return false;
    struct AclGuard {
        PACL acl;
        ~AclGuard() { if (acl) LocalFree(acl); }
    } aclGuard{acl};
    if (createDirectory) {
        SECURITY_DESCRIPTOR created{};
        if (!InitializeSecurityDescriptor(&created, SECURITY_DESCRIPTOR_REVISION)) return false;
        if (!SetSecurityDescriptorDacl(&created, TRUE, acl, FALSE)) return false;
        SetSecurityDescriptorControl(&created, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
        SetSecurityDescriptorOwner(&created, sids.admins, FALSE);
        SECURITY_ATTRIBUTES attributes{sizeof(attributes), &created, FALSE};
        if (!CreateDirectoryW(path.c_str(), &attributes)) return false;
    }
    DWORD info = DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION;
    PSID owner = nullptr;
    if (takeOwner) {
        info |= OWNER_SECURITY_INFORMATION;
        owner = sids.admins;
    }
    const DWORD set = SetNamedSecurityInfoW(const_cast<wchar_t*>(path.c_str()), SE_FILE_OBJECT,
        info, owner, nullptr, acl, nullptr);
    if (set != ERROR_SUCCESS || !daclIsAdminAndSystemOnly(path)) return false;
    return !takeOwner || ownerIsAdministrators(path);
}

inline bool directoryIsEmpty(const std::filesystem::path& path) {
    WIN32_FIND_DATAW data{};
    const HANDLE find = FindFirstFileW((path / L"*").c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) return false;
    bool empty = true;
    do {
        if (std::wcscmp(data.cFileName, L".") != 0 && std::wcscmp(data.cFileName, L"..") != 0) {
            empty = false;
            break;
        }
    } while (FindNextFileW(find, &data));
    FindClose(find);
    return empty;
}

inline void removePlainTree(const std::filesystem::path& directory) {
    WIN32_FIND_DATAW data{};
    const HANDLE find = FindFirstFileW((directory / L"*").c_str(), &data);
    if (find != INVALID_HANDLE_VALUE) {
        do {
            if (std::wcscmp(data.cFileName, L".") == 0 || std::wcscmp(data.cFileName, L"..") == 0) continue;
            const auto child = directory / data.cFileName;
            if (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) RemoveDirectoryW(child.c_str());
                else DeleteFileW(child.c_str());
                continue;
            }
            if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) removePlainTree(child);
            else DeleteFileW(child.c_str());
        } while (FindNextFileW(find, &data));
        FindClose(find);
    }
    RemoveDirectoryW(directory.c_str());
}

inline bool treePrivilegedForElevatedUse(const std::filesystem::path& root) {
    if (!privilegedForElevatedUse(root)) return false;
    std::error_code error;
    std::filesystem::recursive_directory_iterator it(root, error), end;
    for (; !error && it != end; it.increment(error)) {
        if (!privilegedForElevatedUse(it->path())) return false;
    }
    return !error;
}

struct SealedArchive {
    HANDLE handle = INVALID_HANDLE_VALUE;
    std::filesystem::path path;
    ~SealedArchive() {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
    }
    SealedArchive() = default;
    SealedArchive(const SealedArchive&) = delete;
    SealedArchive& operator=(const SealedArchive&) = delete;
};

enum class SealStatus { Sealed, NotPrivileged, Mismatch, Failed };

// Copies source into destDirectory and re-opens the copy. Refuses before
// writing when the destination is not an Administrators-only directory.
inline SealStatus sealFromLockedSource(HANDLE source, const std::filesystem::path& destDirectory,
    const std::uint8_t expected[32], std::uint64_t expectedSize, SealedArchive& sealed) {
    if (sealed.handle != INVALID_HANDLE_VALUE) {
        CloseHandle(sealed.handle);
        sealed.handle = INVALID_HANDLE_VALUE;
    }
    sealed.path.clear();
    if (!privilegedForElevatedUse(destDirectory) || !directoryIsEmpty(destDirectory)) return SealStatus::NotPrivileged;
    if (source == INVALID_HANDLE_VALUE || !expected || expectedSize == 0 || expectedSize > maxSealedArchiveBytes)
        return SealStatus::Failed;
    const auto destination = destDirectory / L"download.zip";
    SidPair sids;
    if (!sids.ok()) return SealStatus::Failed;
    EXPLICIT_ACCESSW entries[2]{};
    for (EXPLICIT_ACCESSW& entry : entries) {
        entry.grfAccessPermissions = GENERIC_ALL;
        entry.grfAccessMode = SET_ACCESS;
        entry.grfInheritance = NO_INHERITANCE;
        entry.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        entry.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    }
    entries[0].Trustee.ptstrName = static_cast<LPWSTR>(sids.system);
    entries[1].Trustee.ptstrName = static_cast<LPWSTR>(sids.admins);
    PACL acl = nullptr;
    if (SetEntriesInAclW(2, entries, nullptr, &acl) != ERROR_SUCCESS || !acl) return SealStatus::Failed;
    struct AclGuard {
        PACL acl;
        ~AclGuard() { if (acl) LocalFree(acl); }
    } aclGuard{acl};
    SECURITY_DESCRIPTOR descriptor{};
    if (!InitializeSecurityDescriptor(&descriptor, SECURITY_DESCRIPTOR_REVISION)
        || !SetSecurityDescriptorDacl(&descriptor, TRUE, acl, FALSE)) return SealStatus::Failed;
    SetSecurityDescriptorControl(&descriptor, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    SetSecurityDescriptorOwner(&descriptor, sids.admins, FALSE);
    SECURITY_ATTRIBUTES attributes{sizeof(attributes), &descriptor, FALSE};
    HANDLE writer = CreateFileW(destination.c_str(), GENERIC_WRITE, 0, &attributes, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (writer == INVALID_HANDLE_VALUE) return SealStatus::Failed;
    LARGE_INTEGER zero{};
    if (!SetFilePointerEx(source, zero, nullptr, FILE_BEGIN)) {
        CloseHandle(writer);
        DeleteFileW(destination.c_str());
        return SealStatus::Failed;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        CloseHandle(writer);
        DeleteFileW(destination.c_str());
        return SealStatus::Failed;
    }
    struct Algorithm {
        BCRYPT_ALG_HANDLE handle;
        ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
    } algorithmHold{algorithm};
    DWORD objectSize = 0, ignored = 0;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> object;
    bool hashOk = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
            sizeof(objectSize), &ignored, 0) == 0 && objectSize > 0 && objectSize <= 4096;
    if (hashOk) {
        object.resize(objectSize);
        hashOk = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) == 0;
    }
    std::vector<unsigned char> buffer(1u << 20);
    std::uint64_t total = 0;
    while (hashOk) {
        DWORD read = 0;
        if (!ReadFile(source, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr)) hashOk = false;
        else if (read == 0) break;
        else if (total > expectedSize || read > expectedSize - total) hashOk = false;
        else {
            DWORD written = 0;
            if (BCryptHashData(hash, buffer.data(), read, 0) != 0
                || !WriteFile(writer, buffer.data(), read, &written, nullptr) || written != read) hashOk = false;
            else total += read;
        }
    }
    std::uint8_t digest[32]{};
    const bool finished = hashOk && hash && BCryptFinishHash(hash, digest, 32, 0) == 0;
    if (hash) BCryptDestroyHash(hash);
    CloseHandle(writer);
    const bool matched = finished && total == expectedSize && std::memcmp(digest, expected, 32) == 0;
    if (!matched) {
        DeleteFileW(destination.c_str());
        return finished ? SealStatus::Mismatch : SealStatus::Failed;
    }
    if (SetNamedSecurityInfoW(const_cast<wchar_t*>(destination.c_str()), SE_FILE_OBJECT,
            OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
            sids.admins, nullptr, acl, nullptr) != ERROR_SUCCESS) {
        DeleteFileW(destination.c_str());
        return SealStatus::NotPrivileged;
    }
    const HANDLE reader = openPlainForRead(destination);
    if (reader == INVALID_HANDLE_VALUE || !privilegedForElevatedUse(destination)) {
        if (reader != INVALID_HANDLE_VALUE) CloseHandle(reader);
        DeleteFileW(destination.c_str());
        return SealStatus::NotPrivileged;
    }
    std::uint8_t again[32]{};
    std::uint64_t againSize = 0;
    if (!sha256OfHandle(reader, again, &againSize) || againSize != expectedSize || std::memcmp(again, expected, 32) != 0) {
        CloseHandle(reader);
        DeleteFileW(destination.c_str());
        return SealStatus::Mismatch;
    }
    sealed.handle = reader;
    sealed.path = destination;
    return SealStatus::Sealed;
}

inline std::filesystem::path knownFolderPath(REFKNOWNFOLDERID id) {
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(id, KF_FLAG_DEFAULT, nullptr, &raw)) || !raw) return {};
    std::filesystem::path path(raw);
    CoTaskMemFree(raw);
    return path;
}

inline std::filesystem::path localUpdateStagingBase() {
    const auto local = knownFolderPath(FOLDERID_LocalAppData);
    if (local.empty()) return {};
    return local / L"Veyra" / L"update-staging";
}

inline bool userStagingDirectory(const std::filesystem::path& staging) {
    const auto base = localUpdateStagingBase();
    if (base.empty() || !pathChainIsPlain(staging)) return false;
    return stagingDirectoryId(base, staging);
}

// Creates ProgramData\Veyra\update-install without changing ProgramData itself.
// Fails when any component is a reparse point. The leaf is Administrators-only.
inline bool preparePrivilegedRoot(std::filesystem::path& root) {
    const auto base = knownFolderPath(FOLDERID_ProgramData);
    if (base.empty() || !pathChainIsPlain(base)) return false;
    const auto veyra = base / L"Veyra";
    root = veyra / L"update-install";
    auto ensure = [](const std::filesystem::path& directory) {
        const DWORD attributes = GetFileAttributesW(directory.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) return protectForAdmins(directory, true);
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT) return false;
        return protectForAdmins(directory, false);
    };
    return ensure(veyra) && ensure(root) && pathChainIsPlain(root);
}

inline bool createFreshPrivilegedDirectory(const std::filesystem::path& parent, std::filesystem::path& created) {
    if (!privilegedForElevatedUse(parent)) return false;
    for (int attempt = 0; attempt < 5; ++attempt) {
        std::uint8_t random[16]{};
        if (BCryptGenRandom(nullptr, random, sizeof(random), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) return false;
        std::wstring name(32, L'0');
        for (int i = 0; i < 16; ++i) {
            static constexpr wchar_t hex[] = L"0123456789abcdef";
            name[static_cast<size_t>(i) * 2] = hex[random[i] >> 4];
            name[static_cast<size_t>(i) * 2 + 1] = hex[random[i] & 0xf];
        }
        const auto directory = parent / name;
        if (GetFileAttributesW(directory.c_str()) != INVALID_FILE_ATTRIBUTES) continue;
        if (!protectForAdmins(directory, true) || !directoryIsEmpty(directory)) {
            RemoveDirectoryW(directory.c_str());
            continue;
        }
        created = directory;
        return true;
    }
    return false;
}

inline std::wstring installMutexName(const std::filesystem::path& root) {
    const auto text = root.lexically_normal().wstring();
    std::uint8_t digest[32]{};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) return {};
    DWORD objectSize = 0, ignored = 0;
    std::vector<UCHAR> object;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectSize),
            sizeof(objectSize), &ignored, 0) == 0 && objectSize > 0 && objectSize <= 4096;
    if (ok) {
        object.resize(objectSize);
        ok = BCryptCreateHash(algorithm, &hash, object.data(), objectSize, nullptr, 0, 0) == 0
            && BCryptHashData(hash, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(text.data())),
                static_cast<ULONG>(text.size() * sizeof(wchar_t)), 0) == 0
            && BCryptFinishHash(hash, digest, 32, 0) == 0;
    }
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return {};
    std::wstring name = L"Local\\VeyraUpdateInstall-";
    static constexpr wchar_t hex[] = L"0123456789abcdef";
    for (int i = 0; i < 16; ++i) {
        name.push_back(hex[digest[i] >> 4]);
        name.push_back(hex[digest[i] & 0xf]);
    }
    return name;
}

// Owns the mutex on success. A live owner makes this return null so a second
// install cannot start. An abandoned mutex (the previous worker died) is taken.
inline HANDLE acquireInstallMutex(const std::filesystem::path& root) {
    const auto name = installMutexName(root);
    if (name.empty()) return nullptr;
    const HANDLE mutex = CreateMutexW(nullptr, TRUE, name.c_str());
    if (!mutex) return nullptr;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        const DWORD wait = WaitForSingleObject(mutex, 0);
        if (wait != WAIT_ABANDONED) {
            CloseHandle(mutex);
            return nullptr;
        }
    }
    return mutex;
}

} // namespace veyra::portable
