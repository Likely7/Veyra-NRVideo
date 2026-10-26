import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Basic as Basic
import QtQuick.Layouts

// Bilibili module: browse, search, open a video, and hand it to the player.
//
// Modelled on newBV's screens (home / popular / dynamics / history / favourites /
// watch later / followed / search), adapted to a desktop mouse-and-keyboard shell:
// the Android TV app navigates with a remote, so its focus-ring-heavy layout is
// replaced by a left rail plus a scrollable content area.
//
// Everything here is read-mostly. The one write path is sign-in (the QR code is
// drawn in-page) plus the per-video like / coin / favourite buttons.
VPage {
    id: root
    pageId: "bili"
    signal requestPage(string page)

    // "home" | "popular" | "follow" | "history" | "fav" | "watchlater" | "search" | "detail"
    property string view: "home"
    property var cards: []
    // The grid iterates this ListModel instead of the array. Appending to a ListModel
    // keeps the scroll position; replacing an array resets it, which is what made the
    // view jump back to the top every time another batch arrived.
    ListModel { id: cardsModel }
    // cards is still the source of truth for the cache and the paging code; this mirrors
    // it into the model incrementally.
    function appendCardsToModel(list, startIndex) {
        for (let i = startIndex; i < list.length; ++i) {
            const c = list[i]
            if (!c) continue
            cardsModel.append({
                bvid: c.bvid || "",
                title: c.title || "",
                cover: c.cover || "",
                owner: c.owner || "",
                duration: c.duration || "",
                play: c.play || 0
            })
        }
    }
    function resetCardsModel(list) {
        cardsModel.clear()
        appendCardsToModel(list, 0)
    }
    // One cached list per rail entry. cards is a single shared array, so without this a
    // switch to another entry overwrote the previous one's cards, and coming back had to
    // fetch again — which made 首页推荐 re-request on every click, the thing the 刷新
    // button exists for. Keys are the view names; "fav:<folderId>" keeps a favourite
    // folder's list separate and "search:<words>" keeps a query's result.
    property var cardCache: ({})
    // True while a list is being fetched. Drives the skeleton grid: the rail click can
    // then paint placeholder tiles immediately and swap them for the real cards when the
    // (blocking) request returns, instead of showing an empty area.
    property bool listLoading: false
    // The account's own favourite folders, and which one is showing. 0 means "the
    // default folder", which is what the API picks when nothing is selected.
    property var favFolders: []
    property var favFolderId: 0
    // The folder chooser the 收藏 button opens. A favourite goes into the folder picked
    // there, not silently into the account's first one.
    property bool favPickerOpen: false
    property var favPickerFolders: []
    property var favPickerTarget: 0
    property string statusText: ""
    property string keyword: ""

    // Detail state
    property var detail: ({})
    property var qualities: []
    property var related: []
    property var comments: []
    property var interaction: ({})
    // 互动状态请求的代号：每发起一次就递增，异步结果回来时用它丢弃过期回复。
    // Generation counter for interaction-state requests: bumped on every request so a
    // late reply can be dropped.
    property int interactionGeneration: 0
    // 扫码轮询是否有一个请求在飞：避免问与答重叠。
    // Whether a QR poll request is in flight, so a request never overlaps its reply.
    property bool qrPolling: false
    // 扫码等待状态（Timer 在布局深处，外层访问不到它的 id，所以放在 root）。
    // Whether a QR code is being waited on. Held on root because the polling Timer
    // lives deep in the layout and this level cannot reach its id.
    property bool qrWaitSession: false
    // 发起一次异步互动状态查询。所有原来直接取返回值的调用点都改成调它。
    // Start one async interaction-state query; replaces every call site that used to
    // read the return value directly.
    function requestInteraction(bvid) {
        if (!bvid) return
        root.interactionGeneration += 1
        veyra.bilibiliRequestInteractionState(bvid)
    }
    property int selectedPart: 0
    property int selectedQuality: 0
    property bool showComments: false
    // Where the account left the selected part, in seconds; -1 when the video is not in
    // the watch history. Drives the play button's label and is re-read on every part
    // change, so the label never promises a resume that bilibiliPlay would not use.
    property real resumeAt: -1
    // Danmaku overlay state; the comments come from the same track the detail API
    // already reports, so enabling it needs no extra request.
    //
    // 弹幕开关只有一个事实来源：偏好项 `biliDanmaku`（持久化）。
    // 详情页按钮和播放设置里的开关都读写它，所以两边永远一致：
    // 在一处打开，另一处就是打开的；关掉一处，另一处也关。重启后保持原样。
    // There is exactly one source of truth for the danmaku switch: the persisted
    // preference `biliDanmaku`. The detail-page button and the switch in 播放设置 both
    // read and write it, so the two can never disagree - turning it on in one place
    // turns it on in the other, and the choice survives a restart.
    readonly property bool danmakuDefault: !!veyra.preferences.biliDanmaku
    // danmakuOn 现在只是 danmakuDefault 的别名，保留这个名字是因为很多调用点已经用它。
    // 它是「用户想看弹幕」的意图，不是「这个视频成功加载了弹幕」。
    // danmakuOn is now an alias of danmakuDefault, kept because many call sites already
    // use the name. It means "the user wants danmaku", not "this video loaded them".
    readonly property bool danmakuOn: danmakuDefault
    property int danmakuLines: 0
    // 播放完毕后是否自动回到视频详情（持久化，默认关）。
    // 默认留在播放画面上：按钮图标会变回「播放」，再点即从头重播。打开这个开关
    // 才回到详情页。
    // Whether to return to the detail page once playback ends; persisted, off by
    // default. Off leaves the player on screen (the glyph turns back into Play and
    // pressing it replays from the start); turning it on returns to the detail page.
    readonly property bool returnAfterEnd: veyra.preferences.biliReturnAfterEnd === undefined
                                           || veyra.preferences.biliReturnAfterEnd === null
                                           ? false
                                           : !!veyra.preferences.biliReturnAfterEnd
    // The module's own settings panel. It lives here rather than only under
    // 设置 → 播放 because that is where someone browsing Bilibili looks for it.
    property bool showSettings: false

    property var account: ({})

    // Sign-in state: "" idle -> "waiting" code shown -> "scanned" -> confirmed.
    property string qrState: ""
    property string qrImage: ""
    property string qrHint: ""

    // Load once, the first time the page is actually shown. Component.onCompleted also
    // fires when the page object is built at startup — Main.qml declares it eagerly —
    // so doing the work there spent three blocking HTTP round-trips (nav + WBI keys +
    // feed) before the first frame of a window the user may never even open here.
    property bool loadedOnce: false
    onCurrentChanged: {
        if (root.current && !loadedOnce) {
            loadedOnce = true
            root.refreshAccount()
            // First load of the page: nothing is cached yet, so this always fetches.
            root.loadFeed(true)
        }
    }

    function refreshAccount() {
        // 异步读账号；结果通过 onBilibiliAccountReady 填。
        // Read the account asynchronously; the reply fills it via onBilibiliAccountReady.
        veyra.bilibiliRequestAccount()
    }

    function startQrLogin() {
        // 异步取二维码；成功后由 onBilibiliQrStarted 继续（显示图、开始轮询）。
        // Request the code asynchronously; onBilibiliQrStarted continues from there.
        root.qrHint = qsTr("正在获取登录二维码…")
        veyra.bilibiliRequestQrStart()
    }

    function cancelQrLogin() {
        root.qrWaitSession = false
        root.qrState = ""
        root.qrImage = ""
        root.qrHint = ""
    }

    // Every list view goes through here so the empty-state message stays uniform.
    //
    // force = false (the rail's default) reuses that entry's cached list instead of
    // fetching it again, so clicking a rail entry is an instant switch; re-fetching is
    // what the 刷新 button at the top right is for. 刷新, the first load of the page and
    // a fresh sign-in pass force = true.
    function load(view_, force) {
        root.showSettings = false
        // 历史记录永远真拉，不吃缓存：进度是边播边上报的，B 站那边随时在变，缓存下来
        // 只会显示过期内容。左侧栏那一项因此每次点击都先显示「加载中…」再填新列表。
        // History always fetches: progress is reported continuously while a video plays,
        // so the site's copy changes at any time and a cached list is simply stale. Each
        // click on the rail entry refreshes the cards.
        if (view_ === "history") force = true
        // A favourite folder keeps its own list, keyed by folder so switching folders does
        // not reuse another folder's cards.
        const key = view_ === "fav" ? ("fav:" + root.favFolderId) : view_
        if (!force && Object.prototype.hasOwnProperty.call(root.cardCache, key)) {
            root.view = view_
            root.cards = root.cardCache[key].slice()
            root.resetCardsModel(root.cards)
            root.listLoading = false
            root.statusText = ""
            // Restore the paging cursor: the cached list may already hold several pages.
            root.activeListKey = key
            root.activeListPage = (root.cardCache[key + ":page"] || 1) + 1
            root.loadingMore = false
            root.lastRequestedPage = 0
            root.listExhausted = false
            return
        }
        root.view = view_
        root.statusText = qsTr("加载中…")
        root.listLoading = true
        if (view_ === "fav") {
            // 收藏夹列表也走异步：它同样是一次网络往返，同步等会把窗口冻住。
            // 结果通过 onBilibiliFavFoldersReady 回来，reason "page" 表示是这一处要的。
            // The folder list is asynchronous too: it is a network round trip, and
            // waiting for it froze the window. The result arrives through
            // onBilibiliFavFoldersReady; reason "page" marks this caller.
            veyra.bilibiliRequestFavFolders("page")
        }
        // Nothing blocks here: the request runs on a worker and arrives through
        // onBilibiliListReady, so the window keeps painting (and the skeleton shows).
        root.pendingListKey = key
        root.pendingListGeneration = veyra.bilibiliLoadList(key)
        // First page of a freshly selected entry: reset the paging state so loadMore
        // starts from page 2.
        root.activeListKey = key
        root.activeListPage = 2
        root.loadingMore = false
        root.lastRequestedPage = 0
        root.listExhausted = false
    }

    // One result slot for the asynchronous fetch. The generation check drops a reply that
    // a newer click has already superseded, so a slow answer cannot overwrite a fast one.
    property string pendingListKey: ""
    property int pendingListGeneration: 0

    property int pendingQualitiesGeneration: 0

    // ---- endless scrolling ----
    // The list currently on screen, so "load more" knows which one to extend. The page
    // counter is per key: only popular/search/favourites have a page parameter, while
    // 推荐/历史/稍后再看 are simply requested again (the site has no page for those).
    property string activeListKey: ""
    property int activeListPage: 1
    property bool loadingMore: false
    property int loadMoreGeneration: 0
    // Stops the same page being appended twice when scroll and layout both fire in one
    // turn.
    property int lastRequestedPage: 0
    // Set when a batch came back empty or with nothing new. Without it a short list that
    // does not fill the viewport kept asking again (the new "content shorter than the
    // viewport" trigger), and an empty reply would restart that loop on every tick.
    property bool listExhausted: false

    function listFetchKey(base, page) {
        const paged = base === "popular" || base.startsWith("search:") || base.startsWith("fav:")
        return paged ? (base + ":" + page) : base
    }

    function loadMore() {
        if (root.loadingMore || root.listLoading || root.listExhausted) return
        if (!root.activeListKey.length || root.cards.length === 0) return
        if (root.lastRequestedPage === root.activeListPage) return
        root.loadingMore = true
        root.lastRequestedPage = root.activeListPage
        root.loadMoreGeneration = veyra.bilibiliLoadList(
            root.listFetchKey(root.activeListKey, root.activeListPage))
    }

    Connections {
        target: veyra
        function onBilibiliListReady(kind, items, generation) {
            // One signal carries several kinds now; dispatch on the prefix so a card list,
            // a quality list and a related list cannot be mistaken for one another.
            if (kind.startsWith("qualities:")) {
                if (generation !== root.pendingQualitiesGeneration) return
                root.qualities = items
                root.statusText = items.length ? "" : qsTr("无法获取画质列表，可能未登录")
                return
            }
            if (kind.startsWith("related:")) {
                if (generation !== root.pendingRelatedGeneration) return
                root.related = items
                return
            }
            // Append or replace? A reply to loadMore carries the loadMoreGeneration and
            // must be added to the end instead of replacing what is already on screen.
            if (root.loadingMore && generation === root.loadMoreGeneration) {
                root.loadingMore = false
                if (!items.length) {
                    // Nothing more to give: stop asking until the list is switched.
                    root.lastRequestedPage = root.activeListPage
                    root.listExhausted = true
                    return
                }
                // Deduplicate by bvid, because the endpoints without a page parameter
                // return fresh batches that can overlap what is already listed.
                const seen = ({})
                for (let i = 0; i < root.cards.length; ++i)
                    if (root.cards[i] && root.cards[i].bvid) seen[root.cards[i].bvid] = true
                const merged = root.cards.slice()
                for (let i = 0; i < items.length; ++i) {
                    const it = items[i]
                    if (!it || !it.bvid || seen[it.bvid]) continue
                    seen[it.bvid] = true
                    merged.push(it)
                }
                if (merged.length === root.cards.length) {
                    // A batch with nothing new (the feed repeated itself): do not keep
                    // asking for the same page.
                    root.lastRequestedPage = root.activeListPage
                    root.listExhausted = true
                    return
                }
                // Only the new tail is appended, so GridView keeps its scroll offset.
                // Assigning the merged array instead rebuilt the whole model and snapped
                // the view back to the top on every batch.
                root.appendCardsToModel(merged, root.cards.length)
                root.cards = merged
                // Advance the cursor only after a batch actually added something. The
                // endpoints with a page parameter take the next page; the others are
                // called again with the same key (the page suffix is ignored for them).
                const paged = root.activeListKey === "popular"
                              || root.activeListKey.startsWith("search:")
                              || root.activeListKey.startsWith("fav:")
                if (paged) root.activeListPage = root.activeListPage + 1
                root.lastRequestedPage = 0
                root.listExhausted = false
                const cache = root.cardCache
                cache[root.activeListKey] = merged.slice()
                cache[root.activeListKey + ":page"] = root.activeListPage - 1
                root.cardCache = cache
                return
            }
            if (generation !== root.pendingListGeneration) return
            // A paged reply may carry the page suffix, so compare the base key only.
            const replyBase = kind.replace(/:[0-9]+$/, "")
            if (replyBase !== root.pendingListKey && kind !== root.pendingListKey) return
            root.listLoading = false
            const result = items
            // A copy, so GridView sees a new array every time.
            root.cards = result.slice()
            root.resetCardsModel(root.cards)
            const cache = root.cardCache
            cache[root.pendingListKey] = result.slice()
            cache[root.pendingListKey + ":page"] = 1
            root.cardCache = cache
            root.statusText = result.length ? "" : (veyra.bilibiliLastError() || qsTr("没有内容"))
        }
    }

    property int pendingRelatedGeneration: 0

    // Switches to one favourite folder. Each folder keeps its own cached list, so going
    // back and forth does not refetch; force reloads the folder that is on screen.
    function openFavFolder(mediaId, force) {
        root.showSettings = false
        root.favFolderId = mediaId
        root.view = "fav"
        const key = "fav:" + mediaId
        if (!force && Object.prototype.hasOwnProperty.call(root.cardCache, key)) {
            root.cards = root.cardCache[key].slice()
            root.resetCardsModel(root.cards)
            root.activeListKey = key
            root.activeListPage = (root.cardCache[key + ":page"] || 1) + 1
            root.loadingMore = false
            root.lastRequestedPage = 0
            root.listExhausted = false
            root.statusText = ""
            return
        }
        root.statusText = qsTr("加载中…")
        root.listLoading = true
        // Same asynchronous path as the rail entries; the reply lands on the shared slot.
        root.pendingListKey = key
        root.pendingListGeneration = veyra.bilibiliLoadList(key)
        root.activeListKey = key
        root.activeListPage = 2
        root.loadingMore = false
        root.lastRequestedPage = 0
        root.listExhausted = false
    }

    function loadFeed(force) { root.load("home", force === true) }

    // force re-runs the same query; without it a repeated search of the same words reuses
    // the cached result, keyed by the query itself so different words still search.
    // 搜索框回车的统一入口。四条通道都会走到这里，用一个 250ms 的闸门防止同一次
    // 按键被两条通道各触发一次。
    // One entry point for the search box's Enter key. All four channels call it; a 250 ms
    // gate keeps a single key press from starting two searches.
    // 用时间戳做闸门，不再依赖 Timer 的 id。上一版写成 root.searchKeyResetTimer.restart()，
    // 而 searchKeyResetTimer 只是一个 id、不是 root 的属性，取值是 undefined，调用即抛
    // TypeError —— 函数在打日志之前就中断了，闸门还停在 true，于是之后每次按键都被挡掉。
    // 现场日志：回车事件确实到达了（key=16777220 五次），却一条触发记录都没有。
    // The gate is a timestamp now instead of a Timer id. The previous version called
    // root.searchKeyResetTimer.restart(), but searchKeyResetTimer is only an id, not a
    // property of root - the value is undefined and calling it throws immediately, before
    // the log line, leaving the gate stuck at true so every later key was dropped. The
    // field log shows the key did arrive (key=16777220, five times) with no trigger record.
    property double searchKeyLastMs: 0
    function triggerSearchFromField() {
        const now = Date.now()
        if (now - root.searchKeyLastMs < 250) return
        root.searchKeyLastMs = now
        root.doSearch(true)
    }

    function doSearch(force) {
        if (!root.keyword.trim().length) return
        root.showSettings = false
        root.view = "search"
        const key = "search:" + root.keyword.trim()
        if (!force && Object.prototype.hasOwnProperty.call(root.cardCache, key)) {
            root.cards = root.cardCache[key].slice()
            root.resetCardsModel(root.cards)
            root.activeListKey = key
            root.activeListPage = (root.cardCache[key + ":page"] || 1) + 1
            root.loadingMore = false
            root.lastRequestedPage = 0
            root.listExhausted = false
            root.statusText = ""
            return
        }
        root.statusText = qsTr("搜索中…")
        root.listLoading = true
        // Asynchronous like the rail entries. The key carries the words, so the reply
        // handler caches under the same slot the synchronous path used.
        root.pendingListKey = key
        root.pendingListGeneration = veyra.bilibiliLoadList(key)
    }

    function openVideo(bvid) {
        root.showSettings = false
        // 换视频就是换轨道：丢掉上一部的弹幕数据，但**不改开关** ——
        // 开关代表用户意图，只有用户自己能改。
        // A different video means a different track: drop the previous items, but leave
        // the switch alone - it represents the user's intent, which only they change.
        if (root.danmakuOn) veyra.bilibiliDisableDanmaku()
        root.danmakuLines = 0
        // Switch first, fetch after: the detail request is a blocking round trip, and it
        // used to run before root.view was set, so the click froze the window instead of
        // showing the page. Now the page appears immediately and fills in when the reply
        // arrives through onBilibiliObjectReady.
        root.view = "detail"
        root.detailLoading = true
        root.detail = ({})
        root.related = []
        root.comments = []
        root.showComments = false
        root.pendingDetailBvid = bvid
        root.pendingDetailGeneration = veyra.bilibiliLoadList("detail:" + bvid)
    }

    // Fills the detail page once its object arrives. Separate slot from the card lists so
    // a slow list fetch cannot overwrite the detail and vice versa.
    property string pendingDetailBvid: ""
    property int pendingDetailGeneration: 0
    property bool detailLoading: false

    Connections {
        target: veyra
        function onBilibiliObjectReady(kind, object, generation) {
            if (generation !== root.pendingDetailGeneration) return
            if (kind !== "detail:" + root.pendingDetailBvid) return
            root.detailLoading = false
            root.detail = object
            if (!object.ok) {
                root.statusText = object.error || qsTr("无法打开该视频")
                return
            }
            root.finishOpenDetail(root.pendingDetailBvid)
        }
        // 互动状态（点赞/投币/收藏）异步回来。只接受当前正在看的那个视频的结果：
        // 用户可能已经翻到别的视频了，迟到的回复必须丢弃，否则按钮状态会对错。
        // Interaction state arrives asynchronously. Only the video currently on screen
        // is accepted: the user may have moved on, and applying a late reply would put
        // another video's liked/coined state on these buttons.
        function onBilibiliInteractionReady(bvid, state, generation) {
            if (generation !== root.interactionGeneration) return
            if (!root.detail || root.detail.bvid !== bvid) return
            root.interaction = state
        }
        // 收藏夹列表异步回来。按 reason 分派到两处：页面顶部的收藏夹视图、互动按钮旁的
        // 选择器。两处共用同一次请求的结果，谁要谁取。
        // The favourite-folder list arrives asynchronously. `reason` routes it to one of
        // the two consumers: the 收藏 page view, or the picker beside the interaction
        // buttons.
        function onBilibiliFavFoldersReady(folders, reason) {
            if (reason === "picker") root.favPickerFolders = folders
            else root.favFolders = folders
        }
        // 收藏动作异步回来。成功才刷新按钮状态；从选择器发起的还要关掉浮层。
        // 失败时状态保持不动 —— 服务端拒绝了，按钮不该假装成功。
        // The favourite action reports back asynchronously. Re-read the button state only
        // on success, and close the picker for the "picker" caller. On failure the state
        // stays put: the server refused, so the button must not pretend otherwise.
        function onBilibiliFavoriteReady(ok, error, add, reason) {
            if (!ok) return
            if (reason === "picker") root.favPickerOpen = false
            if (root.detail && root.detail.bvid) root.requestInteraction(root.detail.bvid)
        }
        // 点赞 / 投币 / 稍后再看 完成后刷新按钮状态。三者形状相同，一个处理器覆盖。
        // Liked / coined / watch-later all finish the same way: re-read the buttons.
        function onBilibiliActionDone(op, ok, error) {
            // 发评论的结果要单独说出来：失败时 B 站给的原因（尤其是风控 -401）
            // 必须让用户看见，否则点了没反应更像坏了。
            // The reply result is reported explicitly: on failure the reason from Bilibili
            // (the -401 risk check in particular) has to be visible, or a silent no-op looks
            // like a bug.
            if (op === "delete-reply") {
                if (!ok) { root.replyMessage = error; root.pendingDeleteRpid = 0; return }
                root.replyMessage = qsTr("评论已删除")
                // 先从本地去掉这一条：服务端列表可能有延迟，等刷新会让人以为没删掉。
                // 然后再拉一次做最终对齐（别人可能刚好也发了评论）。
                // Drop it locally first: the server list can lag, and waiting for the refresh
                // makes the delete look like it did nothing. Then fetch once for the final
                // state, since someone else may have posted meanwhile.
                const gone = root.pendingDeleteRpid
                root.comments = root.comments.filter(c => c.rpid !== gone)
                root.pendingDeleteRpid = 0
                if (root.detail && root.detail.bvid)
                    veyra.bilibiliRequestComments(root.detail.bvid, 1)
                return
            }
            if (op === "reply") {
                root.replySending = false
                if (!ok) { root.replyMessage = error; return }
                root.replyMessage = qsTr("评论已发送")
                root.replyText = ""
                if (root.detail && root.detail.bvid)
                    veyra.bilibiliRequestComments(root.detail.bvid, 1)
                return
            }
            if (!ok) return
            if (root.detail && root.detail.bvid) root.requestInteraction(root.detail.bvid)
        }
        // 评论列表到达。
        function onBilibiliCommentsReady(comments, ok, error) {
            if (!ok) return
            root.comments = comments
        }
        // 弹幕轨道加载结果：只更新**条数**，不碰开关。
        // 开关的状态来自持久化偏好，加载成功与否都不会改变用户的选择。
        // Track load result: only the line count is updated; the switch is never touched.
        // Its state comes from the persisted preference, so whether a load succeeds or
        // fails cannot change what the user chose.
        function onBilibiliDanmakuReady(ok, error, lineCount) {
            if (ok) {
                root.danmakuLines = lineCount
            } else {
                root.danmakuLines = 0
                root.statusText = error || qsTr("这个视频没有弹幕")
            }
        }
        // 续播位置到达。只有在详情页还停在这个视频上时才采用。
        function onBilibiliResumeReady(seconds) {
            root.resumeAt = seconds
        }
        // 二维码地址到达：渲染图片、进入等待、开始轮询。
        function onBilibiliQrStarted(qrUrl, ok, error) {
            if (!ok || !qrUrl) {
                root.qrState = ""
                root.qrHint = qsTr("无法获取登录二维码")
                return
            }
            root.qrImage = veyra.bilibiliQrImage(360)
            root.qrState = "waiting"
            root.qrHint = qsTr("请用 B 站手机客户端扫码")
            root.qrWaitSession = true
        }
        // 扫码轮询结果。
        function onBilibiliQrPolled(state) {
            root.qrPolling = false
            if (state === "confirmed") {
                root.qrWaitSession = false
                root.qrState = ""
                root.qrImage = ""
                root.qrHint = ""
                root.refreshAccount()
                // 登录后可见范围变大，缓存的列表全部作废。
                root.cardCache = ({})
                root.load("home", true)
            } else if (state === "scanned") {
                root.qrState = "scanned"
                root.qrHint = qsTr("已扫码，请在手机上确认")
            } else if (state === "expired") {
                root.qrWaitSession = false
                root.qrState = ""
                root.qrImage = ""
                root.qrHint = qsTr("二维码已过期，请重新点击扫码登录")
            } else if (state === "failed" || state === "none") {
                root.qrWaitSession = false
                root.qrState = ""
                root.qrImage = ""
                root.qrHint = qsTr("登录失败，请重试")
            }
            // waiting / scanned 继续轮询，什么都不用做。
        }
        // 账号信息到达。
        function onBilibiliAccountReady(account, ok) {
            if (ok) root.account = account
        }
    }

    // Everything that depends on the detail response: qualities and the resume point need
    // the first part's cid, which only the detail carries.
    function finishOpenDetail(bvid) {
        root.selectedPart = 0
        root.selectedQuality = 0
        const firstCid = (root.detail.parts && root.detail.parts.length)
            ? root.detail.parts[0].cid : 0
        // 续播位置异步读，画质列表异步拉，两者互不等待。
        // 画质已经把 cid 传过去了，不再重复请求详情。
        // The resume point and the quality list are both fetched asynchronously and
        // neither waits for the other; qualities gets the cid so it does not re-request
        // the detail page.
        if (firstCid) veyra.bilibiliRequestResumeAt(bvid, firstCid)
        else root.resumeAt = -1
        root.qualities = []
        if (firstCid) {
            root.pendingQualitiesGeneration = veyra.bilibiliLoadList(
                "qualities:" + bvid + ":" + firstCid)
        }
        root.deferredBvid = bvid
        detailFillTimer.restart()
    }

    // Filled in one turn after the detail page is already on screen. Each of these is a
    // blocking request, so they are shown late rather than making the user wait.
    property string deferredBvid: ""
    Timer {
        id: detailFillTimer
        interval: 0
        repeat: false
        onTriggered: {
            const bvid = root.deferredBvid
            if (!bvid.length || root.view !== "detail") return
            root.requestInteraction(bvid)
            if (root.view !== "detail") return
            root.related = []
            root.pendingRelatedGeneration = veyra.bilibiliLoadList("related:" + bvid)
            if (root.view !== "detail") return
            if (root.danmakuDefault) root.loadDanmaku()
        }
    }

    // 拉取当前视频的弹幕轨道。**只取数据，不改偏好** —— 播放、换分P 等自动流程
    // 都调它，这些流程不该动用户的开关。
    // Fetch the track for the current video. Data only: the automatic paths (playback,
    // part switch) call this and they must not touch the user's switch.
    function loadDanmaku() {
        // 只看 bvid 是否已知，不等详情页数据：详情是异步到的，弹幕请求本身也只需要
        // bvid 和分P。原来这里检查 root.detail.ok，详情还没回来时请求就被吞掉，
        // 表现为「点播放没有弹幕」。
        // Gate on the bvid alone, not the detail payload: the detail arrives
        // asynchronously while the danmaku request needs only the bvid and part index.
        // The old root.detail.ok check swallowed the request whenever the detail had not
        // landed yet, which showed up as "playing a video has no danmaku".
        const bvid = (root.detail && root.detail.bvid) ? root.detail.bvid : root.deferredBvid
        if (!bvid) return
        veyra.bilibiliRequestEnableDanmaku(bvid, root.selectedPart)
    }

    // 用户主动打开弹幕：设偏好 + 拉数据。详情页按钮和播放设置开关都走这里。
    // The user turning danmaku on: persist the choice and fetch the track. Both the
    // detail-page button and the 播放设置 switch go through this.
    function enableDanmaku() {
        veyra.setPreference("biliDanmaku", true)
        root.loadDanmaku()
    }

    // 关弹幕：清掉当前数据 + 把偏好设成关。两处界面都读这个偏好，所以一起变。
    // Turning danmaku off: clear the current items and persist the choice. Both switches
    // read that preference, so they change together.
    function unloadDanmaku() {
        veyra.bilibiliDisableDanmaku()
        root.danmakuLines = 0
        veyra.setPreference("biliDanmaku", false)
    }

    function formatSeconds(total) {
        const s = Math.max(0, Math.round(total))
        const m = Math.floor(s / 60)
        const rest = s % 60
        return m + ":" + (rest < 10 ? "0" : "") + rest
    }

    // Set while a Bilibili video is playing, so the end can be noticed and the user put
    // back on the page they started from. openPath moves to 极简 for playback; this is
    // the way back, driven from the position the bridge already publishes.
    property bool biliPlaying: false
    property real lastSeenPosition: 0

    function playSelected(fromStart) {
        if (!root.detail.ok) return
        // 不同分P是不同的弹幕轨道：丢掉旧的叠加层数据，否则旧弹幕会画在新画面上。
        // 但**不要**把开关本身设为关：马上就会按新分P重新加载，先把开关灭掉只会
        // 让按钮文字闪一下「弹幕关」，看起来像功能被关掉了。
        // A different part is a different track, so the old items are dropped - but the
        // switch itself stays as the user left it. It is reloaded for the new part
        // immediately below, and flipping it off first only makes the label flash
        // "弹幕关", which reads like the feature was turned off.
        const reloadDanmaku = root.danmakuOn || root.danmakuDefault
        if (root.danmakuOn) veyra.bilibiliDisableDanmaku()
        root.biliPlaying = true
        root.lastSeenPosition = 0
        // The cid comes from the detail response, so the play call no longer asks the site
        // for the page again: that lookup sat between the click and the first frame.
        const parts = root.detail.parts || []
        const cid = (root.selectedPart >= 0 && root.selectedPart < parts.length)
            ? parts[root.selectedPart].cid : 0
        if (cid) {
            veyra.bilibiliPlayCid(root.detail.bvid, cid, root.selectedQuality,
                                  fromStart === true)
        } else {
            veyra.bilibiliPlay(root.detail.bvid, root.selectedPart, root.selectedQuality,
                               fromStart === true)
        }
        // 按新分P重新拉弹幕。叠加层属于播放窗口，openPath 已经在切换来源，
        // 所以这里只发请求；加载成功由 onBilibiliDanmakuReady 点亮开关。
        // Reload the track for the selected part. The overlay belongs to the playback
        // window and openPath is switching sources underneath, so this only starts the
        // request; success lights the switch through onBilibiliDanmakuReady.
        if (reloadDanmaku) root.loadDanmaku()
    }

    // Playback reaching its end: position stops moving near the duration. Polled rather
    // than event-driven because the bridge has no "ended" signal QML can bind to, and a
    // 500 ms poll is far cheaper than the alternatives. Cleared as soon as it fires so a
    // single end returns the user exactly once.
    Timer {
        interval: 500
        repeat: true
        running: root.biliPlaying
        onTriggered: {
            const pos = veyra.position
            const dur = veyra.duration
            if (dur <= 0) return
            if (pos > 0.1) root.lastSeenPosition = pos
            // Near the end, or the position went backwards to zero after having played
            // (a restart) - either way playback is no longer mid-video.
            if (root.lastSeenPosition > 1 && pos >= dur - 0.4) {
                root.biliPlaying = false
                // 回到视频开始的那个页面。引擎保留会话；画面不再显示，因为 "bili"
                // 是浏览页，它的视频区域不是播放面。
                // 用户关掉「播放完成后回到视频详情」时留在播放画面：按钮图标会变回
                // 「播放」，再点即从头重播。
                // Back to the page the video was started from. The engine keeps its
                // session; the picture stops being shown because "bili" is a browsing
                // page and its video rect is not a playing surface.
                // With "return after end" turned off, stay on the player instead: the
                // button glyph turns back into Play, and pressing it replays from the
                // start.
                if (root.returnAfterEnd) {
                    root.requestPage("bili")
                    // 如果这次是从「历史记录」点进去看的，返回时人还站在那一页上，
                    // 不会有人再去点左边栏，所以这里主动重拉一次，否则看到的还是
                    // 播放前那张列表。
                    // If the video was opened from 历史记录 the user lands back on that
                    // same view without touching the rail, so refresh it here; otherwise
                    // the stale pre-playback list is what they see.
                    if (root.view === "history") root.load("history", true)
                }
            }
        }
    }

    // 发评论的界面状态。
    // UI state for posting a comment.
    property string replyText: ""
    property bool replySending: false
    property string replyMessage: ""
    // 正在删除的评论 rpid：成功后先从本地列表移除，界面立刻有反应，
    // 再向服务端要一次最新列表做最终对齐。
    // The rpid being deleted: on success it is removed from the local list so the change is
    // immediate, then the server list is fetched again for the final state.
    property real pendingDeleteRpid: 0

    function toggleComments() {
        root.showComments = !root.showComments
        // 展开时异步拉评论，收起不等；结果通过 onBilibiliCommentsReady 填。
        // Fetch asynchronously while expanding; collapsing needs no data. The list
        // arrives through onBilibiliCommentsReady.
        if (root.showComments && root.comments.length === 0)
            veyra.bilibiliRequestComments(root.detail.bvid, 1)
    }

    // One tappable rail entry.
    component NavItem: Rectangle {
        id: navItem
        property string label
        property string target
        signal activated()

        // While the settings panel is open it owns the content area, so the rail
        // highlights it rather than whichever list was showing before.
        readonly property bool isActive:
            root.showSettings ? target === "settings" : root.view === target

        Layout.fillWidth: true
        // 八项平分侧栏的竖向空间（到账号块上方为止），所以每项都设 fillHeight，
        // 不再有弹簧占位。窗口变高时卡片一起变高，文字随之变大。
        // The eight entries share the rail's vertical space down to the account block,
        // which is why each one fills instead of a spacer absorbing the remainder. A
        // taller window gives taller cards and the type grows with them.
        Layout.fillHeight: true
        Layout.minimumHeight: 26
        // 卡片本身：默认比栏底稍深，悬停再亮一档，选中用强调色的柔和版。
        // The card: slightly darker than the rail by default, one step brighter on hover,
        // and the soft accent for the selected one.
        radius: 8
        color: isActive ? Theme.accentSoft
                        : (navMouse.containsMouse ? Theme.card3 : Theme.card2)
        border.width: 1
        border.color: isActive ? Theme.accent : "transparent"

        // 选中时左侧还有一条竖线，比只靠底色更容易一眼看出位置。
        // A short bar on the left for the selected entry: easier to spot at a glance
        // than a background tint alone.
        Rectangle {
            anchors.left: parent.left
            anchors.leftMargin: 3
            anchors.verticalCenter: parent.verticalCenter
            width: 3
            height: Math.min(parent.height - 14, 24)
            radius: 1.5
            color: Theme.accent
            visible: navItem.isActive
        }

        Text {
            anchors.centerIn: parent
            width: parent.width - 10
            horizontalAlignment: Text.AlignHCenter
            text: navItem.label
            color: navItem.isActive ? Theme.accent : Theme.t2
            font.family: Theme.fontUi
            // 字号跟着卡片高度走，并夹住上下限：窗口很矮也不会小到看不清，
            // 很高时也不会大到放不进 100px 宽的侧栏。
            // Type follows the card height, clamped at both ends: a short window stays
            // readable and a tall one cannot overflow the 100 px rail.
            font.pixelSize: Math.max(11, Math.min(17, Math.round(navItem.height * 0.30)))
            font.weight: navItem.isActive ? Font.DemiBold : Font.Normal
        }

        MouseArea {
            id: navMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: navItem.activated()
        }
    }

    RowLayout {
        anchors.fill: parent
        // 10 rather than 14: with the rail at 100 this is what lets the grid fit five
        // columns in the default 1280 window (1280 - 20 - 100 - 10 = 1150).
        anchors.margins: 10
        spacing: 14

        // ---- left rail ----
        Rectangle {
            Layout.preferredWidth: 100
            Layout.fillHeight: true
            radius: 12
            color: Theme.card2
            border.width: 1
            border.color: Theme.stroke

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 8
                // 项之间的间隔略大一点，卡片之间才有呼吸感；八项各自 fillHeight，
                // 所以这个间距是唯一固定的竖向占用。
                // A slightly wider gutter so the cards breathe; the eight entries fill the
                // height themselves, so this spacing is the only fixed vertical cost.
                spacing: 6

                NavItem { label: qsTr("首页推荐"); target: "home"; onActivated: root.load("home") }
                NavItem { label: qsTr("热门"); target: "popular"; onActivated: root.load("popular") }
                NavItem { label: qsTr("关注动态"); target: "follow"; onActivated: root.load("follow") }
                NavItem { label: qsTr("历史记录"); target: "history"; onActivated: root.load("history", true) }
                NavItem { label: qsTr("我的收藏"); target: "fav"; onActivated: root.load("fav") }
                NavItem { label: qsTr("稍后再看"); target: "watchlater"; onActivated: root.load("watchlater") }
                NavItem { label: qsTr("搜索结果"); target: "search"; onActivated: {} }
                NavItem { label: qsTr("设置"); target: "settings"; onActivated: root.showSettings = true }

                // account block
                Rectangle {
                    Layout.fillWidth: true
                    height: accountColumn.implicitHeight + 16
                    radius: 8
                    color: Theme.card3

                    ColumnLayout {
                        id: accountColumn
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 6

                        Text {
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                            text: root.account.loggedIn
                                  ? root.account.name + (root.account.vip ? qsTr("（大会员）") : "")
                                  : qsTr("未登录")
                            color: root.account.loggedIn ? Theme.ok : Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                        }
                        Text {
                            Layout.fillWidth: true
                            visible: !root.account.loggedIn
                            text: qsTr("仅能看 480P")
                            color: Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: 10
                        }
                        VButton {
                            Layout.fillWidth: true
                            implicitWidth: 0
                            // 侧栏只有 100px，减去内边距后按钮内宽约 84px。「扫码登录」
                            // 用 fsBody 放不下，会被中间省略成「扫…录」。这里用小一号
                            // 字号，其它按钮不受影响。
                            // The rail is 100px, so the button has about 84px of usable
                            // width. "扫码登录" does not fit at fsBody and would elide to
                            // "扫…录", so this instance uses one size smaller.
                            fontSize: 11
                            text: root.account.loggedIn ? qsTr("退出")
                                  : (root.qrState.length ? qsTr("取消登录") : qsTr("扫码登录"))
                            onClicked: {
                                if (root.account.loggedIn) {
                                    veyra.bilibiliSignOut()
                                    root.refreshAccount()
                                    // Signed out: everything cached came from the old
                                    // account (favourites, history, feed), so drop it.
                                    root.cardCache = ({})
                                } else if (root.qrState.length) {
                                    root.cancelQrLogin()
                                } else {
                                    root.startQrLogin()
                                }
                            }
                        }
                    }
                }
            }
        }

        // ---- content ----
        ColumnLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 10

            // search bar
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 34
                    radius: 8
                    color: Theme.card2
                    border.width: 1
                    border.color: searchField.activeFocus ? Theme.accent : Theme.stroke

                    TextInput {
                        id: searchField
                        // C++ 的 eventFilter 靠这个名字认出这是搜索框（TextInput 会消费
                        // press 事件，QML 侧收不到回车）。
                        // The C++ event filter recognises this box by name: TextInput consumes
                        // the key press, so QML never sees Return here.
                        objectName: "bili-search-field"
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        anchors.rightMargin: 12
                        verticalAlignment: TextInput.AlignVCenter
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: 13
                        selectByMouse: true
                        onTextChanged: root.keyword = text
                        // 回车触发搜索。可靠通道是**释放**事件。
                        // 2026-10-26 实测：回车的按下（16777220）到不了控件 —— TextInput
                        // 为自己的编辑把它吞了，连 accepted() 也不发；释放每次都到（29/29），
                        // 退格键的按下也能到（16777219），说明按键投递没问题，被吞的只有
                        // 回车这一个键。应用级 eventFilter 也试过，一次都看不到回车
                        // （Qt Quick 直接调 QQuickItem::keyPressEvent，不走 sendEvent）。
                        // Enter triggers the search. The release is the reliable channel.
                        // Measured 2026-10-26: Return's press (16777220) never reaches the
                        // control - TextInput swallows it for its own editing and does not
                        // emit accepted() either - while the release arrived 29 of 29 times,
                        // and Backspace's press arrived too (16777219). Key delivery is fine;
                        // only Return's press is consumed. An application-level event filter
                        // was tried as well and never saw Return: Qt Quick calls
                        // QQuickItem::keyPressEvent directly rather than through sendEvent.
                        Keys.onReleased: function(event) {
                            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                root.triggerSearchFromField()
                                event.accepted = true
                            }
                        }
                        // 另两条留作兜底：换 Qt 版本后哪个先工作都行，250ms 闸门保证
                        // 一次按键只搜一次。
                        // The other two stay as fallbacks so the box keeps working whichever
                        // one Qt honours; the 250 ms gate keeps one key press to one search.
                        onAccepted: root.triggerSearchFromField()
                        Keys.onPressed: function(event) {
                            if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                                root.triggerSearchFromField()
                                event.accepted = true
                            }
                        }
                    }
                    // TextInput has no placeholder property; this is the usual stand-in.
                    Text {
                        anchors.fill: parent
                        anchors.leftMargin: 12
                        verticalAlignment: Text.AlignVCenter
                        visible: searchField.text.length === 0
                        text: qsTr("搜索 B 站视频")
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 13
                    }
                }

                // The button always runs the query: pressing it is an explicit request
                // for that search, even when the same words were searched before.
                VButton { text: qsTr("搜索"); onClicked: root.doSearch(true) }
                VButton {
                    text: qsTr("刷新")
                    onClicked: {
                        // 刷新 always refetches; the rail entries reuse their cached list.
                        if (root.view === "detail") root.openVideo(root.detail.bvid)
                        else if (root.view === "fav") root.openFavFolder(root.favFolderId, true)
                        else if (root.view === "search") root.doSearch(true)
                        else root.load(root.view === "search" ? "home" : root.view, true)
                    }
                }
            }

            Timer {
                id: qrPoll
                interval: 1000
                repeat: true
                // Polling was a blocking HTTPS round-trip once a second. It is now a
                // request that returns immediately; the state arrives through
                // onBilibiliQrPolled. "session" is the wait state, and the timer only
                // fires while the page is on screen, so leaving for 极简/专业 no longer
                // freezes the UI once a second for the code's whole ~180 s lifetime.
                // The code itself is untouched, so coming back resumes the same session.
                // session 状态提到 root 层（qrWaitSession）：Timer 在页面布局深处，
                // 外层 Connections 访问不到它的 id，所以在回调里改 qrPoll.session 会报错。
                // The session flag lives on root as qrWaitSession: this Timer sits deep in
                // the layout and the outer Connections block cannot reach its id, so
                // touching qrPoll.session from a handler would fail.
                running: root.qrWaitSession && root.current
                onTriggered: {
                    // 一问一答之间不重叠：上一次还没回来就跳过这一拍。
                    // Never overlap request and reply: skip a beat while one is in flight.
                    if (root.qrPolling) return
                    root.qrPolling = true
                    veyra.bilibiliRequestQrPoll()
                }
            }

            // ---- favourite folder picker ----
            // Only on the favourites view: the account can keep several folders, and
            // showing just the default one hid the user's own categories entirely.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8
                visible: root.view === "fav" && root.favFolders.length > 0

                Repeater {
                    model: root.favFolders

                    Rectangle {
                        // Comparing as strings: the id crosses from C++ as qlonglong
                        // and QML compares that strictly against a JS number.
                        readonly property bool active: String(root.favFolderId) === String(modelData.id)
                        Layout.preferredHeight: 28
                        // The label is laid out by the rectangle, not the other way
                        // round: sizing the rectangle from an anchored child's
                        // implicitWidth is a binding loop.
                        Layout.preferredWidth: folderLabel.implicitWidth + 24
                        radius: 14
                        color: active ? Theme.accentSoft : Theme.card2
                        border.width: 1
                        border.color: active ? Theme.accent : Theme.stroke

                        Text {
                            id: folderLabel
                            anchors.centerIn: parent
                            text: modelData.title + " (" + modelData.count + ")"
                            color: active ? Theme.accent : Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                        }

                        MouseArea {
                            anchors.fill: parent
                            cursorShape: Qt.PointingHandCursor
                            onClicked: root.openFavFolder(modelData.id)
                        }
                    }
                }
                Item { Layout.fillWidth: true }
            }

            // status line
            Text {
                Layout.fillWidth: true
                visible: text.length > 0
                wrapMode: Text.Wrap
                color: Theme.t3
                font.family: Theme.fontUi
                font.pixelSize: 12
                text: root.statusText
            }

            // ---- module settings ----
            // The same three preferences the 设置 page exposes, reachable from the
            // module itself so they are where the user looks for them.
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.showSettings
                radius: 12
                color: Theme.card2
                border.width: 1
                border.color: Theme.stroke

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 16

                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: qsTr("B 站播放设置")
                            color: Theme.t1
                            font.family: Theme.fontUi
                            font.pixelSize: 16
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 12
                        text: qsTr("默认画质是「上限」：实际取不高于此档的最高可用画质。"
                                   + "全部可用画质都高于此档时，取最低档。")
                    }

                    Text { text: qsTr("默认画质"); color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 12 }
                    Flow {
                        Layout.fillWidth: true
                        spacing: 6
                        Repeater {
                            model: [
                                { id: "0", label: qsTr("自动") },
                                { id: "127", label: "8K" },
                                { id: "126", label: qsTr("杜比") },
                                { id: "125", label: "HDR" },
                                { id: "120", label: "4K" },
                                { id: "116", label: "1080P60" },
                                { id: "112", label: "1080P+" },
                                { id: "80", label: "1080P" },
                                { id: "74", label: "720P60" },
                                { id: "64", label: "720P" },
                                { id: "32", label: "480P" },
                                { id: "16", label: "360P" }
                            ]
                            Rectangle {
                                readonly property bool active:
                                    String(veyra.preferences.biliQuality ?? 0) === modelData.id
                                width: label.implicitWidth + 22
                                height: 28
                                radius: 14
                                color: active ? Theme.accentSoft : Theme.card3
                                border.width: 1
                                border.color: active ? Theme.accent : Theme.stroke
                                Text {
                                    id: label
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: active ? Theme.accent : Theme.t2
                                    font.family: Theme.fontUi
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: veyra.setPreference("biliQuality", Number(modelData.id))
                                }
                            }
                        }
                    }

                    Text { text: qsTr("视频编码"); color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 12 }
                    RowLayout {
                        spacing: 6
                        Repeater {
                            model: [
                                { id: "auto", label: qsTr("自动") },
                                { id: "avc", label: "H.264" },
                                { id: "hevc", label: "HEVC" }
                            ]
                            Rectangle {
                                readonly property bool active:
                                    (veyra.preferences.biliCodec || "auto") === modelData.id
                                width: codecLabel.implicitWidth + 22
                                height: 28
                                radius: 14
                                color: active ? Theme.accentSoft : Theme.card3
                                border.width: 1
                                border.color: active ? Theme.accent : Theme.stroke
                                Text {
                                    id: codecLabel
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: active ? Theme.accent : Theme.t2
                                    font.family: Theme.fontUi
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: veyra.setPreference("biliCodec", modelData.id)
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }

                    Text { text: qsTr("音频编码"); color: Theme.t2; font.family: Theme.fontUi; font.pixelSize: 12 }
                    RowLayout {
                        spacing: 6
                        Repeater {
                            model: [
                                { id: "auto", label: qsTr("自动") },
                                { id: "flac", label: "Hi-Res" },
                                { id: "dolby", label: qsTr("杜比全景声") },
                                { id: "aac192", label: "AAC 192K" },
                                { id: "aac132", label: "AAC 132K" },
                                { id: "aac64", label: "AAC 64K" }
                            ]
                            Rectangle {
                                readonly property bool active:
                                    (veyra.preferences.biliAudio || "auto") === modelData.id
                                width: audioLabel.implicitWidth + 22
                                height: 28
                                radius: 14
                                color: active ? Theme.accentSoft : Theme.card3
                                border.width: 1
                                border.color: active ? Theme.accent : Theme.stroke
                                Text {
                                    id: audioLabel
                                    anchors.centerIn: parent
                                    text: modelData.label
                                    color: active ? Theme.accent : Theme.t2
                                    font.family: Theme.fontUi
                                    font.pixelSize: 12
                                }
                                MouseArea {
                                    anchors.fill: parent
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: veyra.setPreference("biliAudio", modelData.id)
                                }
                            }
                        }
                        Item { Layout.fillWidth: true }
                    }
                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 11
                        text: qsTr("Hi-Res 与杜比全景声需要大会员，且视频本身要有对应音轨；"
                                   + "选中的音轨不存在时自动回退到最高码率 AAC。")
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 10
                        CheckBox {
                            checked: veyra.preferences.biliAutoCdn !== false
                            onToggled: veyra.setPreference("biliAutoCdn", checked)
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2
                            Text {
                                text: qsTr("备用 CDN")
                                color: Theme.t2
                                font.family: Theme.fontUi
                                font.pixelSize: 12
                            }
                            Text {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: qsTr("主地址打不开时换用镜像节点")
                                color: Theme.t3
                                font.family: Theme.fontUi
                                font.pixelSize: 11
                            }
                        }
                    }

                    // ---- danmaku appearance ----
                    // The engine already takes these four; they were hardcoded until now.
                    // Title and switch on one line, the note under it. The switch is the
                    // master default for the module; everything below it is how that
                    // danmaku looks once loaded.
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        spacing: 4

                        RowLayout {
                            spacing: 8
                            Text {
                                text: qsTr("弹幕")
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: 14
                            }
                            VSwitch {
                                objectName: "bili-danmaku-default"
                                checked: root.danmakuDefault
                                onToggled: function (on) {
                                    // 与详情页按钮共用同一对入口，两边永远同步。
                                    // 偏好由 enableDanmaku/unloadDanmaku 自己写，
                                    // 这里不再单独 setPreference，避免两处不一致。
                                    // Shares the same two entry points as the detail-page
                                    // button, so the two stay in sync. The preference is
                                    // written inside them; setting it here as well would
                                    // be a second source of truth.
                                    if (on) root.enableDanmaku()
                                    else root.unloadDanmaku()
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("打开视频即加载弹幕；关闭后新视频不带弹幕，详情页也可以单独开关")
                            color: Theme.t3
                            wrapMode: Text.Wrap
                            font.family: Theme.fontUi
                            font.pixelSize: 11
                        }
                    }

                    // ---- 播放完毕后的行为 ----
                    // 默认关闭：播完停在播放画面上，按钮图标变回「播放」，再点即从头
                    // 重播。打开后播完自动回到视频详情。
                    // Off by default: playback ends on the player, the glyph turns back
                    // into Play, and pressing it replays from the start. Turning it on
                    // returns to the detail page when playback ends.
                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.topMargin: 8
                        spacing: 4

                        RowLayout {
                            spacing: 8
                            Text {
                                text: qsTr("播放完成后回到视频详情")
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: 14
                            }
                            VSwitch {
                                objectName: "bili-return-after-end"
                                checked: root.returnAfterEnd
                                onToggled: function (on) {
                                    veyra.setPreference("biliReturnAfterEnd", on)
                                }
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: qsTr("默认关闭：播放结束停在播放画面，点播放按钮从头重播")
                            color: Theme.t3
                            wrapMode: Text.Wrap
                            font.family: Theme.fontUi
                            font.pixelSize: 11
                        }
                    }

                    // A compact stepper: the value is shown between two buttons, which
                    // keeps a 50..250 range usable without a full-width slider.
                    component StepRow: RowLayout {
                        id: stepRow
                        property string label
                        property string prefKey
                        property int current
                        property int step
                        property int lowest
                        property int highest
                        property string suffix

                        spacing: 8
                        Text {
                            Layout.preferredWidth: 84
                            text: stepRow.label
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                        }
                        VButton {
                            text: "−"
                            implicitWidth: 34
                            enabled: stepRow.current > stepRow.lowest
                            onClicked: veyra.setPreference(stepRow.prefKey,
                                Math.max(stepRow.lowest, stepRow.current - stepRow.step))
                        }
                        Rectangle {
                            Layout.preferredWidth: 72
                            Layout.preferredHeight: 28
                            radius: 6
                            color: Theme.card3
                            Text {
                                anchors.centerIn: parent
                                text: stepRow.current + stepRow.suffix
                                color: Theme.t1
                                font.family: Theme.fontMono
                                font.pixelSize: 12
                            }
                        }
                        VButton {
                            text: "+"
                            implicitWidth: 34
                            enabled: stepRow.current < stepRow.highest
                            onClicked: veyra.setPreference(stepRow.prefKey,
                                Math.min(stepRow.highest, stepRow.current + stepRow.step))
                        }
                        Item { Layout.fillWidth: true }
                    }

                    StepRow {
                        label: qsTr("大小")
                        prefKey: "danmakuScale"
                        current: veyra.preferences.danmakuScale ?? 100
                        step: 10; lowest: 50; highest: 250; suffix: "%"
                    }
                    StepRow {
                        label: qsTr("不透明度")
                        prefKey: "danmakuOpacity"
                        current: veyra.preferences.danmakuOpacity ?? 80
                        step: 10; lowest: 10; highest: 100; suffix: "%"
                    }
                    StepRow {
                        label: qsTr("速度")
                        prefKey: "danmakuSpeed"
                        current: veyra.preferences.danmakuSpeed ?? 100
                        step: 10; lowest: 50; highest: 200; suffix: "%"
                    }
                    StepRow {
                        label: qsTr("显示区域")
                        prefKey: "danmakuArea"
                        current: veyra.preferences.danmakuArea ?? 50
                        step: 10; lowest: 10; highest: 100; suffix: "%"
                    }

                    Text {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 11
                        text: qsTr("显示区域 = 弹幕最多占用画面上方多高的比例，100% 会盖住整个画面。"
                                   + "防遮挡需要 B 站的额外蒙版数据，目前未支持。")
                    }

                    Item { Layout.fillHeight: true }
                }
            }

            // ---- sign-in panel ----
            Rectangle {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.qrState.length > 0
                radius: 12
                color: Theme.card2
                border.width: 1
                border.color: Theme.stroke

                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 14

                    Rectangle {
                        Layout.alignment: Qt.AlignHCenter
                        width: 300
                        height: 300
                        radius: 8
                        color: "#fff"

                        Image {
                            anchors.fill: parent
                            anchors.margins: 10
                            source: root.qrImage
                            fillMode: Image.PreserveAspectFit
                            cache: false
                            smooth: false
                        }
                    }

                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        color: root.qrState === "scanned" ? Theme.ok : Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: 13
                        text: root.qrHint
                    }
                    Text {
                        Layout.alignment: Qt.AlignHCenter
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 11
                        text: qsTr("扫码后画质上限提升到账号等级允许的档位")
                    }
                }
            }

            // ---- loading skeleton ----
            // Shown instead of the grid while a fetch is in flight. The request itself
            // blocks the GUI thread, so without this the click paints nothing and the
            // window freezes on the previous list until the answer arrives; the skeleton
            // at least gives the click immediate feedback. It replaces the grid rather
            // than covering it, so the two never paint on top of each other.
            GridView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: root.listLoading && root.view !== "detail"
                         && root.qrState.length === 0 && !root.showSettings
                // Follows the real grid so the placeholders sit exactly where the tiles
                // will land. The binding is re-evaluated once the grid exists, so the
                // declaration order above it is harmless.
                cellWidth: grid.fittedCellWidth > 0 ? grid.fittedCellWidth : 220
                cellHeight: grid.fittedCellHeight > 0 ? grid.fittedCellHeight : 196
                clip: true
                interactive: false
                // Enough to fill a maximised window; the count is capped so a huge screen
                // does not build hundreds of placeholder tiles.
                model: root.listLoading ? Math.min(40, Math.max(12, Math.ceil(width / cellWidth) * Math.ceil(height / cellHeight) + 4)) : 0

                delegate: Item {
                    width: GridView.view ? GridView.view.cellWidth : 232
                    height: GridView.view ? GridView.view.cellHeight : 196

                    readonly property real pad: 6

                    Rectangle {
                        x: parent.pad
                        y: parent.pad
                        width: parent.width - parent.pad * 2
                        height: width * 0.5625
                        radius: 6
                        color: Theme.card3
                    }
                    // 骨架屏的占位条也按卡片宽度缩放，否则最大化时骨架和真卡片对不上。
                    // The skeleton bars scale with the card width as well, or they no longer
                    // line up with the real cards once the window is enlarged.
                    // 用 delegate 自己的 width，不能用 parent.width：属性初始化时
                    // parent 可能还是 null，那样启动就抛 TypeError。
                    // The delegate's own width, not parent.width: at property-initialisation
                    // time `parent` can still be null, which threw on startup.
                    readonly property int skelTitleSize:
                        Math.max(11, Math.min(25, Math.round(
                            13 * Math.pow(width / 220, 0.8))))
                    Rectangle {
                        x: parent.pad
                        y: parent.pad + (parent.width - parent.pad * 2) * 0.5625 + 5
                        width: parent.width - parent.pad * 2
                        height: Math.ceil(parent.skelTitleSize * 1.4)
                        radius: 3
                        color: Theme.card3
                    }
                    Rectangle {
                        x: parent.pad
                        y: parent.pad + (parent.width - parent.pad * 2) * 0.5625 + 5 + Math.ceil(13 * 1.4) + 3
                        width: (parent.width - parent.pad * 2) * 0.55
                        height: 12
                        radius: 3
                        color: Theme.card3
                    }
                }
            }

            // ---- video grid ----
            GridView {
                id: grid
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !root.listLoading && root.view !== "detail"
                         && root.qrState.length === 0 && !root.showSettings
                // The app's own scrollbar, same as every other list in this program: a thin
                // thumb, draggable, no track or arrows.
                ScrollBar.vertical: VScrollBar { }
                // 滚轮平滑滚动：用 Flickable 自己的 flick()，不要直接动 contentY。
                // 上一版给 contentY 挂了 NumberAnimation，那会和 Flickable 内部状态打架
                // —— 它自己也在维护 contentY，结果连正常滚动都失效了。flick() 是官方
                // API：给一个初速度，Flickable 按 flickDeceleration 自然减速，惯性是
                // 它自带的，内部状态也不会被破坏。
                // Smooth wheel scrolling through Flickable's own flick(). The previous version
                // animated contentY directly, which fought Flickable's internal state - it
                // maintains contentY itself, so even normal scrolling stopped. flick() is the
                // supported API: hand it a velocity and Flickable decelerates by
                // flickDeceleration, so the inertia comes for free and nothing is corrupted.
                // 对齐 Chrome 的滚轮手感：**没有甩动惯性**，只有「平滑动画到目标位置、
                // 到位即停」。这和 flick() 是两种机制 —— flick 是给一个速度让它自己滑，
                // Chrome 是算出目标再走一段固定时长的缓动。
                // 参数取自 Chrome 的观感：一格约 100 px、时长约 150 ms、缓出曲线。
                // Matching Chrome's wheel feel: **no fling inertia**, just a smooth move to
                // a target that stops on arrival. That is a different mechanism from flick(),
                // which is handed a velocity and coasts. Chrome computes a target and eases
                // to it over a fixed duration. The values follow Chrome's behaviour: about
                // 100 px per notch, about 150 ms, ease-out.
                property real wheelTargetY: 0
                readonly property real wheelStep: 100
                WheelHandler {
                    acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
                    onWheel: function(event) {
                        event.accepted = true
                        // 触控板给像素增量，直接用；鼠标一格算 100 px。
                        // A touchpad reports pixels and they are used as-is; a mouse notch
                        // counts as 100 px.
                        const delta = event.pixelDelta.y !== 0
                            ? -event.pixelDelta.y
                            : -(event.angleDelta.y / 120.0) * grid.wheelStep
                        const maxY = Math.max(0, grid.contentHeight - grid.height)
                        // 动画进行中就从当前目标继续累加，连续滚才连得上。
                        // While the animation runs, accumulate onto its target so repeated
                        // notches chain instead of fighting each other.
                        const base = wheelMove.running ? grid.wheelTargetY : grid.contentY
                        grid.wheelTargetY = Math.max(0, Math.min(maxY, base + delta))
                        // 显式给 from：只改 to 会让动画从旧起点重算，位置会跳。
                        // `from` is set explicitly: changing only `to` makes the animation
                        // recompute from the old origin, which shows up as a jump.
                        wheelMove.stop()
                        wheelMove.from = grid.contentY
                        wheelMove.to = grid.wheelTargetY
                        wheelMove.start()
                    }
                }
                NumberAnimation {
                    id: wheelMove
                    target: grid
                    property: "contentY"
                    duration: 150
                    // Chrome 的滚轮缓动接近 ease-out，不是线性。
                    // Chrome's wheel easing is close to ease-out, not linear.
                    easing.type: Easing.OutQuad
                }
                // The column count follows the ideal card width and the cells then share
                // the available width exactly, so every window size is flush with no
                // leftover strip: a fixed 232 fits four columns in the default window and
                // leaves 32 px at 2544 because 2352 is not a multiple of 232.
                readonly property real idealCellWidth: 220
                // 列数封顶 5：默认窗口（1280 宽）正好排 5 列，窗口再宽就把多出来的宽度
                // 给卡片，而不是往里塞更多列 —— 拉大窗口看到的是更大的卡片，不是更密的
                // 网格。窗口比默认更窄时仍按原来的逻辑自动减少列数。
                // Columns are capped at five: the default 1280-wide window fits exactly five,
                // and anything wider hands the extra width to the cards instead of adding
                // columns - enlarging the window shows bigger cards, not a denser grid. A
                // window narrower than the default still drops columns the way it did before.
                readonly property int maxColumns: 5
                readonly property int columnCount:
                    Math.min(maxColumns, Math.max(1, Math.round(width / idealCellWidth)))
                readonly property real fittedCellWidth: width / columnCount
                // A wider cell means a taller cover (16:9 on the inner width), so the cell
                // grows with it or the title and author line get clipped.
                // 文字区高度同样按卡片宽度缩放：字号变大了，两行标题加作者行就要更多
                // 空间，固定 81 在最大化时会裁掉标题第二行。
                // The text block scales with the card width too: larger type needs more room,
                // and a fixed 81 would clip the second title line when maximised.
                readonly property real fittedCellHeight:
                    (fittedCellWidth - 12) * 0.5625 + 81 * (fittedCellWidth / 220)
                cellWidth: fittedCellWidth
                cellHeight: fittedCellHeight
                clip: true
                // A ListModel, not the array: appending to it leaves contentY untouched,
                // while assigning a new array rebuilds the model and jumps to the top.
                model: cardsModel
                // Endless scrolling: ask for the next batch while two rows are still
                // below the viewport, so the append usually lands before the user reaches
                // the end. contentHeight changes as delegates are built, hence the wider
                // threshold rather than an exact "at the bottom" test.
                onContentYChanged: maybeLoadMore()
                onContentHeightChanged: maybeLoadMore()
                onHeightChanged: maybeLoadMore()
                // Load when the bottom is near, and also when the content does not even
                // fill the viewport: the old early return treated a short list as "nothing
                // to scroll" and stopped there forever. That happened as soon as the window
                // was maximised on a 4K display — the viewport grew taller than one page of
                // cards, so the list could never grow and the grid stayed un-scrollable.
                function maybeLoadMore() {
                    if (!root.activeListKey.length || root.loadingMore || root.listLoading) return
                    if (contentHeight <= height || contentY + height >= contentHeight - cellHeight * 1.5)
                        root.loadMore()
                }
                // GridView keeps delegates for a band around the viewport. The default
                // builds roughly a screen of extra tiles that the user may never scroll
                // to, and every one of them costs a cover decode on the CPU under the
                // software renderer. Half a cell of lead-in is enough to keep scrolling
                // smooth while halving the eager work at each model swap.
                cacheBuffer: fittedCellHeight / 2

                // The tile is the single hottest piece of QML in this page: every rail
                // click replaces the model, so all visible delegates are rebuilt at once.
                // Under the software raster path (this machine runs the RTSS/NVIDIA
                // overlay compatibility mode) three things dominated that rebuild and are
                // gone here: Text.Outline (the duration label drew the string several
                // times per frame), clip on the cover (a rounded clip forces an offscreen
                // surface per tile) and the nested Column (absolute positions instead).
                delegate: Item {
                    id: tile
                    width: grid.cellWidth
                    height: grid.cellHeight

                    readonly property real pad: 6
                    readonly property real coverHeight: (width - pad * 2) * 0.5625
                    // 字号随卡片宽度缩放，而不是写死。
                    // 基准：卡片 220 宽时标题 13、作者行 12（这是 B 站网页 254 宽卡片
                    // 标题 15、作者行 13 按比例换算来的）。窗口拉大后卡片变宽，字号必须
                    // 跟着长，否则最大化时字会显得很小 —— 这正是实际反馈的问题。
                    // Sizes scale with the card width instead of being fixed. The baseline is
                    // 13 / 12 at 220 px (Bilibili's 254 px cards use 15 / 13). When the window
                    // grows the cards grow, and the text has to grow with them or it looks
                    // tiny when maximised - which is exactly what was reported.
                    readonly property real baseCardWidth: 220
                    // 用 0.8 次幂而不是线性：卡片宽一倍时字号不该也宽一倍，那样最大化
                    // 会显得字过大 —— 实际反馈就是「最大化时字体大了一点点」。幂函数让
                    // 增长逐渐变缓，宽卡片配的字号更协调。
                    // A 0.8 power rather than linear: doubling the card should not double the
                    // type, which is what made it look oversized when maximised (the reported
                    // "a little too large"). The power curve flattens as cards grow, keeping
                    // large cards proportionate.
                    readonly property int titleFontSize:
                        Math.max(11, Math.min(25, Math.round(
                            13 * Math.pow(width / baseCardWidth, 0.8))))
                    readonly property int metaFontSize:
                        Math.max(10, Math.min(23, Math.round(
                            12 * Math.pow(width / baseCardWidth, 0.8))))
                    // Fixed, so the two text blocks sit at absolute positions and the
                    // delegate needs no layout pass beyond this. 1.4 is a comfortable line
                    // box for a two-line title.
                    readonly property real titleLineHeight: Math.ceil(titleFontSize * 1.4)

                    // 鼠标经过时整张卡片亮一档，和左边栏的反馈一致。放在最底层，
                    // 封面和文字盖在它上面，所以只会在卡片留白处露出来。
                    // The whole tile brightens one step under the pointer, matching the rail's
                    // feedback. It sits underneath, so the cover and the text draw over it and
                    // it only shows in the tile's own margins.
                    Rectangle {
                        anchors.fill: parent
                        radius: 10
                        color: tileMouse.containsMouse ? Theme.card3 : "transparent"

                        Behavior on color {
                            ColorAnimation { duration: 110; easing.type: Easing.OutQuad }
                        }
                    }

                    Rectangle {
                        // Not "cover": that is a ListModel role name, and an id with the
                        // same name wins the lookup, so source: cover resolved to this
                        // rectangle instead of the image URL and every tile went blank.
                        id: coverBox
                        x: tile.pad
                        y: tile.pad
                        width: tile.width - tile.pad * 2
                        height: tile.coverHeight
                        radius: 6
                        color: Theme.card3

                        Image {
                            anchors.fill: parent
                            source: cover || ""
                            fillMode: Image.PreserveAspectCrop
                            asynchronous: true
                            cache: true
                            // Decoded at tile size, not at the source's size: the software
                            // renderer pays for the full bitmap on every repaint, and a
                            // Bilibili cover is much larger than a 220x124 tile.
                            sourceSize.width: 480
                            sourceSize.height: 270
                        }
                        // The duration sits on its own dark strip instead of an outlined
                        // string: one translucent rectangle costs far less than outlined
                        // text under the software renderer.
                        Rectangle {
                            anchors.right: parent.right
                            anchors.bottom: parent.bottom
                            anchors.margins: 3
                            width: durationLabel.implicitWidth + 8
                            height: durationLabel.implicitHeight + 2
                            radius: 3
                            color: Qt.rgba(0, 0, 0, 0.66)
                            visible: (duration || "").length > 0
                            Text {
                                id: durationLabel
                                anchors.centerIn: parent
                                text: duration
                                color: "#fff"
                                font.pixelSize: 10
                                font.family: Theme.fontUi
                            }
                        }
                    }

                    Text {
                        x: tile.pad
                        y: coverBox.y + coverBox.height + 5
                        width: tile.width - tile.pad * 2
                        height: tile.titleLineHeight * 2
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        wrapMode: Text.Wrap
                        color: Theme.t1
                        font.family: Theme.fontUi
                        font.pixelSize: tile.titleFontSize
                        text: title || ""
                    }
                    Text {
                        x: tile.pad
                        y: coverBox.y + coverBox.height + 5 + tile.titleLineHeight * 2 + 3
                        width: tile.width - tile.pad * 2
                        elide: Text.ElideRight
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: tile.metaFontSize
                        text: (owner || "") + "  " + (play || 0)
                    }

                    MouseArea {
                        id: tileMouse
                        anchors.fill: parent
                        // 必须开 hoverEnabled，否则 containsMouse 永远是 false。
                        // hoverEnabled has to be on, or containsMouse is always false.
                        hoverEnabled: true
                        cursorShape: Qt.PointingHandCursor
                        onClicked: root.openVideo(bvid)
                    }
                }
            }

            // ---- detail ----
            Flickable {
                Layout.fillWidth: true
                Layout.fillHeight: true
                // 扫码面板出现时也要让位：原来只判断 showSettings，于是从详情页点
                // 「扫码登录」会和二维码上下各占一半，看起来像二维码被挤扁了。
                // Step aside while the QR panel is up as well: this used to check only
                // showSettings, so opening the code from the detail page split the area
                // in half and the code looked squeezed.
                visible: root.view === "detail" && !root.showSettings
                         && root.qrState.length === 0
                contentHeight: detailColumn.implicitHeight
                clip: true

                ColumnLayout {
                    id: detailColumn
                    width: parent.width
                    spacing: 10

                    // The page is already on screen while the detail request is in flight,
                    // so say so rather than showing an empty frame with no explanation.
                    Text {
                        Layout.fillWidth: true
                        visible: root.detailLoading
                        text: qsTr("加载中…")
                        color: Theme.t3
                        font.family: Theme.fontUi
                        font.pixelSize: 12
                    }

                    RowLayout {
                        spacing: 12
                        VButton { text: qsTr("← 返回"); onClicked: root.view = "home" }

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.t1
                            font.family: Theme.fontUi
                            font.pixelSize: 16
                            text: root.detail.title || ""
                        }
                    }

                    // Cover on the left, details on the right (newBV's detail layout).
                    // Clicking the cover plays, which is the gesture users already know.
                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 14

                        Rectangle {
                            Layout.preferredWidth: 300
                            Layout.preferredHeight: 169
                            Layout.alignment: Qt.AlignTop
                            radius: 8
                            color: Theme.card3
                            clip: true

                            Image {
                                anchors.fill: parent
                                source: root.detail.cover || ""
                                fillMode: Image.PreserveAspectCrop
                                asynchronous: true
                                cache: true
                            }

                            // Play affordance, shown on hover so the picture still reads
                            // as a cover at rest.
                            Rectangle {
                                anchors.centerIn: parent
                                width: 56
                                height: 56
                                radius: 28
                                color: Qt.rgba(0, 0, 0, 0.55)
                                visible: coverMouse.containsMouse
                                Text {
                                    anchors.centerIn: parent
                                    text: "▶"
                                    color: "#fff"
                                    font.pixelSize: 22
                                }
                            }

                            MouseArea {
                                id: coverMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.playSelected()
                            }
                        }

                        ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 10

                        Text {
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                            color: Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                            text: (root.detail.owner || "") + "   "
                                  + (root.detail.play || 0) + qsTr(" 播放   ")
                                  + (root.detail.like || 0) + qsTr(" 点赞")
                        }

                        // interaction row
                        RowLayout {
                            spacing: 8

                            VButton {
                                text: (root.interaction.liked ? qsTr("已点赞") : qsTr("点赞"))
                                onClicked: {
                                    // 异步提交；成功后由 onBilibiliActionDone 刷新状态。
                                    // Async submit; onBilibiliActionDone re-reads the state.
                                    const next = !root.interaction.liked
                                    veyra.bilibiliRequestLike(root.detail.bvid, next)
                                }
                            }
                            VButton {
                                text: root.interaction.coined ? qsTr("已投币") : qsTr("投币")
                                onClicked: veyra.bilibiliRequestCoin(root.detail.bvid, 1)
                            }
                            VButton {
                                text: root.interaction.favorited ? qsTr("已收藏") : qsTr("收藏")
                                onClicked: {
                                    // Cancelling does not need a folder; adding asks which one.
                                    if (root.interaction.favorited) {
                                        // 取消收藏走异步；成功与否由 onBilibiliFavoriteReady
                                        // 回来时判断，reason "toggle" 表示是这一处发起的。
                                        // Async: the outcome is reported by
                                        // onBilibiliFavoriteReady; reason "toggle" marks it.
                                        veyra.bilibiliRequestFavorite(root.detail.bvid, false, 0, "toggle")
                                    } else {
                                        // 选择器打开不等数据：先开浮层，folder 列表异步填。
                                        // reason "picker" 让回调知道该填哪个列表。
                                        // Open the picker first and fill it when the
                                        // list arrives; reason "picker" routes the reply.
                                        root.favPickerTarget = 0
                                        root.favPickerOpen = true
                                        veyra.bilibiliRequestFavFolders("picker")
                                    }
                                }
                            }
                            VButton {
                                text: qsTr("稍后再看")
                                onClicked: veyra.bilibiliRequestWatchLater(root.detail.bvid, true)
                            }
                            VButton {
                                text: root.showComments ? qsTr("收起评论") : qsTr("查看评论")
                                onClicked: root.toggleComments()
                            }
                        }

                        // Folder chooser for 收藏: the account keeps several folders and a
                        // favourite used to land in the first one whatever the user meant.
                        Rectangle {
                            Layout.fillWidth: true
                            visible: root.favPickerOpen
                            implicitHeight: pickerRow.implicitHeight + 20
                            radius: 10
                            color: Theme.card2
                            border.width: 1
                            border.color: Theme.stroke

                            ColumnLayout {
                                id: pickerRow
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 8

                                Text {
                                    Layout.fillWidth: true
                                    text: qsTr("收藏到哪个收藏夹？")
                                    color: Theme.t2
                                    font.family: Theme.fontUi
                                    font.pixelSize: 12
                                }
                                Flow {
                                    Layout.fillWidth: true
                                    spacing: 8
                                    Repeater {
                                        model: root.favPickerFolders
                                        Rectangle {
                                            readonly property bool picked:
                                                String(root.favPickerTarget) === String(modelData.id)
                                            width: pickText.implicitWidth + 24
                                            height: 26
                                            radius: 13
                                            color: picked ? Theme.accentSoft : Theme.card3
                                            border.width: 1
                                            border.color: picked ? Theme.accent : Theme.stroke
                                            Text {
                                                id: pickText
                                                anchors.centerIn: parent
                                                text: modelData.title + " (" + modelData.count + ")"
                                                color: picked ? Theme.accent : Theme.t2
                                                font.family: Theme.fontUi
                                                font.pixelSize: 12
                                            }
                                            MouseArea {
                                                anchors.fill: parent
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: root.favPickerTarget = modelData.id
                                            }
                                        }
                                    }
                                }
                                RowLayout {
                                    spacing: 8
                                    VButton {
                                        text: qsTr("收藏")
                                        primary: true
                                        enabled: root.favPickerTarget !== 0
                                        onClicked: {
                                            // 选定文件夹后异步提交；成功时由回调关浮层
                                            // 并刷新互动状态（reason "picker"）。
                                            // Submit asynchronously; on success the reply
                                            // closes the picker and re-reads the state.
                                            veyra.bilibiliRequestFavorite(root.detail.bvid, true,
                                                                          root.favPickerTarget, "picker")
                                        }
                                    }
                                    VButton {
                                        text: qsTr("取消")
                                        ghost: true
                                        onClicked: root.favPickerOpen = false
                                    }
                                    Item { Layout.fillWidth: true }
                                }
                            }
                        }

                        RowLayout {
                            spacing: 10

                            Text {
                                text: qsTr("分P")
                                color: Theme.t2
                                font.family: Theme.fontUi
                                font.pixelSize: 12
                            }
                            VSelect {
                                Layout.preferredWidth: 260
                                title: qsTr("分P")
                                // value comes straight from selectedPart, so opening another
                                // video (which resets it to 0) needs no rebinding.
                                value: {
                                    const ps = root.detail.parts || []
                                    if (!ps.length) return ""
                                    const i = Math.max(0, Math.min(root.selectedPart, ps.length - 1))
                                    return (i + 1) + ". " + ps[i].title
                                }
                                options: (root.detail.parts || []).map(function (p, i) {
                                    return { id: String(i), label: (i + 1) + ". " + p.title }
                                })
                                onPicked: function (id) {
                                    const index = parseInt(id, 10) || 0
                                    root.selectedPart = index
                                    // Each part has its own position in the history, so the
                                    // play button's label has to follow the part as well.
                                    // The cid is already known, so neither call re-fetches
                                    // the page; qualities is asynchronous like the first load.
                                    const parts = root.detail.parts || []
                                    const cid = (index >= 0 && index < parts.length)
                                        ? parts[index].cid : 0
                                    if (cid) veyra.bilibiliRequestResumeAt(root.detail.bvid, cid)
                                    else root.resumeAt = -1
                                    root.qualities = []
                                    if (cid) {
                                        root.pendingQualitiesGeneration = veyra.bilibiliLoadList(
                                            "qualities:" + root.detail.bvid + ":" + cid)
                                    }
                                    root.selectedQuality = 0
                                    // 分P就是弹幕轨道：换了分P必须换轨道，否则要么旧弹幕
                                    // 留着、要么开关停在旧状态。这里按新分P重新拉一次。
                                    // The part is the track: switching it has to switch the
                                    // track too, otherwise either the old comments stay or
                                    // the switch keeps the previous part's state. Reload
                                    // for the newly picked part.
                                    if (root.danmakuOn || root.danmakuDefault) root.loadDanmaku()
                                }
                            }

                            Text {
                                text: qsTr("画质")
                                color: Theme.t2
                                font.family: Theme.fontUi
                                font.pixelSize: 12
                            }
                            VSelect {
                                id: qualityBox
                                Layout.preferredWidth: 200
                                title: qsTr("画质")
                                // An explicit "automatic" entry (id "0") rather than the
                                // first real quality: that way the visible choice and
                                // root.selectedQuality (0 = best the account may use)
                                // always agree, whatever the list happens to contain.
                                // value is derived from selectedQuality, so switching parts
                                // (which resets it to 0) needs no rebinding.
                                readonly property var allOptions: [{
                                    id: "0", label: qsTr("自动（最高画质）")
                                }].concat((root.qualities || []).map(function (q) {
                                    return {
                                        id: String(q.id),
                                        label: q.label + (q.vip ? qsTr("（会员）") : "")
                                    }
                                }))
                                value: {
                                    for (let i = 0; i < allOptions.length; ++i)
                                        if (Number(allOptions[i].id) === root.selectedQuality)
                                            return allOptions[i].label
                                    return allOptions[0].label
                                }
                                options: allOptions
                                onPicked: function (id) {
                                    root.selectedQuality = parseInt(id, 10) || 0
                                }
                            }
                        }

                        RowLayout {
                            spacing: 8

                            VButton {
                                // The label states what the click will actually do: with a
                                // history entry it resumes at that position, without one it
                                // simply plays. Saying "continue" over a video that will start
                                // from zero is what made the old label misleading.
                                primary: true
                                text: root.resumeAt > 0
                                      ? qsTr("继续上次播放（%1）").arg(root.formatSeconds(root.resumeAt))
                                      : qsTr("播放")
                                onClicked: root.playSelected(false)
                            }

                            // A video already in the watch history resumes where it was left,
                            // so without this the user could never watch it again from the
                            // beginning.
                            VButton {
                                objectName: "bili-play-from-start"
                                text: qsTr("从头播放")
                                onClicked: root.playSelected(true)
                            }

                            VButton {
                                text: root.danmakuOn
                                      ? qsTr("弹幕开（%1 条）").arg(root.danmakuLines)
                                      : qsTr("弹幕关")
                                // 与播放设置里的同名开关是同一个状态：点这里会写偏好，
                                // 那边跟着变，重启后保持。
                                // Same state as the switch of the same name in 播放设置:
                                // clicking here writes the preference, that switch
                                // follows, and the choice survives a restart.
                                onClicked: root.danmakuOn ? root.unloadDanmaku()
                                                          : root.enableDanmaku()
                            }
                        }

                        // 简介改成只读的 TextEdit：Text 不支持选中，换成它才能用鼠标
                        // 划选并复制。只读 + 可选中，外观和原来的 Text 一致（无背景、无边框）。
                        // 高度必须显式给 contentHeight —— TextEdit 不像 Text 那样自己撑开，
                        // 在 ColumnLayout 里会塌成 0。
                        // The description is a read-only TextEdit now: Text cannot be selected,
                        // this can, so the text can be dragged over and copied. Read-only with
                        // mouse selection keeps it looking like the old Text (no background, no
                        // frame). The height has to be contentHeight explicitly - unlike Text,
                        // a TextEdit does not size itself and would collapse to 0 in a layout.
                        TextEdit {
                            Layout.fillWidth: true
                            Layout.preferredHeight: contentHeight
                            readOnly: true
                            selectByMouse: true
                            textFormat: TextEdit.PlainText
                            wrapMode: TextEdit.Wrap
                            selectionColor: Theme.accentSoft
                            selectedTextColor: Theme.t1
                            color: Theme.t3
                            font.family: Theme.fontUi
                            font.pixelSize: 12
                            text: root.detail.desc || ""
                        }
                        }
                    }

                    // comments
                    ColumnLayout {
                        Layout.fillWidth: true
                        spacing: 8
                        visible: root.showComments

                        Text {
                            text: root.comments.length ? qsTr("评论") : qsTr("暂无评论")
                            color: Theme.t2
                            font.family: Theme.fontUi
                            font.pixelSize: 13
                        }

                        // 发评论：一个输入框加一个按钮。结果（成功或 B 站给的原因）
                        // 显示在下面一行。
                        // Posting a comment: one field and one button, with the outcome -
                        // success or Bilibili's own reason - on the line below.
                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8

                            Rectangle {
                                Layout.fillWidth: true
                                implicitHeight: 32
                                radius: 8
                                color: Theme.card2
                                border.width: 1
                                border.color: replyInput.activeFocus ? Theme.accent : Theme.stroke

                                TextInput {
                                    id: replyInput
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    verticalAlignment: TextInput.AlignVCenter
                                    color: Theme.t1
                                    font.family: Theme.fontUi
                                    font.pixelSize: 12
                                    selectByMouse: true
                                    clip: true
                                    text: root.replyText
                                    onTextChanged: root.replyText = text
                                    enabled: !root.replySending
                                    onAccepted: replySendButton.send()
                                }
                                Text {
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    verticalAlignment: Text.AlignVCenter
                                    visible: replyInput.text.length === 0
                                    text: qsTr("发一条评论…")
                                    color: Theme.t3
                                    font.family: Theme.fontUi
                                    font.pixelSize: 12
                                }
                            }

                            VButton {
                                id: replySendButton
                                implicitWidth: 68
                                fontSize: 12
                                enabled: !root.replySending && root.replyText.trim().length > 0
                                text: root.replySending ? qsTr("发送中") : qsTr("发送")
                                function send() {
                                    if (root.replySending) return
                                    const body = root.replyText.trim()
                                    if (!body.length) return
                                    if (!root.detail || !root.detail.bvid) return
                                    root.replySending = true
                                    root.replyMessage = ""
                                    veyra.bilibiliPostReply(root.detail.bvid, body)
                                }
                                onClicked: send()
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            visible: root.replyMessage.length > 0
                            wrapMode: Text.Wrap
                            text: root.replyMessage
                            color: root.replyMessage === qsTr("评论已发送") ? Theme.ok : Theme.warn
                            font.family: Theme.fontUi
                            font.pixelSize: 11
                        }

                        Repeater {
                            model: root.comments

                            Rectangle {
                                Layout.fillWidth: true
                                height: commentColumn.implicitHeight + 16
                                radius: 8
                                color: Theme.card2

                                ColumnLayout {
                                    id: commentColumn
                                    anchors.fill: parent
                                    anchors.margins: 8
                                    spacing: 4

                                    RowLayout {
                                        Layout.fillWidth: true
                                        spacing: 8

                                        Text {
                                            Layout.fillWidth: true
                                            elide: Text.ElideRight
                                            text: modelData.author + "   " + (modelData.like || 0) + qsTr(" 赞")
                                            color: Theme.t3
                                            font.family: Theme.fontUi
                                            font.pixelSize: 11
                                        }
                                        // 只有自己发的评论才有删除按钮：拿作者 mid 和当前
                                        // 账号的 mid 比。mid 是 9-10 位数，双精度能精确表示。
                                        // Only the account's own comments get a delete button:
                                        // the author mid is compared with the signed-in one.
                                        // A mid is 9-10 digits, exact in a double.
                                        Text {
                                            visible: modelData.mid > 0
                                                     && root.account.mid === modelData.mid
                                            text: qsTr("删除")
                                            color: deletingMouse.containsMouse ? Theme.warn : Theme.t3
                                            font.family: Theme.fontUi
                                            font.pixelSize: 11

                                            MouseArea {
                                                id: deletingMouse
                                                anchors.fill: parent
                                                anchors.margins: -4
                                                hoverEnabled: true
                                                cursorShape: Qt.PointingHandCursor
                                                onClicked: {
                                                    if (!root.detail || !root.detail.bvid) return
                                                    root.replyMessage = ""
                                                    root.pendingDeleteRpid = modelData.rpid
                                                    veyra.bilibiliDeleteReply(root.detail.bvid,
                                                                              modelData.rpid)
                                                }
                                            }
                                        }
                                    }
                                    // 评论正文同样改成只读 TextEdit，可划选复制。
                                    // The comment body is a read-only TextEdit too, so it can
                                    // be selected and copied.
                                    TextEdit {
                                        Layout.fillWidth: true
                                        Layout.preferredHeight: contentHeight
                                        readOnly: true
                                        selectByMouse: true
                                        textFormat: TextEdit.PlainText
                                        wrapMode: TextEdit.Wrap
                                        selectionColor: Theme.accentSoft
                                        selectedTextColor: Theme.t1
                                        text: modelData.message || ""
                                        color: Theme.t1
                                        font.family: Theme.fontUi
                                        font.pixelSize: 12
                                    }
                                }
                            }
                        }
                    }

                    // related videos
                    Text {
                        visible: root.related.length > 0
                        text: qsTr("相关推荐")
                        color: Theme.t2
                        font.family: Theme.fontUi
                        font.pixelSize: 13
                    }

                    GridView {
                        id: relatedGrid
                        Layout.fillWidth: true
                        // The row count has to follow the width: four per row was wrong as
                        // soon as the window was narrower, and the clipped rows could not
                        // be scrolled to. The height goes through Layout.preferredHeight
                        // because a ColumnLayout overrides the item's own height.
                        // Same fitting rule as the main grid: describe the column count
                        // from an ideal width, then divide the available width between the
                        // cells so nothing is left over. rows and the height follow.
                        readonly property real idealCellWidth: 220
                        readonly property int columns: Math.max(1, Math.round(width / idealCellWidth))
                        readonly property real fittedCellWidth: width / columns
                        readonly property real fittedCellHeight:
                            (fittedCellWidth - 12) * 0.5625 + 81 * (fittedCellWidth / 220)
                        readonly property int rows: Math.ceil(root.related.length / Math.max(1, columns))
                        readonly property int wantedHeight: rows * fittedCellHeight
                        Layout.preferredHeight: wantedHeight
                        Layout.maximumHeight: wantedHeight
                        implicitHeight: wantedHeight
                        visible: root.related.length > 0
                        cellWidth: fittedCellWidth
                        cellHeight: fittedCellHeight
                        // Only ask for scrolling when the rows really overflow; the height
                        // above already covers every row, so this stays false in practice.
                        interactive: contentHeight > height + 1
                        clip: true
                        model: root.related

                        // Same shape as the main grid's tile: absolute positions, no clip on
                        // the cover, and the width taken from the grid instead of a literal
                        // (a literal stayed at 232 while the columns were computed from
                        // cellWidth, so the tiles overhung their cells).
                        delegate: Item {
                            id: relatedTile
                            width: relatedGrid.cellWidth
                            height: relatedGrid.cellHeight

                            readonly property real pad: 6
                            // 与主网格同一套规则：字号随卡片宽度缩放，两处看起来才一致。
                            // Same rule as the main grid: the type scales with the card width
                            // so both card styles match.
                            readonly property real baseCardWidth: 220
                            readonly property int titleFontSize:
                                Math.max(11, Math.min(25, Math.round(
                                    13 * Math.pow(width / baseCardWidth, 0.8))))
                            readonly property real titleLineHeight: Math.ceil(titleFontSize * 1.4)

                            // 与主网格同一套悬停反馈。
                            // Same hover feedback as the main grid.
                            Rectangle {
                                anchors.fill: parent
                                radius: 10
                                color: relatedMouse.containsMouse ? Theme.card3 : "transparent"

                                Behavior on color {
                                    ColorAnimation { duration: 110; easing.type: Easing.OutQuad }
                                }
                            }

                            Rectangle {
                                x: relatedTile.pad
                                y: relatedTile.pad
                                width: relatedTile.width - relatedTile.pad * 2
                                height: width * 0.5625
                                radius: 6
                                color: Theme.card3

                                Image {
                                    anchors.fill: parent
                                    source: modelData.cover || ""
                                    fillMode: Image.PreserveAspectCrop
                                    asynchronous: true
                                    sourceSize.width: 480
                                    sourceSize.height: 270
                                }
                            }

                            Text {
                                x: relatedTile.pad
                                y: relatedTile.pad + (relatedTile.width - relatedTile.pad * 2) * 0.5625 + 5
                                width: relatedTile.width - relatedTile.pad * 2
                                height: relatedTile.titleLineHeight * 2
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                wrapMode: Text.Wrap
                                color: Theme.t1
                                font.family: Theme.fontUi
                                font.pixelSize: relatedTile.titleFontSize
                                text: modelData.title || ""
                            }

                            MouseArea {
                                id: relatedMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                cursorShape: Qt.PointingHandCursor
                                onClicked: root.openVideo(modelData.bvid)
                            }
                        }
                    }
                }
            }
        }
    }
}
