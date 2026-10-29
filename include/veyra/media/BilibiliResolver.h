#pragma once
#include <string>

namespace veyra::media {

// Resolves a Bilibili page URL to a playable media stream URL.
//
// The player can already open any http(s) media URL; what it cannot do is turn
// "https://www.bilibili.com/video/BV..." into the CDN address of the actual
// stream. That is what this does, through Bilibili's public web API:
//
//   view?bvid=...   -> cid (the page's clip id) and the title
//   playurl?...     -> a DASH manifest of video/audio renditions
//
// The chosen rendition is the highest H.264 one, because H.264 is what every
// hardware decoder on Windows handles without the browser codec question.
//
// The CDN requires the site's Referer and User-Agent; those come back in
// `headers` so the demuxer can send them (see networkMediaHeaders).
struct BilibiliStream {
    bool ok = false;
    std::wstring url;        // playable media URL (video)
    // DASH serves audio separately: the video URL carries no audio track, so the
    // player has to open this second address as well. Empty when the response
    // delivered a muxed stream.
    std::wstring audioUrl;
    std::wstring headers;    // "Referer: https://www.bilibili.com\r\n"
    // Sent through FFmpeg's user_agent option: a User-Agent inside `headers` would be
    // sent twice and the CDN rejects the request.
    std::wstring userAgent;
    std::wstring title;      // page title, for the UI
    std::wstring error;      // human-readable reason when ok == false
    unsigned width = 0;
    unsigned height = 0;
    std::wstring codec;      // e.g. "avc1.64001F"
    std::wstring audioCodec; // e.g. "mp4a.40.2"
    int quality = 0;         // Bilibili quality id that was selected
};

// True when `url` looks like a Bilibili video page.
bool isBilibiliPageUrl(const std::wstring& url);

// Resolves the page. Network access happens on the calling thread; call it off the
// UI thread or accept a short block.
//
// `cookie` is the site's Cookie header value (the user's own login). Without it the
// API only serves low renditions - a member's 1080P+/4K streams require the SESSDATA
// and bili_jct cookies from a signed-in browser.
BilibiliStream resolveBilibiliStream(const std::wstring& pageUrl,
                                     const std::wstring& cookie = {});

// Where the user's cookie is kept between runs.
std::wstring bilibiliCookiePath();

// Reads the stored cookie (empty when none). The file holds only the Cookie header.
std::wstring loadBilibiliCookie();

// Stores the cookie. An empty string clears it. Returns false when the file cannot
// be written.
bool saveBilibiliCookie(const std::wstring& cookie);

// True when `cookie` looks like a signed-in Bilibili session: it must carry SESSDATA.
bool bilibiliCookieLooksSignedIn(const std::wstring& cookie);

// --- QR sign-in -------------------------------------------------------------
// The user scans a QR code with the Bilibili app; the poll then returns the
// session cookies. This is the same flow the website uses and needs no password.
struct BilibiliQrLogin {
    bool ok = false;          // the generate call succeeded
    std::wstring qrKey;       // opaque key for pollBilibiliQr()
    std::wstring qrUrl;       // URL to encode as the QR image
    std::wstring error;
};

// One poll result.
struct BilibiliQrPoll {
    enum class State { Waiting, Scanned, Confirmed, Expired, Error };
    State state = State::Error;
    std::wstring cookie;      // set when state == Confirmed
    std::wstring message;
};

// Asks the server for a new QR code.
BilibiliQrLogin startBilibiliQrLogin();

// Polls once. Call about once a second; `Confirmed` carries the Cookie header value.
BilibiliQrPoll pollBilibiliQrLogin(const std::wstring& qrKey);

} // namespace veyra::media
