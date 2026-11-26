#include "veyra/base/InstanceWindow.h"

#include "veyra/base/PortableFiles.h"
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>

namespace veyra::instance {
namespace {

using Path = std::filesystem::path;
constexpr auto ownerName = L".veyra-instance.v1";
constexpr auto endingName = L".veyra-exit-request.v1";

uint32_t rotr32(uint32_t value, unsigned bits) {
    return (value >> bits) | (value << (32u - bits));
}

// Pure-C++ FIPS 180-4 SHA-256 (uppercase hex) so Veyra.exe computes the exact
// same profile key as veyra_qml_ui.exe (including across in-app updates and
// rollbacks) without importing bcrypt.dll.
std::string sha256HexUpper(const uint8_t* data, size_t size) {
    static constexpr std::array<uint32_t, 64> k{
        0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
        0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
        0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
        0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
        0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
        0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
        0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
        0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u
    };
    std::array<uint32_t, 8> state{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u
    };
    auto compress = [&](const uint8_t block[64]) {
        uint32_t w[64]{};
        for (size_t i = 0; i < 16; ++i) {
            w[i] = (uint32_t(block[i * 4]) << 24)
                 | (uint32_t(block[i * 4 + 1]) << 16)
                 | (uint32_t(block[i * 4 + 2]) << 8)
                 | uint32_t(block[i * 4 + 3]);
        }
        for (size_t i = 16; i < 64; ++i) {
            const uint32_t s0 = rotr32(w[i - 15], 7) ^ rotr32(w[i - 15], 18) ^ (w[i - 15] >> 3);
            const uint32_t s1 = rotr32(w[i - 2], 17) ^ rotr32(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
        uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
        for (size_t i = 0; i < 64; ++i) {
            const uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
            const uint32_t ch = (e & f) ^ ((~e) & g);
            const uint32_t temp1 = h + s1 + ch + k[i] + w[i];
            const uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
            const uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
            const uint32_t temp2 = s0 + maj;
            h = g; g = f; f = e; e = d + temp1;
            d = c; c = b; b = a; a = temp1 + temp2;
        }
        state[0] += a; state[1] += b; state[2] += c; state[3] += d;
        state[4] += e; state[5] += f; state[6] += g; state[7] += h;
    };

    size_t offset = 0;
    while (size - offset >= 64) {
        compress(data + offset);
        offset += 64;
    }
    uint8_t tail[128]{};
    const size_t remaining = size - offset;
    if (remaining > 0) std::memcpy(tail, data + offset, remaining);
    tail[remaining] = 0x80u;
    const size_t totalTail = remaining < 56 ? 64 : 128;
    const uint64_t bitLength = uint64_t(size) * 8u;
    for (size_t i = 0; i < 8; ++i)
        tail[totalTail - 8 + i] = static_cast<uint8_t>((bitLength >> ((7 - i) * 8)) & 0xFFu);
    compress(tail);
    if (totalTail == 128) compress(tail + 64);

    static constexpr char hexDigits[] = "0123456789ABCDEF";
    std::string hex;
    hex.reserve(64);
    for (const uint32_t word : state) {
        for (int shift = 28; shift >= 0; shift -= 4)
            hex.push_back(hexDigits[(word >> shift) & 0xFu]);
    }
    return hex;
}

uint64_t fileTime(const FILETIME& value) {
    return (uint64_t(value.dwHighDateTime) << 32) | value.dwLowDateTime;
}

Path pathFromUtf8(std::string_view text) {
    return Path(std::u8string_view(reinterpret_cast<const char8_t*>(text.data()), text.size()));
}

} // namespace

std::wstring profileKey(const std::filesystem::path& profile) {
    std::error_code ec;
    auto absolute = std::filesystem::absolute(profile, ec);
    if (ec) return {};
    auto canonical = std::filesystem::weakly_canonical(absolute, ec);
    auto normalized = (ec ? absolute.lexically_normal() : canonical).make_preferred();
    if (normalized.filename().empty() && normalized != normalized.root_path()) normalized = normalized.parent_path();
    auto path = normalized.wstring();
    CharLowerBuffW(path.data(), DWORD(path.size()));
    const auto hash = sha256HexUpper(reinterpret_cast<const uint8_t*>(path.data()), path.size() * sizeof(wchar_t));
    return hash.empty() ? std::wstring{} : L"Local\\Veyra.Qml.Profile." + std::wstring(hash.begin(), hash.end());
}

std::wstring launchTarget(std::span<const std::wstring> arguments) {
    std::wstring target;
    for (size_t i = 0; i < arguments.size(); ++i) {
        const auto& option = arguments[i];
        if (option == L"--managed-portable") continue;
        if (option == L"--data-dir" || option == L"--import-1.4.4-data") {
            ++i;
            continue;
        }
        // Other switches belong to a fresh launch or an isolated test run.
        if (option.starts_with(L'-')) return {};
        target = option;
    }
    const auto colon = target.find(L':');
    // Leave URL schemes for the player's existing URL validation. A colon at
    // character one belongs to a Windows drive.
    if (target.empty() || (colon != std::wstring::npos && colon > 1 && target.find_first_of(L"\\/") > colon))
        return target;
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(std::filesystem::path(target), ec);
    return ec ? std::wstring{} : absolute.lexically_normal().wstring();
}

HWND findWindow(const std::wstring& key) {
    if (key.empty()) return nullptr;
    struct Search {
        const std::wstring& key;
        HWND window = nullptr;
    } search{key};
    EnumWindows([](HWND window, LPARAM arg) -> BOOL {
        auto& search = *reinterpret_cast<Search*>(arg);
        if (!GetPropW(window, search.key.c_str())) return TRUE;
        search.window = window;
        return FALSE;
    }, reinterpret_cast<LPARAM>(&search));
    return search.window;
}

std::filesystem::path newestHangDump(const std::filesystem::path& directory, DWORD pid, uint64_t created) {
    if (directory.empty() || !pid) return {};
    WIN32_FIND_DATAW entry{};
    const auto pattern = directory / (L"veyra-hang-*-" + std::to_wstring(pid) + L".dmp");
    const HANDLE scan = FindFirstFileW(pattern.c_str(), &entry);
    if (scan == INVALID_HANDLE_VALUE) return {};
    Path newest;
    uint64_t latest = created;
    do {
        const auto written = fileTime(entry.ftLastWriteTime);
        if ((entry.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
            || (!entry.nFileSizeHigh && !entry.nFileSizeLow) || written < latest) continue;
        newest = directory / entry.cFileName;
        latest = written;
    } while (FindNextFileW(scan, &entry));
    FindClose(scan);
    return newest;
}

bool forwardLaunch(HWND window, DWORD pid, const std::wstring& target) {
    if (target.size() > maxTargetChars) return false;
    AllowSetForegroundWindow(pid);
    COPYDATASTRUCT data{launchMessage, DWORD(target.size() * sizeof(wchar_t)), const_cast<wchar_t*>(target.data())};
    DWORD_PTR reply = 0;
    return SendMessageTimeoutW(window, WM_COPYDATA, 0, reinterpret_cast<LPARAM>(&data),
                               SMTO_BLOCK | SMTO_ABORTIFHUNG | SMTO_ERRORONEXIT,
                               3000, &reply) && reply == launchMessage;
}

bool timedOut(HWND window, DWORD timeout) {
    // A protocol mismatch, destroyed window, or elevation barrier must never
    // be treated as an unresponsive message pump.
    DWORD_PTR reply = 0;
    SetLastError(ERROR_SUCCESS);
    return !SendMessageTimeoutW(window, WM_NULL, 0, 0, SMTO_BLOCK | SMTO_ERRORONEXIT,
                                timeout, &reply) && GetLastError() == ERROR_TIMEOUT;
}

std::optional<OwnerStamp> readOwnerStamp(const std::filesystem::path& directory) {
    const auto file = directory / ownerName;
    if (!portable::plainPath(file, false)) return {};
    const auto text = portable::readSmallFile(file, maxTargetChars * 4 + 64);
    const auto space = text.find(' ');
    const auto first = text.find('\n');
    const auto second = first == std::string::npos ? first : text.find('\n', first + 1);
    if (space == std::string::npos || first == std::string::npos || space >= first
        || second == std::string::npos || second - first != 2
        || (text[first + 1] != '0' && text[first + 1] != '1')) return {};
    OwnerStamp stamp;
    const auto pid = std::from_chars(text.data(), text.data() + space, stamp.pid);
    const auto start = std::from_chars(text.data() + space + 1, text.data() + first, stamp.created);
    if (pid.ec != std::errc{} || pid.ptr != text.data() + space || !stamp.pid
        || stamp.pid == GetCurrentProcessId() || start.ec != std::errc{}
        || start.ptr != text.data() + first || !stamp.created) return {};
    try {
        stamp.logs = pathFromUtf8(std::string_view(text).substr(second + 1));
    } catch (const std::filesystem::filesystem_error&) {
        return {};
    }
    stamp.windowReady = text[first + 1] == '1';
    return stamp;
}

void clearOwnerStamp(const std::filesystem::path& directory) {
    if (directory.empty()) return;
    for (const auto* name : {ownerName, endingName}) {
        const auto file = directory / name;
        if (portable::plainPath(file, false)) DeleteFileW(file.c_str());
    }
}

Handoff handoffWindowOnly(const std::filesystem::path& directory, const std::filesystem::path& logDirectory,
                          const std::wstring& key, HWND window, const std::wstring& target, const Notify& notify) {
    DWORD pid = 0;
    if (!window || !GetWindowThreadProcessId(window, &pid) || !GetPropW(window, key.c_str()))
        return Handoff::NoWindow;
    const auto sameWindow = [&] {
        DWORD current = 0;
        GetWindowThreadProcessId(window, &current);
        return current == pid && GetPropW(window, key.c_str());
    };
    if (forwardLaunch(window, pid, target)) return Handoff::Done;
    if (!sameWindow()) return Handoff::NoWindow;
    if (!timedOut(window)) {
        if (!sameWindow()) return Handoff::NoWindow;
        notify(Notice::AlreadyOpen, 0, {});
        return Handoff::Done;
    }
    const auto stamp = readOwnerStamp(directory);
    const bool matchedStamp = stamp && stamp->pid == pid;
    const auto& logs = matchedStamp && !stamp->logs.empty() ? stamp->logs : logDirectory;
    const uint64_t created = matchedStamp ? stamp->created : 0;
    notify(Notice::Hung, pid, newestHangDump(logs, pid, created));
    return Handoff::Done;
}

bool reportStalledOwnerFromStamp(const std::filesystem::path& directory, const std::filesystem::path& logDirectory,
                                 const Notify& notify) {
    const auto stamp = readOwnerStamp(directory);
    if (!stamp || !stamp->windowReady) return false;
    notify(Notice::EndPending, stamp->pid,
           newestHangDump(stamp->logs.empty() ? logDirectory : stamp->logs, stamp->pid, stamp->created));
    return true;
}

} // namespace veyra::instance
