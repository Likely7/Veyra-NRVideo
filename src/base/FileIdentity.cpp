#include "veyra/FileIdentity.h"

#include <windows.h>
#include <bcrypt.h>
#include <softpub.h>
#include <wintrust.h>
#include <wincrypt.h>

#include <array>
#include <format>
#include <vector>

#include "veyra/Log.h"

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "wintrust.lib")
#pragma comment(lib, "crypt32.lib")
#pragma comment(lib, "version.lib")

namespace veyra {

namespace {

std::string narrow(const std::wstring& text)
{
    if (text.empty()) {
        return {};
    }
    const int length = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) {
        return {};
    }
    std::string converted(static_cast<size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), static_cast<int>(text.size()), converted.data(), length, nullptr, nullptr);
    return converted;
}

constexpr size_t kHashChunkBytes = 1u << 20; // 1 MiB read chunks

std::string bytesToHexUpper(const std::array<unsigned char, 32>& bytes)
{
    static const char* kHex = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(bytes.size() * 2);
    for (const unsigned char byte : bytes) {
        hex.push_back(kHex[byte >> 4]);
        hex.push_back(kHex[byte & 0x0F]);
    }
    return hex;
}

bool computeSha256(const std::wstring& path, std::string& hexOut, uint64_t& sizeOut)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        log::error("base", std::format("sha256: BCryptOpenAlgorithmProvider failed ntstatus=0x{:X}", static_cast<uint32_t>(status)));
        return false;
    }

    BCRYPT_HASH_HANDLE hash = nullptr;
    status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        log::error("base", std::format("sha256: BCryptCreateHash failed ntstatus=0x{:X}", static_cast<uint32_t>(status)));
        return false;
    }

    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        const DWORD lastError = GetLastError();
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        log::error("base", std::format("sha256: CreateFileW failed lastError={} path-suffix-check", lastError));
        return false;
    }

    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size)) {
        const DWORD lastError = GetLastError();
        CloseHandle(file);
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        log::error("base", std::format("sha256: GetFileSizeEx failed lastError={}", lastError));
        return false;
    }
    sizeOut = static_cast<uint64_t>(size.QuadPart);

    std::vector<unsigned char> buffer(kHashChunkBytes);
    for (;;) {
        DWORD readBytes = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &readBytes, nullptr)) {
            const DWORD lastError = GetLastError();
            CloseHandle(file);
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            log::error("base", std::format("sha256: ReadFile failed lastError={}", lastError));
            return false;
        }
        if (readBytes == 0) {
            break;
        }
        status = BCryptHashData(hash, buffer.data(), readBytes, 0);
        if (!BCRYPT_SUCCESS(status)) {
            CloseHandle(file);
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            log::error("base", std::format("sha256: BCryptHashData failed ntstatus=0x{:X}", static_cast<uint32_t>(status)));
            return false;
        }
    }
    CloseHandle(file);

    std::array<unsigned char, 32> digest{};
    status = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0);
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!BCRYPT_SUCCESS(status)) {
        log::error("base", std::format("sha256: BCryptFinishHash failed ntstatus=0x{:X}", static_cast<uint32_t>(status)));
        return false;
    }

    hexOut = bytesToHexUpper(digest);
    return true;
}

bool queryVersionStrings(const std::wstring& path, std::wstring& fileVersion, std::wstring& productVersion)
{
    DWORD handle = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.c_str(), &handle);
    if (size == 0) {
        return false;
    }
    std::vector<unsigned char> buffer(size);
    if (!GetFileVersionInfoW(path.c_str(), 0, size, buffer.data())) {
        return false;
    }
    VS_FIXEDFILEINFO* fixedInfo = nullptr;
    UINT fixedInfoLength = 0;
    if (VerQueryValueW(buffer.data(), L"\\", reinterpret_cast<void**>(&fixedInfo), &fixedInfoLength) && fixedInfoLength >= sizeof(VS_FIXEDFILEINFO)) {
        fileVersion = std::format(L"{}.{}.{}.{}",
            HIWORD(fixedInfo->dwFileVersionMS), LOWORD(fixedInfo->dwFileVersionMS),
            HIWORD(fixedInfo->dwFileVersionLS), LOWORD(fixedInfo->dwFileVersionLS));
        productVersion = std::format(L"{}.{}.{}.{}",
            HIWORD(fixedInfo->dwProductVersionMS), LOWORD(fixedInfo->dwProductVersionMS),
            HIWORD(fixedInfo->dwProductVersionLS), LOWORD(fixedInfo->dwProductVersionLS));
        return true;
    }
    return false;
}

bool verifyAuthenticode(const std::wstring& path, uint32_t& winTrustError)
{
    WINTRUST_FILE_INFO fileInfo{};
    fileInfo.cbStruct = sizeof(fileInfo);
    fileInfo.pcwszFilePath = path.c_str();

    WINTRUST_DATA trustData{};
    trustData.cbStruct = sizeof(trustData);
    trustData.dwUIChoice = WTD_UI_NONE;
    trustData.fdwRevocationChecks = WTD_REVOKE_NONE;
    trustData.dwUnionChoice = WTD_CHOICE_FILE;
    trustData.pFile = &fileInfo;
    trustData.dwStateAction = WTD_STATEACTION_VERIFY;
    trustData.dwProvFlags = WTD_SAFER_FLAG;

    GUID verifyAction = WINTRUST_ACTION_GENERIC_VERIFY_V2;
    const LONG result = WinVerifyTrust(nullptr, &verifyAction, &trustData);
    winTrustError = static_cast<uint32_t>(result);

    WINTRUST_DATA closeData = trustData;
    closeData.dwStateAction = WTD_STATEACTION_CLOSE;
    WinVerifyTrust(nullptr, &verifyAction, &closeData);

    return result == 0;
}

std::wstring querySignerSubject(const std::wstring& path)
{
    HCERTSTORE store = nullptr;
    HCRYPTMSG message = nullptr;
    if (!CryptQueryObject(CERT_QUERY_OBJECT_FILE, path.c_str(),
            CERT_QUERY_CONTENT_FLAG_PKCS7_SIGNED_EMBED,
            CERT_QUERY_FORMAT_FLAG_BINARY,
            0, nullptr, nullptr, nullptr, &store, &message, nullptr)) {
        return {};
    }

    std::wstring subject;
    DWORD signerInfoSize = 0;
    if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, nullptr, &signerInfoSize) && signerInfoSize > 0) {
        std::vector<unsigned char> signerBuffer(signerInfoSize);
        auto* signerInfo = reinterpret_cast<PCMSG_SIGNER_INFO>(signerBuffer.data());
        if (CryptMsgGetParam(message, CMSG_SIGNER_INFO_PARAM, 0, signerInfo, &signerInfoSize)) {
            CERT_INFO searchInfo{};
            searchInfo.Issuer = signerInfo->Issuer;
            searchInfo.SerialNumber = signerInfo->SerialNumber;
            const PCCERT_CONTEXT certificate = CertFindCertificateInStore(
                store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING, 0, CERT_FIND_SUBJECT_CERT, &searchInfo, nullptr);
            if (certificate != nullptr) {
                wchar_t nameBuffer[512]{};
                const DWORD nameLength = CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, nameBuffer, 512);
                if (nameLength > 0) {
                    subject = nameBuffer;
                }
                CertFreeCertificateContext(certificate);
            }
        }
    }

    CryptMsgClose(message);
    CertCloseStore(store, CERT_CLOSE_STORE_FORCE_FLAG);
    return subject;
}

} // namespace

const char* identityErrorString(IdentityError error)
{
    switch (error) {
    case IdentityError::None: return "None";
    case IdentityError::FileOpen: return "FileOpen";
    case IdentityError::Read: return "Read";
    case IdentityError::Hash: return "Hash";
    case IdentityError::VersionInfo: return "VersionInfo";
    case IdentityError::Signature: return "Signature";
    default: return "Unknown";
    }
}

bool computeFileSha256(const std::wstring& path, std::string& hex, uint64_t& size)
{
    return computeSha256(path, hex, size);
}

bool computeFileIdentity(const std::wstring& path, FileIdentity& out, IdentityError& error)
{
    error = IdentityError::None;
    out = FileIdentity{};
    out.path = path;

    const DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES || (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
        error = IdentityError::FileOpen;
        return false;
    }

    if (!computeSha256(path, out.sha256Upper, out.sizeBytes)) {
        error = IdentityError::Hash;
        return false;
    }

    // Version strings are optional: unsigned binaries may lack them entirely.
    (void)queryVersionStrings(path, out.fileVersion, out.productVersion);

    out.signatureValid = verifyAuthenticode(path, out.winTrustError);
    if (out.signatureValid) {
        out.signerSubject = querySignerSubject(path);
        out.signerIsNvidia = out.signerSubject.find(L"NVIDIA") != std::wstring::npos;
    }

    log::info("base", std::format("file-identity path-size={} sha256={} fileVersion={} signature={} signer={}",
        out.sizeBytes,
        out.sha256Upper.empty() ? std::string("<none>") : out.sha256Upper,
        out.fileVersion.empty() ? std::string("<none>") : narrow(out.fileVersion),
        out.signatureValid ? std::string("Valid") : std::string("Invalid(") + std::to_string(out.winTrustError) + ")",
        out.signerSubject.empty() ? std::string("<none>") : narrow(out.signerSubject)));
    return true;
}

std::string sha256Hex(const uint8_t* data, size_t size)
{
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(status)) {
        return {};
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
    if (!BCRYPT_SUCCESS(status)) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return {};
    }
    std::string hex;
    if (BCRYPT_SUCCESS(BCryptHashData(hash, const_cast<unsigned char*>(data), static_cast<ULONG>(size), 0))) {
        std::array<unsigned char, 32> digest{};
        if (BCRYPT_SUCCESS(BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0))) {
            hex = bytesToHexUpper(digest);
        }
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    return hex;
}

} // namespace veyra
