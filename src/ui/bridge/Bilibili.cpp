// Bilibili module: bridge side.
// Split out of QmlPlayerBridge.cpp when 2.1.2 reorganised the bridge into
// src/ui/bridge/*.cpp - this file carries the module that 2.1.2 does not have.
#include "../QmlPlayerBridge_p.h"

#include "veyra/engine/FrameGenerationSelection.h"
#include "veyra/engine/DisplayFgBudget.h"
#include "veyra/media/QrImage.h"
#include "veyra/media/BilibiliApi.h"
#include "veyra/media/BilibiliResolver.h"
#include "veyra/media/DanmakuWindow.h"
#include "veyra/media/FormatBadgeWindow.h"

#include <QTimer>
#include <QPointer>
#include <future>

namespace veyra::ui {

void QmlPlayerBridge::tickBiliList() {
    auto& i = *impl_;
    // 每个槽位单独回收：qualities 和 related 同时在路上时各自发自己的信号，
    // 一次遍历处理所有已完成的，不会互相顶替。
    // Collect each slot on its own: qualities and related can be in flight together and
    // each emits its own signal, so one sweep drains everything that finished.
    for (size_t n = 0; n < i.biliListSlots.size(); ++n) {
        auto& slot = i.biliListSlots[n];
        if (!slot.busy) {
            // 槽位空着但有排队的请求：启动下一个。
            // The slot is free and something is queued: start the next one.
            if (!slot.pending.empty()) {
                auto next = std::move(slot.pending.front());
                slot.pending.erase(slot.pending.begin());
                if (!next.kind.isEmpty()) {
                    startListWorker(next.kind, next.generation);
                }
            }
            continue;
        }
        if (slot.query.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;

        auto r = slot.query.get();
        slot.busy = false;
        // A cancelled or superseded request drops its result: the user has already asked
        // for something else and must not see this one land afterwards.
        if (slot.kind.isEmpty()) continue;
        const QString kind = slot.kind;
        const int generation = slot.generation;
        slot.kind.clear();

        const bool objectRequest = kind.startsWith(QLatin1String("detail:"));
        if (!r.ok) {
            i.bilibiliError = r.error;
            if (objectRequest) emit bilibiliObjectReady(kind, QVariantMap{}, generation);
            else emit bilibiliListReady(kind, QVariantList{}, generation);
            continue;
        }
        i.bilibiliError.clear();
        if (objectRequest) emit bilibiliObjectReady(kind, r.object, generation);
        else emit bilibiliListReady(kind, r.items, generation);
    }
}


void QmlPlayerBridge::bilibiliSignOut(){
    media::saveBilibiliCookie({});
    log::info("bilibili", "cookie cleared");
    emit notice(tr("B 站登录已清除"), false);
}
// --- Bilibili module --------------------------------------------------------

namespace {
static double fgAutoTargetRate(const engine::PresentationSettings& effective,
                               const engine::PresentationSettings& requested,
                               double reportedRefreshHz) {
    // 引擎解析过的有效值优先（跟随显示器时那里已经是真实 Hz）。
    if (effective.outputRate == engine::OutputRateMode::Custom ||
        effective.outputRate == engine::OutputRateMode::FollowDisplay)
        return effective.customFps;
    // 打开片源之前引擎还没解析过，用待下发的请求值。
    if (requested.outputRate == engine::OutputRateMode::Custom)
        return requested.customFps;
    if (requested.outputRate == engine::OutputRateMode::FollowDisplay)
        return requested.customFps > 0.01 ? requested.customFps : reportedRefreshHz;
    // 两处都说「关闭」：按显示器实际刷新率算。
    return reportedRefreshHz;
}

// Cards travel to QML as plain maps; the page decides how to draw them.
QVariantList cardsToVariant(const std::vector<media::BiliVideoCard>& cards) {
    QVariantList out;
    out.reserve(int(cards.size()));
    for (const auto& c : cards) {
        QVariantMap m;
        m.insert(QStringLiteral("bvid"), QString::fromStdWString(c.bvid));
        m.insert(QStringLiteral("title"), QString::fromStdWString(c.title));
        m.insert(QStringLiteral("cover"), QString::fromStdWString(c.cover));
        m.insert(QStringLiteral("owner"), QString::fromStdWString(c.owner));
        m.insert(QStringLiteral("duration"), QString::fromStdWString(c.duration));
        // Numbers travel as int: QML only needs them for display, and qlonglong
        // needs a registered metatype to cross the boundary safely.
        m.insert(QStringLiteral("play"), QVariant(int(std::min<long long>(c.play, 2147483647LL))));
        m.insert(QStringLiteral("danmaku"), QVariant(int(std::min<long long>(c.danmaku, 2147483647LL))));
        out.push_back(m);
    }
    return out;
}
} // namespace
// 异步版：worker 拉收藏夹列表，tickBiliTasks 取回后发信号。
// The async form: a worker fetches the folder list; tickBiliTasks() collects it
// and emits the signal.

void QmlPlayerBridge::bilibiliRequestFavFolders(const QString& reason){
    media::setBiliCookie(media::loadBilibiliCookie());
    startBiliTask(BiliOp::FavFolders, [reason] {
        BiliTaskResult r;
        r.reason = reason;
        std::vector<media::BiliFavFolder> folders;
        std::wstring error;
        if (!media::biliFavFolders(folders, error)) {
            r.error = QString::fromStdWString(error);
            return r;
        }
        for (const auto& f : folders) {
            QVariantMap m;
            m.insert(QStringLiteral("id"), QVariant::fromValue<qlonglong>(f.id));
            m.insert(QStringLiteral("title"), QString::fromStdWString(f.title));
            m.insert(QStringLiteral("count"), QVariant::fromValue<qlonglong>(f.count));
            r.list.push_back(m);
        }
        r.ok = true;
        return r;
    });
}

void QmlPlayerBridge::bilibiliDisableDanmaku(){
    impl_->danmakuEnabled = false;
    if (impl_->danmakuWindow) {
        impl_->danmakuWindow->clearItems();
        impl_->danmakuWindow->hide();
    }
    log::info("danmaku", "disabled");
}
// 异步版：worker 发收藏请求，tickBiliTasks 取回后发信号。
// 提示语（已收藏/已取消收藏）和错误提示都在 GUI 线程发，worker 只带回结果。
// The async form: a worker performs the request, tickBiliTasks() collects the
// outcome. The user-facing notice stays on the GUI thread; the worker only carries
// the result back.

void QmlPlayerBridge::bilibiliRequestFavorite(const QString& bvid, bool add,
                                              qlonglong folderId, const QString& reason){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::Favorite, [wbvid, add, folderId, reason] {
        BiliTaskResult r;
        r.add = add;
        r.reason = reason;
        std::wstring error;
        r.ok = media::biliFavorite(wbvid, add, folderId, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

// ---------------------------------------------------------------------------
// 通用异步 B 站任务：9 个方法共用一套发起 / 回收。每个 op 有自己的槽位，
// 所以同时发起的请求互不覆盖。
// Generic async Bilibili task: nine methods share one start/collect path, and each op
// owns its slot, so requests started together never evict one another.
// ---------------------------------------------------------------------------

// 内部：发起一个任务。worker 闭包只捕获值，绝不捕获 this 指针或 Qt 容器。
// Internal: start one task. The worker closure captures values only - never `this` and
// never a Qt container.

bool QmlPlayerBridge::startBiliTask(BiliOp op, std::function<BiliTaskResult()> work) {
    auto& i = *impl_;
    auto& slot = i.biliTaskSlots[static_cast<size_t>(op)];
    slot.busy = true;
    slot.query = std::async(std::launch::async, [op, work = std::move(work)] {
        BiliTaskResult r = work();
        r.op = op;
        return r;
    });
    return true;
}

// GUI tick：把所有已完成的任务取走，按 op 发对应信号。一次遍历处理多个槽位，
// 所以同时完成的请求会在同一帧送达，不会各占一帧。
// GUI tick: drain every finished slot and emit the matching signal. Iterating all
// slots means several completions in one frame arrive together.

void QmlPlayerBridge::tickBiliTasks() {
    auto& i = *impl_;
    for (size_t n = 0; n < i.biliTaskSlots.size(); ++n) {
        auto& slot = i.biliTaskSlots[n];
        if (!slot.busy) continue;
        if (slot.query.wait_for(std::chrono::seconds(0)) != std::future_status::ready) continue;
        BiliTaskResult r = slot.query.get();
        slot.busy = false;
        if (!r.ok && !r.error.isEmpty()) {
            i.bilibiliError = r.error.toStdWString();
            emit notice(r.error, true);
        } else if (r.ok) {
            i.bilibiliError.clear();
        }
        switch (r.op) {
        case BiliOp::Like:
        case BiliOp::Coin:
        case BiliOp::WatchLater:
        // 发评论和删评论也走这一组：它们同样只需要把结果送回界面。
        // 之前漏了 Reply 这个 case，结果被 switch 丢掉，界面就一直停在「发送中」。
        // Posting and deleting a reply belong here too: they only need their result
        // delivered. Reply was missing, so the switch discarded it and the button stayed at
        // "sending" forever.
        case BiliOp::Reply:
        case BiliOp::DeleteReply:
            // 成功提示要留在 GUI 线程发：worker 不能碰翻译表和信号。
            // 三个动作各有自己的文案，所以按 op 分派而不是用一句通用的话。
            // The success notice belongs on the GUI thread: the worker cannot touch the
            // translation table or emit signals. Each action has its own wording, so
            // dispatch on the op rather than using one generic sentence.
            if (r.ok) {
                switch (r.op) {
                case BiliOp::Like:       emit notice(r.add ? tr("已点赞") : tr("已取消点赞"), false); break;
                case BiliOp::Coin:       emit notice(tr("已投币"), false); break;
                case BiliOp::WatchLater: emit notice(r.add ? tr("已加入稍后再看") : tr("已从稍后再看移除"), false); break;
                case BiliOp::Reply:       emit notice(tr("评论已发送"), false); break;
                case BiliOp::DeleteReply: emit notice(tr("评论已删除"), false); break;
                default: break;
                }
            }
            emit bilibiliActionDone(biliOpName(r.op), r.ok, r.error);
            break;
        case BiliOp::Comments:
            emit bilibiliCommentsReady(r.list, qlonglong(r.number), r.ok, r.error);
            break;
        case BiliOp::AuthorVideos:
            // 复用卡片列表信号，kind 带 "author:" 前缀，界面按前缀分流。
            // Reuses the card-list signal; the kind carries an "author:" prefix and the UI
            // dispatches on it.
            emit bilibiliListReady(r.text, r.list, 0);
            break;
        case BiliOp::EmotePanel:
            // 这里只发信号，不发 notice：面板是点按钮才拉的，失败时在面板位置
            // 显示原因更贴切，弹一条全局提示反而突兀。
            // Signal only, no notice: the panel is fetched on a button press, so showing the
            // reason where the panel would be is clearer than a global toast.
            emit bilibiliEmotePanelReady(r.text.split(QLatin1Char('\n')), r.ok, r.error);
            break;
        case BiliOp::EnableDanmaku: {
            // 创建叠加层、清洗、交给渲染器 —— 三步都在 GUI 线程。
            // Create the overlay, filter, hand it to the renderer - all on the GUI thread.
            int shown = 0;
            if (r.ok) {
                if (!i.danmakuWindow) {
                    i.danmakuWindow = std::make_unique<media::DanmakuWindow>();
                    if (!i.danmakuWindow->create(i.videoWindow)) {
                        i.danmakuWindow.reset();
                        r.ok = false;
                        r.error = tr("无法创建弹幕叠加层");
                    }
                }
                if (r.ok) {
                    std::vector<media::DanmakuItem> items;
                    items.reserve(r.list.size());
                    for (const auto& v : r.list) {
                        const QVariantMap m = v.toMap();
                        media::DanmakuItem item;
                        item.time = m.value(QStringLiteral("time")).toDouble();
                        item.mode = m.value(QStringLiteral("mode")).toInt();
                        item.color = m.value(QStringLiteral("color")).toULongLong();
                        item.text = m.value(QStringLiteral("text")).toString().toStdWString();
                        items.push_back(std::move(item));
                    }
                    log::info("danmaku", std::format("stage: fetched items={}", items.size()));
                    // 渲染器接不住的字符串先剔掉：空的或过长的无害，孤立代理项会让
                    // GDI+ 的 AddString 崩溃。
                    // Drop strings the renderer cannot handle before they reach GDI+: an
                    // empty or over-long one is harmless, a lone surrogate crashes
                    // AddString.
                    size_t dropped = 0;
                    items.erase(std::remove_if(items.begin(), items.end(), [&](const media::DanmakuItem& it) {
                        if (it.text.empty() || it.text.size() > 200) { ++dropped; return true; }
                        for (size_t k = 0; k < it.text.size(); ++k) {
                            const char32_t c = it.text[k];
                            if (c >= 0xD800 && c <= 0xDBFF) {
                                if (k + 1 >= it.text.size() ||
                                    it.text[k + 1] < 0xDC00 || it.text[k + 1] > 0xDFFF) {
                                    ++dropped; return true;
                                }
                                ++k;
                            } else if (c >= 0xDC00 && c <= 0xDFFF) { ++dropped; return true; }
                        }
                        return false;
                    }), items.end());
                    if (dropped) log::warn("danmaku", std::format("dropped {} unusable strings", dropped));
                    log::info("danmaku", std::format("stage: cleaned items={}", items.size()));
                    shown = int(items.size());
                    i.danmakuWindow->setItems(std::move(items));
                    log::info("danmaku", "stage: setItems done");
                    i.danmakuEnabled = true;
                    i.danmakuLastSeconds = -1.0;
                    log::info("danmaku", std::format("enabled lines={}", i.danmakuWindow->itemCount()));
                }
            }
            if (!r.ok && !r.error.isEmpty()) {
                i.bilibiliError = r.error.toStdWString();
                emit notice(r.error, true);
            }
            emit bilibiliDanmakuReady(r.ok, r.error, shown);
            break;
        }
        case BiliOp::ResumeAtCid:
            emit bilibiliResumeReady(r.number);
            break;
        case BiliOp::QrStart:
            // qrKey / qrUrl 必须落进 impl_：bilibiliQrImage() 用 qrUrl 画二维码，
            // 轮询用 qrKey 去问状态。worker 不能写 impl_（那是 GUI 线程的状态），
            // 所以在这里写。漏了这一步的后果是二维码显示成白框、点完立刻退回原页面。
            // qrKey and qrUrl have to land in impl_: bilibiliQrImage() draws the code
            // from qrUrl and the poll asks with qrKey. The worker cannot write impl_ -
            // that state belongs to the GUI thread - so it happens here. Missing this
            // step left the code as a white square and sent the page straight back.
            if (r.ok) {
                i.bilibiliQrKey = r.object.value(QStringLiteral("qrKey")).toString().toStdWString();
                i.bilibiliQrUrl = r.text.toStdWString();
            }
            emit bilibiliQrStarted(r.text, r.ok, r.error);
            break;
        case BiliOp::QrPoll:
            // confirmed 时 worker 把 cookie 带回来，落盘并清掉一次性 key。
            // 完整性校验和成功/失败提示都在 GUI 线程做，与原来的同步版一致。
            // On confirmation the worker carries the cookie back: save it and drop the
            // one-shot key. The integrity check and the notices stay on the GUI thread,
            // matching the synchronous version.
            if (r.text == QLatin1String("confirmed")) {
                const std::wstring cookie =
                    r.object.value(QStringLiteral("cookie")).toString().toStdWString();
                i.bilibiliQrKey.clear();
                i.bilibiliQrUrl.clear();
                if (cookie.empty() || !media::bilibiliCookieLooksSignedIn(cookie)) {
                    emit notice(tr("登录返回的 Cookie 不完整"), true);
                    r.text = QStringLiteral("failed");
                } else if (!media::saveBilibiliCookie(cookie)) {
                    emit notice(tr("无法保存登录信息"), true);
                    r.text = QStringLiteral("failed");
                } else {
                    log::info("bilibili", "qr sign-in stored");
                    emit notice(tr("B 站登录成功，可以播放会员画质"), false);
                }
            } else if (r.text == QLatin1String("expired") || r.text == QLatin1String("none")) {
                i.bilibiliQrKey.clear();
                i.bilibiliQrUrl.clear();
            }
            emit bilibiliQrPolled(r.text);
            break;
        case BiliOp::Account:
            emit bilibiliAccountReady(r.object, r.ok);
            break;
        // 观看进度上报：结果不通知界面。播放结束时弹一句「上报失败」只会打断观看，
        // 而这个请求本来就是尽力而为（作者原注释：best effort）。
        // The progress report tells the UI nothing: a "report failed" toast at the end of
        // a video would only interrupt playback, and the request is best-effort by design.
        case BiliOp::ReportProgress:
            break;
        case BiliOp::Count:
            break;
        // 三个早先完成的异步方法，现在也走这套槽位，回收逻辑只有一份。
        // The three earlier async methods now share this path too, so collection
        // logic exists once.
        case BiliOp::InteractionState:
            emit bilibiliInteractionReady(r.bvid, r.object, r.generation);
            break;
        case BiliOp::Favorite:
            if (r.ok) emit notice(r.add ? tr("已收藏") : tr("已取消收藏"), false);
            emit bilibiliFavoriteReady(r.ok, r.error, r.add, r.reason);
            break;
        case BiliOp::FavFolders:
            emit bilibiliFavFoldersReady(r.list, r.reason);
            break;
        }
    }
}

// --- 动作型：点赞 / 投币 / 稍后再看 -----------------------------------------
// 三者形状一致：发一个写请求，成功则刷新互动状态。提示语在 GUI 线程发。
// All three have the same shape: one write request, then re-read the interaction
// state. The user-facing notice stays on the GUI thread.

void QmlPlayerBridge::bilibiliRequestEmotePanel(){
    media::setBiliCookie(media::loadBilibiliCookie());
    startBiliTask(BiliOp::EmotePanel, [] {
        BiliTaskResult r;
        std::wstring error;
        std::vector<media::BiliEmote> emotes;
        if (!media::biliEmotePanel(emotes, error)) {
            r.error = QString::fromStdWString(error);
            return r;
        }
        // 每项 "text<TAB>url"：文本里不会出现制表符，所以这个分隔是安全的。
        // One "text<TAB>url" each; a tab never appears in either field.
        QStringList flat;
        for (const auto& e : emotes)
            flat.push_back(QString::fromStdWString(e.text) + QLatin1Char('\t')
                           + QString::fromStdWString(e.url) + QLatin1Char('\t')
                           + QString::fromStdWString(e.package));
        r.text = flat.join(QLatin1Char('\n'));
        r.ok = true;
        return r;
    });
}

void QmlPlayerBridge::bilibiliDeleteReply(const QString& bvid, qlonglong rpid){
    if (bvid.isEmpty() || rpid <= 0) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    const long long wrpid = rpid;
    startBiliTask(BiliOp::DeleteReply, [wbvid, wrpid] {
        BiliTaskResult r;
        std::wstring error;
        r.ok = media::biliDeleteReply(wbvid, wrpid, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

void QmlPlayerBridge::bilibiliPostReply(const QString& bvid, const QString& message){
    if (bvid.isEmpty() || message.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    const std::wstring wmessage = message.toStdWString();
    startBiliTask(BiliOp::Reply, [wbvid, wmessage] {
        BiliTaskResult r;
        std::wstring error;
        r.ok = media::biliPostReply(wbvid, wmessage, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

void QmlPlayerBridge::bilibiliRequestLike(const QString& bvid, bool like){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::Like, [wbvid, like] {
        BiliTaskResult r;
        r.add = like;   // 点赞方向：true=点赞 false=取消
        std::wstring error;
        r.ok = media::biliLike(wbvid, like, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

void QmlPlayerBridge::bilibiliRequestCoin(const QString& bvid, int count){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::Coin, [wbvid, count] {
        BiliTaskResult r;
        std::wstring error;
        r.ok = media::biliCoin(wbvid, count, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

void QmlPlayerBridge::bilibiliRequestWatchLater(const QString& bvid, bool add){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::WatchLater, [wbvid, add] {
        BiliTaskResult r;
        r.add = add;    // 方向：true=加入 false=移除
        std::wstring error;
        r.ok = media::biliAddWatchLater(wbvid, add, error);
        if (!r.ok) r.error = QString::fromStdWString(error);
        return r;
    });
}

// --- 取数型：评论 -----------------------------------------------------------

void QmlPlayerBridge::bilibiliRequestComments(const QString& bvid, int page, int sort){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::Comments, [wbvid, page, sort] {
        BiliTaskResult r;
        std::wstring error;
        std::vector<media::BiliComment> comments;
        long long total = 0;
        if (!media::biliComments(wbvid, page, sort, comments, total, error)) {
            r.error = QString::fromStdWString(error);
            return r;
        }
        // 评论总数走 number：结构体里现成的数值字段，不必为此加成员。
        // The total rides on `number`, an existing numeric field, instead of adding a member.
        r.number = double(total);
        for (const auto& c : comments) {
            QVariantMap m;
            m.insert(QStringLiteral("author"), QString::fromStdWString(c.author));
            m.insert(QStringLiteral("face"), QString::fromStdWString(c.face));
            m.insert(QStringLiteral("message"), QString::fromStdWString(c.message));
            // 富文本版（表情已换成 <img>），供评论正文用 RichText 渲染。
            // The rich version with emotes turned into <img>, rendered as RichText.
            m.insert(QStringLiteral("messageRich"), QString::fromStdWString(c.messageRich));
            m.insert(QStringLiteral("like"), QVariant::fromValue<qlonglong>(c.like));
            m.insert(QStringLiteral("ctime"), c.ctime);
            m.insert(QStringLiteral("replies"), c.replies);
            // 删除按钮只给自己的评论，所以把 rpid 和作者 mid 一起送过去；
            // 「是不是我的」由 QML 拿它与 root.account.mid 比较 —— 账号数据本来就在那边，
            // 不必在这里再存一份。
            // The delete button only appears on the account's own comments, so rpid and the
            // author mid go along; QML compares that against root.account.mid, which it
            // already holds, instead of this layer keeping a second copy.
            m.insert(QStringLiteral("rpid"), QVariant::fromValue<qlonglong>(c.rpid));
            m.insert(QStringLiteral("mid"), QVariant::fromValue<qlonglong>(c.mid));
            r.list.push_back(m);
        }
        r.ok = true;
        return r;
    });
}

// --- 取数型：开弹幕 ---------------------------------------------------------
// worker 只负责取数据（两次网络往返）；创建叠加层、清洗字符串、交给渲染器全部在
// GUI 线程的 tick 里做 —— 那三步都碰 impl_ 和窗口句柄，不能离开 UI 线程。
// 早先这版只把「条数」带回来，也没 setItems，所以开关看起来动了但画面没弹幕。
// The worker only fetches (two round trips). Creating the overlay, filtering the
// strings and handing them to the renderer all happen in the GUI tick - those three
// touch impl_ and a window handle and cannot leave the UI thread. An earlier revision
// carried back only a count and never called setItems, so the switch moved but no
// danmaku appeared.

void QmlPlayerBridge::bilibiliRequestEnableDanmaku(const QString& bvid, int pageIndex){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::EnableDanmaku, [wbvid, pageIndex] {
        BiliTaskResult r;
        // 分P 校验也在 worker 里做：biliVideoPage 本身是一次网络往返。
        // The part lookup runs on the worker too: biliVideoPage is a network round trip.
        const auto page = media::biliVideoPage(wbvid);
        if (!page.ok) { r.error = QString::fromStdWString(page.error); return r; }
        if (pageIndex < 0 || pageIndex >= int(page.pages.size())) {
            r.error = QStringLiteral("分P序号无效");
            return r;
        }
        std::vector<media::BiliDanmaku> lines;
        std::wstring error;
        if (!media::biliDanmaku(page.pages[size_t(pageIndex)].first, lines, error)) {
            r.error = QString::fromStdWString(error);
            return r;
        }
        // 原始弹幕按 QML 能接的形态带回去：时间、模式、颜色、文本。
        // 清洗和渲染留给 GUI 线程。
        // Carry the raw lines back in a form QML can hold: time, mode, colour, text.
        // Filtering and rendering stay on the GUI thread.
        for (const auto& line : lines) {
            QVariantMap m;
            m.insert(QStringLiteral("time"), line.time);
            m.insert(QStringLiteral("mode"), line.mode);
            m.insert(QStringLiteral("color"), QVariant::fromValue<qulonglong>(line.color));
            m.insert(QStringLiteral("text"), QString::fromStdWString(line.text));
            r.list.push_back(m);
        }
        r.number = double(lines.size());
        r.ok = true;
        return r;
    });
}

// --- 取数型：续播位置 -------------------------------------------------------

void QmlPlayerBridge::bilibiliRequestResumeAt(const QString& bvid, qlonglong cid){
    if (bvid.isEmpty() || cid <= 0) {
        emit bilibiliResumeReady(-1.0);
        return;
    }
    media::setBiliCookie(media::loadBilibiliCookie());
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::ResumeAtCid, [wbvid, cid] {
        BiliTaskResult r;
        r.number = media::biliResumePosition(wbvid, cid);
        r.ok = true;
        return r;
    });
}

// --- 扫码登录：取二维码 -----------------------------------------------------

void QmlPlayerBridge::bilibiliRequestQrStart(){
    startBiliTask(BiliOp::QrStart, [] {
        BiliTaskResult r;
        const auto login = media::startBilibiliQrLogin();
        if (!login.ok) {
            r.error = QString::fromStdWString(login.error);
            return r;
        }
        // qrKey / qrUrl 要留到 poll 用，所以带回去由 GUI 线程写进 impl_。
        // qrKey and qrUrl are kept for the poll, so they travel back to the GUI thread.
        r.text = QString::fromStdWString(login.qrUrl);
        r.object.insert(QStringLiteral("qrKey"), QString::fromStdWString(login.qrKey));
        r.ok = true;
        return r;
    });
}

// --- 扫码登录：轮询 ---------------------------------------------------------

void QmlPlayerBridge::bilibiliRequestQrPoll(){
    auto& i = *impl_;
    if (i.bilibiliQrKey.empty()) {
        emit bilibiliQrPolled(QStringLiteral("none"));
        return;
    }
    const std::wstring key = i.bilibiliQrKey;
    startBiliTask(BiliOp::QrPoll, [key] {
        BiliTaskResult r;
        const auto poll = media::pollBilibiliQrLogin(key);
        using State = media::BilibiliQrPoll::State;
        switch (poll.state) {
        case State::Waiting:   r.text = QStringLiteral("waiting"); break;
        case State::Scanned:   r.text = QStringLiteral("scanned"); break;
        case State::Expired:   r.text = QStringLiteral("expired"); break;
        case State::Confirmed:
            // cookie 落盘必须在 GUI 线程：它会改 impl_ 并触发界面刷新。
            // Saving the cookie has to happen on the GUI thread: it writes impl_ and
            // refreshes the UI.
            r.text = QStringLiteral("confirmed");
            r.object.insert(QStringLiteral("cookie"), QString::fromStdWString(poll.cookie));
            break;
        default:               r.text = QStringLiteral("none"); break;
        }
        r.ok = true;
        return r;
    });
}

// --- 取数型：账号信息 -------------------------------------------------------

void QmlPlayerBridge::bilibiliRequestAccount(){
    media::setBiliCookie(media::loadBilibiliCookie());
    startBiliTask(BiliOp::Account, [] {
        BiliTaskResult r;
        const auto a = media::biliAccount();
        r.object.insert(QStringLiteral("loggedIn"), a.loggedIn);
        r.object.insert(QStringLiteral("name"), QString::fromStdWString(a.name));
        r.object.insert(QStringLiteral("face"), QString::fromStdWString(a.face));
        r.object.insert(QStringLiteral("level"), a.level);
        r.object.insert(QStringLiteral("vip"), a.vip);
        // mid 用来判断哪条评论是自己发的。
        // mid identifies which comments belong to this account.
        r.object.insert(QStringLiteral("mid"), QVariant::fromValue<qlonglong>(a.mid));
        r.ok = true;
        return r;
    });
}

int QmlPlayerBridge::bilibiliLoadAuthorVideos(qlonglong mid, int page){
    // 走和卡片列表同一条通道，只是 kind 用 "author:<mid>:<page>" ——
    // 排队、槽位、结果分发都是现成的，不必再造一套。
    // Uses the same channel as the card lists with an "author:<mid>:<page>" kind: queueing,
    // slots and result delivery are all already there.
    return bilibiliLoadList(QStringLiteral("author:") + QString::number(mid)
                            + QLatin1Char(':') + QString::number(page));
}

int QmlPlayerBridge::bilibiliLoadList(const QString& kind){
    // Follows the shape the capture-device query already uses: start the work with
    // std::async, collect it from the GUI tick with a zero-timeout wait. That keeps the
    // result hand-off on the GUI thread and needs no locking for the returned list.
    //
    // Cookie first, here on the GUI thread: the media layer keeps it in one global, and
    // letting the worker write it would race with every other call reading it.
    media::setBiliCookie(media::loadBilibiliCookie());

    const int generation = ++impl_->biliListGeneration;
    // 按 kind 选槽位：qualities / related / detail / 卡片列表各有各的位置，
    // 所以详情页里同时发起的几个请求不会互相覆盖。
    // Pick the slot by kind: qualities, related, detail and card lists each have their
    // own, so the requests the detail page starts together cannot overwrite each other.
    auto& slot = impl_->biliListSlots[size_t(Impl::listSlotOf(kind))];
    // 同一槽位里已经有一个在途请求时，把新请求排进队列，而不是覆盖它。
    // 直接给 slot.query 赋新值会销毁旧 future，那个请求取到的数据就永远不会送达
    // QML —— 表现是数据明明回来了，界面却是空列表。队列里派发的请求在 tick 回收
    // 完当前那个之后再启动，所以既不丢结果，也不会让 GUI 线程等待网络。
    // Queue a new request instead of overwriting the one in flight in this slot.
    // Assigning over slot.query destroys the old future, so whatever that request
    // fetched never reaches QML - which shows as an empty list even though the data
    // came back. A queued request starts only after the current one has been collected,
    // so no result is lost and the GUI thread never waits on the network.
    if (slot.busy) {
        slot.pending.push_back(Impl::PendingListRequest{kind, generation});
        return generation;
    }
    startListWorker(kind, generation);
    return generation;
}

// 启动一个列表 worker。发起路径和 tick 的队列派发共用这一处。
// Start one list worker; the direct path and the tick's queue both use this.

void QmlPlayerBridge::startListWorker(const QString& kind, int generation) {
    auto& slot = impl_->biliListSlots[size_t(Impl::listSlotOf(kind))];
    slot.kind = kind;
    slot.generation = generation;
    slot.busy = true;

    // kind carries everything the worker needs: "home"/"popular"/... , "fav:<id>" or
    // "search:<words>".
    slot.query = std::async(std::launch::async, [kind]() {
        std::vector<media::BiliVideoCard> cards;
        std::wstring error;
        bool ok = false;
        // A kind may carry a page ("popular:2", "search:<words>:2", "fav:<id>:2"). The
        // endpoints with no page parameter (推荐/历史/稍后再看) are simply called again —
        // the site has no page for them, and that is what endless scrolling uses.
        int page = 1;
        QString base = kind;
        const int lastColon = kind.lastIndexOf(QLatin1Char(':'));
        if (lastColon > 0) {
            bool okPage = false;
            const int parsed = kind.mid(lastColon + 1).toInt(&okPage);
            if (okPage && parsed > 0) {
                const QString head = kind.left(lastColon);
                // detail:/qualities:/related: carry a bvid or a cid, not a page.
                // fav: 的两种写法只差一件事：head（最后一个冒号左边）是不是就是
                // "fav" 本身。
                //   "fav:203714961"    → head=="fav"          → 截掉的是文件夹 id
                //   "fav:203714961:2"  → head=="fav:203714961" → 截掉的是页码
                // 不要再去数 head 里的冒号：带页码时 head 必然含一个冒号，用那个做判据
                // 会把页码也当成 id（第一版就是这样，两处都错）。
                // The two fav: forms differ in one thing only: whether head (everything
                // left of the last colon) is the bare token "fav".
                //   "fav:203714961"   -> head=="fav"           -> the cut piece is the id
                //   "fav:203714961:2" -> head=="fav:203714961" -> the cut piece is a page
                // Do not count colons in head: with a page, head always holds one, and
                // using that as the test treats the page as an id too.
                const bool favIdWithoutPage = head == QLatin1String("fav");
                // "author:<mid>" 的最后一个冒号切掉的是页码，但 head 是 "author:<mid>"，
                // 所以和 fav 不同：这里需要页码，判据是 head 是否以 "author:" 开头。
                // For "author:<mid>" the last colon separates the page, and head is
                // "author:<mid>" - unlike fav, the page IS wanted here, so the test is simply
                // whether head starts with "author:".
                const bool authorPage = head.startsWith(QLatin1String("author:"));
                if (!head.startsWith(QLatin1String("detail:"))
                    && !head.startsWith(QLatin1String("qualities:"))
                    && !head.startsWith(QLatin1String("related:"))
                    && !favIdWithoutPage) {
                    page = parsed;
                    base = head;
                }
            }
        }
        if (base == QLatin1String("home")) ok = media::biliRecommended(cards, error);
        else if (base == QLatin1String("popular")) ok = media::biliPopular(cards, error, page);
        else if (base == QLatin1String("follow")) ok = media::biliFollowed(cards, error);
        else if (base == QLatin1String("history")) ok = media::biliHistory(cards, error);
        else if (base == QLatin1String("watchlater")) ok = media::biliWatchLater(cards, error);
        else if (base.startsWith(QLatin1String("author:"))) {
            // "author:<mid>"，页码已经在上面解析到 page。
            // "author:<mid>"; the page has already been parsed above.
            bool okMid = false;
            const qlonglong mid = base.mid(7).toLongLong(&okMid);
            ok = okMid && media::biliAuthorVideos(mid, page, cards, error);
        }
        else if (base.startsWith(QLatin1String("fav:")))
            ok = media::biliFavorites(cards, error, base.mid(4).toLongLong(), page);
        else if (base.startsWith(QLatin1String("search:")))
            ok = media::biliSearch(base.mid(7).toStdWString(), page, cards, error);
        else if (kind.startsWith(QLatin1String("qualities:"))) {
            // "qualities:<bvid>:<cid>" — the cid comes from the detail response, so this
            // does not ask the site for the page a second time (the synchronous
            // bilibiliQualities does, which was a whole extra round trip).
            const QStringList parts = kind.mid(10).split(QLatin1Char(':'));
            if (parts.size() == 2) {
                const QString bvid = parts[0];
                const qlonglong cid = parts[1].toLongLong();
                if (cid > 0) {
                    QVariantList out;
                    for (const auto& q : media::biliQualities(bvid.toStdWString(), cid)) {
                        QVariantMap m;
                        m.insert(QStringLiteral("id"), q.id);
                        m.insert(QStringLiteral("label"), QString::fromStdWString(q.label));
                        m.insert(QStringLiteral("vip"), q.needsVip);
                        out.push_back(m);
                    }
                    ListFetchResult r;
                    r.ok = !out.isEmpty();
                    if (!r.ok) r.error = L"无法获取画质列表";
                    r.items = out;
                    return r;
                }
            }
        }
        else if (kind.startsWith(QLatin1String("related:"))) {
            std::vector<media::BiliVideoCard> related;
            std::wstring relError;
            const bool relOk = media::biliRelated(kind.mid(8).toStdWString(), related, relError);
            ListFetchResult r;
            r.ok = relOk;
            r.error = relError;
            if (relOk) r.items = cardsToVariant(related);
            return r;
        }
        else if (kind.startsWith(QLatin1String("detail:"))) {
            // One object instead of a card list. Built here rather than through the
            // synchronous bilibiliVideoDetail so the open no longer blocks a frame.
            const auto page = media::biliVideoPage(kind.mid(7).toStdWString());
            ListFetchResult r;
            r.ok = page.ok;
            r.error = page.error;
            if (page.ok) {
                QVariantMap m;
                m.insert(QStringLiteral("ok"), true);
                m.insert(QStringLiteral("error"), QString());
                m.insert(QStringLiteral("bvid"), QString::fromStdWString(page.bvid));
                m.insert(QStringLiteral("title"), QString::fromStdWString(page.title));
                m.insert(QStringLiteral("cover"), QString::fromStdWString(page.cover));
                m.insert(QStringLiteral("desc"), QString::fromStdWString(page.desc));
                m.insert(QStringLiteral("owner"), QString::fromStdWString(page.owner));
                // 作者 mid：界面要用它拉「作者视频」。
                // The uploader's mid, which the UI needs for the author's video list.
                m.insert(QStringLiteral("ownerMid"), QVariant::fromValue<qlonglong>(page.ownerMid));
                m.insert(QStringLiteral("play"), QVariant(int(std::min<long long>(page.play, 2147483647LL))));
                m.insert(QStringLiteral("like"), QVariant(int(std::min<long long>(page.like, 2147483647LL))));
                m.insert(QStringLiteral("duration"), page.duration);
                QVariantList parts;
                for (const auto& [cid, title] : page.pages) {
                    QVariantMap p;
                    p.insert(QStringLiteral("cid"), QVariant::fromValue<qlonglong>(cid));
                    p.insert(QStringLiteral("title"), QString::fromStdWString(title));
                    parts.push_back(p);
                }
                m.insert(QStringLiteral("parts"), parts);
                r.object = m;
            }
            return r;
        }
        ListFetchResult r;
        r.ok = ok;
        r.error = error;
        if (ok) r.items = cardsToVariant(cards);
        return r;
    });
}
// 异步版：本函数只启动 worker 就返回，网络请求不再占用 GUI 线程。
// 结果由 tickBiliTasks() 取走并通过 bilibiliInteractionReady 交给 QML。
// 查询期间按钮的旧状态继续显示，等信号到了再更新 —— 比"点一下界面卡半秒"好。
// The async form starts a worker and returns; the round trip no longer occupies the
// GUI thread. tickBiliTasks() collects the result and emits
// bilibiliInteractionReady. The old button state stays on screen until then, which
// beats freezing for half a second on every click.

void QmlPlayerBridge::bilibiliRequestInteractionState(const QString& bvid){
    if (bvid.isEmpty()) return;
    media::setBiliCookie(media::loadBilibiliCookie());
    // 新的请求顶掉还没回来的旧请求：generation 让 QML 丢掉过期结果。
    // A newer request supersedes one still in flight; the generation lets QML drop
    // the stale reply.
    const int generation = ++impl_->biliInteractionGeneration;
    const std::wstring wbvid = bvid.toStdWString();
    startBiliTask(BiliOp::InteractionState, [wbvid, bvid, generation] {
        BiliTaskResult r;
        r.bvid = bvid;
        r.generation = generation;
        const auto state = media::biliInteractionState(wbvid);
        r.object.insert(QStringLiteral("liked"), state.liked);
        r.object.insert(QStringLiteral("coined"), state.coined);
        r.object.insert(QStringLiteral("favorited"), state.favorited);
        r.object.insert(QStringLiteral("coins"), state.coins);
        r.ok = true;
        return r;
    });
}

bool QmlPlayerBridge::bilibiliPlay(const QString& bvid, int pageIndex, int qn, bool fromStart){
    media::setBiliCookie(media::loadBilibiliCookie());
    // This entry point has to look the page up itself: only the cid is not known here.
    const auto page = media::biliVideoPage(bvid.toStdWString());
    if (!page.ok) {
        emit notice(QString::fromStdWString(page.error), true);
        return false;
    }
    if (pageIndex < 0 || pageIndex >= int(page.pages.size())) {
        emit notice(tr("分P序号无效"), true);
        return false;
    }
    return bilibiliPlayResolved(bvid, page.pages[size_t(pageIndex)].first, page.title,
                                qn, fromStart);
}

bool QmlPlayerBridge::bilibiliPlayCid(const QString& bvid, qlonglong cid, int qn, bool fromStart){
    // No page lookup: the detail response already carried the cid, so this skips one
    // whole round trip between the click and the first frame. The title is not needed for
    // the resolve, and the notice still shows the video's own title from the caller.
    if (cid <= 0) {
        emit notice(tr("分P序号无效"), true);
        return false;
    }
    return bilibiliPlayResolved(bvid, cid, std::wstring(), qn, fromStart);
}

// Shared tail of both play entry points: everything after the part is known. Kept in
// one place so the cid-carrying variant cannot drift from the page-lookup one.


// 把观看进度从 impl_ 状态里取出来，网络上报交给调用方。

QString QmlPlayerBridge::bilibiliLastError() const {
    return QString::fromStdWString(impl_->bilibiliError);
}
QString QmlPlayerBridge::bilibiliQrImage(int size){
    if (impl_->bilibiliQrUrl.empty()) return {};
    // The encoder lives in its own translation unit (see QrImage.h): its headers
    // must not meet Qt's, or the process aborts at startup.
    const std::string png = media::qrPngBase64(
        QString::fromStdWString(impl_->bilibiliQrUrl).toStdString(), size);
    if (png.empty()) return {};
    return QStringLiteral("data:image/png;base64,") + QString::fromStdString(png);
}


bool QmlPlayerBridge::bilibiliPlayResolved(const QString& bvid, qlonglong cid,
                                           const std::wstring& pageTitle, int qn,
                                           bool fromStart){
    // B 站打开要发三个网络往返（上报进度 / 解析播放地址 / 读续播位置）。它们原先全在
    // GUI 线程同步执行，而 WinHTTP 每个请求 15 秒超时 —— 网络一慢主线程就静止十几秒
    // （现场 2026-10-24 15:03：从列表切一个 4K 视频后静止 22.9 秒）。
    // 现在改成：主线程只取快照和发起请求，请求在 worker 里跑，结果由 tickBiliPlay 回收
    // 后再推进播放状态。状态仍然只在 GUI 线程改。
    if (impl_->biliPlayJob.resolving) {
        veyra::log::info("bilibili", "play request ignored: the previous one is still resolving");
        return false;
    }
    // 上一部的观看进度先取下来（这一部马上要覆盖 biliWatch 状态），网络部分交给 worker。
    int pendingPlayed = 0;
    std::wstring pendingReportBvid;
    long long pendingReportCid = 0;
    if (takeBiliWatchProgress(pendingReportBvid, pendingReportCid, pendingPlayed))
        veyra::log::info("bilibili", std::format("watch progress reporting queued bvid={} played_time={}",
            utf8Of(pendingReportBvid).toStdString(), pendingPlayed));
    media::setBiliCookie(media::loadBilibiliCookie());
    // 记下这次要播什么，供播完后「播放」重播用：地址会过期，bvid/cid/画质不会。
    // Remember what this play is, so Play after the end can resolve again: the address
    // expires, the bvid/cid/quality do not.
    impl_->biliReplayBvid = bvid;
    impl_->biliReplayCid = cid;
    impl_->biliReplayQuality = qn;
    // Every play is a new track (a different part, or the same part replayed): the
    // comments already on screen belong to the previous one. Done before the resolve so
    // a failure below leaves nothing stale on screen either.
    if (impl_->danmakuEnabled) bilibiliDisableDanmaku();
    // The three Bilibili settings that change what gets requested. qn 0 means the
    // quality preference, which itself may be 0 (automatic).
    const int qualityPref = impl_->prefInt("biliQuality", 0);
    const int effectiveQn = qn > 0 ? qn : qualityPref;
    QString codecPref = impl_->prefString("biliCodec");
    if (codecPref.isEmpty()) codecPref = QStringLiteral("auto");
    const bool backupCdn = impl_->prefBool("biliAutoCdn", true);
    QString audioPref = impl_->prefString("biliAudio");
    if (audioPref.isEmpty()) audioPref = QStringLiteral("auto");
    const bool wantResume = !fromStart &&
        (impl_->prefBool("rememberPosition", true) || impl_->prefBool("autoResume", false));

    auto& job = impl_->biliPlayJob;
    const uint64_t generation = ++job.generation;
    job.busy = job.resolving = true;
    job.bvid = bvid;
    job.cid = cid;
    job.pageTitle = pageTitle;
    job.qn = effectiveQn;
    job.fromStart = fromStart;
    job.plannedResume = wantResume;
    // 记下「点击时」引擎用的会话号。续播 seek 要等它变成别的值才发 —— 见 Impl 里
    // biliResumeAfterSession 的说明。这里取引擎的真实快照而不是缓存副本，缓存可能
    // 已经滞后。
    // Record the session the engine was using at click time. The resume seek waits
    // until it changes - see biliResumeAfterSession in Impl. The engine's own snapshot
    // is used rather than the cached copy, which may already be stale.
    impl_->biliResumeAfterSession = impl_->engine.snapshot().sessionId;
    // worker 只碰自己的副本：Qt 对象（impl_ 里的 QString/容器）全部在发起前拷好。
    const std::wstring wbvid = bvid.toStdWString();
    const std::string codec = codecPref.toStdString();
    const std::string audio = audioPref.toStdString();
    // 结果放在 shared_ptr 里：worker 写它，GUI tick 读它。worker 不碰任何 Qt 对象。
    job.result = std::make_shared<BiliPlayWorkerResult>();
    auto result = job.result;
    job.query = std::async(std::launch::async,
        [wbvid, cid, effectiveQn, codec, audio, backupCdn, wantResume,
         pendingReportBvid, pendingReportCid, pendingPlayed, result]() mutable {
            // 顺序与原来一致：先解析播放地址，再上报上一部的进度，最后读续播位置。
            result->urls = media::biliResolvePlay(wbvid, cid, effectiveQn, codec, backupCdn, audio);
            if (pendingReportCid > 0 && !pendingReportBvid.empty())
                result->reportOk = media::biliReportWatchProgress(pendingReportBvid, pendingReportCid, pendingPlayed);
            result->reportBvid = pendingReportBvid;
            result->reportCid = pendingReportCid;
            result->reportPlayed = pendingPlayed;
            if (wantResume) result->resumeAt = media::biliResumePosition(wbvid, cid);
            result->done.store(true);
        });
    return true;
}

// 把观看进度从 impl_ 状态里取出来，网络上报交给调用方。

void QmlPlayerBridge::maybeApplyFgAutoMultiplier() {
    const auto& s = impl_->snapshot;
    if (!s.running || s.sessionId == 0) return;
    // 本会话已经决定过：不再动。会话号变化才重新算。
    if (impl_->fgAutoSession == s.sessionId) return;
    // 打开前已按元数据定档：本会话不必再算，否则会把刚建好的管线拆了重建。
    if (impl_->fgAutoPlanned) {
        impl_->fgAutoSession = s.sessionId;
        impl_->fgAutoPlanned = false;
        return;
    }
    if (!impl_->chain.fgAutoMultiplier) { impl_->fgAutoSession = s.sessionId; return; }
    const auto backend = settings().frameGenerationBackend;
    if (!fgAutoMultiplierAvailable()) { impl_->fgAutoSession = s.sessionId; return; }
    // 补帧节点不存在才放弃；节点存在但被关着必须继续决策 —— 上一个片源可能按规则
    // 把补帧关掉了（源帧率超过上限），下一个低帧率片源要能重新打开它。
    const auto* node = impl_->chain.firstOf(engine::EffectType::FrameGeneration);
    if (!node) { impl_->fgAutoSession = s.sessionId; return; }

    // 目标帧率：输出上限为自定义/跟随显示器时用它的值；关闭时用显示器实际刷新率，
    // 自动选倍数照常工作（用户 2026-10-24 要求）。
    const double targetFps = fgAutoTargetRate(s.presentationEffective, s.presentationEffective,
                                              s.displayFgRefreshHz);
    if (!(targetFps > 0.01)) {
        // 显示器刷新率也没拿到：下一帧再试（不擅自改设置）。
        veyra::log::info("fg-auto", std::format("waiting for a target rate cap={} refresh={:.3f}",
            int(s.presentationEffective.outputRate), s.displayFgRefreshHz));
        return;
    }
    const double sourceFps = s.nominalSourceFps;
    if (!(sourceFps > 0.01)) {
        // 源帧率还没就绪：下一帧再试。
        veyra::log::info("fg-auto", std::format("waiting for source rate target={:.3f}", targetFps));
        return;
    }

    impl_->fgAutoSession = s.sessionId;
    const int before = fgMultiplier();
    const uint64_t session = s.sessionId;

    // 源帧率高于/等于输出上限：补了也显示不出来，关闭补帧。
    if (sourceFps >= targetFps / engine::kDisplayFgFitTolerance) {
        veyra::log::info("fg-auto", std::format("session={} backend={} source={:.3f} target={:.3f} chosen=off previous={} (source at/above the cap: frame generation off)",
            session, engine::frameGenerationBackendName(backend), sourceFps, targetFps, before));
        QTimer::singleShot(0, this, [this, session] {
            if (impl_->snapshot.sessionId != session) return;
            if (!impl_->chain.fgAutoMultiplier) return;
            veyra::log::info("fg-auto", std::format("turning frame generation off session={}", session));
            applyFgMultiplier(1);
        });
        return;
    }

    // 源帧率低于上限：按公式挑档，让它刚好打满上限。
    const unsigned ceiling = unsigned(std::clamp(fgMaxMultiplier(), 2, 8));
    const unsigned wanted = engine::displayFgAutoMultiplier(backend, sourceFps, targetFps, ceiling);
    if (wanted == 0) {
        veyra::log::info("fg-auto", std::format("no usable step backend={} source={:.3f} target={:.3f} ceiling={} (multiplier kept)",
            engine::frameGenerationBackendName(backend), sourceFps, targetFps, ceiling));
        return;
    }
    const int target = int(wanted);
    veyra::log::info("fg-auto", std::format("session={} backend={} source={:.3f} target={:.3f} ceiling={} chosen={} previous={}",
        session, engine::frameGenerationBackendName(backend), sourceFps, targetFps, ceiling, target, before));
    if (before == target && node->enabled) return;
    QTimer::singleShot(0, this, [this, session, target] {
        if (impl_->snapshot.sessionId != session) return;
        if (!impl_->chain.fgAutoMultiplier) return;
        veyra::log::info("fg-auto", std::format("applying {}X session={}", target, session));
        applyFgMultiplier(target);
    });
}

bool QmlPlayerBridge::planFgAutoMultiplierBeforeOpen(double sourceFps) {
    if (!(sourceFps > 0.01)) {
        veyra::log::info("fg-auto", std::format("pre-open skipped: no source rate ({:.3f})", sourceFps));
        return false;
    }
    if (!impl_->chain.fgAutoMultiplier) {
        veyra::log::info("fg-auto", "pre-open skipped: automatic selection is off");
        return false;
    }
    if (!fgAutoMultiplierAvailable()) return false;
    const auto* node = impl_->chain.firstOf(engine::EffectType::FrameGeneration);
    if (!node) return false;

    const double targetFps = fgAutoTargetRate(impl_->snapshot.presentationEffective,
                                              impl_->presentation,
                                              impl_->snapshot.displayFgRefreshHz);
    if (!(targetFps > 0.01)) {
        veyra::log::info("fg-auto", std::format("pre-open skipped: no target rate (eff={} req={} refresh={:.3f})",
            int(impl_->snapshot.presentationEffective.outputRate), int(impl_->presentation.outputRate),
            impl_->snapshot.displayFgRefreshHz));
        return false;
    }

    const auto backend = settings().frameGenerationBackend;
    const int before = fgMultiplier();
    // 源帧率已达上限：关闭补帧（与首帧后决策同一条规则）。
    int target = 1;
    if (sourceFps < targetFps / engine::kDisplayFgFitTolerance) {
        const unsigned ceiling = unsigned(std::clamp(fgMaxMultiplier(), 2, 8));
        const unsigned wanted = engine::displayFgAutoMultiplier(backend, sourceFps, targetFps, ceiling);
        if (wanted == 0) return false;      // 无可用档：交给首帧后决策
        target = int(wanted);
    }
    if (before == target && node->enabled == (target > 1)) {
        // 已经是这个档：什么都不用改。
        veyra::log::info("fg-auto", std::format("pre-open: already {}X source={:.3f} target={:.3f}",
            target, sourceFps, targetFps));
        return true;
    }
    // 先把 chain 改成目标档，openPath 之后构建管线即用它，不产生二次重建。
    applyFgMultiplier(target);
    // 标记"本会话已按预决策定档"，否则首帧后的决策会再走一遍、把刚建好的管线又拆掉
    // （实测 2026-10-24：pre-open 关掉补帧后，首帧后又关一次，卡 614ms）。
    impl_->fgAutoPlanned = true;
    veyra::log::info("fg-auto", std::format("pre-open: backend={} source={:.3f} target={:.3f} chosen={} previous={} (decided before the graph is built)",
        engine::frameGenerationBackendName(backend), sourceFps, targetFps, target, before));
    return true;
}

// 自动选择倍率 的运行时决策（用户 2026-10-24 定稿）。
// 开关关闭：完全按作者原版逻辑，倍率就是用户选的那个数字档，这里什么都不做。
// 开关开启：
//   · 源帧率低于输出上限 → 按公式挑档（ceil(上限 ÷ (源帧率×1.02))，档位向上取）
//   · 源帧率高于/等于输出上限 → 关闭补帧（节点 enabled=false，开关也跟着关）
//   · 输出上限为「关闭」→ 用显示器实际刷新率当目标，照样自动选
// 只在每个会话首次进入播放时算一次：改倍率要重建 GPU 管线，播放中改会卡顿。

void QmlPlayerBridge::setFgAutoMultiplier(bool value) {
    if (!fgAutoMultiplierAvailable()) {
        if (value) emit notice(tr("自动倍率只支持 DLSS 帧生成和 NVIDIA VFG"), true);
        return;
    }
    if (impl_->chain.fgAutoMultiplier == value) return;
    const auto before = impl_->chain.fgAutoMultiplier;
    impl_->chain.fgAutoMultiplier = value;
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        impl_->chain.fgAutoMultiplier = before; impl_->revalidate();
        emit notice(uiText(error), true); return;
    }
    // 打开自动选择时，若补帧正被关着就抬到 2X，让下一次播放有档可用。
    // 注意：不覆盖用户已选的具体档位（关闭自动后仍按那个档跑）。
    if (value && impl_->chain.fgMultiplier < 2) impl_->chain.fgMultiplier = 2;
    // 重新打开必须让它再决策一次：fgAutoSession 记的是"本会话已经算过档位"，
    // 用户中途关掉再打开时，不清这个标记就不会重新生效（2026-10-24 实测）。
    if (value) impl_->fgAutoSession = 0;
    veyra::log::info("ui-fg-auto", std::format("auto={} backend={} multiplier={}{}",
        value, engine::frameGenerationBackendName(settings().frameGenerationBackend), impl_->chain.fgMultiplier,
        value ? " (session decision reset)" : " (author-default multiplier logic)"));
    emit settingsChanged(); emitChainChanged();
}

// 目标帧率：输出上限 = 自定义/跟随显示器 时用它的值，关闭时用显示器实际刷新率。
// 打开片源之前也要能算（那时 snapshot 还没有本会话的数据），所以两个入口共用它：
// 一个读引擎已解析的 snapshot，一个读待下发的 presentation + 最近一次上报的刷新率。

// 打开片源之前先把倍率定下来，避开播放中重建管线导致的 ~2 秒卡顿。
// sourceFps 来自容器/DASH 元数据；拿不到就返回 false，由首帧后的决策兜底。

void QmlPlayerBridge::showFormatBadge(const engine::PlayerSnapshot& s) {
    impl_->formatBadgeSession = s.sessionId;
    if (impl_->videoWindow == nullptr) return;
    if (!impl_->formatBadge) {
        impl_->formatBadge = std::make_unique<media::FormatBadgeWindow>();
        if (!impl_->formatBadge->create(impl_->videoWindow)) {
            impl_->formatBadge.reset();
            return;
        }
    }
    const QString label = uiText(s.sourceHdrLabel);
    const QString title = label.isEmpty() ? QStringLiteral("SDR") : label;
    QStringList detailParts;
    const QString rate = sourceRateText();
    if (!rate.isEmpty()) detailParts << rate;
    QString summary = sourceSummary();
    if (!summary.isEmpty()) detailParts << summary.replace(QLatin1Char('x'), QString::fromUtf8("×"));
    const QString detail = detailParts.join(QString::fromUtf8(" · "));
    impl_->formatBadge->show(impl_->videoWindow, title.toStdWString(), detail.toStdWString());
    veyra::log::info("format-badge", std::format("shown title={} detail={} session={}",
        title.toStdString(), detail.toStdString(), s.sessionId));
    QTimer::singleShot(3200, this, [this]() {
        if (impl_->formatBadge) impl_->formatBadge->hide();
    });
}

bool QmlPlayerBridge::takeBiliWatchProgress(std::wstring& bvid, long long& cid, int& played) {    if (impl_->biliWatchBvid.empty() || impl_->biliWatchCid <= 0) return false;
    // Nothing was ever observed playing: do not write a bogus position into the history.
    if (impl_->biliWatchLastPos < 0.0) return false;
    played = int(impl_->biliWatchLastPos);
    // The site stores -1 for "watched to the end", which is what makes a finished video
    // show as finished instead of resuming a few seconds before the credits.
    if (impl_->biliWatchDuration > 0 && impl_->biliWatchLastPos >= impl_->biliWatchDuration - 1.0)
        played = -1;
    if (played < 0 && played != -1) played = 0;
    bvid = std::move(impl_->biliWatchBvid);
    cid = impl_->biliWatchCid;
    impl_->biliWatchBvid.clear();
    impl_->biliWatchCid = 0;
    impl_->biliWatchLastPos = -1.0;
    impl_->biliWatchDuration = 0.0;
    return true;
}

void QmlPlayerBridge::tickBiliPlay() {
    auto& i = *impl_;
    auto& job = i.biliPlayJob;
    if (!job.busy) return;
    // 只在 worker 真正完成时才取结果；done 由 worker 最后一步置位。
    if (!job.result || !job.result->done.load()) return;
    if (job.query.valid()) job.query.get();   // 回收线程；异常在 worker 里不会抛到这里
    auto result = std::move(job.result);
    job.busy = job.resolving = false;
    // 期间用户又点了别的（新请求把 generation 顶掉）：这次结果作废。
    if (job.generation == 0 || !result) return;
    if (result->reportCid > 0)
        veyra::log::info("bilibili", std::format("watch progress reported bvid={} played_time={} ok={}",
            utf8Of(result->reportBvid).toStdString(), result->reportPlayed,
            result->reportOk ? 1 : 0));
    const auto& urls = result->urls;
    if (!urls.ok) {
        emit notice(QString::fromStdWString(urls.error), true);
        return;
    }
    const std::wstring pageTitle = job.pageTitle;
    i.pendingTitle = pageTitle;
    // Backup CDN: remember the full mirror list so a failed open can try the next.
    i.biliMirrors = i.prefBool("biliAutoCdn", true) ? urls.videoMirrors
                                                    : std::vector<std::wstring>{};
    i.biliMirrorIndex = 0;
    i.biliRetriesLeft = int(i.biliMirrors.size()) > 1 ? int(i.biliMirrors.size()) - 1 : 0;
    i.biliRetryPending = false;
    i.biliVideoUrl = urls.videoUrl;
    // Remember what is playing so the position can be reported when this play ends.
    i.biliWatchBvid = job.bvid.toStdWString();
    i.biliWatchCid = job.cid;
    i.biliWatchLastPos = -1.0;
    i.biliWatchDuration = 0.0;
    // 开播即写一次历史：这样点开就算「看过」，不必等播完 —— B 站网页播放器也是
    // 一开始播放就出现在历史记录里。**不碰**上面刚设好的记录字段，所以 tick 仍会
    // 继续追踪真实位置，播完时照旧按 -1 上报。
    // （真正的调用在 resumeAt 算完之后：续播必须报续播位置，不能报 0，否则会把 B 站
    //   记录下来的进度清掉，用户下次就没法从原处继续了。）
    // Write the history as soon as playback starts, so opening a video counts as watched
    // without waiting for it to finish - the site's own player behaves the same way. The
    // fields above are left untouched, so the tick keeps tracking the real position and the
    // end still reports -1.
    // (The call itself sits after resumeAt is resolved: a resume must report the resumed
    // position, not 0, or it would wipe the progress the site stored and the next resume
    // would start over.)
    // Where the account left this part, so playback resumes there.
    bool startedFromTop = false;
    if (job.plannedResume) {
        i.biliResumeAt = result->resumeAt;
        if (i.biliResumeAt < 0.0) startedFromTop = true;
    } else {
        i.biliResumeAt = -1.0;
        startedFromTop = job.fromStart;
    }
    // 报「实际起播位置」：续播就是那个续播点，从头播放（或本来就没有进度）才是 0。
    // Report where playback actually starts: the resume point when resuming, 0 only when
    // starting from the top (or when there was no saved position at all).
    reportBiliWatchStartedAsync(i.biliWatchBvid, i.biliWatchCid,
                                i.biliResumeAt > 0.0 ? int(i.biliResumeAt) : 0);
    i.biliResumeSession = 0;
    // 这一路打开的是 B 站来源，记下来：播完后「播放」不能再拿过期地址重开。
    // This open is a Bilibili source, so remember it: after the video ends, Play must
    // not reopen the stale address.
    i.biliSourceActive = true;
    // Kept on impl_ as well: openPath() re-applies them after rebuilding options.
    i.pendingHeaders = urls.headers;
    i.pendingUserAgent = urls.userAgent;
    i.pendingAudioPath = urls.audioUrl;
    i.bilibiliOpenPending = true;
    i.options.network.requestHeaders = urls.headers;
    i.options.network.userAgent = urls.userAgent;
    i.options.audioPath = urls.audioUrl;
    // 自动选择倍率：用 DASH 的 frame_rate 在打开前定档，让管线一次就按对的倍率构建。
    planFgAutoMultiplierBeforeOpen(urls.frameRate);
    QString info = QStringLiteral("%1 × %2  %3")
        .arg(urls.width).arg(urls.height).arg(QString::fromStdWString(urls.codec));
    if (urls.audioUrl.empty()) info += QStringLiteral("  ") + tr("无音频");
    const QString fromTop = startedFromTop ? tr("（从头开始）") : QString();
    if (!pageTitle.empty())
        emit notice(tr("正在播放：") + QString::fromStdWString(pageTitle) + QStringLiteral("  ")
                    + info + fromTop, false);
    else
        emit notice(tr("正在播放：") + info + fromTop, false);
    openPath(QString::fromStdWString(urls.videoUrl));
}


void QmlPlayerBridge::applyFgMultiplier(int multiplier) {
    const auto choices=settings().frameGenerationBackend==engine::FrameGenerationBackend::Vfg?
        std::span<const uint32_t>(engine::kVfgMultiplierChoices):std::span<const uint32_t>(engine::kFgMultiplierChoices);
    const bool legal = std::find(choices.begin(),choices.end(),uint32_t(std::max(multiplier,0)))!=choices.end();
    if (!legal || multiplier > fgMaxMultiplier()) {
        emit notice(tr("补帧倍率超出当前后端范围"), true); return;
    }
    auto& chain = impl_->chain;
    auto* node = chain.firstOf(engine::EffectType::FrameGeneration);
    if (!node) { if (multiplier > 1) emit notice(tr("请先添加补帧节点"), true); return; }
    const bool enabled = multiplier > 1;
    if (node->enabled == enabled && (!enabled || chain.fgMultiplier == uint32_t(multiplier))) return;
    const auto beforeMultiplier = chain.fgMultiplier; const auto beforeEnabled = node->enabled;
    node->enabled = enabled; if (enabled) chain.fgMultiplier = uint32_t(multiplier);
    if (!impl_->revalidate(true)) {
        const auto error = impl_->validation.message;
        node->enabled = beforeEnabled; chain.fgMultiplier = beforeMultiplier; impl_->revalidate();
        emit notice(uiText(error), true);
    }
    emit settingsChanged(); emitChainChanged();
}

bool QmlPlayerBridge::fgAutoMultiplierAvailable() const {
    const auto backend = settings().frameGenerationBackend;
    return backend == engine::FrameGenerationBackend::Dlss ||
           backend == engine::FrameGenerationBackend::Vfg;
}
bool QmlPlayerBridge::fgAutoMultiplier() const { return impl_->chain.fgAutoMultiplier; }

bool QmlPlayerBridge::doviFollow()const{return settings().doviFollow;}
void QmlPlayerBridge::setDoviFollow(bool on){
    auto s=settings();if(s.doviFollow==on)return;s.doviFollow=on;
    // The shell preference is the global fallback: it re-applies at the next start
    // even for a source that has no stored per-film profile yet.
    auto& i=*impl_;
    i.prefs[QStringLiteral("doviFollow")]=on;
    if(!i.savePrefs())veyra::log::warn("ui-prefs","dolby vision follow flag not saved");
    commitHdrTuning(std::move(s));
}
bool QmlPlayerBridge::doviP7Compose()const{return settings().doviP7Compose;}
void QmlPlayerBridge::setDoviP7Compose(bool on){
    auto s=settings();if(s.doviP7Compose==on)return;s.doviP7Compose=on;
    // Lives with the shell preference like the Level 1 follow flag, so it survives
    // a restart and applies before the next file is opened.
    auto& i=*impl_;
    i.prefs[QStringLiteral("doviP7Compose")]=on;
    if(!i.savePrefs())veyra::log::warn("ui-prefs","dolby vision p7 compose flag not saved");
    commitHdrTuning(std::move(s));
}
bool QmlPlayerBridge::doviFollowAverage()const{return settings().doviFollowAverage;}
void QmlPlayerBridge::setDoviFollowAverage(bool on){
    auto s=settings();if(s.doviFollowAverage==on)return;s.doviFollowAverage=on;
    commitHdrTuning(std::move(s));}
// Danmaku rides the same per-frame tick as the subtitle overlay: it reads the
// engine's own position, so pause, seek and speed changes need no extra wiring.
void QmlPlayerBridge::tickDanmaku() {
    auto& i = *impl_;
    if (!i.danmakuEnabled || !i.danmakuWindow) return;
    const auto& s = i.snapshot;
    // Only over a moving picture: a still image or a capture has no video timeline.
    if (!s.running || s.image || s.capture) { i.danmakuWindow->hide(); return; }
    if (s.duration <= 0) { i.danmakuWindow->hide(); return; }

    // A jump means a seek: the engine rebuilds its tracks rather than letting old
    // comments fly in from stale positions.
    if (i.danmakuLastSeconds >= 0 &&
        (s.position < i.danmakuLastSeconds - 0.5 || s.position > i.danmakuLastSeconds + 2.0))
        i.danmakuWindow->noteSeek();
    i.danmakuLastSeconds = s.position;

    i.danmakuWindow->follow(impl_->videoWindow);

    media::DanmakuView view;
    view.scale = double(i.prefInt("danmakuScale", 100)) / 100.0;
    view.opacity = double(i.prefInt("danmakuOpacity", 80)) / 100.0;
    view.speedFactor = double(i.prefInt("danmakuSpeed", 100)) / 100.0;
    view.area = double(i.prefInt("danmakuArea", 50)) / 100.0;
    view.videoWidth = int(s.metrics.resolution.output.width);
    view.videoHeight = int(s.metrics.resolution.output.height);
    i.danmakuWindow->render(s.position, view);
}

} // namespace veyra::ui
