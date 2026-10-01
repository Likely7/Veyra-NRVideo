// SPDX-License-Identifier: GPL-3.0-only
#include "veyra/moonlight/IdentityStore.h"

#include <windows.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <cstring>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <vector>

namespace veyra::moonlight {

namespace {

constexpr char kMagic[4] = {'V', 'M', 'L', '1'};

struct Secret {
    std::vector<uint8_t> bytes;
    ~Secret() { if (!bytes.empty()) SecureZeroMemory(bytes.data(), bytes.size()); }
};

struct Blob {
    DATA_BLOB value{};
    ~Blob() {
        if (value.pbData) { SecureZeroMemory(value.pbData, value.cbData); LocalFree(value.pbData); }
    }
};

void put(std::vector<uint8_t>& out, const std::string& text) {
    const uint32_t size = uint32_t(text.size());
    for (unsigned i = 0; i < 4; ++i) out.push_back(uint8_t(size >> (8 * i)));
    out.insert(out.end(), text.begin(), text.end());
}

bool take(const uint8_t*& cursor, const uint8_t* end, std::string& text) {
    if (end - cursor < 4) return false;
    const uint32_t size = uint32_t(cursor[0]) | (uint32_t(cursor[1]) << 8) | (uint32_t(cursor[2]) << 16) | (uint32_t(cursor[3]) << 24);
    cursor += 4;
    if (size > (1u << 16) || size_t(end - cursor) < size) return false;
    text.assign(reinterpret_cast<const char*>(cursor), size);
    cursor += size;
    return true;
}

} // namespace

std::filesystem::path dataDirectory() {
    // Tests and demos point this somewhere disposable; it is not a user setting.
    wchar_t override[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"VEYRA_MOONLIGHT_DATA", override, MAX_PATH) > 0) return std::filesystem::path(override);
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw)))
        throw std::runtime_error("Cannot resolve the user data directory");
    std::filesystem::path result(raw);
    CoTaskMemFree(raw);
    return result / L"Veyra" / L"moonlight";
}

std::optional<Identity> loadIdentity(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<uint8_t> encrypted((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (encrypted.empty() || encrypted.size() > (1u << 20)) return std::nullopt;
    DATA_BLOB input{DWORD(encrypted.size()), encrypted.data()};
    Blob plain;
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &plain.value)) return std::nullopt;
    const uint8_t* cursor = plain.value.pbData;
    const uint8_t* end = cursor + plain.value.cbData;
    if (end - cursor < 4 || std::memcmp(cursor, kMagic, 4) != 0) return std::nullopt;
    cursor += 4;
    Identity identity;
    if (!take(cursor, end, identity.uniqueId) || !take(cursor, end, identity.certPem) || !take(cursor, end, identity.keyPem) || cursor != end)
        return std::nullopt;
    if (!identity.valid()) return std::nullopt;
    return identity;
}

bool saveIdentity(const std::filesystem::path& file, const Identity& identity) {
    if (!identity.valid()) return false;
    Secret plain;
    plain.bytes.insert(plain.bytes.end(), kMagic, kMagic + 4);
    put(plain.bytes, identity.uniqueId);
    put(plain.bytes, identity.certPem);
    put(plain.bytes, identity.keyPem);
    DATA_BLOB input{DWORD(plain.bytes.size()), plain.bytes.data()};
    Blob encrypted;
    if (!CryptProtectData(&input, L"Veyra Moonlight identity", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted.value)) return false;

    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    if (ec) return false;
    auto temp = file;
    temp += L"." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L".tmp";
    HANDLE handle = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(handle, encrypted.value.pbData, encrypted.value.cbData, &written, nullptr) && written == encrypted.value.cbData;
    if (ok) ok = FlushFileBuffers(handle) != FALSE;
    CloseHandle(handle);
    if (ok) ok = MoveFileExW(temp.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != FALSE;
    if (!ok) DeleteFileW(temp.c_str());
    return ok;
}

Identity loadOrCreateIdentity(const std::filesystem::path& file, bool* created) {
    if (auto existing = loadIdentity(file)) {
        if (created) *created = false;
        return *existing;
    }
    Identity fresh = createIdentity();
    if (!saveIdentity(file, fresh)) throw std::runtime_error("The Moonlight identity could not be saved");
    if (created) *created = true;
    return fresh;
}

} // namespace veyra::moonlight
