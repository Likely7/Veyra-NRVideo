// SPDX-License-Identifier: GPL-3.0-only
// Sign-in sequence ported from xal-node's Msal class (unknownskl/xal-node, MIT) as used by Greenlight.
#include "veyra/xbox/Account.h"

#include <windows.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>

namespace veyra::xbox {
namespace {

using json = nlohmann::json;

constexpr const char* kScope = "xboxlive.signin openid profile offline_access";

int64_t systemNow() {
    return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

// "2026-10-01T12:34:56.1234567Z" -> unix seconds (0 when it does not parse).
int64_t parseTime(const std::string& text) {
    std::tm t{};
    if (sscanf_s(text.c_str(), "%d-%d-%dT%d:%d:%d", &t.tm_year, &t.tm_mon, &t.tm_mday, &t.tm_hour, &t.tm_min, &t.tm_sec) != 6) return 0;
    t.tm_year -= 1900;
    t.tm_mon -= 1;
    return int64_t(_mkgmtime(&t));
}

json parse(const HttpsResponse& response, const char* what) {
    if (response.status < 200 || response.status > 299)
        throw ServiceError(response.status, response.body, std::string(what) + " returned HTTP " + std::to_string(response.status));
    try {
        return json::parse(response.body);
    } catch (const json::exception&) {
        throw std::runtime_error(std::string(what) + " returned something that is not JSON");
    }
}

std::string form(std::initializer_list<std::pair<const char*, std::string>> fields) {
    std::string out;
    for (const auto& [k, v] : fields) {
        if (!out.empty()) out += '&';
        out += k;
        out += '=';
        out += urlEncode(v);
    }
    return out;
}

HttpsRequest formPost(const char* host, const char* path, std::string body) {
    HttpsRequest r;
    r.method = "POST";
    r.host = host;
    r.path = path;
    r.headers["Content-Type"] = "application/x-www-form-urlencoded";
    r.headers["Cache-Control"] = "no-store, must-revalidate, no-cache";
    r.body = std::move(body);
    return r;
}

HttpsRequest xblPost(const char* host, const char* path, const json& body) {
    HttpsRequest r;
    r.method = "POST";
    r.host = host;
    r.path = path;
    r.headers["x-xbl-contract-version"] = "1";
    r.headers["Cache-Control"] = "no-cache";
    r.headers["Content-Type"] = "application/json";
    r.headers["Accept"] = "*/*";
    r.headers["Origin"] = "https://www.xbox.com";
    r.headers["Referer"] = "https://www.xbox.com/";
    r.headers["ms-cv"] = "0";
    r.body = body.dump();
    return r;
}

std::vector<uint8_t> protect(const std::string& plain) {
    DATA_BLOB in{DWORD(plain.size()), reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Veyra Xbox sign-in", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    std::vector<uint8_t> bytes(out.pbData, out.pbData + out.cbData);
    LocalFree(out.pbData);
    return bytes;
}

std::optional<std::string> unprotect(std::vector<uint8_t> bytes) {
    DATA_BLOB in{DWORD(bytes.size()), bytes.data()};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return std::nullopt;
    std::string plain(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return plain;
}

} // namespace

std::filesystem::path dataDirectory() {
    wchar_t override[MAX_PATH] = {};
    if (GetEnvironmentVariableW(L"VEYRA_XBOX_DATA", override, MAX_PATH) > 0) return std::filesystem::path(override);
    PWSTR raw = nullptr;
    if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, 0, nullptr, &raw))) throw std::runtime_error("Cannot resolve the user data directory");
    std::filesystem::path result(raw);
    CoTaskMemFree(raw);
    return result / L"Veyra" / L"xbox";
}

std::optional<SavedTokens> loadTokens(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::nullopt;
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (bytes.empty() || bytes.size() > (1u << 20)) return std::nullopt;
    const auto plain = unprotect(std::move(bytes));
    if (!plain) return std::nullopt;
    try {
        const json j = json::parse(*plain);
        SavedTokens t;
        t.refreshToken = j.value("refresh", "");
        t.accessToken = j.value("access", "");
        t.accessExpiresAt = j.value("accessExpires", int64_t(0));
        t.userHash = j.value("uhs", "");
        if (t.refreshToken.empty()) return std::nullopt;
        return t;
    } catch (const json::exception&) {
        return std::nullopt;
    }
}

bool saveTokens(const std::filesystem::path& file, const SavedTokens& tokens) {
    std::error_code ec;
    std::filesystem::create_directories(file.parent_path(), ec);
    std::string plain = json{{"refresh", tokens.refreshToken}, {"access", tokens.accessToken},
                             {"accessExpires", tokens.accessExpiresAt}, {"uhs", tokens.userHash}}.dump();
    const auto bytes = protect(plain);
    SecureZeroMemory(plain.data(), plain.size());
    if (bytes.empty()) return false;
    const auto temp = std::filesystem::path(file).concat(L".tmp");
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out.write(reinterpret_cast<const char*>(bytes.data()), std::streamsize(bytes.size()));
        if (!out) return false;
    }
    std::filesystem::rename(temp, file, ec);
    return !ec;
}

std::wstring describeXboxError(const std::string& responseBody) {
    try {
        const json j = json::parse(responseBody);
        const uint64_t code = j.value("XErr", uint64_t(0));
        switch (code) {
        case 2148916233: return L"这个微软账号还没有 Xbox 档案。请先在 xbox.com 用它登录一次，创建档案后再试。";
        case 2148916235: return L"这个账号所在的国家或地区不提供 Xbox Live。";
        case 2148916236: case 2148916237: return L"这个账号需要先在 xbox.com 完成成人验证。";
        case 2148916238: return L"这是儿童账号，需要家长在家庭组里允许后才能使用。";
        default: return {};
        }
    } catch (const json::exception&) {
        return {};
    }
}

Account::Account(std::shared_ptr<HttpsTransport> transport, std::filesystem::path tokenFile)
    : http_(std::move(transport)), file_(std::move(tokenFile)), clock_(systemNow) {
    tokens_ = loadTokens(file_);
}

bool Account::signedIn() const {
    std::lock_guard lock(mutex_);
    return tokens_.has_value();
}

void Account::signOut() {
    std::lock_guard lock(mutex_);
    tokens_.reset();
    userToken_.clear();
    streaming_.reset();
    std::error_code ec;
    std::filesystem::remove(file_, ec);
}

void Account::store() {
    if (tokens_) saveTokens(file_, *tokens_);
}

DeviceCode Account::beginSignIn() {
    const json j = parse(http_->send(formPost("login.microsoftonline.com", "/consumers/oauth2/v2.0/devicecode",
                                               form({{"client_id", kMsalClientId}, {"scope", kScope}}))), "device code request");
    DeviceCode code;
    code.userCode = j.value("user_code", "");
    code.deviceCode = j.value("device_code", "");
    code.verificationUri = j.value("verification_uri", "https://www.microsoft.com/link");
    code.expiresInSeconds = j.value("expires_in", 900);
    code.intervalSeconds = std::max(1, j.value("interval", 5));
    if (code.userCode.empty() || code.deviceCode.empty()) throw std::runtime_error("device code request returned no code");
    return code;
}

SignInPoll Account::pollSignIn(const DeviceCode& code, std::string* detail) {
    const HttpsResponse response = http_->send(formPost("login.microsoftonline.com", "/consumers/oauth2/v2.0/token",
        form({{"grant_type", "urn:ietf:params:oauth:grant-type:device_code"}, {"client_id", kMsalClientId}, {"device_code", code.deviceCode}})));
    json j;
    try { j = json::parse(response.body); } catch (const json::exception&) { return SignInPoll::Failed; }
    if (response.status >= 200 && response.status <= 299) {
        SavedTokens t;
        t.refreshToken = j.value("refresh_token", "");
        t.accessToken = j.value("access_token", "");
        t.accessExpiresAt = clock_() + j.value("expires_in", 3600);
        if (t.refreshToken.empty() || t.accessToken.empty()) return SignInPoll::Failed;
        std::lock_guard lock(mutex_);
        tokens_ = std::move(t);
        userToken_.clear();
        streaming_.reset();
        store();
        return SignInPoll::Done;
    }
    const std::string error = j.value("error", "");
    if (detail) *detail = error;
    if (error == "authorization_pending") return SignInPoll::Pending;
    if (error == "slow_down") return SignInPoll::SlowDown;
    if (error == "expired_token" || error == "code_expired") return SignInPoll::Expired;
    if (error == "authorization_declined" || error == "access_denied") return SignInPoll::Declined;
    return SignInPoll::Failed;
}

// Caller holds mutex_.
std::string Account::userAccessToken() {
    if (!tokens_) throw std::runtime_error("not signed in");
    if (!tokens_->accessToken.empty() && tokens_->accessExpiresAt - 60 > clock_()) return tokens_->accessToken;
    const HttpsResponse response = http_->send(formPost("login.microsoftonline.com", "/consumers/oauth2/v2.0/token",
        form({{"client_id", kMsalClientId}, {"grant_type", "refresh_token"}, {"refresh_token", tokens_->refreshToken}, {"scope", kScope}})));
    if (response.status == 400 || response.status == 401) {
        // The refresh token is no longer accepted (password changed, signed out elsewhere, 90 days unused).
        tokens_.reset();
        std::error_code ec;
        std::filesystem::remove(file_, ec);
        throw ServiceError(response.status, response.body, "sign-in expired");
    }
    const json j = parse(response, "token refresh");
    tokens_->accessToken = j.value("access_token", "");
    tokens_->accessExpiresAt = clock_() + j.value("expires_in", 3600);
    const std::string rotated = j.value("refresh_token", "");
    if (!rotated.empty()) tokens_->refreshToken = rotated;
    store();
    return tokens_->accessToken;
}

// Caller holds mutex_.
std::string Account::xstsUserToken() {
    if (!userToken_.empty() && userTokenExpiresAt_ - 60 > clock_()) return userToken_;
    const std::string access = userAccessToken();
    const json body = {{"Properties", {{"AuthMethod", "RPS"}, {"RpsTicket", "d=" + access}, {"SiteName", "user.auth.xboxlive.com"}}},
                       {"RelyingParty", "http://auth.xboxlive.com"}, {"TokenType", "JWT"}};
    const json j = parse(http_->send(xblPost("user.auth.xboxlive.com", "/user/authenticate", body)), "Xbox Live user authentication");
    userToken_ = j.value("Token", "");
    userTokenExpiresAt_ = parseTime(j.value("NotAfter", ""));
    if (userTokenExpiresAt_ == 0) userTokenExpiresAt_ = clock_() + 3600;
    try { tokens_->userHash = j.at("DisplayClaims").at("xui").at(0).value("uhs", ""); } catch (const json::exception&) {}
    if (userToken_.empty()) throw std::runtime_error("Xbox Live user authentication returned no token");
    return userToken_;
}

StreamingAccess Account::streamingAccess() {
    std::lock_guard lock(mutex_);
    if (streaming_ && streaming_->expiresAt - 120 > clock_()) return *streaming_;
    const std::string user = xstsUserToken();
    const json authorize = {{"Properties", {{"SandboxId", "RETAIL"}, {"UserTokens", json::array({user})}}},
                            {"RelyingParty", "http://gssv.xboxlive.com/"}, {"TokenType", "JWT"}};
    const json xsts = parse(http_->send(xblPost("xsts.auth.xboxlive.com", "/xsts/authorize", authorize)), "Xbox Live authorization");
    const std::string gssv = xsts.value("Token", "");
    if (gssv.empty()) throw std::runtime_error("Xbox Live authorization returned no token");

    HttpsRequest request;
    request.method = "POST";
    request.host = "xhome.gssv-play-prod.xboxlive.com";
    request.path = "/v2/login/user";
    request.headers["Content-Type"] = "application/json";
    request.headers["Cache-Control"] = "no-store, must-revalidate, no-cache";
    request.headers["x-gssv-client"] = "XboxComBrowser";
    request.body = json{{"token", gssv}, {"offeringId", "xhome"}}.dump();
    const json j = parse(http_->send(request), "streaming sign-in");

    StreamingAccess access;
    access.gsToken = j.value("gsToken", "");
    access.expiresAt = clock_() + j.value("durationInSeconds", 3600);
    try {
        for (const auto& region : j.at("offeringSettings").at("regions")) {
            if (!region.value("isDefault", false) && !access.host.empty()) continue;
            std::string uri = region.value("baseUri", "");
            if (uri.rfind("https://", 0) == 0) uri = uri.substr(8);
            while (!uri.empty() && uri.back() == '/') uri.pop_back();
            access.host = uri;
            access.regionName = region.value("name", "");
            if (region.value("isDefault", false)) break;
        }
    } catch (const json::exception&) {}
    if (access.gsToken.empty() || access.host.empty()) throw std::runtime_error("streaming sign-in returned no region or token");
    streaming_ = access;
    return access;
}

std::string Account::transferToken() {
    std::lock_guard lock(mutex_);
    if (!tokens_) throw std::runtime_error("not signed in");
    userAccessToken();   // keeps the refresh token current
    const json j = parse(http_->send(formPost("login.live.com", "/oauth20_token.srf",
        form({{"client_id", kMsalClientId}, {"scope", "service::http://Passport.NET/purpose::PURPOSE_XBOX_CLOUD_CONSOLE_TRANSFER_TOKEN"},
              {"grant_type", "refresh_token"}, {"refresh_token", tokens_->refreshToken}}))), "console transfer token");
    const std::string token = j.value("access_token", "");
    if (token.empty()) throw std::runtime_error("console transfer token missing");
    return token;
}

} // namespace veyra::xbox
