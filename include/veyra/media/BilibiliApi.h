#pragma once
#include <string>
#include <vector>
#include <cstdint>

namespace veyra::media {

// Minimal Bilibili client for the in-app Bilibili module.
//
// The endpoints, the WBI signing scheme and the quality/codec selection follow the
// newBV project (MIT, github.com/Frost819/newBV), rewritten for WinHTTP so the app
// keeps a single HTTP stack. Nothing here renders a page: the module lists videos
// and hands a resolved stream to the normal player pipeline.

struct BiliVideoCard {
    std::wstring bvid;
    std::wstring title;
    std::wstring cover;        // https URL of the cover image
    std::wstring owner;        // uploader name
    std::wstring duration;     // "12:34"
    long long play = 0;        // view count
    long long danmaku = 0;
    int pubdate = 0;
};

struct BiliPage {
    std::wstring bvid;
    std::wstring title;
    std::wstring cover;
    std::wstring desc;
    std::wstring owner;
    std::wstring ownerFace;
    // 作者的 mid：拉「作者视频」列表时要用。没有它就只知道名字。
    // The uploader's mid, needed to list that author's videos; without it only the name
    // is known.
    long long ownerMid = 0;
    long long play = 0;
    long long like = 0;
    long long danmaku = 0;
    long long coin = 0;
    long long favorite = 0;
    int duration = 0;          // seconds
    int pubdate = 0;
    std::vector<std::pair<long long, std::wstring>> pages;   // (cid, part title)
    bool ok = false;
    std::wstring error;
};

struct BiliQuality {
    int id = 0;                // qn: 127=8K, 120=4K, 116=1080P60, 80=1080P, 64=720P
    std::wstring label;        // "1080P", "4K" ...
    bool needsVip = false;
};

// One resolved playable pair. DASH keeps video and audio apart.
struct BiliPlayUrls {
    bool ok = false;
    std::wstring videoUrl;
    std::wstring audioUrl;
    std::wstring headers;
    // Sent through FFmpeg's user_agent option, not inside `headers`.
    std::wstring userAgent;
    std::wstring error;
    std::wstring codec;
    unsigned width = 0;
    unsigned height = 0;
    // DASH advertises the stream's nominal rate ("frame_rate", e.g. "60" or "16000/672").
    // The automatic frame-generation multiplier needs a source rate BEFORE the graph is
    // built: deciding it after the first frame rebuilds the whole pipeline mid-playback
    // and stalls for about two seconds (field log 2026-10-24). 0 means the response did
    // not carry it, and the caller falls back to deciding on the first frame.
    double frameRate = 0.0;
    // Mirrors of the chosen video rendition, primary first. Only filled when
    // biliResolvePlay was asked for backup addresses; the bridge retries the next
    // one when a node cannot be opened.
    std::vector<std::wstring> videoMirrors;
};

// Session cookie, "SESSDATA=...; bili_jct=...". Empty means anonymous.
void setBiliCookie(const std::wstring& cookie);
std::wstring biliCookie();

// Account state from /x/web-interface/nav.
struct BiliAccount {
    bool loggedIn = false;
    std::wstring name;
    std::wstring face;
    int level = 0;
    bool vip = false;
    long long mid = 0;
};
BiliAccount biliAccount();

// Search: keyword -> video cards. `page` starts at 1.
bool biliSearch(const std::wstring& keyword, int page, std::vector<BiliVideoCard>& out,
                std::wstring& error);

// Home feed (recommended videos).
bool biliRecommended(std::vector<BiliVideoCard>& out, std::wstring& error);

// Video detail by bvid.
BiliPage biliVideoPage(const std::wstring& bvid);

// History / favourites / followed uploaders (all need a session).
bool biliHistory(std::vector<BiliVideoCard>& out, std::wstring& error);
// One of the account's own favourite folders. `mediaId` 0 means the first folder
// (the default one); biliFavFolders() lists them so the UI can offer a choice.
bool biliFavorites(std::vector<BiliVideoCard>& out, std::wstring& error,
                   long long mediaId = 0, int page = 1);
bool biliFollowed(std::vector<BiliVideoCard>& out, std::wstring& error);

// One favourite folder: its id, name, how many videos it holds, and the site's attr
// flag. attr 0 marks the account's default folder, which is the one media_id=0 is
// supposed to mean; the API does not guarantee it comes first in the list.
// 一个收藏夹：id、名称、视频数，以及站方的 attr 标志。attr 为 0 表示默认收藏夹 ——
// 也就是 media_id=0 本该指向的那个；接口不保证它排在列表第一位。
struct BiliFavFolder {
    long long id = 0;
    std::wstring title;
    long long count = 0;
    int attr = -1;   // -1 表示接口没给这个字段
};
// The folders the signed-in account created, in the site's own order.
bool biliFavFolders(std::vector<BiliFavFolder>& out, std::wstring& error);

// Watch later, popular, and the related-videos list under a player.
bool biliWatchLater(std::vector<BiliVideoCard>& out, std::wstring& error);
bool biliPopular(std::vector<BiliVideoCard>& out, std::wstring& error, int page = 1);
bool biliRelated(const std::wstring& bvid, std::vector<BiliVideoCard>& out, std::wstring& error);

// One comment on a video.
struct BiliComment {
    std::wstring author;
    std::wstring face;
    std::wstring message;
    long long like = 0;
    int ctime = 0;
    int replies = 0;
    // rpid 是删除时唯一需要的标识；mid 用来判断这条是不是自己发的
    // （只有自己的评论才给删除按钮）。两者都来自同一次回复解析。
    // rpid identifies the comment for deletion, mid says whether it is the account's own
    // (only those get a delete button). Both come from the same reply parse.
    long long rpid = 0;
    long long mid = 0;
    // 富文本版本：文本里的 [表情名] 已经换成 <img>，供 Text.RichText 渲染。
    // 纯文本的 message 保留，用于复制等场合。
    // A rich-text version where [emote] became <img>, for Text.RichText. The plain
    // `message` stays for copying and the like.
    std::wstring messageRich;
};

// Comments, newest first. The endpoint keys on the video's aid, which is resolved
// from the bvid here so callers do not have to carry it around.
// sort: 0 按时间（最新），1 按热度（最热）—— 与网页版的两个选项一一对应。
// sort: 0 by time (newest), 1 by popularity (hottest) - the same two choices as the web.
// total 回填评论总数（data.page.count），用于标题上的「评论 N」。
// total receives the overall comment count (data.page.count) for the "评论 N" heading.
// 某个 UP 主的投稿，按发布时间从新到旧。需要 WBI 签名。
// One uploader's videos, newest first. Requires WBI signing.
bool biliAuthorVideos(long long mid, int page, std::vector<BiliVideoCard>& out, std::wstring& error);

bool biliComments(const std::wstring& bvid, int page, int sort, std::vector<BiliComment>& out,
                  long long& total, std::wstring& error);

// One danmaku (bullet) line.
struct BiliDanmaku {
    double time = 0.0;      // seconds into the video
    int mode = 1;           // 1..3 scroll, 4 bottom, 5 top
    int color = 0xFFFFFF;
    std::wstring text;
};

// 一条字幕轨的元信息。lan 是站方的语言码（zh / en / ai-zh），lanDoc 是给人看的名字。
// ai 为真表示这条是语音识别的 AI 字幕，通常带错字。
// One subtitle track as the site describes it. `lan` is the site's language code
// (zh / en / ai-zh), `lanDoc` is the display name, and `ai` marks speech recognition.
struct BiliSubtitle {
    std::wstring lan;
    std::wstring lanDoc;
    std::wstring url;      // absolute https URL; empty when the request was not signed in
    bool ai = false;
};

// 站方字幕自带显示参数（BCC 头）。网页版的「白字 + 半透明紫底」就来自这几个字段，
// 和我们自己的全局字幕偏好是两套东西。
// The site ships display parameters in the BCC header; they are what give the web player
// its white-on-purple look, independent of our own subtitle preferences.
struct BiliSubtitleStyle {
    double fontSize = 0.0;          // font_size，站方给的是相对值
    std::wstring fontColor;         // font_color，形如 "#FFFFFF"
    double backgroundAlpha = 0.0;   // background_alpha，0..1
    std::wstring backgroundColor;   // background_color，形如 "#9C27B0"
    std::wstring stroke;            // "none" 或更粗；AI 字幕用大写 Stroke
    int location = 2;               // 2 = 底部居中（同 ASS 的 numpad）
    bool present = false;           // 头里确实带了这些字段
};

// 一条字幕文本。时间单位是秒，和站方的 JSON 一致。
struct BiliSubtitleCue {
    double from = 0.0;
    double to = 0.0;
    std::wstring content;
};

// 列出某个分P的字幕轨。必须走 wbi 签名端点：未签名的 /x/player/v2 也会返回列表，
// 但 subtitle_url 是空的，看起来像"这个视频没有字幕"。
// Lists one part's subtitle tracks. It has to be the wbi-signed endpoint: the unsigned
// /x/player/v2 returns the list with empty subtitle_url values, which reads as "this
// video has no subtitles".
bool biliSubtitleList(const std::wstring& bvid, long long cid, std::vector<BiliSubtitle>& out, std::wstring& error);

// 下载并解析一条字幕。URL 的主机是 CDN（aisubtitle.hdslb.com），不是 api.bilibili.com。
// Downloads one track. The URL lives on a CDN host, not on api.bilibili.com.
bool biliSubtitleBody(const std::wstring& url, std::vector<BiliSubtitleCue>& out, std::wstring& error,
                      BiliSubtitleStyle* style = nullptr);

// The danmaku track for one part. `cid` comes from data.pages[].cid.
bool biliDanmaku(long long cid, std::vector<BiliDanmaku>& out, std::wstring& error);

// Interactions. All need a signed-in session and return false with a reason.
bool biliLike(const std::wstring& bvid, bool like, std::wstring& error);
bool biliCoin(const std::wstring& bvid, int count, std::wstring& error);
// 发一条文字评论（纯文本，不含表情/@）。先用来验证风控是否放行。
// Post one plain-text comment (no emoji, no mentions). Used first to find out whether
// the risk check lets it through.
bool biliPostReply(const std::wstring& bvid, const std::wstring& message, std::wstring& error);
// 删除一条自己发的评论。
// Delete one comment the account owns.
bool biliDeleteReply(const std::wstring& bvid, long long rpid, std::wstring& error);

// 一栏可用的评论表情。text 是插进正文的代码（如 "[doge]"），url 是它的图片地址
// （协议相对，显示时要补 https:）。
// One usable reply emote. `text` is the code inserted into the body (e.g. "[doge]") and
// `url` its image address, protocol-relative and needing https: when displayed.
struct BiliEmote {
    std::wstring text;
    std::wstring url;
    // 所属表情包的名字（"小黄脸"、"颜文字"…）。界面按它分组，和网页版的标签一致。
    // The name of the package this belongs to ("小黄脸", "颜文字", ...). The UI groups by
    // it, matching the web front end's tabs.
    std::wstring package;
};

// 拉取当前账号可用的评论表情。
// Fetch the reply emotes available to this account.
bool biliEmotePanel(std::vector<BiliEmote>& out, std::wstring& error);
// folderId 0 means the account's first folder; the UI passes the folder the user picked.
bool biliFavorite(const std::wstring& bvid, bool add, long long folderId,
                  std::wstring& error);
bool biliAddWatchLater(const std::wstring& bvid, bool add, std::wstring& error);

// Whether the account already liked / coined / favourited this video.
struct BiliInteractionState {
    bool liked = false;
    bool coined = false;
    bool favorited = false;
    int coins = 0;
};
BiliInteractionState biliInteractionState(const std::wstring& bvid);

// Resolves a bvid+cid to playable URLs at the best quality the account allows.
// `qn` 0 asks for the highest available.
// Resolves a bvid+cid to playable URLs at the best quality the account allows.
// `qn` 0 asks for the highest available; a specific id is honoured exactly.
// `codec` picks between renditions of the same quality: "auto" prefers the
// HDR-capable one (HEVC), "avc" forces H.264, "hevc" forces HEVC.
// `tryBackupCdn` falls back to the response's backupUrl when the primary address
// cannot be opened.
// `audio` picks the audio track: "auto" (best available: Hi-Res, then Dolby, then
// the highest-bitrate AAC), "flac", "dolby", "aac192", "aac132" or "aac64". A
// preference the video cannot satisfy falls back to "auto" rather than going silent.
BiliPlayUrls biliResolvePlay(const std::wstring& bvid, long long cid, int qn,
                             const std::string& codec = "auto",
                             bool tryBackupCdn = false,
                             const std::string& audio = "auto");

// Qualities the account may use for this video, best first.
std::vector<BiliQuality> biliQualities(const std::wstring& bvid, long long cid);

// Tells Bilibili how far the user got, so the video lands in the account's own watch
// history and keeps its resume point. playedSeconds is the position reached; pass a
// negative value once the video has been watched to its end, which is what the site
// itself stores for finished. Needs a signed-in session: without a cookie this returns
// false without touching the network. A failure is not a playback error, so the caller
// ignores the result.
bool biliReportWatchProgress(const std::wstring& bvid, long long cid, int playedSeconds);

// Where the account left this exact part, in seconds, from the site's own watch
// history. Returns -1 when the video is not in the history (never watched), when it was
// watched to the end, or when the account is signed out, so the caller starts from the
// top in every one of those cases. `duration` is filled from the same entry when known.
double biliResumePosition(const std::wstring& bvid, long long cid, double* duration = nullptr);

} // namespace veyra::media
