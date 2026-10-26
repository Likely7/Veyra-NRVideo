// Bilibili web API client for the in-app module.
//
// The endpoint list, the WBI signing scheme and the quality/codec selection follow
// the newBV project (MIT, github.com/Frost819/newBV). Reimplemented on WinHTTP so
// the app keeps one HTTP stack and no Kotlin/Android dependency.
//
// Response parsing goes through veyra::media::json (BiliJson.h). The Bilibili API
// nests most of what we need: the UP name is data.owner.name, the counters are
// data.stat, search results are data.result (an array), the feed is data.item, and
// the login flag is the boolean data.isLogin. A substring search for `"key":` only
// ever finds flat scalars, which is why those fields used to read as empty.
#include "veyra/media/BilibiliApi.h"
#include "veyra/media/BiliJson.h"
// The write actions need to know whether a session is present; the resolver owns
// the cookie file and the signed-in check.
#include "veyra/media/BilibiliResolver.h"
#include "veyra/Log.h"
#include <windows.h>
#include <winhttp.h>
#include <bcrypt.h>
#include <algorithm>
#include <chrono>
#include <cwctype>
#include <format>
#include <future>
#include <map>
#include <mutex>
#include <sstream>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "bcrypt.lib")

namespace veyra::media {
namespace {

std::wstring g_cookie;

constexpr const wchar_t* kReferer = L"https://www.bilibili.com";
constexpr const wchar_t* kUserAgent =
    L"Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
    L"(KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36";

std::string toUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n > 0 ? n : 0), '\0');
    if (n > 0) WideCharToMultiByte(CP_UTF8, 0, w.c_str(), int(w.size()), out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring fromUtf8(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n > 0 ? n : 0), L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), int(s.size()), out.data(), n);
    return out;
}

// Percent-encode one query value the way the site does.
std::string urlEncode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(char(c));
        else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 0xF]); }
    }
    return out;
}

// One HTTP exchange. `form` is a urlencoded body; empty means a GET. The write
// actions (like / coin / favourite) are POSTs and need the CSRF token, which the
// callers append to the form as csrf=<bili_jct>.
bool httpRequest(const std::wstring& host, const std::wstring& path, const std::wstring& cookie,
                 const std::string& form, std::string& body, std::wstring& error) {
    HINTERNET session = WinHttpOpen(L"Veyra/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { error = L"WinHttpOpen 失败"; return false; }
    WinHttpSetTimeouts(session, 15000, 15000, 15000, 15000);
    // The danmaku endpoint answers with Content-Encoding: deflate. Asking WinHTTP to
    // decompress saves shipping an inflate implementation.
    DWORD decompress = WINHTTP_DECOMPRESSION_FLAG_ALL;
    WinHttpSetOption(session, WINHTTP_OPTION_DECOMPRESSION, &decompress, sizeof(decompress));
    const bool post = !form.empty();
    bool ok = false;
    HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (connect) {
        HINTERNET request = WinHttpOpenRequest(connect, post ? L"POST" : L"GET", path.c_str(), nullptr,
                                               WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                               WINHTTP_FLAG_SECURE);
        if (request) {
            WinHttpAddRequestHeaders(request, (L"User-Agent: " + std::wstring(kUserAgent)).c_str(), -1L,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            WinHttpAddRequestHeaders(request, (L"Referer: " + std::wstring(kReferer)).c_str(), -1L,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            WinHttpAddRequestHeaders(request, L"Origin: https://www.bilibili.com", -1L,
                                     WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            if (!cookie.empty())
                WinHttpAddRequestHeaders(request, (L"Cookie: " + cookie).c_str(), -1L,
                                         WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            if (post)
                WinHttpAddRequestHeaders(request,
                    L"Content-Type: application/x-www-form-urlencoded", -1L,
                    WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
            const BOOL sent = post
                ? WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     const_cast<char*>(form.data()), DWORD(form.size()),
                                     DWORD(form.size()), 0)
                : WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                     WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
            if (sent && WinHttpReceiveResponse(request, nullptr)) {
                DWORD status = 0, len = sizeof(status);
                WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                    WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX);
                if (status == 200) {
                    DWORD available = 0;
                    do {
                        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
                        std::string chunk(available, '\0');
                        DWORD read = 0;
                        if (!WinHttpReadData(request, chunk.data(), available, &read)) break;
                        body.append(chunk.data(), read);
                    } while (available > 0);
                    ok = true;
                } else {
                    error = std::format(L"HTTP {}", status);
                }
            } else {
                error = L"请求失败";
            }
            WinHttpCloseHandle(request);
        } else error = L"WinHttpOpenRequest 失败";
        WinHttpCloseHandle(connect);
    } else error = L"WinHttpConnect 失败";
    WinHttpCloseHandle(session);
    return ok;
}

bool httpGet(const std::wstring& host, const std::wstring& path, const std::wstring& cookie,
             std::string& body, std::wstring& error) {
    return httpRequest(host, path, cookie, {}, body, error);
}

// ---- CDN candidate ranking -------------------------------------------------

// 从完整 URL 拆出 host 和 path（含查询串）。失败返回 false。
// Split a full URL into host and path (query included). False when it is not https.
bool splitMediaUrl(const std::wstring& url, std::wstring& host, std::wstring& path) {
    const std::wstring prefix = L"https://";
    if (url.compare(0, prefix.size(), prefix) != 0) return false;
    const size_t slash = url.find(L'/', prefix.size());
    if (slash == std::wstring::npos) return false;
    host = url.substr(prefix.size(), slash - prefix.size());
    path = url.substr(slash);
    // Bilibili sometimes appends the host again in the query; the path alone is enough.
    return !host.empty() && !path.empty();
}

// PCDN（mcdn）和第三方中转节点的带宽通常远低于官方 upos，主地址落在它们上面就是
// 「缓冲一直涨不起来」的常见原因。IP 直连地址同理（没有就近调度）。
// newBV 的 officialCdnCandidates() 正是这样过滤的，这里采用同一组规则。
// PCDN (mcdn) and third-party relay nodes usually deliver far less bandwidth than the
// official upos ones, and a primary address landing on them is the usual reason a buffer
// never fills. IP-literal addresses have the same problem (no nearby scheduling).
// newBV's officialCdnCandidates() filters exactly these, so the same rules apply here.
bool isLowQualityCdn(const std::wstring& url) {
    if (url.find(L"mcdn.bilivideo") != std::wstring::npos) return true;
    if (url.find(L".szbdyd.com") != std::wstring::npos) return true;
    // https://1.2.3.4:8080/... — an IP literal instead of a CDN hostname.
    const std::wstring prefix = L"https://";
    if (url.compare(0, prefix.size(), prefix) == 0) {
        size_t i = prefix.size();
        int groups = 0;
        while (i < url.size() && std::iswdigit(url[i])) {
            while (i < url.size() && std::iswdigit(url[i])) ++i;
            ++groups;
            if (i < url.size() && url[i] == L'.') { ++i; continue; }
            break;
        }
        if (groups == 4 && (i >= url.size() || url[i] == L'/' || url[i] == L':')) return true;
    }
    return false;
}

// host -> 实测速度（MB/s）。同一节点的速度短时间内不会剧变，按 host 缓存，这样只有
// 第一次遇到某个节点才付测速的延迟。失败记 0，下次仍会重测。
// host -> measured MB/s. A node's speed does not swing wildly in a short time, so results
// are cached per host; only the first encounter pays the probe cost. Failures are not
// cached, so they are retried next time.
std::mutex g_cdnSpeedMutex;
std::map<std::wstring, double> g_cdnSpeedCache;

// 测一个地址的下载速度：取前 256 KB，返回 MB/s；失败返回 -1。
// 超时必须短 —— 这段延迟加在「点击播放」到「出第一帧」之间。
// Measure one address by downloading the first 256 KB and return MB/s, or -1 on failure.
// The timeout has to stay short: this delay sits between the click and the first frame.
double probeCdnSpeed(const std::wstring& url, int timeoutMs) {
    std::wstring host, path;
    if (!splitMediaUrl(url, host, path)) return -1.0;
    HINTERNET session = WinHttpOpen(L"Veyra/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                    WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return -1.0;
    // 四个阶段（解析/连接/发送/接收）各自限制在 timeoutMs/2，总最坏才是两倍而不是四倍。
    // 这段延迟直接压在「点击播放」到「第一帧」之间，宁可把慢节点判成失败排到后面。
    // Each of the four stages (resolve/connect/send/receive) gets timeoutMs/2, so the worst
    // case is twice the budget rather than four times. This delay sits between the click and
    // the first frame, so a slow node is better misjudged as failed than waited for.
    const int phaseMs = timeoutMs > 1 ? timeoutMs / 2 : 1;
    WinHttpSetTimeouts(session, phaseMs, phaseMs, phaseMs, phaseMs);
    struct Guard { HINTERNET h; ~Guard() { if (h) WinHttpCloseHandle(h); } };
    Guard sessionGuard{session};
    HINTERNET connect = WinHttpConnect(session, host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!connect) return -1.0;
    Guard connectGuard{connect};
    HINTERNET request = WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr,
                                           WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                           WINHTTP_FLAG_SECURE);
    if (!request) return -1.0;
    Guard requestGuard{request};
    WinHttpAddRequestHeaders(request, (L"User-Agent: " + std::wstring(kUserAgent)).c_str(), -1L,
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    WinHttpAddRequestHeaders(request, (L"Referer: " + std::wstring(kReferer)).c_str(), -1L,
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);
    // Only the head of the stream: enough to tell a fast node from a crawling one.
    WinHttpAddRequestHeaders(request, L"Range: bytes=0-262143", -1L,
                             WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE);

    const auto begin = std::chrono::steady_clock::now();
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr))
        return -1.0;
    DWORD status = 0, len = sizeof(status);
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &len, WINHTTP_NO_HEADER_INDEX))
        return -1.0;
    if (status != 200 && status != 206) return -1.0;
    size_t total = 0;
    while (total < 262144) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request, &available) || available == 0) break;
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(request, chunk.data(), available, &read) || read == 0) break;
        total += read;
        // Enough to rank; reading further only slows the click down.
        if (std::chrono::steady_clock::now() - begin > std::chrono::milliseconds(timeoutMs)) break;
    }
    if (total == 0) return -1.0;
    const double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    if (ms <= 0.0) return -1.0;
    return (double(total) / (1024.0 * 1024.0)) / (ms / 1000.0);
}

// 给候选地址排序：先滤掉 PCDN / 第三方 / IP 直连（它们排到最后），再按实测速度降序。
// 全部节点都不可测速时保持服务端给的顺序 —— 那个顺序本身是有意义的。
// Rank the candidates: low-quality nodes (PCDN / third party / IP literal) sink to the
// end, the rest are ordered by measured speed. If nothing can be measured the server's
// own order is kept, which carries meaning of its own.
std::vector<std::wstring> rankCdnCandidates(const std::vector<std::wstring>& urls,
                                              bool probe = true) {
    if (urls.size() < 2) return urls;
    std::vector<std::wstring> good, poor;
    for (const auto& url : urls) {
        if (url.empty()) continue;
        (isLowQualityCdn(url) ? poor : good).push_back(url);
    }
    if (good.empty()) good = poor;               // 全是低质量节点：保持原序
    if (good.empty()) return urls;

    // 测速：只测没有缓存的 host，并发进行，任何一个慢都不拖累其它。
    // Probe only uncached hosts, concurrently: one slow node must not hold up the rest.
    struct Probe { size_t index; std::wstring host; };
    std::vector<Probe> pending;
    std::vector<double> speed(good.size(), -1.0);
    {
        std::lock_guard lock(g_cdnSpeedMutex);
        for (size_t i = 0; i < good.size(); ++i) {
            std::wstring host, path;
            if (!splitMediaUrl(good[i], host, path)) continue;
            const auto hit = g_cdnSpeedCache.find(host);
            if (hit != g_cdnSpeedCache.end()) { speed[i] = hit->second; continue; }
            pending.push_back({i, std::move(host)});
        }
    }
    if (!pending.empty() && !probe) pending.clear();   // 音频只过滤，不付测速的时间
    if (!pending.empty()) {
        // 最多测 4 个：再多也只是把点击到首帧的时间拉长。
        // At most four: probing more only lengthens the click-to-first-frame path.
        if (pending.size() > 4) pending.resize(4);
        std::vector<std::future<double>> running;
        running.reserve(pending.size());
        for (const auto& item : pending) {
            // 按值捕获目标地址。捕获 `item` 本身是错的：它是范围 for 的循环变量引用，
            // 迭代结束就失效了，而这段 lambda 是异步执行的 —— 那会读到悬空引用。
            // Capture the address by value. Capturing `item` itself is wrong: it is the
            // range-for loop variable's reference, dead once the iteration ends, and this
            // lambda runs asynchronously - that would read a dangling reference.
            const std::wstring target = good[item.index];
            running.push_back(std::async(std::launch::async, [target] {
                return probeCdnSpeed(target, 800);
            }));
        }
        // 先全部取完再拿锁：future.get() 会阻塞到探测结束，持锁等待会让其它解析排在这里。
        // Collect every result first, then take the lock: get() blocks until the probe
        // finishes, and holding the mutex across that would queue other resolutions behind it.
        std::vector<double> measured;
        measured.reserve(running.size());
        for (auto& task : running) measured.push_back(task.get());
        std::lock_guard lock(g_cdnSpeedMutex);
        for (size_t k = 0; k < measured.size(); ++k) {
            speed[pending[k].index] = measured[k];
            if (measured[k] >= 0.0) g_cdnSpeedCache[pending[k].host] = measured[k];
        }
    }

    std::vector<size_t> order(good.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&speed](size_t a, size_t b) {
        // 未测出速度的排在测出速度的后面，但保持彼此的相对顺序。
        // Unmeasured entries follow the measured ones, keeping their relative order.
        if ((speed[a] < 0.0) != (speed[b] < 0.0)) return speed[a] >= 0.0;
        if (speed[a] >= 0.0 && speed[b] >= 0.0) return speed[a] > speed[b];
        return false;
    });
    std::vector<std::wstring> ranked;
    ranked.reserve(good.size() + poor.size());
    for (size_t i : order) ranked.push_back(good[i]);
    ranked.insert(ranked.end(), poor.begin(), poor.end());
    return ranked;
}

// The CSRF token the write endpoints require: bili_jct out of the session cookie.
std::wstring csrfToken(const std::wstring& cookie) {
    const std::wstring needle = L"bili_jct=";
    const size_t p = cookie.find(needle);
    if (p == std::wstring::npos) return {};
    const size_t start = p + needle.size();
    const size_t end = cookie.find(L';', start);
    return cookie.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
}

// ---- response helpers ------------------------------------------------------

// Titles from the search endpoint arrive with the matched term wrapped in
// <em class="keyword">...</em>. The grid draws plain text, so the markup has to go.
std::string stripHtml(const std::string& in) {
    std::string out;
    out.reserve(in.size());
    bool inside = false;
    for (char c : in) {
        if (c == '<') { inside = true; continue; }
        if (c == '>') { inside = false; continue; }
        if (!inside) out.push_back(c);
    }
    return out;
}

// Covers are served over https; the feed hands back http:// and the QML image
// loader refuses the mixed-content case on some setups.
std::wstring normalizeCover(const std::string& url) {
    if (url.empty()) return {};
    if (url.rfind("//", 0) == 0) return L"https:" + fromUtf8(url);
    if (url.rfind("http://", 0) == 0) return L"https://" + fromUtf8(url.substr(7));
    return fromUtf8(url);
}

// "12:34" when the API gave a formatted duration, otherwise seconds -> "M:SS".
std::wstring formatDuration(long long seconds) {
    if (seconds <= 0) return {};
    return std::format(L"{}:{:02d}", seconds / 60, seconds % 60);
}

// Reads data.code, reporting a human-readable failure when it is not 0.
bool checkCode(const json::Value& root, std::wstring& error, const wchar_t* what) {
    const long long code = root["code"].asInt(-1);
    if (code == 0) return true;
    // -101 is the anonymous-account answer: say so instead of showing a number.
    if (code == -101) error = std::format(L"{}需要登录", what);
    // -401 on a write endpoint is the site's own risk check, not a fault here. Measured
    // with this account: reading the coin state, signing in and liking all answer 0,
    // while every coin request answers -401 regardless of parameters, referer or device
    // cookie — including the exact set newBV sends. Saying that plainly is more useful
    // than echoing "非法访问", which reads like the app did something wrong.
    else if (code == -401)
        error = std::format(L"{}失败：B 站的风控拒绝了这次操作，请到网页端完成；"
                            L"同一账号的点赞、收藏不受影响", what);
    else {
        const std::string message = root["message"].asString();
        error = std::format(L"{}失败（code={}）", what, code);
        if (!message.empty() && message != "0") error += L"：" + fromUtf8(message);
    }
    return false;
}

// Fills the fields every card shares. Endpoint-specific shapes are mapped onto
// this by the callers before calling in.
void fillCard(const json::Value& j, BiliVideoCard& c) {
    c.bvid = fromUtf8(j["bvid"].asString());
    if (c.bvid.empty()) {
        const long long aid = j["aid"].asInt(0);
        if (aid > 0) c.bvid = L"av" + std::to_wstring(aid);
    }
    c.title = fromUtf8(stripHtml(j["title"].asString()));
    if (c.title.empty()) c.title = fromUtf8(stripHtml(j["desc"].asString()));

    std::string cover = j["pic"].asString();
    if (cover.empty()) cover = j["cover"].asString();
    c.cover = normalizeCover(cover);

    // owner.name (feed / detail), author (search), upper.name (favourites).
    std::string owner = j["owner"]["name"].asString();
    if (owner.empty()) owner = j["author"].asString();
    if (owner.empty()) owner = j["upper"]["name"].asString();
    if (owner.empty()) owner = j["uname"].asString();
    if (owner.empty()) owner = j["author_name"].asString();
    c.owner = fromUtf8(owner);

    // duration: a formatted string in search results, seconds in feeds.
    const json::Value& duration = j["duration"];
    if (duration.isString() && !duration.asString().empty()) {
        c.duration = fromUtf8(duration.asString());
    } else if (duration.isNumber()) {
        c.duration = formatDuration(duration.asInt(0));
    }
    if (c.duration.empty()) c.duration = fromUtf8(j["duration_text"].asString());

    // Counters live in stat.view / stat.danmaku, or cnt_info for favourites.
    c.play = j["play"].asInt(0);
    if (c.play == 0) c.play = j["stat"]["view"].asInt(0);
    if (c.play == 0) c.play = j["cnt_info"]["play"].asInt(0);
    c.danmaku = j["danmaku"].asInt(0);
    if (c.danmaku == 0) c.danmaku = j["stat"]["danmaku"].asInt(0);
    if (c.danmaku == 0) c.danmaku = j["cnt_info"]["danmaku"].asInt(0);
    c.pubdate = int(j["pubdate"].asInt(0));
}

// ---- WBI signing -----------------------------------------------------------
// The scheme newBV uses (ApiSign.kt): the img_key and sub_key from /x/web-interface/nav
// are concatenated, permuted through a fixed table, and the first 32 characters form
// the mixin key. The query is sorted, the mixin key appended, and the MD5 of that
// string becomes w_rid.

const int kMixinTab[64] = {
    46,47,18,2,53,8,23,32,15,50,10,31,58,3,45,35,
    27,43,5,49,33,9,42,19,29,28,14,39,12,38,41,13,
    37,48,7,16,24,55,40,61,26,17,0,1,60,51,30,4,
    22,25,54,21,56,59,6,63,57,62,11,36,20,34,44,52
};

std::string md5Hex(const std::string& input) {
    // Implemented with the Windows CNG hash API so no crypto library is added.
    BCRYPT_ALG_HANDLE alg = nullptr;
    std::string result;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_MD5_ALGORITHM, nullptr, 0) == 0) {
        DWORD hashLen = 0, got = 0;
        BCryptGetProperty(alg, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLen), sizeof(hashLen), &got, 0);
        std::vector<UCHAR> hash(hashLen);
        BCRYPT_HASH_HANDLE h = nullptr;
        if (BCryptCreateHash(alg, &h, nullptr, 0, nullptr, 0, 0) == 0) {
            BCryptHashData(h, reinterpret_cast<PUCHAR>(const_cast<char*>(input.data())), DWORD(input.size()), 0);
            BCryptFinishHash(h, hash.data(), hashLen, 0);
            BCryptDestroyHash(h);
            static const char* hex = "0123456789abcdef";
            result.reserve(hashLen * 2);
            for (UCHAR b : hash) { result.push_back(hex[b >> 4]); result.push_back(hex[b & 0xF]); }
        }
        BCryptCloseAlgorithmProvider(alg, 0);
    }
    return result;
}

struct WbiKeys {
    std::string imgKey;
    std::string subKey;
    int64_t fetchedAt = 0;
};

WbiKeys& wbiKeys() {
    static WbiKeys keys;
    return keys;
}

// The nav response carries the keys as full URLs; only the file stem is signed.
std::string keyFromUrl(const std::string& url) {
    const size_t slash = url.find_last_of('/');
    std::string stem = slash == std::string::npos ? url : url.substr(slash + 1);
    const size_t dot = stem.find('.');
    return dot == std::string::npos ? stem : stem.substr(0, dot);
}

bool refreshWbiKeys() {
    std::string body;
    std::wstring error;
    if (!httpGet(L"api.bilibili.com", L"/x/web-interface/nav", g_cookie, body, error)) return false;
    const auto root = json::parse(body);
    const std::string imgUrl = root["data"]["wbi_img"]["img_url"].asString();
    const std::string subUrl = root["data"]["wbi_img"]["sub_url"].asString();
    if (imgUrl.empty() || subUrl.empty()) return false;
    auto& k = wbiKeys();
    k.imgKey = keyFromUrl(imgUrl);
    k.subKey = keyFromUrl(subUrl);
    k.fetchedAt = GetTickCount64();
    log::info("bilibili", std::format("wbi keys refreshed img={} sub={}", k.imgKey.size(), k.subKey.size()));
    return !k.imgKey.empty() && !k.subKey.empty();
}

std::string mixinKey() {
    auto& k = wbiKeys();
    const std::string orig = k.imgKey + k.subKey;
    if (orig.size() < 64) return {};
    std::string out;
    out.reserve(32);
    for (int i = 0; i < 32; ++i) out.push_back(orig[size_t(kMixinTab[i])]);
    return out;
}

// Signs `params` in place (adds wts and w_rid) and returns the query string.
std::string signWbi(std::map<std::string, std::string>& params) {
    auto& k = wbiKeys();
    if (k.imgKey.empty() || GetTickCount64() - k.fetchedAt > 3600ull * 1000) refreshWbiKeys();
    const std::string mixin = mixinKey();
    // wts is a Unix timestamp in seconds.
    const int64_t wts = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    params["wts"] = std::to_string(wts);

    std::string joined;
    for (const auto& [key, value] : params) {
        if (!joined.empty()) joined += '&';
        // The site strips !'()* from values before signing.
        std::string filtered;
        for (char c : value) if (c != '!' && c != '\'' && c != '(' && c != ')' && c != '*') filtered.push_back(c);
        joined += key + "=" + filtered;
    }
    params["w_rid"] = md5Hex(joined + mixin);

    std::string query;
    for (const auto& [key, value] : params) {
        if (!query.empty()) query += '&';
        query += key + "=" + urlEncode(value);
    }
    return query;
}

// The signed feed endpoint, shared by the home feed and the other list views.
bool fetchCards(const std::wstring& path, const char* arrayPath,
                std::vector<BiliVideoCard>& out, std::wstring& error, const wchar_t* what) {
    std::string body;
    if (!httpGet(L"api.bilibili.com", path, g_cookie, body, error)) return false;
    const auto root = json::parse(body);
    if (!checkCode(root, error, what)) return false;

    // `arrayPath` is a dotted path such as "data.result" or "data.item".
    const json::Value* node = &root;
    std::string segment;
    std::istringstream stream(arrayPath);
    while (std::getline(stream, segment, '.')) node = &(*node)[segment];
    for (const auto* element : node->elements()) {
        BiliVideoCard card;
        fillCard(*element, card);
        if (!card.bvid.empty()) out.push_back(card);
    }
    return true;
}

} // namespace

void setBiliCookie(const std::wstring& cookie) { g_cookie = cookie; }
std::wstring biliCookie() { return g_cookie; }

BiliAccount biliAccount() {
    BiliAccount a;
    std::string body;
    std::wstring error;
    if (!httpGet(L"api.bilibili.com", L"/x/web-interface/nav", g_cookie, body, error)) return a;
    const auto root = json::parse(body);
    // Signed out, nav answers code -101 while still carrying the WBI keys; the
    // account fields only exist inside data once a session is present.
    const auto& data = root["data"];
    a.loggedIn = data["isLogin"].asBool(false);
    if (!a.loggedIn) return a;
    a.name = fromUtf8(data["uname"].asString());
    a.face = normalizeCover(data["face"].asString());
    a.mid = data["mid"].asInt(0);
    // The account level lives in the nested level_info, not at the top of data.
    a.level = int(data["level_info"]["current_level"].asInt(0));
    // vipStatus is 0/1; vipType distinguishes monthly from annual.
    a.vip = data["vipStatus"].asInt(0) != 0;
    return a;
}

bool biliSearch(const std::wstring& keyword, int page, std::vector<BiliVideoCard>& out,
                std::wstring& error) {
    std::map<std::string, std::string> params{
        {"keyword", toUtf8(keyword)},
        {"search_type", "video"},
        {"page", std::to_string(page)},
        {"page_size", "30"},
    };
    const std::string query = signWbi(params);
    // The results are data.result, an array of video objects.
    return fetchCards(L"/x/web-interface/wbi/search/type?" + fromUtf8(query),
                      "data.result", out, error, L"搜索");
}

bool biliRecommended(std::vector<BiliVideoCard>& out, std::wstring& error) {
    std::map<std::string, std::string> params{
        {"ps", "30"}, {"fresh_type", "4"}, {"feed_version", "V8"},
        {"homepage_ver", "1"},
    };
    const std::string query = signWbi(params);
    // The feed is data.item; entries that are not videos (ads, live rooms) carry
    // no bvid and are dropped by fetchCards.
    if (!fetchCards(L"/x/web-interface/wbi/index/top/feed/rcmd?" + fromUtf8(query),
                    "data.item", out, error, L"推荐")) return false;
    if (out.empty()) {
        error = L"推荐列表为空，请先扫码登录";
        return false;
    }
    return true;
}

BiliPage biliVideoPage(const std::wstring& bvid) {
    BiliPage page;
    page.bvid = bvid;
    std::string body;
    std::wstring error;
    if (!httpGet(L"api.bilibili.com", L"/x/web-interface/view?bvid=" + bvid, g_cookie, body, error)) {
        page.error = error;
        return page;
    }
    const auto root = json::parse(body);
    if (!checkCode(root, page.error, L"视频信息")) return page;
    const auto& data = root["data"];

    page.title = fromUtf8(stripHtml(data["title"].asString()));
    page.cover = normalizeCover(data["pic"].asString());
    page.desc = fromUtf8(stripHtml(data["desc"].asString()));
    page.owner = fromUtf8(data["owner"]["name"].asString());
    page.ownerFace = normalizeCover(data["owner"]["face"].asString());
    page.duration = int(data["duration"].asInt(0));
    page.pubdate = int(data["pubdate"].asInt(0));
    page.play = data["stat"]["view"].asInt(0);
    page.like = data["stat"]["like"].asInt(0);
    page.danmaku = data["stat"]["danmaku"].asInt(0);
    page.coin = data["stat"]["coin"].asInt(0);
    page.favorite = data["stat"]["favorite"].asInt(0);

    // pages[]: one entry per part (分P).
    for (const auto* part : data["pages"].elements()) {
        const long long cid = (*part)["cid"].asInt(0);
        if (cid <= 0) continue;
        page.pages.emplace_back(cid, fromUtf8((*part)["part"].asString()));
    }
    page.ok = !page.pages.empty();
    if (!page.ok) page.error = L"该视频没有可播放的分P";
    return page;
}

bool biliHistory(std::vector<BiliVideoCard>& out, std::wstring& error) {
    std::string body;
    if (!httpGet(L"api.bilibili.com",
                 L"/x/web-interface/history/cursor?ps=30&max=0&view_at=0&business=&type=archive",
                 g_cookie, body, error)) return false;
    const auto root = json::parse(body);
    if (!checkCode(root, error, L"历史记录")) return false;
    // Each entry keeps the video identity under `history` and the display fields
    // on the entry itself.
    for (const auto* entry : root["data"]["list"].elements()) {
        BiliVideoCard card;
        fillCard(*entry, card);
        if (card.bvid.empty()) card.bvid = fromUtf8((*entry)["history"]["bvid"].asString());
        if (card.owner.empty()) card.owner = fromUtf8((*entry)["author_name"].asString());
        if (!card.bvid.empty()) out.push_back(card);
    }
    return true;
}

bool biliFavFolders(std::vector<BiliFavFolder>& out, std::wstring& error) {
    const BiliAccount account = biliAccount();
    if (!account.loggedIn) { error = L"收藏需要登录"; return false; }
    const std::wstring path =
        L"/x/v3/fav/folder/created/list-all?up_mid=" + std::to_wstring(account.mid);
    std::string body;
    if (!httpGet(L"api.bilibili.com", path, g_cookie, body, error)) return false;
    const auto root = json::parse(body);
    if (!checkCode(root, error, L"收藏夹列表")) return false;
    for (const auto* folder : root["data"]["list"].elements()) {
        BiliFavFolder f;
        f.id = (*folder)["id"].asInt(0);
        f.title = fromUtf8((*folder)["title"].asString());
        f.count = (*folder)["media_count"].asInt(0);
        f.attr = int((*folder)["attr"].asInt(-1));
        if (f.id > 0) out.push_back(std::move(f));
    }
    return true;
}

bool biliFavorites(std::vector<BiliVideoCard>& out, std::wstring& error,
                   long long mediaId, int page) {
    // media_id=0 is not a real folder: it answers code=0 with an empty list, which
    // used to look like "no favourites". Resolve the account's folders first.
    const BiliAccount account = biliAccount();
    if (!account.loggedIn) { error = L"收藏需要登录"; return false; }

    if (mediaId <= 0) {
        std::vector<BiliFavFolder> folders;
        if (!biliFavFolders(folders, error)) return false;
        if (folders.empty()) { error = L"账号下没有收藏夹"; return false; }
        // media_id=0 应当表示「默认收藏夹」，但 list-all 接口不保证把默认收藏夹
        // 排在第一位 —— 原来取 folders.front() 会挑中一个空文件夹，于是「我的收藏」
        // 显示「没有内容」，而旁边几个收藏夹都是好的。
        // 先用站方的 attr 标志（0 = 默认），它没给时退回「收藏数最多」的那个：
        // 默认收藏夹几乎总是最大的一夹。
        // media_id=0 is supposed to mean "the default folder", but list-all does not
        // guarantee the default comes first. Taking folders.front() therefore picked
        // some empty folder, which showed as "我的收藏 has no content" while the other
        // folders listed fine. Use the site's attr flag (0 = default) first, and fall
        // back to "the folder with the most items", which the default almost always is.
        const BiliFavFolder* chosen = nullptr;
        for (const auto& f : folders) {
            if (f.attr == 0) { chosen = &f; break; }
        }
        if (!chosen) {
            for (const auto& f : folders) {
                if (!chosen || f.count > chosen->count) chosen = &f;
            }
        }
        mediaId = chosen ? chosen->id : folders.front().id;
        log::info("bili-fav", std::format("media_id=0 resolved to {} attr={} count={}",
            mediaId, chosen ? chosen->attr : -1, chosen ? chosen->count : -1));
    }
    if (page < 1) page = 1;

    // 参数对齐网页收藏夹页：order=mtime、type=0、tid=0。
    // 只带 media_id/ps/pn/platform 时服务端会省略已失效的收藏 —— 于是 125 个的夹
    // 只列出 121 条（apiCount=125 但 entries 合计 121）。网页端带上 type 之后才把
    // 失效项一并给出。
    // Match the parameters the site's own favourites page sends: order=mtime, type=0,
    // tid=0. With only media_id/ps/pn/platform the server omits unavailable items, so a
    // folder of 125 listed just 121 (apiCount=125 while the entries added up to 121).
    // The site's page sends type, which is what brings the unavailable ones back.
    const std::wstring listPath = L"/x/v3/fav/resource/list?media_id="
        + std::to_wstring(mediaId) + L"&ps=30&pn=" + std::to_wstring(page)
        + L"&order=mtime&type=0&tid=0&platform=web";
    std::string body;
    if (!httpGet(L"api.bilibili.com", listPath, g_cookie, body, error)) return false;
    const auto root = json::parse(body);
    if (!checkCode(root, error, L"收藏")) return false;
    size_t seen = 0;
    size_t skipped = 0;
    for (const auto* media : root["data"]["medias"].elements()) {
        ++seen;
        BiliVideoCard card;
        fillCard(*media, card);
        if (!card.bvid.empty()) out.push_back(card);
        else ++skipped;
    }
    // 这一页实际给了几条、其中几条没有 bvid 被跳过。收藏夹的 media_count 包含已
    // 失效的收藏，而本接口不返回那些条目（2026-10-25 实测：某夹 media_count=125，
    // 逐页累计只有 121，且 noBvid 始终为 0 —— 说明服务端就没给那 4 条，不是我们
    // 过滤掉的）。补上 order/type/tid 参数也没能拿回来，故接受这个差异。
    // How many entries this page returned, and how many were skipped for having no
    // bvid. A folder's media_count includes unavailable favourites, and this endpoint
    // omits them (measured 2026-10-25: media_count=125 while the pages added up to 121,
    // with noBvid always 0 - so the server never sent those four; we did not filter
    // them). Adding order/type/tid did not bring them back either, hence accepting it.
    log::info("bili-fav", std::format("media_id={} page={} entries={} parsed={} skipped={}",
        mediaId, page, seen, out.size(), skipped));
    return true;
}

bool biliFollowed(std::vector<BiliVideoCard>& out, std::wstring& error) {
    // The followed-uploaders feed. /x/web-interface/dynamic/region is the per-region
    // board and rejects rid=0; the video-only dynamic feed is what the site uses.
    const std::wstring path =
        L"/x/polymer/web-dynamic/v1/feed/all?type=video&page=1&features=itemOpusStyle";
    std::string body;
    if (!httpGet(L"api.bilibili.com", path, g_cookie, body, error)) return false;
    const auto root = json::parse(body);
    if (!checkCode(root, error, L"关注动态")) return false;
    for (const auto* item : root["data"]["items"].elements()) {
        const auto& modules = (*item)["modules"];
        const auto& archive = modules["module_dynamic"]["major"]["archive"];
        if (!archive.isObject()) continue;
        BiliVideoCard card;
        fillCard(archive, card);
        if (card.owner.empty())
            card.owner = fromUtf8(modules["module_author"]["name"].asString());
        if (!card.bvid.empty()) out.push_back(card);
    }
    return true;
}

bool biliWatchLater(std::vector<BiliVideoCard>& out, std::wstring& error) {
    std::string body;
    if (!httpGet(L"api.bilibili.com", L"/x/v2/history/toview", g_cookie, body, error)) return false;
    const auto root = json::parse(body);
    if (!checkCode(root, error, L"稍后再看")) return false;
    for (const auto* item : root["data"]["list"].elements()) {
        BiliVideoCard card;
        fillCard(*item, card);
        if (card.owner.empty()) card.owner = fromUtf8((*item)["owner"]["name"].asString());
        if (!card.bvid.empty()) out.push_back(card);
    }
    return true;
}

bool biliPopular(std::vector<BiliVideoCard>& out, std::wstring& error, int page) {
    if (page < 1) page = 1;
    const std::wstring path = L"/x/web-interface/popular?ps=30&pn=" + std::to_wstring(page);
    std::string body;
    if (!httpGet(L"api.bilibili.com", path, g_cookie, body, error)) return false;
    const auto root = json::parse(body);
    if (!checkCode(root, error, L"热门")) return false;
    for (const auto* item : root["data"]["list"].elements()) {
        BiliVideoCard card;
        fillCard(*item, card);
        if (!card.bvid.empty()) out.push_back(card);
    }
    return true;
}

bool biliRelated(const std::wstring& bvid, std::vector<BiliVideoCard>& out, std::wstring& error) {
    const std::wstring path = L"/x/web-interface/archive/related?bvid=" + bvid;
    std::string body;
    if (!httpGet(L"api.bilibili.com", path, g_cookie, body, error)) return false;
    const auto root = json::parse(body);
    if (!checkCode(root, error, L"相关推荐")) return false;
    for (const auto* item : root["data"].elements()) {
        BiliVideoCard card;
        // fillCard already reads owner.name (it is the first source it tries), so the
        // override that used to sit here only re-read the same field. Its condition was
        // inverted (!empty instead of empty), which replaced a name it had just resolved
        // with an empty string whenever that second read came back blank.
        fillCard(*item, card);
        if (!card.bvid.empty()) out.push_back(card);
    }
    return true;
}

// 把评论正文转成富文本：文本里的 [表情名] 换成 <img>，其余字符做 HTML 转义。
// 不遍历 emote 对象 —— 表情名就写在正文里，直接拿 "[doge]" 去查表即可，
// 顺带避开了「哪个 key 更长该先匹配」这类问题。
// Turn a comment body into rich text: [emote] becomes <img> and the rest is HTML-escaped.
// The emote object is not enumerated - the name is already in the text, so "[doge]" is
// looked up directly, which also sidesteps any longest-key-first matching question.
std::wstring richCommentText(const json::Value& content) {
    const std::string message = content["message"].asString();
    const json::Value& emotes = content["emote"];
    std::string out;
    out.reserve(message.size() + 96);
    for (size_t i = 0; i < message.size();) {
        if (message[i] == '[') {
            const size_t close = message.find(']', i + 1);
            if (close != std::string::npos && close - i <= 40) {
                const std::string name = message.substr(i, close - i + 1);
                const std::string url = emotes[name]["url"].asString();
                if (!url.empty()) {
                    // 接口给的是 //i0.hdslb.com/... 这种协议相对地址。
                    // The endpoint returns a protocol-relative //i0.hdslb.com/... address.
                    const std::string src = url.rfind("//", 0) == 0 ? "https:" + url : url;
                    out += "<img src=\"" + src + "\" width=\"22\" height=\"22\"/>";
                    i = close + 1;
                    continue;
                }
            }
        }
        switch (message[i]) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += message[i]; break;
        }
        ++i;
    }
    return fromUtf8(out);
}

bool biliComments(const std::wstring& bvid, int page, std::vector<BiliComment>& out,
                  std::wstring& error) {
    if (page < 1) page = 1;
    // The reply endpoint keys on aid, so resolve it from the video first.
    std::string viewBody;
    if (!httpGet(L"api.bilibili.com", L"/x/web-interface/view?bvid=" + bvid, g_cookie,
                 viewBody, error)) return false;
    const auto viewRoot = json::parse(viewBody);
    if (!checkCode(viewRoot, error, L"视频信息")) return false;
    const long long aid = viewRoot["data"]["aid"].asInt(0);
    if (aid <= 0) { error = L"视频信息里没有 aid"; return false; }

    const std::wstring path = L"/x/v2/reply?type=1&oid=" + std::to_wstring(aid)
        + L"&sort=0&pn=" + std::to_wstring(page) + L"&ps=20";
    std::string body;
    if (!httpGet(L"api.bilibili.com", path, g_cookie, body, error)) return false;
    const auto root = json::parse(body);
    if (!checkCode(root, error, L"评论")) return false;
    for (const auto* reply : root["data"]["replies"].elements()) {
        BiliComment comment;
        comment.author = fromUtf8((*reply)["member"]["uname"].asString());
        comment.face = normalizeCover((*reply)["member"]["avatar"].asString());
        comment.message = fromUtf8((*reply)["content"]["message"].asString());
        comment.messageRich = richCommentText((*reply)["content"]);
        comment.like = (*reply)["like"].asInt(0);
        comment.ctime = int((*reply)["ctime"].asInt(0));
        comment.replies = int((*reply)["rcount"].asInt(0));
        comment.rpid = (*reply)["rpid"].asInt(0);
        comment.mid = (*reply)["mid"].asInt(0);
        out.push_back(std::move(comment));
    }
    return true;
}

bool biliDanmaku(long long cid, std::vector<BiliDanmaku>& out, std::wstring& error) {
    if (cid <= 0) { error = L"缺少 cid"; return false; }
    // The XML track is served from the comment CDN keyed on cid. The api.bilibili.com
    // variant now answers in protobuf, which would need a schema to read.
    const std::wstring path = L"/" + std::to_wstring(cid) + L".xml";
    std::string body;
    if (!httpGet(L"comment.bilibili.com", path, g_cookie, body, error)) return false;
    if (body.empty()) { error = L"弹幕为空"; return false; }
    if (body.find("<d ") == std::string::npos) {
        error = L"弹幕数据格式异常";
        return false;
    }

    size_t pos = 0;
    while (true) {
        const size_t open = body.find("<d p=\"", pos);
        if (open == std::string::npos) break;
        const size_t attrStart = open + 6;
        const size_t attrEnd = body.find('"', attrStart);
        if (attrEnd == std::string::npos) break;
        const size_t textStart = body.find('>', attrEnd);
        if (textStart == std::string::npos) break;
        const size_t textEnd = body.find("</d>", textStart);
        if (textEnd == std::string::npos) break;

        const std::string attrs = body.substr(attrStart, attrEnd - attrStart);
        // p is "time,mode,size,color,timestamp,pool,uid,rowid".
        std::vector<std::string> fields;
        size_t start = 0;
        for (;;) {
            const size_t comma = attrs.find(',', start);
            fields.push_back(attrs.substr(start, comma == std::string::npos ? std::string::npos : comma - start));
            if (comma == std::string::npos) break;
            start = comma + 1;
        }
        if (fields.size() >= 4) {
            BiliDanmaku danmaku;
            try {
                danmaku.time = std::stod(fields[0]);
                danmaku.mode = std::stoi(fields[1]);
                danmaku.color = std::stoi(fields[3]);
            } catch (...) {
                pos = textEnd + 4;
                continue;
            }
            danmaku.text = fromUtf8(body.substr(textStart + 1, textEnd - textStart - 1));
            out.push_back(std::move(danmaku));
        }
        pos = textEnd + 4;
    }
    if (out.empty()) { error = L"没有弹幕"; return false; }
    return true;
}

namespace {

// Runs one write action and reports the API's own message on refusal.
bool postAction(const std::wstring& path, const std::string& form, std::wstring& error,
                const wchar_t* what) {
    if (!bilibiliCookieLooksSignedIn(g_cookie)) { error = std::format(L"{}需要登录", what); return false; }
    std::string body;
    if (!httpRequest(L"api.bilibili.com", path, g_cookie, form, body, error)) return false;
    const auto root = json::parse(body);
    return checkCode(root, error, what);
}

std::string csrfForm() {
    const std::wstring token = csrfToken(g_cookie);
    if (token.empty()) return {};
    return "csrf=" + toUtf8(token);
}

} // namespace

bool biliLike(const std::wstring& bvid, bool like, std::wstring& error) {
    std::string form = "bvid=" + toUtf8(bvid) + "&like=" + (like ? "1" : "0");
    const std::string csrf = csrfForm();
    if (csrf.empty()) { error = L"登录信息缺少 csrf 令牌"; return false; }
    form += "&" + csrf;
    return postAction(L"/x/web-interface/archive/like", form, error, L"点赞");
}

// 发一条文字评论。oid 要的是 aid 而不是 bvid，所以先解析一次。
// Posting a plain-text comment. The endpoint keys on aid, not bvid, so resolve it first.
bool biliPostReply(const std::wstring& bvid, const std::wstring& message, std::wstring& error) {
    if (!bilibiliCookieLooksSignedIn(g_cookie)) { error = L"发评论需要登录"; return false; }
    // 项目里没有现成的 trim，这里只去掉首尾空白再判空 —— 全是空格也算空。
    // No trim helper exists in this file; strip the ends and reject an all-blank message.
    const size_t first = message.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) { error = L"评论内容为空"; return false; }
    const size_t last = message.find_last_not_of(L" \t\r\n");
    const std::wstring text = message.substr(first, last - first + 1);
    if (text.size() > 1000) { error = L"评论太长（上限 1000 字）"; return false; }

    std::string viewBody;
    if (!httpGet(L"api.bilibili.com", L"/x/web-interface/view?bvid=" + bvid, g_cookie,
                 viewBody, error)) return false;
    const auto viewRoot = json::parse(viewBody);
    if (!checkCode(viewRoot, error, L"视频信息")) return false;
    const long long aid = viewRoot["data"]["aid"].asInt(0);
    if (aid <= 0) { error = L"视频信息里没有 aid"; return false; }

    const std::string csrf = csrfForm();
    if (csrf.empty()) { error = L"登录信息缺少 csrf 令牌"; return false; }
    // plat=1 是网页端，ordering=2 表示按时间排序（网页端默认就是这一组）。
    // plat=1 marks the web client and ordering=2 sorts by time, which is what the web
    // front end sends by default.
    std::string form = "oid=" + std::to_string(aid)
        + "&type=1&message=" + urlEncode(toUtf8(text))
        + "&plat=1&ordering=2&" + csrf;
    return postAction(L"/x/v2/reply/add", form, error, L"发评论");
}

// 拉取评论表情面板。接口按业务分类，business=reply 是评论区用的那一组。
// Fetch the reply emote panel: the endpoint groups by business, reply being the comment one.
bool biliEmotePanel(std::vector<BiliEmote>& out, std::wstring& error) {
    std::string body;
    if (!httpGet(L"api.bilibili.com", L"/x/emote/user/panel/web?business=reply", g_cookie,
                 body, error)) return false;
    const auto root = json::parse(body);
    if (!checkCode(root, error, L"表情")) return false;
    // data.packages[].emotes[]，每个包是一个表情系列；这里把全部平铺出来。
    // data.packages[].emotes[] - one package per emote series; they are flattened here.
    for (const auto* package : root["data"]["packages"].elements()) {
        for (const auto* emote : (*package)["emotes"].elements()) {
            BiliEmote item;
            item.text = fromUtf8((*emote)["text"].asString());
            item.url = fromUtf8((*emote)["url"].asString());
            if (!item.text.empty()) out.push_back(std::move(item));
        }
    }
    return true;
}

// 删除自己的一条评论。B 站同样按 aid 索引。
// Deleting one of the account's own comments; the endpoint keys on aid as well.
bool biliDeleteReply(const std::wstring& bvid, long long rpid, std::wstring& error) {
    if (!bilibiliCookieLooksSignedIn(g_cookie)) { error = L"删除评论需要登录"; return false; }
    if (rpid <= 0) { error = L"评论标识无效"; return false; }
    std::string viewBody;
    if (!httpGet(L"api.bilibili.com", L"/x/web-interface/view?bvid=" + bvid, g_cookie,
                 viewBody, error)) return false;
    const auto viewRoot = json::parse(viewBody);
    if (!checkCode(viewRoot, error, L"视频信息")) return false;
    const long long aid = viewRoot["data"]["aid"].asInt(0);
    if (aid <= 0) { error = L"视频信息里没有 aid"; return false; }
    const std::string csrf = csrfForm();
    if (csrf.empty()) { error = L"登录信息缺少 csrf 令牌"; return false; }
    std::string form = "oid=" + std::to_string(aid) + "&type=1&rpid="
        + std::to_string(rpid) + "&" + csrf;
    return postAction(L"/x/v2/reply/del", form, error, L"删除评论");
}

bool biliCoin(const std::wstring& bvid, int count, std::wstring& error) {
    if (count < 1 || count > 2) { error = L"投币数只能是 1 或 2"; return false; }
    std::string form = "bvid=" + toUtf8(bvid) + "&multiply=" + std::to_string(count)
        + "&select_like=0";
    const std::string csrf = csrfForm();
    if (csrf.empty()) { error = L"登录信息缺少 csrf 令牌"; return false; }
    form += "&" + csrf;
    return postAction(L"/x/web-interface/coin/add", form, error, L"投币");
}

namespace {
// The write endpoints key on aid, not on the BV id the page carries, so it has to be
// resolved first. Sending the BV id in its place is what made 稍后再看 do nothing, the
// favourite state always read back false, and 收藏 fail with -400.
bool bvidToAid(const std::wstring& bvid, long long& aid, std::wstring& error) {
    std::string viewBody;
    if (!httpGet(L"api.bilibili.com", L"/x/web-interface/view?bvid=" + bvid, g_cookie,
                 viewBody, error)) return false;
    const auto viewRoot = json::parse(viewBody);
    if (!checkCode(viewRoot, error, L"视频信息")) return false;
    aid = viewRoot["data"]["aid"].asInt(0);
    if (aid <= 0) { error = L"视频信息里没有 aid"; return false; }
    return true;
}
} // namespace

bool biliFavorite(const std::wstring& bvid, bool add, long long folderId,
                   std::wstring& error) {
    const BiliAccount account = biliAccount();
    if (!account.loggedIn) { error = L"收藏需要登录"; return false; }
    // folderId 0 asks for the account's first folder, which is where the site itself
    // puts a one-click favourite; the UI passes the folder the user picked instead.
    long long mediaId = folderId;
    if (mediaId <= 0) {
        const std::wstring foldersPath =
            L"/x/v3/fav/folder/created/list-all?up_mid=" + std::to_wstring(account.mid);
        std::string folders;
        if (!httpGet(L"api.bilibili.com", foldersPath, g_cookie, folders, error)) return false;
        const auto folderRoot = json::parse(folders);
        if (!checkCode(folderRoot, error, L"收藏夹列表")) return false;
        const auto first = folderRoot["data"]["list"].elements();
        if (first.empty()) { error = L"账号下没有收藏夹"; return false; }
        mediaId = (*first.front())["id"].asInt(0);
    }
    if (mediaId <= 0) { error = L"收藏夹 id 无效"; return false; }

    // rid is the video's aid, not its BV id: a non-numeric rid is answered with -400.
    long long aid = 0;
    if (!bvidToAid(bvid, aid, error)) return false;
    std::string form = "rid=" + std::to_string(aid) + "&type=2&add_media_ids="
        + (add ? std::to_string(mediaId) : std::string()) + "&del_media_ids="
        + (add ? std::string() : std::to_string(mediaId));
    const std::string csrf = csrfForm();
    if (csrf.empty()) { error = L"登录信息缺少 csrf 令牌"; return false; }
    form += "&" + csrf;
    return postAction(L"/x/v3/fav/resource/deal", form, error, L"收藏");
}

bool biliAddWatchLater(const std::wstring& bvid, bool add, std::wstring& error) {
    long long aid = 0;
    if (!bvidToAid(bvid, aid, error)) return false;
    std::string form = "aid=" + std::to_string(aid);
    const std::string csrf = csrfForm();
    if (csrf.empty()) { error = L"登录信息缺少 csrf 令牌"; return false; }
    form += "&" + csrf;
    return postAction(add ? L"/x/v2/history/toview/add" : L"/x/v2/history/toview/del",
                      form, error, add ? L"加入稍后再看" : L"从稍后再看移除");
}

BiliInteractionState biliInteractionState(const std::wstring& bvid) {
    BiliInteractionState state;
    if (!bilibiliCookieLooksSignedIn(g_cookie)) return state;

    // Each of these answers {code:0, data:{...}} or a non-zero code when the
    // question does not apply (e.g. not signed in); a failure just leaves false.
    auto fetch = [&](const std::wstring& path) {
        std::string body;
        std::wstring error;
        if (!httpGet(L"api.bilibili.com", path, g_cookie, body, error)) return json::Value{};
        return json::parse(body);
    };

    const auto like = fetch(L"/x/web-interface/archive/has/like?bvid=" + bvid);
    state.liked = like["data"].asInt(0) != 0;

    const auto coin = fetch(L"/x/web-interface/archive/coins?bvid=" + bvid);
    state.coins = int(coin["data"]["multiply"].asInt(0));
    state.coined = state.coins > 0;

    // The favourite-state endpoint keys on aid too; without it favoured always reads
    // back false, so an already-favourited video looked un-favourited and the button
    // only ever added.
    std::wstring aidError;
    long long aid = 0;
    if (!bvidToAid(bvid, aid, aidError)) return state;
    const auto fav = fetch(L"/x/v2/fav/video/favoured?aid=" + std::to_wstring(aid));
    state.favorited = fav["data"]["favoured"].asBool(false);
    return state;
}

// The playurl response for one part. `qualities` reports what the account may
// actually play: accept_quality is the account's ceiling, while dash.video is what
// the server is willing to hand over right now. Those differ for anonymous users,
// so the list is built from dash.video.
namespace {

struct PlayResponse {
    bool ok = false;
    std::wstring error;
    json::Value root;
};

PlayResponse fetchPlayurl(const std::wstring& bvid, long long cid, int qn) {
    PlayResponse result;
    const std::wstring path = L"/x/player/playurl?bvid=" + bvid + L"&cid=" + std::to_wstring(cid)
                              + L"&qn=" + std::to_wstring(qn) + L"&fnval=4048&fnver=0&fourk=1";
    std::string body;
    if (!httpGet(L"api.bilibili.com", path, g_cookie, body, result.error)) return result;
    result.root = json::parse(body);
    if (!checkCode(result.root, result.error, L"获取播放地址")) return result;
    if (!result.root["data"]["v_voucher"].isNull()) {
        result.error = L"触发风控，请稍后再试";
        return result;
    }
    result.ok = true;
    return result;
}

std::wstring qualityLabel(long long id) {
    switch (id) {
    case 127: return L"8K";
    case 126: return L"杜比视界";
    case 125: return L"HDR";
    case 120: return L"4K";
    case 116: return L"1080P60";
    case 112: return L"1080P+";
    case 80: return L"1080P";
    case 74: return L"720P60";
    case 64: return L"720P";
    case 32: return L"480P";
    case 16: return L"360P";
    default: return std::to_wstring(id);
    }
}

bool qualityNeedsVip(long long id) {
    switch (id) {
    case 127: case 126: case 125: case 120: case 116: case 112: case 74: return true;
    default: return false;
    }
}

} // namespace

bool biliReportWatchProgress(const std::wstring& bvid, long long cid, int playedSeconds) {
    // Anonymous play has no history to write: the endpoint answers -101 without a
    // session, so this returns before touching the network.
    if (!bilibiliCookieLooksSignedIn(g_cookie)) return false;
    if (bvid.empty() || cid <= 0) return false;
    const std::string csrf = csrfForm();
    if (csrf.empty()) return false;
    // The same shape the site's own player posts. played_time is the position in
    // seconds; a negative value means the end was reached, which is how the site
    // records a finished video.
    std::string form = "bvid=" + toUtf8(bvid) + "&cid=" + std::to_string(cid)
        + "&played_time=" + std::to_string(playedSeconds)
        + "&realtime=" + std::to_string(playedSeconds)
        + "&start_ts=" + std::to_string(std::time(nullptr))
        + "&type=3&sub_type=0&dt=2&play_type=0&csrf=" + csrf;
    std::string body;
    std::wstring error;
    // Best effort: a failed report must never surface as a playback problem.
    if (!httpRequest(L"api.bilibili.com", L"/x/click-interface/web/heartbeat", g_cookie,
                     form, body, error)) {
        log::warn("bilibili", std::format("watch report transport failed: {}", toUtf8(error)));
        return false;
    }
    const auto root = json::parse(body);
    const long long code = root["code"].asInt(-1);
    if (code != 0) {
        log::warn("bilibili", std::format("watch report refused code={}", code));
        return false;
    }
    return true;
}


double biliResumePosition(const std::wstring& bvid, long long cid, double* duration) {
    if (duration) *duration = 0.0;
    if (!bilibiliCookieLooksSignedIn(g_cookie)) return -1.0;
    if (bvid.empty() || cid <= 0) return -1.0;
    // The history endpoint returns the newest entries first; one page of 30 is enough to
    // cover the usual case (the video the user just came back to), and asking for more
    // pages here would delay every open.
    std::string body;
    std::wstring error;
    if (!httpGet(L"api.bilibili.com",
                 L"/x/web-interface/history/cursor?ps=30&max=0&view_at=0&business=&type=archive",
                 g_cookie, body, error)) {
        log::warn("bilibili", std::format("history for resume failed: {}", toUtf8(error)));
        return -1.0;
    }
    const auto root = json::parse(body);
    if (root["code"].asInt(-1) != 0) return -1.0;
    for (const auto* entry : root["data"]["list"].elements()) {
        const std::wstring entryBvid = fromUtf8((*entry)["history"]["bvid"].asString());
        if (entryBvid != bvid) continue;
        // cid identifies the part: a multi-part video resumes the part it was left on,
        // and a different part must not inherit this one's position.
        const long long entryCid = (*entry)["history"]["cid"].asInt(0);
        if (entryCid != 0 && entryCid != cid) continue;
        const long long progress = (*entry)["progress"].asInt(-1);
        const long long total = (*entry)["duration"].asInt(0);
        if (duration && total > 0) *duration = double(total);
        // -1 means the site recorded it as finished; 0 means it was opened and dropped.
        if (progress <= 0) return -1.0;
        // Starting again for the last few seconds is not useful, and the site itself
        // clears a finished entry, so treat that as "from the top" too.
        if (total > 0 && progress >= total - 2) return -1.0;
        log::info("bilibili", std::format("resume position bvid={} cid={} at={}s",
                                          toUtf8(bvid), cid, progress));
        return double(progress);
    }
    return -1.0;
}

std::vector<BiliQuality> biliQualities(const std::wstring& bvid, long long cid) {
    std::vector<BiliQuality> out;
    if (cid <= 0) { log::warn("bili-quality", "cid<=0, no request"); return out; }

    // qn=80（1080p）而不是 127（8K）：playurl 的 qn 是「我要这一档」，服务端只会
    // 在 dash.video 里给出被请求的那一档。用 127 请求时没 8K 权限的账号只能拿回
    // 服务端降级后的单一档位，于是画质菜单只剩「自动」。用 80 请求才会列出全部
    // 可选档位。newbv 的默认值同为 80，见其 BiliHttpApi.kt 的 `qn: Int? = 80`。
    // qn=80 (1080p) rather than 127 (8K): playurl treats qn as "serve me this tier",
    // so dash.video carries only the requested tier. Asking for 8K on an account
    // without it returns just the single downgraded tier, which left the quality menu
    // with nothing but 自动. Asking for 80 lists every tier instead. newbv defaults to
    // 80 as well (BiliHttpApi.kt: `qn: Int? = 80`).
    const auto response = fetchPlayurl(bvid, cid, 80);
    if (!response.ok) {
        log::warn("bili-quality", "playurl failed for qn=80");
        return out;
    }

    std::vector<long long> ids;
    // support_formats 是服务端给出的「本片支持哪些画质」清单，与请求的 qn 无关，
    // 是画质菜单的权威来源。dash.video 只反映这次请求实际取回的流，作为兜底。
    // support_formats is the server's list of the tiers this video has, independent
    // of the requested qn, so it is the authoritative menu source. dash.video only
    // reflects the streams this request actually returned, hence the fallback.
    for (const auto* format : response.root["data"]["support_formats"].elements()) {
        const long long id = (*format)["quality"].asInt(0);
        if (id > 0 && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
    }
    const char* source = "support_formats";
    if (ids.empty()) {
        source = "dash.video";
        for (const auto* video : response.root["data"]["dash"]["video"].elements()) {
            const long long id = (*video)["id"].asInt(0);
            if (id > 0 && std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
        }
    }
    {
        std::string joined;
        for (long long id : ids) joined += std::to_string(id) + " ";
        log::info("bili-quality", std::format("cid={} via={} ids=[{}]", cid, source, joined));
    }
    std::sort(ids.begin(), ids.end(), std::greater<>());
    for (long long id : ids) {
        BiliQuality q;
        q.id = int(id);
        q.label = qualityLabel(id);
        q.needsVip = qualityNeedsVip(id);
        out.push_back(q);
    }
    return out;
}

BiliPlayUrls biliResolvePlay(const std::wstring& bvid, long long cid, int qn,
                             const std::string& codec, bool tryBackupCdn,
                             const std::string& audio) {
    BiliPlayUrls result;
    if (cid <= 0) { result.error = L"缺少 cid"; return result; }
    const int requested = qn;
    if (qn <= 0) qn = 127;

    auto response = fetchPlayurl(bvid, cid, qn);
    if (!response.ok) { result.error = response.error; return result; }

    const auto& data = response.root["data"];

    // The server silently caps the rendition to what the account may use: asking
    // for 8K while signed out yields 480P. It also sets data.quality to what it
    // actually served, so the log records the gap instead of pretending otherwise.
    const long long served = data["quality"].asInt(0);
    if (served > 0 && served < qn) {
        log::info("bilibili", std::format("qn={} not permitted for this account, served qn={}",
                                          qn, served));
    }

    // Video selection follows newBV (PlaybackSelection.kt / resolveMediaUrls):
    //   1. The preferred quality is a CEILING, not an exact match. Among the qualities
    //      the response actually offers, take the highest one that does not exceed it.
    //      Only when every offered quality is above the preference does it fall back to
    //      the lowest, which is closest to what was asked and cheapest to decode.
    //   2. Within the chosen quality, the codec preference decides (avc / hevc / auto);
    //      a quality that only ships one codec still plays.
    //   3. If nothing matches, the first stream is used rather than failing.
    // qn 0 means "no preference": take the best the account may use, which on a video
    // with an HDR master (id 125, HEVC only) is HDR.
    auto isHdrCodec = [](const std::string& value) {
        return value.rfind("hvc1", 0) == 0 || value.rfind("hev1", 0) == 0
            || value.rfind("dvhe", 0) == 0 || value.rfind("dvh1", 0) == 0;
    };
    auto matchesCodec = [&](const std::string& value) {
        if (codec == "avc") return value.rfind("avc1", 0) == 0;
        if (codec == "hevc") return isHdrCodec(value);
        // "auto": no constraint, the ordering below decides.
        return true;
    };
    // Distinct quality ids, descending.
    std::vector<long long> qualityIds;
    for (const auto* video : data["dash"]["video"].elements()) {
        const long long id = (*video)["id"].asInt(0);
        if (id > 0 && std::find(qualityIds.begin(), qualityIds.end(), id) == qualityIds.end())
            qualityIds.push_back(id);
    }
    std::sort(qualityIds.begin(), qualityIds.end(), std::greater<>());
    if (qualityIds.empty()) { result.error = L"没有可用的视频流"; return result; }

    // The ceiling rule from newBV's orderQualities().
    long long chosenQuality = qualityIds.front();
    if (qn > 0) {
        const auto atOrBelow = std::find_if(qualityIds.begin(), qualityIds.end(),
                                            [qn](long long id) { return id <= qn; });
        if (atOrBelow != qualityIds.end()) {
            chosenQuality = *atOrBelow;
        } else {
            // Everything offered is above the preference: take the lowest.
            chosenQuality = qualityIds.back();
            log::info("bilibili", std::format("every offered quality is above qn={}; using {}",
                                              qn, chosenQuality));
        }
    }

    // Within that quality: the preferred codec, then any codec, then the first stream.
    const json::Value* best = nullptr;
    for (const auto* video : data["dash"]["video"].elements()) {
        if ((*video)["id"].asInt(0) != chosenQuality) continue;
        if (!matchesCodec((*video)["codecs"].asString())) continue;
        // "auto" has no codec constraint, so pick the HDR-capable one when both exist.
        if (!best || (codec == "auto" && isHdrCodec((*video)["codecs"].asString())
                      && !isHdrCodec((*best)["codecs"].asString())))
            best = video;
    }
    if (!best) {
        for (const auto* video : data["dash"]["video"].elements()) {
            if ((*video)["id"].asInt(0) == chosenQuality) { best = video; break; }
        }
    }
    if (!best) {
        // Last resort: whatever the response listed first. The vector from elements()
        // is a temporary, so the pointer has to be taken inside this loop's lifetime.
        for (const auto* video : data["dash"]["video"].elements()) { best = video; break; }
    }
    if (!best) { result.error = L"没有可用的视频流"; return result; }

    std::string videoUrl = (*best)["baseUrl"].asString();
    if (videoUrl.empty()) videoUrl = (*best)["base_url"].asString();
    if (videoUrl.empty()) { result.error = L"视频流地址为空"; return result; }

    // Bilibili returns several mirrors of the same rendition. Keeping them lets the
    // caller retry a different node when one is unreachable, which is the usual
    // reason a play fails on a single CDN.
    // 候选集合并完之后统一排序：PCDN / 第三方 / IP 直连沉到最后，其余按实测速度排序，
    // 然后把最快的那个当成主地址。主地址落在 PCDN 上正是「缓冲涨不起来」的常见原因。
    // The full candidate set is ranked before anything is chosen: low-quality nodes sink to
    // the end, the rest go by measured speed, and the fastest becomes the primary address.
    // A primary landing on PCDN is the usual reason a buffer never fills.
    {
        std::vector<std::wstring> candidates;
        candidates.push_back(fromUtf8(videoUrl));
        auto addBackups = [&](const char* key) {
            for (const auto* backup : (*best)[key].elements()) {
                const std::wstring url = fromUtf8((*backup).asString());
                if (!url.empty() && std::find(candidates.begin(), candidates.end(), url) == candidates.end())
                    candidates.push_back(url);
            }
        };
        addBackups("backupUrl");
        if (candidates.size() == 1) addBackups("backup_url");
        // 用户关掉「自动选源」时只做过滤（把 PCDN 沉底），不测速 —— 那种情况下不该
        // 为排序多付一段点击延迟。
        // With 自动选源 off, filter only (PCDN sinks) and skip probing: the user did not
        // ask for ranking, and it must not add click latency.
        std::vector<std::wstring> ranked = rankCdnCandidates(candidates, tryBackupCdn);
        if (!ranked.empty()) {
            if (ranked.front() != fromUtf8(videoUrl)) {
                log::info("bilibili", std::format("video CDN reordered: primary host is now {}",
                                                  toUtf8(ranked.front())));
            }
            videoUrl = toUtf8(ranked.front());
            if (tryBackupCdn) result.videoMirrors = std::move(ranked);
        } else if (tryBackupCdn) {
            result.videoMirrors = std::move(candidates);
        }
    }

    // Audio. DASH keeps it in its own arrays and the video stream carries no audio
    // track at all. Bilibili publishes three kinds:
    //   dash.audio[]            AAC, ids 30216 (64k) / 30232 (132k) / 30280 (192k)
    //   dash.dolby.audio        Dolby Atmos, ec-3, member-only
    //   dash.flac.audio         Hi-Res lossless, fLaC, member-only
    // The preference picks between them; anything unavailable falls back to the
    // highest-bitrate AAC so a non-member still gets sound.
    std::string audioUrl, audioCodec;
    auto urlOf = [](const json::Value& entry) {
        std::string base = entry["baseUrl"].asString();
        if (base.empty()) base = entry["base_url"].asString();
        return base;
    };
    auto takeAudio = [&](const json::Value& entry) {
        const std::string base = urlOf(entry);
        if (base.empty()) return false;
        // 音轨**不做** CDN 换序，就用服务端给的主地址。
        // 试过一次，现场 2026-10-25 直接让它换到了另一条流：日志里音轨总长从 97216 ms
        // 变成 1663936 ms，而且读到 22763 ms 就 EIO 关闭 —— 声音整段没有。音频码率只有
        // 一百多 kbps，慢节点通常也撑得住，换序的收益远小于它带来的风险。
        // The audio track is **not** reordered; the server's primary address is used as is.
        // It was tried once and on 2026-10-25 it moved to a different stream: the log shows
        // the track duration going from 97216 ms to 1663936 ms, then EOF/EIO at 22763 ms and
        // the track closing - the whole video was silent. At a couple of hundred kbps a slow
        // node usually copes, and the gain from reordering is far smaller than the risk.
        audioUrl = base;
        audioCodec = entry["codecs"].asString();
        return true;
    };
    // Dolby and FLAC are shaped differently by the API: dolby.audio is an ARRAY of
    // entries, flac.audio is a single object. Reading both as objects silently
    // dropped Dolby, and the request then fell through to Hi-Res.
    const json::Value& dolbyAudio = data["dash"]["dolby"]["audio"];
    const json::Value& flacAudio = data["dash"]["flac"]["audio"];
    // elements() hands back a temporary vector, so the entry is taken inside a loop
    // rather than kept as a pointer past it.
    auto firstDolby = [&](const json::Value** out) {
        for (const auto* entry : dolbyAudio.elements()) { *out = entry; return true; }
        return false;
    };

    bool audioPicked = false;
    if (audio == "flac") {
        audioPicked = flacAudio.isObject() && takeAudio(flacAudio);
    } else if (audio == "dolby") {
        const json::Value* entry = nullptr;
        if (firstDolby(&entry)) audioPicked = takeAudio(*entry);
        else if (dolbyAudio.isObject()) audioPicked = takeAudio(dolbyAudio);
    } else if (audio == "aac192" || audio == "aac132" || audio == "aac64") {
        const long long wantedId = audio == "aac192" ? 30280 : (audio == "aac132" ? 30232 : 30216);
        for (const auto* entry : data["dash"]["audio"].elements()) {
            if ((*entry)["id"].asInt(0) == wantedId) { audioPicked = takeAudio(*entry); break; }
        }
    }
    // "auto" (and any preference this video cannot satisfy): best first. Hi-Res is
    // lossless, then Dolby, then the highest-bitrate AAC.
    if (!audioPicked) {
        const json::Value* dolbyEntry = nullptr;
        if (flacAudio.isObject() && takeAudio(flacAudio)) {
            audioPicked = true;
        } else if (firstDolby(&dolbyEntry) && takeAudio(*dolbyEntry)) {
            audioPicked = true;
        } else if (dolbyAudio.isObject() && takeAudio(dolbyAudio)) {
            audioPicked = true;
        } else {
            long long bestBandwidth = -1;
            for (const auto* entry : data["dash"]["audio"].elements()) {
                const std::string base = urlOf(*entry);
                if (base.empty()) continue;
                const long long bandwidth = (*entry)["bandwidth"].asInt(0);
                if (bandwidth > bestBandwidth) {
                    bestBandwidth = bandwidth;
                    audioUrl = base;
                    audioCodec = (*entry)["codecs"].asString();
                    audioPicked = true;
                }
            }
        }
    }
    if (!audioPicked)
        log::info("bilibili", std::format("no audio track matched preference '{}'", audio));

    result.ok = true;
    result.videoUrl = fromUtf8(videoUrl);
    result.audioUrl = fromUtf8(audioUrl);
    result.codec = fromUtf8((*best)["codecs"].asString());
    result.width = unsigned((*best)["width"].asInt(0));
    result.height = unsigned((*best)["height"].asInt(0));
    // DASH "frame_rate" is either a plain number ("60") or a rational ("16000/672").
    // Parsing it here lets the UI pick the frame-generation multiplier before the graph
    // is built, which avoids a two-second pipeline rebuild right after playback starts.
    {
        const std::string rate = (*best)["frame_rate"].asString();
        if (!rate.empty()) {
            const auto slash = rate.find('/');
            const double num = std::atof(rate.substr(0, slash).c_str());
            const double den = slash == std::string::npos ? 1.0 : std::atof(rate.substr(slash + 1).c_str());
            if (num > 0.0 && den > 0.0) result.frameRate = num / den;
        }
    }
    // FFmpeg's `headers` option is a CRLF-separated block ending in CRLF, and it must
    // not carry User-Agent: FFmpeg owns that through its own option, and a duplicate
    // makes the request malformed. The CDN checks both the Referer and a browser-like
    // UA, which is the same pair newBV's PlayerConstants sends.
    result.headers = L"Referer: https://www.bilibili.com\r\n";
    result.userAgent = kUserAgent;
    if (audioUrl.empty())
        log::warn("bilibili", "no DASH audio track in the response; playback would be silent");
    log::info("bilibili", std::format("play {} cid={} qn={} -> codec={} {}x{} audio={} frameRate={:.3f}",
        toUtf8(bvid), cid, qn, toUtf8(result.codec), result.width, result.height,
        audioCodec, result.frameRate));
    return result;
}

} // namespace veyra::media
