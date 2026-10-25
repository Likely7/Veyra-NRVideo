# Veyra B 站模块 — 开发史与技术记录

> ⚠️ **两条读前须知**
>
> 1. 文中 `D:\项目\Veyra-NV\...` 是**当时的记录**，那个目录已于 2026-10-25 删除，
>    当前路径见 `HANDOFF.md` 第二节。
> 2. 本文的「基线恢复」机制（`restore.py` + 快照）**已废弃**，现在用 `git` 管理。

**整理日期**：2026-10-25（原为逐版本追加的流水账，已重排）
**来源**：`D:\DSH\.dsh-tmp\keep\veyra-history-README.md`（2026-10-25 迁入仓库）

---

> **文档地图**（我自己的 5 份文档，作者原版 600+ 份在 `docs/` 不动）
>
> | 文件 | 一句话定位 | 什么时候看 |
> |---|---|---|
> | **MY_WORK.md** | 记录 + 20 条行动规则 | 想提 PR、想知道做过什么、查当前状态 |
> | **HANDOFF.md** | 环境、构建、部署、验证 | 要动手编译或部署 |
> | **docs/DOLBY_VISION.md** | 杜比视界 P5/P7/L1 技术档案 | 改杜比视界相关代码 |
> | **docs/BILIBILI_MODULE_HISTORY.md** | B 站模块开发史 + 未解决问题 | B 站功能出问题时 |
> | **docs/HISTORY.md** | 13 个专题的技术排查链 | 遇到具体技术障碍（AMD 光流、采集卡…）|

## 📖 想找什么，先看这里

| 你想查什么 | 看哪节 |
|---|---|
| **现在还有哪些问题没解决？** | 一、未解决问题与已知限制 |
| B 站模块是怎么一步步做出来的 | 二、版本演进（v23 → v2，最新在前）|
| 投币为什么一直失败 | 一、未解决问题 → 投币失败的技术分析 |
| 当前模块状态与相关 PR | 见 `MY_WORK.md` 第一、二节 |

---

## 一、未解决问题与已知限制

### 1. 搜索框回车无响应（未解决，2026-10-25 复测确认仍在）

| 项目 | 内容 |
|---|---|
| 位置 | `qml/Veyra/BilibiliPage.qml` 的搜索框 |
| 现象 | 输入关键词后按回车不搜索；点「搜索」按钮正常 |
| 引入版本 | **既存问题**，基线 v16 就有，不是后来引入的 |
| 现状 | **2026-10-25 用户复测：仍然不响应** |
| 替代方案 | 用「搜索」按钮 |

**注意**：`BilibiliPage.qml:879` 里**写着** `onAccepted: root.doSearch(true)`，
但它不触发 —— 不是忘了写，是焦点链拿不到按键（见下面证据）。改代码前先看这一节，
别以为没人接这个信号。

**诊断证据**（在 `QmlPlayerBridge.cpp` 的 eventFilter 临时加日志得到）：
```
[ui-keys] key=16777249 isWindow=true focus=QQuickRootItem
```
- `16777249` = `Qt::Key_Control`：**只收到 Ctrl 键**，打字的字母和回车都没进 eventFilter
- `focus=QQuickRootItem`：**焦点在窗口根节点，不在搜索框控件上**

**已试并失败的 4 种方案**（不要再重复）：

| # | 方案 | 结果 |
|---|---|---|
| 1 | `TextInput.onAccepted` | 不触发 |
| 2 | `Keys.onReturnPressed` + `onEnterPressed` | 不触发 |
| 3 | 改用 `Basic.TextField`（项目里 18 处在用，其它都能回车）| 不触发 |
| 4 | `Main.qml` 窗口级 `Shortcut` + `searchFieldFocused` 守卫 | 不触发（守卫判断为 false）|

**根因判断**：焦点链问题。控件拿不到 `activeFocus`，所以按键既不到控件、
也不触发任何基于焦点的通路。第 4 种方案失败正好印证——守卫读到的 `activeFocus` 就是 false。

**下一步可以试的方向**（未验证）：
1. 排查 `Window.activeFocusItem` 实际值，逐层看 `focusScope`
2. 参考 `PlaybackRateDialog.qml`：它在 `onOpened` 调 `input.forceActiveFocus()` 后回车可用。
   BilibiliPage 是常驻页面没有 `onOpened`，但可以在点击时调 `forceActiveFocus()`
   —— 需注意 `MouseArea` 会挡住 `TextField` 的文本输入，层级要摆对
3. 在 C++ `eventFilter` 里直接识别 Return 并转发（那区域历史上崩过多次，谨慎）


### 1-1. 观看进度上报的五个位置（2026-10-25 定稿）

一次观看**最多发 3 次** HTTP 请求（`/x/click-interface/web/heartbeat`）。5 个代码位置
里，后 3 个互斥 —— 都会先取走并清空 `biliWatch*`，谁先取走谁报，后到的发现记录为空
（`biliWatchLastPos < 0`）直接返回。

| # | 位置 | 触发时机 | 报的值 |
|---|---|---|---|
| A | `bilibiliPlayResolved` → `reportBiliWatchStartedAsync` | 点播放、地址解析成功 | **起播位置**：续播=续播点，从头=0 |
| B | `tickSubtitles` 检测到 `Transport::Ended` → `reportBiliWatchProgressAsync` | 播放自然结束 | **`-1`**（已看完）|
| C1 | `bilibiliPlayResolved` 开头 `takeBiliWatchProgress` → worker 内 | 点另一个 **B 站** 视频 | 上一个视频的最终位置 |
| C2 | `openPath()` → `flushBiliWatchProgress` | 打开任意新来源（本地文件等）| 同上 |
| C3 | `~QmlPlayerBridge()` → `flushBiliWatchProgress` | 程序退出 | 同上 |

**典型序列**

| 场景 | 上报 | 次数 |
|---|---|---|
| 开播 → 播完 | 起播位置 → `-1` | 2 |
| 开播 → 中途切到别的 B 站视频 | 起播位置 → 上一个的位置 | 2 |
| 开播 → 播完 → 再开一个 | 起播 → `-1` → 新起播 | 3 |

**A 必须报「实际起播位置」，不能一律报 0**：续播时报 0 会把 B 站存下的进度覆盖成
0，下次「继续上次播放」就查不到位置，只能从头开始。实现上 A 的调用位置必须在
`biliResumeAt` 算完之后（`resumeAt > 0 ? int(resumeAt) : 0`）。

**与 B 站网页播放器的差别**：网页端播放中约每 15 秒心跳一次，我们不做。所以程序被
强杀时，最后一次上报之后的位置会丢；正常退出不受影响，`openPath` 和析构会补报。

**同步 vs 异步**：A 和 B 走异步槽位 `BiliOp::ReportProgress`（因为
`media::biliReportWatchProgress` 内部是同步 HTTP POST，在 GUI tick 里直接调会卡住
界面）；C1 本来就在 worker 里；C2/C3 是同步的，但发生在打开来源和退出时，可以接受。

### 1-2. 播完视频后「历史记录」不更新（已解决 2026-10-25）

| 项目 | 内容 |
|---|---|
| 现象 | 播完一个 B 站视频，点左侧「历史记录」还是播放前那张列表，必须点右上角刷新 |
| 原因 | 推荐 / 热门 / 历史**共用一个 card cache**，`load()` 只有 `force = true` 才重新请求，而只有「刷新」按钮传 true。播放本身从不让历史那条缓存失效 |
| 修法 | **`load("history")` 无条件强制真拉**（不吃缓存）；左侧栏那一项也显式传 `force`。其它列表（推荐/热门）照旧走缓存 |
| 附带 | 从历史页点进去看的视频，播完会回到同一页而不经过左边栏，所以结束处理在 `view === "history"` 时直接重拉 |
| 走过弯路 | 先试过「播放后置脏标记、点历史时消费」的方案（`biliHistoryDirty`），但用户要的是**每次点都刷新**，标记方案在其余情况下仍显示缓存 —— 已改回无条件 force，标记已删 |

**为什么历史不能像其它列表那样缓存**：观看进度在播放中每隔几秒上报一次、看完再上报
一次（标记已看完），B 站那边的数据一直在变，跨播放缓存必然过期。

### 2. 「继续上次播放」的实现方式（已解决 2026-10-25；闪一下开头不是 BUG）

| 项目 | 内容 |
|---|---|
| 现象 | 点「继续上次播放」会先显示开头画面，再跳到目标位置，期间缓冲两轮 |
| 原因 | 作者的 `EngineController::open()` 不支持指定起播位置，只能“先打开再从 0 秒 seek” |
| **定性** | **「闪一下开头」是「先打开再 seek」的必然结果，不是缺陷**（2026-10-25 用户确认）。想消除只能改 `EngineController::open()` 支持起播位置，代价是改作者核心接口 |
| 现状 | **续播本身已修好**，能准确跳到上次位置（见下）|

**不再把「闪一下」当待办**。真正的 bug 是「点了根本不跳」，那个已于 2026-10-25 修好。

**曾尝试 4 次修复，3 次导致播放闪退**，详见 v13 章节的失败记录。
（那几次都在改 `EngineController::open()`；下面的修复没有碰引擎。）

#### 2026-10-25 修复：续播根本不生效（不是「闪一下」）

用户报「点了继续上次播放，还是从头播」。两端加日志后定性：

```
手动 seek：seek-call → seek-drain 同毫秒内     3/3 成功
续播 seek：只有 seek-call，完全没有 seek-drain  5/5 失败
```

**根因**：续播 seek 打在了**上一个视频**的快照上。点「继续上次播放」的那一刻引擎还
没建新会话，`impl_->snapshot` 里仍是旧会话的 id/position/duration（实测那次 seek 旁边
是 `pos=32.867 dur=103.533`，而正在打开的视频是 120.834 秒）。这条 seek 落在正要被
拆掉的会话上，引擎永远取不到；而 `biliResumeAt` 已被清成 -1，新会话没有可重试的东西。

**修复**：`bilibiliPlayResolved()` 记下点击时的会话号（`biliResumeAfterSession`），
tick 里要求「快照报出的会话号已经不同」才发 seek：

```cpp
const bool biliSessionIsNew = s.sessionId != 0
    && s.sessionId != impl_->biliResumeAfterSession;
if (impl_->biliResumeAt >= 0.0 && s.running && s.duration > 0 && biliSessionIsNew) { ... }
```

依据：引擎每次打开都执行 `snapshot_.sessionId = ++sessionId_` 并 `snapshot_={}`
（duration 归零），所以「会话号变了 + duration>0」就等价于「新会话已就绪」。

**验证**：两个视频来回切，三次续播全部被引擎取走，目标值精确一致
（7.000 / 3.000 / 15.000），延迟 64-450 毫秒。

**排查方法（下次遇到同类问题直接用）**：在 `seekTo()` 和引擎取走 `seekSeconds_` 的
地方各加一条日志，看 `seek-call` 和 `seek-drain` 是否配对 —— **配对不上就是没生效**，
比看界面可靠得多。


### 3. 播完后点底部播放条按钮 → 403（**已解决 2026-10-25**）

| 项目 | 内容 |
|---|---|
| 现象 | 视频播完后点底部控制条的播放按钮，报「无法连接到服务器」|
| 原因 | `togglePlayPause()` 复用已过期的 B 站 CDN 地址（带时效签名）|
| **修复** | 记住 `bvid/cid/画质` 重新解析，拿到一条新签名地址，从头播 |
| **关键点** | B 站分支必须排在 `togglePlayPause()` 里**通用网络地址分支之前** —— B 站来源也是网络地址（`liveKind=="url"`），那个分支会先拿过期地址重连 |
| 位置 | `QmlPlayerBridge::togglePlayPause()`；身份记录在 `bilibiliPlayResolved()`，清理在 `openPath()` 的 `!keepBilibiliStream` 分支 |

**早先那次「曾尝试修复，导致闪退」见 v13 章节——那是另一条思路（改 `EngineController::open()`），
与本次无关。本次只动 `togglePlayPause()` 的分派顺序，没有碰引擎。**

顺带加了「播放完成后回到视频详情」开关（偏好 `biliReturnAfterEnd`，默认关）。
默认关时播完停在播放画面，点播放按钮从头重播；打开则播完回详情页（原行为）。


### 4. B 站页内播放 + Esc 退出（已放弃）

需求是把极简的播放界面搬到 B 站页内。技术上受两个限制：

- **原生视频窗口永远画在 QML 之上** —— 放在画面范围内的 QML 按钮点不到
- **全屏状态下 Qt 的 `Shortcut` 收不到 Esc**（原因未查明）

极简页的控制条是靠**独立顶层窗口**（`FullscreenBar.qml`，`Qt.Tool | Qt.WindowDoesNotAcceptFocus`）解决的，
要在 B 站页做同样的事需要 C++ 改动。**已回滚，未实现。**


### 5. 构建注意事项（不是 bug，但极易踩）

**改 C++ 后点播放闪退** —— 已查明并解决，见 v16 章节。

根因是 `build-nv` 目录里混着跨越 20 天的旧 obj，增量编译产生 ABI 不一致的二进制。

**正确做法**：
```bash
cd D:\项目\Veyra-NV\build && ninja veyra_qml_ui     # 增量，约 10 秒
# 改头文件时：
cd D:\项目\Veyra-NV\build && ninja -t clean && ninja veyra_qml_ui   # 全量，约 1.5 分钟
```

**唯一构建目录**：`D:\项目\Veyra-NV\build`（旧的 `build-nv` 和 `veyra-cleanbuild` 已删除）。
---


### 一、现象

点详情页的「投币」按钮 → `投币失败（code=-401）：非法访问`。


### 二、已排除的原因

**用该账号的真实 Cookie 实测（Python 直连 B 站 API，不经过 Veyra）：**

| 检查项 | 结果 |
|---|---|
| 登录状态 `/x/web-interface/nav` | `isLogin: true` ✅ |
| 查询已投币数 `/x/web-interface/archive/coins` | `code 0` ✅ |
| **点赞** `/x/web-interface/archive/like` | **`code 0` ✅ 写接口正常** |
| **投币** `/x/web-interface/coin/add` | **`code -401` ❌** |

**这一组对照很关键**：同一个 Cookie、同一套请求头，**点赞成功、投币失败**。


### 三、Web 端投币的 6 种参数组合全部失败

| 测试 | 结果 |
|---|---|
| 仅 `bvid` + csrf | `-401` |
| `aid` 代替 bvid | `-401` |
| 加 `Origin: https://www.bilibili.com` | `-401` |
| 加 SPI 获取的 `buvid3` + `b_nut` 设备 cookie | `-401` |
| **newBV 的完整参数**（`cross_domain=true` / `source=web_normal` / `ga=1` / `spmid=333.788.0.0`）| `-401` |
| Referer 用首页 vs 视频页 | 都 `-401` |
| 对 3 个不同视频投币 | 全部 `-401` |


### 四、newBV 是怎么做的（关键参考）

读了 `D:\DSH\.dsh-tmp\newbv\BiliHttpApi.kt` 和 `ApiSign.kt`：

**1. newBV 投币用的也是 Web 接口**，不是 App 接口：
```kotlin
// PlayerViewModel.kt:488 —— 真正被调用的
coinRepository.sendVideoCoin(aid = aid, preferApiType = getApiType())

// sendVideoCoinApp 定义了，但整个项目零调用
```

**2. newBV 源码里有一条重要注释**（`BiliHttpApi.kt:191`）：
> 本地随机生成的 buvid3 未在 B 站注册，投币等敏感操作会返回 -401（非法访问）。

它通过两步获取已注册的 buvid3：
1. `/x/frontend/finger/spi` 拿 `b_3` / `b_4`
2. **带 buvid3 访问 `https://www.bilibili.com/`，从 `Set-Cookie` 拿 `b_nut` 等设备 cookie**

**我按这个流程做了，仍然 `-401`**（第 3 节第 4 行）。

**3. `-401` 与 WBI 签名无关**：
`encApiSign()` 只对含 `wbi` 的路径签名。`/x/web-interface/coin/add` 不含 wbi，不需要签名。


### 五、App 端接口为什么也不行

newBV 定义了 `sendVideoCoinApp(avid, multiply, like, accessKey)`，走
`https://app.bilibili.com/x/v2/view/coin/add`。

**实测（无 access_key，只有 Web Cookie）：**
```
App 接口 + Web Cookie  → -101 账号未登录
App 接口 + 无 Cookie   → -101 账号未登录
```

**App 接口只认 `access_key`，完全不认 Web Cookie。**

要用它必须实现：
1. 整套 App OAuth 登录（`app.bilibili.com/x/passport/...`，独立于 Web Cookie 登录）
2. access_key 的持久化存储与刷新
3. **仍不保证成功** —— App 接口同样有风控

**而 newBV 自己也没有走这条路**（`sendVideoCoinApp` 零调用），说明它不是捷径。


### 六、结论与建议

**投币功能在 Veyra 里不可用，原因是 B 站服务端风控该账号。**

| 方案 | 可行性 |
|---|---|
| 继续调 Web 接口参数 | ❌ 已穷尽 6 种组合 |
| 用 App 接口 | ❌ 需要完整 OAuth，cost 高且不保证 |
| **去网页端投币** | ✅ **推荐** |

**代码里已做的**：`-401` 提示改为「B 站的风控拒绝了这次操作，请到网页端完成」，
让用户知道这不是程序出错。

**如果哪天要重试**，先确认：
1. 浏览器里给视频投币是否也要求验证码？如果要 → 服务端风控，代码无法绕过
2. 换个 B 站账号测试同一段代码 → 如果那个账号能投币，则确认是账号级风控
---



---

## 二、版本演进（最新在前）

> 从 v23 倒序到 v2。每节记录当时的改动、遇到的问题和验证结果。

### 基线 v23（2026-10-24 17:30）

在 v22 基础上：移除自制刻度条，恢复作者自带的滚动条；修复最大化后不加载的问题。

### 第 27 个改动：滚动条回归作者的 VScrollBar

放弃自制刻度条（三轮尝试）：

| 尝试 | 问题 |
|---|---|
| 1. Dock 式纵向放大 | 刻度位置固定、只改尺寸，邻居重叠成一片 |
| 2. 横向伸缩并高亮当前行 | 长列表下刻度被压扁，4K 最大化时映射错乱 |
| 3. 视口高亮带 | 同上，本质是「一行一像素」的映射在长列表下不成立 |

最终方案：用作者定义在 qml/Veyra/VScrollBar.qml 的组件，
程序里所有列表（设置页、专业模式、菜单）都在用它。

特性：6px 细条、无轨道无箭头、可拖放、悬停变亮，与全程序一致。

删掉的代码约 180 行：刻度条本体、外层包装 Item，以及只为它服务的
flashRow、tileRow、totalRows、visibleRows、topRow 等属性。

### 第 28 个改动：修复最大化后不能加载也不能滚动

现象：首次进 B 站首页加载了卡片，点最大化（4K）后无法加载更多、无法滚动。

根因：

    function maybeLoadMore() {
        if (contentHeight <= height) return   // 内容不满一屏就认为不需要滚动
        ...
    }

1280 窗口下 30 张卡片能滚，但 4K 最大化后：

    grid 高度 约 560 变成 约 2000
    一页 30 张、12 列，3 行乘 200px 等于 600px 内容
    contentHeight(600) <= height(2000)，于是永远 return

而且 onHeightChanged 触发的检查也被同一条件拦住，内容不会增长，也就滚不动。

修复 1：条件改成「或」——内容不满一屏时也继续加载。

修复 2：加 listExhausted 防止死循环。
新条件会导致内容不满一屏就持续加载，接口返回空时会无限循环，
所以空批次或去重后无新增时标记到底，切栏目、刷新、成功追加时重置。

覆盖全部 5 个重置点：load 缓存命中、load 新请求、openFavFolder、doSearch、成功追加后。

#### 基线 v23 哈希

    veyra_qml_ui.exe              = EE6B14D92E7A4608   （未变，v22 的）
    veyra_qml_ui.exe.p5           = EE6B14D92E7A4608
    qml/Veyra/BilibiliPage.qml    = 155F8FC14C5839C2   （本次唯一改动）
    src/ui/QmlPlayerBridge.cpp    = 2C5106F693AECE3E

### 经验记录：视口高度变化是列表加载的隐藏依赖

凡是「内容不满一屏就认为不用滚动、于是不加载」的写法，在窗口变大后都会死锁。
正确做法：内容不满一屏时也应该继续加载，但必须配一个「到底」标志防止空转。

### 基线 v22（2026-10-24 16:30）

在 v21 基础上加 **B 站网格无限滚动**。

### 第 26 个改动：无限滚动加载

**需求**：滚到底部自动加载更多，而不是固定 30 张卡片就到底。

**C++（1 处）**：异步通道的 kind 支持尾部页码

```cpp
// "popular:2" / "search:<词>:2" / "fav:<id>:2" 取指定页；
// 推荐/历史/稍后再看 没有页码参数，直接再调一次（B 站接口本身无分页）
int page = 1;
QString base = kind;
const int lastColon = kind.lastIndexOf(QLatin1Char(':'));
if (lastColon > 0) {
    bool okPage = false;
    const int parsed = kind.mid(lastColon + 1).toInt(&okPage);
    if (okPage && parsed > 0) {
        const QString head = kind.left(lastColon);
        // detail:/qualities:/related: 带的是 bvid 或 cid，不是页码
        if (!head.startsWith("detail:") && !head.startsWith("qualities:")
            && !head.startsWith("related:")) { page = parsed; base = head; }
    }
}
```

**各接口的分页能力（实测）**：

| 接口 | 分页 | 说明 |
|---|---|---|
| `popular` | ✅ `page` | 真正的分页 |
| `search` | ✅ `page` | |
| `favorites` | ✅ `page` | |
| **`recommended`** | ❌ | 用 `fresh_type=4` 返回新批次，无页码 |
| `history` / `watchlater` | ❌ | 无页码 |

### 关键问题 1：滚到底加载后跳回顶部

**根因**：
```qml
root.cards = merged    // ← 新数组 → GridView 整体重建 → contentY 归零
```

**这是 QML 框架行为**：GridView/ListView 只要 model 被替换，就重置滚动位置。

**修法**：改用 `ListModel` 增量追加
```qml
ListModel { id: cardsModel }

function appendCardsToModel(list, startIndex) {
    for (let i = startIndex; i < list.length; ++i)
        cardsModel.append({ bvid: ..., title: ..., cover: ..., ... })
}

// 追加时只 append 新的那一截，不触碰 contentY
root.appendCardsToModel(merged, root.cards.length)
```

`cards` 数组保留作为缓存与分页数据源（双写）。

### 关键问题 2：改用 ListModel 后封面全空

**根因**：命名冲突
```qml
Rectangle {
    id: cover                      // ← 这个 id
    Image { source: cover || "" }  // ← 解析到 Rectangle，不是 ListModel 角色
}
```

**QML 里 id 的优先级高于上下文属性（ListModel 角色名）。**
之前用 `modelData.cover` 无歧义，改用角色名后就撞了。

**修法**：重命名组件 id
```qml
Rectangle { id: coverBox }         // 避开角色名 cover
y: coverBox.y + coverBox.height + 5   // 三处引用同步改
```

### 实现细节

| 项目 | 做法 |
|---|---|
| 触发阈值 | 距底部 1.5 行（提前量，避免用户看到空白）|
| 去重 | 按 `bvid`，推荐接口返回的批次可能重叠 |
| 防重复请求 | `lastRequestedPage` 挡住同一页 |
| 空批次 | 记下页码不再追问 |
| 缓存 | `cardCache[key]` + `cardCache[key + ":page"]` |
| 切栏目 | `activeListPage` 重置，滚动位置重置（正常）|

#### 基线 v22 哈希

```
veyra_qml_ui.exe              = EE6B14D92E7A4608
veyra_qml_ui.exe.p5           = EE6B14D92E7A4608
qml/Veyra/BilibiliPage.qml    = A2BAEFE193F51E87
src/ui/QmlPlayerBridge.cpp    = 2C5106F693AECE3E
```

### 经验记录：改 ListModel 后的两个坑

1. **不要整体替换数组** —— 会重置滚动位置。用 `ListModel.append()`。
2. **组件 id 不要和角色名同名** —— id 优先级更高，会静默解析错对象，
   表现为图片/文字全空，但没有报错。
---


### 基线 v21（2026-10-24 15:40）

在 v20 基础上修 **快速连点播放卡顿** + 补 **从头播放的反馈**。

### 第 24 个改动：播放请求不再堆积

**现象**：快速连点「继续上次播放」和「从头播放」，界面卡住。

**根因**：`bilibiliPlay` 里有 4 次串行同步请求，全在 GUI 线程：
```cpp
flushBiliWatchProgress()          // ① 进度上报
media::biliVideoPage(bvid)        // ② 又一次 view 请求
media::biliResolvePlay(...)       // ③ playurl 解析
media::biliResumePosition(...)    // ④ 历史查询
```
连点 3 次就是 3 秒的界面冻结。

**修法 1：防重入（RAII 守卫）**
```cpp
if (impl_->biliPlayResolving) {
    log::info("bilibili", "play request ignored: the previous one is still resolving");
    return false;
}
struct ResolveGuard {
    explicit ResolveGuard(bool* f) : flag(f) { *flag = true; }
    ~ResolveGuard() { *flag = false; }   // ← 所有 return 路径都复位
} guard(&impl_->biliPlayResolving);
```
用 RAII 而不是手动复位，异常路径也不会卡死。

**修法 2：省掉一次 view 请求**

新增 `bilibiliPlayCid(bvid, cid, qn, fromStart)`：cid 由 QML 从 `detail.parts` 带过来，
内部不再调 `biliVideoPage`。

顺带把共用尾部抽成 `bilibiliPlayResolved()`，两个入口不会漂移。

### 第 25 个改动：从头播放的反馈

**现象**：点「从头播放」后没有任何提示，不知道它生效了没。
（用户连续点多次就是这个原因）

**修法**：播放成功的提示里加来源标记：
```
继续上次播放 →  正在播放：标题  3840x2160  hevc
从头播放     →  正在播放：标题  3840x2160  hevc  （从头开始）
```

视频本来就没历史时不显示这个标记，避免误导。

### 续播按钮的设计依据（用户提问后分析）

**用户问**：点「从头播放」后，「继续上次播放」的状态该不该变？

**答：不该变。** `resumeAt` 读的是 B 站历史记录，
「你上次看到 1:00」是客观事实，重看一遍不会让它消失。

如果改成点「从头」就让主按钮变回「播放」，会有不一致：
切换分P或刷新后按钮又跳回「继续上次播放」，看起来像 bug。

**行业惯例对照**：

| 产品 | 有进度时的行为 |
|---|---|
| YouTube | 缩略图红条 + 浮层「Play from start」，点标题默认续播 |
| Netflix | 弹层两个选项「Resume」「Play from beginning」|
| B 站网页 | 显示「上次看到 03:24」，默认续播，菜单里有「从头播放」|
| Plex/Emby | 「Resume」主按钮 + 「Play from start」次要 |
| **本项目** | 主按钮「继续上次播放（1:00）」+「从头播放」次要 |

**结论：本项目设计已与大厂一致，之前唯一缺的是操作反馈，现已补上。**

#### 基线 v21 哈希

```
veyra_qml_ui.exe              = 643F9DBA4C6FB4E6
veyra_qml_ui.exe.p5           = 643F9DBA4C6FB4E6
qml/Veyra/BilibiliPage.qml    = 6C277C97A969DF6A
include/veyra/ui/QmlPlayerBridge.h = E952E1734C4F6F16
src/ui/QmlPlayerBridge.cpp    = 1AF226E360D627FD
```

### 显卡/刷新率实测（更正早期错误记录）

**早期记录错误**：曾写过「用户显示器是 120Hz，P50 延迟从 25.3ms 降到 4.9ms」。
那是误引用，与本机实际不符。

**实测**：

| 刷新率 | UI 帧率（qml-frames） | 日志证据 |
|---|---|---|
| 60Hz | 62 FPS（main=619~632/10s）| `refresh=60.000 Hz` |
| 120Hz | 82 FPS（main=819/10s） | `refresh=120.000 Hz` |

显示器支持的模式（WmiMonitorListedSupportedSourceModes 实测）：
```
3840x2160 @ 60 Hz
2288x1430 @ 61 Hz
1920x1080 @ 60 Hz
```

早期所有功能测试（异步化、自适应网格、详情页优化）都是在 60Hz 下完成的。
---


### 基线 v20（2026-10-24 15:00）

在 v19 基础上：**B 站全部网络请求异步化**。

### 第 21-23 个改动：异步加载

**为什么做**：所有 B 站接口都是同步 HTTPS 请求，跑在 GUI 线程上。
一次请求 200-800ms，期间界面完全冻结 —— 这也是骨架屏一开始看不到的原因：
`listLoading = true` 之后没有机会重绘就又被阻塞了。

**做法照搬作者现成的模式**（`tickCapture` 的 `captureQuery`）：

```cpp
// 启动：GUI 线程设 cookie，然后丢给 worker
int QmlPlayerBridge::bilibiliLoadList(const QString& kind) {
    media::setBiliCookie(media::loadBilibiliCookie());   // ← 关键：不能在 worker 里设
    const int generation = ++impl_->biliListGeneration;
    impl_->biliListQuery = std::async(std::launch::async, [kind]() {
        ListFetchResult r;
        ...  // 只读，不碰全局状态
        return r;
    });
    return generation;
}

// 收集：GUI tick 里 wait_for(0)，结果不跨线程传递活对象
void QmlPlayerBridge::tickBiliList() {
    if (!impl_->biliListBusy) return;                    // ← 空转时只有一次判断
    if (impl_->biliListQuery.wait_for(0s) != ready) return;
    auto r = impl_->biliListQuery.get();
    emit bilibiliListReady(kind, r.items, generation);
}
```

### 覆盖范围

| 请求 | kind | 返回 |
|---|---|---|
| 首页推荐 | `home` | 卡片列表 |
| 热门 | `popular` | 卡片列表 |
| 关注动态 | `follow` | 卡片列表 |
| 历史记录 | `history` | 卡片列表 |
| 稍后再看 | `watchlater` | 卡片列表 |
| 我的收藏（按文件夹）| `fav:<id>` | 卡片列表 |
| 搜索（按关键词）| `search:<词>` | 卡片列表 |
| **视频详情** | `detail:<bvid>` | **单个对象** |
| **画质列表** | `qualities:<bvid>:<cid>` | 画质列表 |
| **相关推荐** | `related:<bvid>` | 卡片列表 |

单个对象走独立信号 `bilibiliObjectReady`，列表走 `bilibiliListReady`。

### 三处安全设计

| 风险 | 处理 |
|---|---|
| **`g_cookie` 数据竞争** | `setBiliCookie` 在 GUI 线程、启动 worker **之前**调用；worker 只读 |
| **过期结果覆盖新结果** | `generation` 递增，QML 侧三套独立校验（detail / qualities / related / list）|
| **结果跨线程传递** | 通过 `std::future` 传，`.get()` 在 GUI 线程执行 |

### 顺带又省掉一次重复请求

`bilibiliQualities(bvid, pageIndex)` 内部原本会调 `biliVideoPage()` 拿 cid，
而 `detail.parts[]` 里已经有 cid。异步版直接传：`qualities:<bvid>:<cid>`。

### 详情页加载顺序（改后）

```
第 0ms    点视频 → 立刻切页 + 显示「加载中…」        （无阻塞）
异步①    detail 返回 → 标题/封面/描述/分P 填充
         └─ resumeAt（同步，轻）
         └─ 启动异步② qualities
异步②    qualities 返回 → 画质下拉可用
下一轮    detailFillTimer:
         ├─ interaction（同步，轻）
         ├─ 启动异步③ related
         └─ loadDanmaku（同步）
异步③    related 返回 → 相关推荐填充
```

**从 6 次阻塞降到只剩 2 次轻量同步调用**（`resumeAt`、`interaction`）。

#### 基线 v20 哈希

```
veyra_qml_ui.exe              = E9420258E3256BB6
veyra_qml_ui.exe.p5           = E9420258E3256BB6
qml/Veyra/BilibiliPage.qml    = A796C325AFC8961D
include/veyra/ui/QmlPlayerBridge.h = F3AD09A641D2E080
src/ui/QmlPlayerBridge.cpp    = FE601BC4E29ACFD7
src/media/BilibiliApi.cpp     = 7472EAC387166932
```

### 对非 B 站功能的影响（用户询问后核查）

| 项目 | 影响 | 证据 |
|---|---|---|
| 主 tick 循环 | **可忽略** | `tickBiliList` 空转时只做一次 bool 判断就 return |
| BilibiliPage 常驻 | 几 MB 内存 | QML 对象常驻（作者原设计），所有 Timer 有 `running` 守卫 |
| 栏目缓存 cardCache | 几百 KB | 每栏目一份卡片数组 |
| 本地播放 / 专业模式 / 设置 / 采集 / 串流 | **无** | 相关代码路径不参与 |

三个 Timer 的守卫：
```qml
detailFillTimer  : repeat false            → 只触发一次
biliPlaying Timer: running: root.biliPlaying  → 只在 B 站播放时
qrPoll          : running: session && root.current  → 只在扫码且在 B 站页
```
---


### 基线 v18（2026-10-24 13:30）

在 v17 基础上：**B 站详情页打开更快** + **投币失败提示更清楚**。

### 第 19 个改动：详情页加载优化

**问题**：点视频进详情页有明显等待感。

**根因**：`openVideo()` 里 **6 次串行的同步 HTTPS 请求**，全在 GUI 线程，界面全程无响应。

```qml
detail      = bilibiliVideoDetail(bvid)        // ① /x/web-interface/view
qualities   = bilibiliQualities(bvid, 0)       // ② playurl（可能还含 wbi）
resumeAt    = bilibiliResumeAt(bvid, 0)        // ③ 内部又请求一次 view！
interaction = bilibiliInteractionState(bvid)   // ④
related     = bilibiliRelated(bvid)            // ⑤
loadDanmaku()                                  // ⑥（弹幕开关开启时）
```

**优化后**：3 次阻塞 + 3 次延后。

| 阶段 | 内容 |
|---|---|
| 阻塞（页面必需）| `detail` / `qualities` / `resumeAt` |
| 延后（`Timer{interval:0}`）| `interaction` / `related` / `danmaku` |

**关键：`resumeAt` 不再重复请求**

新增 `bilibiliResumeAtCid(bvid, cid)`：
```cpp
double QmlPlayerBridge::bilibiliResumeAtCid(const QString& bvid, qlonglong cid) {
    // 不调 biliVideoPage，cid 由调用方从 detail.parts 里带过来
    if (cid <= 0) return -1.0;
    return media::biliResumePosition(bvid.toStdWString(), cid);
}
```

原来 `bilibiliResumeAt` 内部有 `biliVideoPage(bvid)`，是一次完整的 view 请求；
而 `detail.parts` 里**已经有 cid**，白等一次往返。
切换分 P 时同样改用 `parts[index].cid`。

> **现状（2026-10-25）**：`bilibiliResumeAtCid` 已随其它同步方法一起删除，改用异步的
> `bilibiliRequestResumeAt(bvid, cid)`，结果经 `bilibiliResumeReady` 回到 QML。
> 上面这段是当时的记录，留作背景。

**延后的安全措施**：每次延后请求后检查 `root.view !== "detail"`，
用户已离开就不再发请求。

### 第 20 个改动：投币失败提示

`checkCode()` 为 `-401` 加了专门分支：

改前：`投币失败（code=-401）：非法访问`
改后：`投币失败：B 站的风控拒绝了这次操作，请到网页端完成；同一账号的点赞、收藏不受影响`

#### 基线 v18 哈希

```
veyra_qml_ui.exe              = 852AA37585397022
veyra_qml_ui.exe.p5           = 852AA37585397022
qml/Veyra/BilibiliPage.qml    = FAC1BDFD0BCBBF0A
include/veyra/ui/QmlPlayerBridge.h = 15FEEB24289BE4F7
src/ui/QmlPlayerBridge.cpp    = D3E8B2560752A06C
src/media/BilibiliApi.cpp     = 7472EAC387166932
```

---

> **投币功能为什么不可用** → 完整调研已移到本文第一节
> 「一、未解决问题与已知限制 → 投币失败的技术分析」。
> 结论：B 站服务端对该账号的风控，不是代码缺陷；除非 B 站放松风控或改用
> App OAuth（成本高、不保证成功）。


### 基线 v17（2026-10-24 12:40）

在 v16 基础上给 B 站页加了 **栏目独立缓存**。纯 QML。

### 第 18 个改动：栏目独立缓存

**问题**：点左栏任何栏目都会重新请求 B 站，包括已经看过的栏目。

**根因**：`root.cards` 是所有栏目**共用同一个数组**。
```
进首页推荐  → cards = 推荐数据
点「热门」  → cards = 热门数据   ← 推荐数据被覆盖
点回首页推荐 → 判定无缓存 → 重新请求
```

**实现**：新增缓存表，按栏目键存储。

```qml
property var cardCache: ({})
```

| 栏目 | 缓存键 |
|---|---|
| 首页推荐 | `home` |
| 热门 | `popular` |
| 关注动态 | `follow` |
| 历史记录 | `history` |
| 稍后再看 | `watchlater` |
| **我的收藏** | `fav:<文件夹ID>`（每个收藏夹独立）|
| **搜索** | `search:<关键词>`（每次搜索独立）|

### 刷新策略

| 操作 | 行为 |
|---|---|
| 点左栏任意栏目 | **用缓存，瞬间切换** |
| 切换收藏夹文件夹 | 已看过的文件夹瞬间切换 |
| 重复搜索同一关键词 | 用缓存 |
| 点「刷新」 | 强制重载当前栏目 |
| 首次进入 B 站页 | 强制（无缓存可复用）|
| 点「搜索」按钮 | 强制（明确要求搜索）|
| 扫码登录成功 | **清空所有缓存** + 刷新（账号权限变了）|
| 退出登录 | **清空所有缓存**（旧账号数据）|

#### 基线 v17 哈希

```
veyra_qml_ui.exe              = 5E31FA77943105BF
veyra_qml_ui.exe.p5           = 5E31FA77943105BF
qml/Veyra/BilibiliPage.qml    = 7EF41232D7B9D0E7   ← 本次唯一改动
qml/Veyra/Main.qml            = FC9399240F5D785B
src/ui/QmlPlayerBridge.cpp    = 7E8A0A1B851385E9
src/media/BilibiliApi.cpp     = E9C6E6C67EDC65D3
src/media/DemuxSession.cpp    = 69E9092E1C128EDD
src/media/BilibiliResolver.cpp= 0B79E65B08E8F030
src/media/DanmakuOverlay.cpp  = EBA91C2B3FCFB48A
```

### 已知未解决问题：搜索框回车无响应（既存，非本次引入）

**现象**：在搜索框输入关键词后按回车，不执行搜索；点「搜索」按钮正常。

**基线 v16 里同样存在**（那里也是 `onAccepted: root.doSearch()`）。

**诊断证据（C++ eventFilter 加的日志）**：
```
[ui-keys] key=16777249 isWindow=true focus=QQuickRootItem
```
- `key=16777249` = `Qt::Key_Control`，只收到 Ctrl 键，打字的字母和回车**没进 eventFilter**
- `focus=QQuickRootItem`：**焦点在窗口根节点，不在搜索框**

**已尝试 4 种方案，全部失败**：
1. `TextInput.onAccepted` — 不触发
2. `Keys.onReturnPressed` / `onEnterPressed` — 不触发
3. 改用 `Basic.TextField`（项目主流控件，18 处使用）— 不触发
4. `Main.qml` 窗口级 `Shortcut` + `searchFieldFocused` 判断 — 不触发（焦点判断为 false）

**结论**：这是焦点链问题，不是某个 API 用法错误。**替代方案：用「搜索」按钮**（功能完全可用）。

要彻底解决需要排查 Qt 焦点链（`Window.activeFocusItem`、`focusScope` 层级），
或参考 `PlaybackRateDialog.qml` 的做法——它在 `onOpened` 时调 `forceActiveFocus()`，
但 BilibiliPage 是常驻页面，没有 `onOpened` 时机。
---

> **已知问题汇总** → 已移到本文第一节「一、未解决问题与已知限制」
> （按「是否有人需要再查一遍」排序：能复现、有诊断、有替代方案的排前面）。


### 基线 v16（2026-10-24 11:00）

在 v15 基础上增加 **B 站弹幕总开关**，并且 **exe 换成全量干净重建版**。

### 第 17 个改动：弹幕总开关

**需求**：B 站播放设置里加一个弹幕开关，开则新视频带弹幕，关则不带。

**C++（唯一改动，1 行）**：`setPreference` 白名单加入 `biliDanmaku`。

```cpp
static const QStringList flags{..., "biliAutoCdn", "biliDanmaku"};
```

**关键坑**：`setPreference` 用显式白名单，未列出的键走 `else { return false; }`
**静默拒绝**，不报错。所以第一次做开关时点了没反应——不是 UI 问题，是 C++ 拒绝了。

**QML（三处）**：

| 位置 | 内容 |
|---|---|
| 属性 | `readonly property bool danmakuDefault: !!veyra.preferences.biliDanmaku` |
| | `property bool danmakuOn: danmakuDefault` |
| 函数 | 新增 `loadDanmaku()` / `unloadDanmaku()`，设置开关与详情页按钮共用 |
| `openVideo` | `if (root.danmakuDefault) root.loadDanmaku()` |

**UI 布局**（用户指定的形式）：
```
弹幕  [●]
打开视频即加载弹幕；关闭后新视频不带弹幕，详情页也可以单独开关
```
标题和开关同一行（`RowLayout`），说明文字下一行（`ColumnLayout`）。

### 重大发现：C++ 改动必须全量重建（避免崩溃）

**现象**：改动 C++ 后用 `build-nv` 增量编译，点播放必崩 `0xC0000005`。
累计 5 次：`0x3AEB5D` / `0x3AED7D` / `0x3AE56D`(x2) / `0x3AE35D`，全在 2.6KB 区域内。

**排查过程**：
1. 41 个 dump 全部解析：崩溃线程全是 GUI 主线程，地址集中在同一区域
2. 发现 `build-nv` 里 obj 时间跨度 2026-10-04 ~ 2026-10-24
3. **只改一行字符串常量（白名单）也崩**，和播放路径无关 —— 证明不是我的代码问题
4. 三次构建对比（同一份源码 36/36 一致）：

| exe | 构建方式 | 大小 | 结果 |
|---|---|---|---|
| `BCB218CA` | 基线（早期构建） | 7,646,208 | ✅ |
| `E00CA2BB` | **增量重编**（2 obj） | 7,647,744 | ❌ 点播放闪退 |
| `1029509D` | 全量干净重建 | 7,646,720 | ✅ |
| `F6BDBA89` | 全量重建+弹幕开关 | — | ✅ |

**结论**：增量编译只重编 1-2 个 obj，与 340 个旧 obj 混合链接，
产生 ABI 不一致的二进制 → 访问冲突。**不是移植者的代码问题。**

### 以后改 C++ 的正确流程（重要）

```bash
# 禁止：build-nv 增量编译（会混入旧 obj）
# 正确：在干净构建目录全量重建
cd /d/DSH/.dsh-tmp/veyra-cleanbuild
ninja -t clean && ninja veyra_qml_ui       # 约 10-15 分钟，227 个编译单元
```

`veyra-cleanbuild` 是从 `build-nv` 复制的独立构建目录，
配置完全一致但产物干净，已验证可靠。

#### 基线 v16 哈希

```
veyra_qml_ui.exe                  = F6BDBA8954C28D4A   ← 全量重建，可复现
veyra_qml_ui.exe.p5               = F6BDBA8954C28D4A
qml/Veyra/BilibiliPage.qml        = 08EF8942F909A20C
qml.p5/Veyra/BilibiliPage.qml     = 08EF8942F909A20C
src/ui/QmlPlayerBridge.cpp        = 7E8A0A1B851385E9   ← 白名单加 biliDanmaku
src/media/BilibiliApi.cpp         = E9C6E6C67EDC65D3
src/media/DemuxSession.cpp        = 69E9092E1C128EDD
src/media/BilibiliResolver.cpp    = 0B79E65B08E8F030
src/media/DanmakuOverlay.cpp      = EBA91C2B3FCFB48A
qml/Veyra/Main.qml                = FC9399240F5D785B
```

### 备份

旧基线 exe（可用但不可复现）备份在：
```
D:\DSH\.dsh-tmp\bili-audit2\baseline-exe-backup.exe = BCB218CAC229A3A7
```

### v16 累计功能

```
17 个修复/功能：
播放核心  B站能播 / 断网恢复 / CDN重试守卫 / 播完切回详情页
B站功能   收藏夹 / 稍后再看 / b23短链 / 画质分P / 懒加载 / 扫码登录
          续播（读B站历史）/ 动态播放按钮 / 观看历史上报 / 弹幕总开关
安全      Cookie DPAPI 加密
性能      弹幕内存泄漏 / 日志刷屏
界面      全屏排除bili页 / ComboBox真绑定 / UI风格统一 / 5列自适应网格
```
### 基线 v16 修订（2026-10-24 11:50）

**exe 更新为 `5E31FA77`**（用户测试通过）。

原因：v16 初次固化时用的是 `F6BDBA89`，它来自 `.dsh-tmp\veyra-cleanbuild`。
现在构建目录已统一为 `D:\项目\Veyra-NV\build`，重新全量构建后部署，用户验证可用。

```
veyra_qml_ui.exe    = 5E31FA77943105BF   ← 
veyra_qml_ui.exe.p5 = 5E31FA77943105BF
qml/Veyra/BilibiliPage.qml = 08EF8942F909A20C  （未变）
```

### 构建目录已统一（重要）

删除了两个冗余构建目录：

| 目录 | 大小 | 原因 |
|---|---|---|
| `D:\项目\Veyra-NV\build-nv` | **1,000 MB** | obj 时间跨度 10-04~10-24（混了 20 天），**是 5 次播放崩溃的根源** |
| `D:\DSH\.dsh-tmp\veyra-cleanbuild` | **391 MB** | 临时位置，已被新构建目录取代 |

现在**唯一构建目录**：
```
D:\项目\Veyra-NV\build        320 MB
  132 个 obj，全部来自同一次全量构建
  build.ninja 无旧路径残留
  CMakeCache 指向自身
```

### 实测构建耗时（修正之前的错误说法）

之前 README 写「全量 10-15 分钟」是**错的**，实测：

| 场景 | 命令 | 实测耗时 |
|---|---|---|
| 无改动（只复制 QML）| `ninja veyra_qml_ui` | **0.46 秒** |
| 改一个 .cpp | `ninja veyra_qml_ui` | **9.5 秒** |
| 全量（227 编译单元）| `ninja -t clean && ninja veyra_qml_ui` | **1 分 24 秒** |

**结论**：增量编译可以放心用（10 秒级），只有改头文件时才需要全量（1.5 分钟）。

### 正确的工作流

```bash
# 只改 QML（无需编译）
cp qml/Veyra/*.qml <部署包>/qml/Veyra/
cp qml/Veyra/*.qml <部署包>/qml.p5/Veyra/

# 改 .cpp（增量，9.5 秒）
cd D:\项目\Veyra-NV\build && ninja veyra_qml_ui

# 改 .h 或不确定（全量，1.5 分钟）
cd D:\项目\Veyra-NV\build && ninja -t clean && ninja veyra_qml_ui
```

### 部署必须四个位置都写

```
veyra_qml_ui.exe      ← 主 exe
veyra_qml_ui.exe.p5   ← P5 版（启动脚本会用它覆盖主 exe）
qml/Veyra/*.qml
qml.p5/Veyra/*.qml    ← 启动脚本 /MIR 会用它覆盖 qml/
```

**用户请用 `启动-P5修复版.cmd` 启动**：它会替换 P5 着色器、同步 qml.p5、带正确的 `--data-dir`。

### 磁盘清理记录（2026-10-24）

| 批次 | 内容 | 释放 |
|---|---|---|
| 临时目录 | 14 个（dsh-im-sandbox 627MB、bili-verify 202MB 等）| 1,034 MB |
| 日志/dump | 77 个文件 | 123 MB |
| 构建目录 | build-nv + cleanbuild | 1,391 MB |
| **合计** | | **约 2,548 MB** |

**全部走回收站**，可从回收站恢复。

保留的目录：
```
D:\项目\Veyra-NV\build           320 MB   唯一构建目录
D:\DSH\.dsh-tmp\veyra-baseline    19 MB   基线
D:\DSH\.dsh-tmp\bili-audit2        9 MB   含基线 exe 备份
D:\DSH\.dsh-tmp\newbv              1 MB   newBV 参考实现
```
---


### 基线 v15（2026-10-24 00:20）

在 v14 基础上把 B 站页的**原生控件换成作者设计系统组件**。纯 QML。

### 第 16 个修复：B 站页 UI 风格统一

**问题**：B 站页用了 Qt 原生 `Button` / `ComboBox`，它们跟随系统主题显示为
**白底深字**，和 Veyra 深色风格完全不搭。用户截图指出。

**改动**：

| 原来 | 换成 | 数量 |
|---|---|---|
| `Button` | `VButton` | 16 处 |
| `ComboBox` | `VSelect` | 2 处 |

**按钮清单**：
- 左侧栏：退出/扫码登录
- 顶部：搜索、刷新
- B站设置：− 加 / − 减（`implicitWidth: 34` 窄按钮）
- 详情页：← 返回
- 详情页：点赞、投币、收藏、稍后再看、查看评论
- 收藏夹选择：收藏（`primary: true`）、取消（`ghost: true`）
- 播放行：播放/继续上次播放（`primary: true`）、从头播放、弹幕开关

**下拉框清单**：
- 分P：`value` 从 `selectedPart` 派生
- 画质：`value` 从 `selectedQuality` 派生

**顺带的逻辑简化**：
`VSelect` 用 `value` + `options`（普通绑定），不再需要原来的
`currentIndex = Qt.binding(...)` 手动重绑定，因为 `value` 每次求值都读最新的
`selectedPart` / `selectedQuality`，本身就不会失效。

**用户验收**：通过（截图确认）。

#### 基线 v15 哈希

```
veyra_qml_ui.exe                  = BCB218CAC229A3A7   (未重新编译)
veyra_qml_ui.exe.p5               = BCB218CAC229A3A7
qml/Veyra/BilibiliPage.qml        = FB419776F994E009   ← 本次唯一改动
qml/Veyra/Main.qml                = FC9399240F5D785B
src/ui/QmlPlayerBridge.cpp        = C3142FF70CD12755
src/media/BilibiliApi.cpp         = E9C6E6C67EDC65D3
src/media/DemuxSession.cpp        = 69E9092E1C128EDD
src/media/BilibiliResolver.cpp    = 0B79E65B08E8F030
src/media/DanmakuOverlay.cpp      = EBA91C2B3FCFB48A
```

### v15 累计功能状态

```
16 个修复 + 3 个新功能：

播放核心  B站能播 / 断网恢复 / CDN重试守卫 / 播完切回详情页
B站功能   收藏夹 / 稍后再看 / b23短链 / 画质分P / 懒加载 / 扫码登录
          续播（读B站历史）/ 动态播放按钮 / 观看历史上报
安全      Cookie DPAPI 加密
性能      弹幕内存泄漏 / 日志刷屏
界面      全屏排除bili页 / ComboBox真绑定 / UI风格统一
```

### 仍存在的已知限制

| 问题 | 替代方案 |
|---|---|
| 续播闪一下开头 + 缓冲两次 | 无（`EngineController::open` 不支持指定起播位置）|
| 播完点播放条按钮会 403 | 回 B 站页重新点播放 / 用「从头播放」|
| 播完切页只能检测自然结束 | 主动停止需手动切回 |
| B 站页内播放 + Esc 退出 | **已放弃**，全屏下 Qt Shortcut 收不到 Esc |
---


### 基线 v14（2026-10-23 23:58）

在 v13 基础上增加 **B 站视频播完自动切回详情页**。

### 第 15 个修复：播完切回详情页

**需求**：既然点播放能自动跳到极简页，播完也应该自动切回 B 站页。

**实现方式：纯 QML，零 C++ 改动。**

原因是 C++ 侧 `openPath` 附近四次改动三次闪退（见下文失败记录），
改用 QML 能读到的 `veyra.position` / `veyra.duration` 自行检测。

```qml
property bool biliPlaying: false
property real lastSeenPosition: 0

function playSelected(fromStart) {
    ...
    root.biliPlaying = true
    root.lastSeenPosition = 0
    veyra.bilibiliPlay(...)
}

Timer {
    interval: 500
    repeat: true
    running: root.biliPlaying
    onTriggered: {
        const pos = veyra.position
        const dur = veyra.duration
        if (dur <= 0) return
        if (pos > 0.1) root.lastSeenPosition = pos
        if (root.lastSeenPosition > 1 && pos >= dur - 0.4) {
            root.biliPlaying = false
            root.requestPage("bili")
        }
    }
}
```

**关键设计**：
- `lastSeenPosition > 1` 保证真正播放过才判定结束（刚打开时 position=0、duration 未定）
- `pos >= dur - 0.4` 判定播完
- 触发后立刻置 `biliPlaying = false`，Timer 停止，只切一次
- `requestPage("bili")` 是现成信号（`Main.qml:316` 已接 `onRequestPage: p => root.goPage(p)`）

**用户验收**：通过。

**已知限制**：只能检测「播到结尾」，无法检测「用户主动停止」（需要 C++ 信号）。

#### 基线 v14 哈希

```
veyra_qml_ui.exe                  = BCB218CAC229A3A7   (与 v13 相同，未重新编译)
veyra_qml_ui.exe.p5               = BCB218CAC229A3A7
qml/Veyra/BilibiliPage.qml        = F31C2014F8BDE03B   ← 本次唯一改动
qml/Veyra/Main.qml                = FC9399240F5D785B
src/ui/QmlPlayerBridge.cpp        = C3142FF70CD12755
src/media/BilibiliApi.cpp         = E9C6E6C67EDC65D3
src/media/DemuxSession.cpp        = 69E9092E1C128EDD
src/media/BilibiliResolver.cpp    = 0B79E65B08E8F030
src/media/DanmakuOverlay.cpp      = EBA91C2B3FCFB48A
```

### 失败记录：放弃的「B 站页内播屏 + Esc 退出」方案

**用户需求**：点播放后留在 B 站页播放，把极简的控制栏搬过来，Esc 退出。

**尝试过程**：
1. 改 `Main.qml` 的 `videoRect`，让 bili 页返回真实画面区 → 部分成功（画面出现在右侧）
2. 加全宽覆盖 + `Shortcut` Esc 退出 → **Esc 完全不生效**
3. 排查：`playingVideo` 标志确实为 true（日志证明）、两个 Esc Shortcut 条件互斥、
   `FullscreenBar` 声明 `Qt.WindowDoesNotAcceptFocus`、视频窗是 `WS_CHILD` 子窗口
4. 结论：**全屏状态下 Qt 的 `Shortcut` 收不到 Esc**，原因未查明

**已回滚**。要用这个功能必须改用 C++ 的 `eventFilter`（`QmlPlayerBridge.cpp:8049`），
但那正是崩溃高发区。

**技术约束记录（重要）**：
- 原生视频窗口（HWND）永远画在 QML 之上，**放在画面范围内的 QML 按钮点不到**
- 极简页的控制条靠**独立顶层窗口**（`FullscreenBar.qml`，`Qt.Tool | Qt.WindowDoesNotAcceptFocus`）解决
- 要在 B 站页做播放控件，需要同样开独立窗口，属 C++ 改动
---


### 基线 v13（2026-10-23 22:21）

在 v12 基础上增加 **B 站续播** 与 **动态播放按钮**。

### 第 13 个修复：B 站视频续播

**问题**：Veyra 播 B 站视频永远从 0 秒开始，不会接着上次看。

**原因**：`rememberPosition()` 有个 `QFileInfo(file).isFile()` 判断，只记录本地文件；
B 站播的是 CDN 网络地址，`isFile()` 返回 false，所以从不记录位置。

**实现**（跨端同步，读 B 站自己的历史）：

| 位置 | 作用 |
|---|---|
| `BilibiliApi.cpp` 新增 `biliResumePosition()` | 查 `/x/web-interface/history/cursor`，用 bvid + cid 匹配 |
| `bilibiliPlay()` | 打开前查一次，结果存 `biliResumeAt` |
| `tickSubtitles()` | 等 session 真正 running 后 `seekTo(at)` |

**判定规则**：
- 不在历史里（没看过）→ -1，从头播
- `progress <= 0`（打开但没看）→ -1
- `progress >= duration - 2`（看完了）→ -1
- 否则 → 返回 progress（秒）

**用 cid 而非 bvid 匹配**：多分 P 视频每个分 P 进度不同，不能混用。

### 第 14 个修复：动态播放按钮

详情页原来的按钮叫「播放（使用 Veyra 增强管线）」，用户看不出它和「从头播放」的区别。

**改为**：

| 情况 | 按钮文字 |
|---|---|
| 有历史进度 | 继续上次播放（1:00）|
| 没有 | 播放 |

**实现**：
- 新增 `Q_INVOKABLE double bilibiliResumeAt(bvid, pageIndex)`，QML 加载详情时查一次
- 切换分 P 时重新查（每个分 P 进度不同）
- 新增 `formatSeconds()` 把秒数格式化成 `m:ss`

#### 基线 v13 哈希

```
veyra_qml_ui.exe              = BCB218CAC229A3A7
veyra_qml_ui.exe.p5           = BCB218CAC229A3A7
src/ui/QmlPlayerBridge.cpp    = C3142FF70CD12755
src/media/BilibiliApi.cpp     = E9C6E6C67EDC65D3
include/veyra/media/BilibiliApi.h      = 89EDC1BE8C0D1BD8
include/veyra/ui/QmlPlayerBridge.h     = 916B057FFD55B5DE
qml/Veyra/BilibiliPage.qml    = 00CBF75117558637
qml/Veyra/Main.qml            = EEB0C08D9804A7F2
src/media/DemuxSession.cpp    = 69E9092E1C128EDD
src/media/BilibiliResolver.cpp= 0B79E65B08E8F030
src/media/DanmakuOverlay.cpp  = EBA91C2B3FCFB48A
```

### v13 的已知缺陷（用户实测发现）

**续播时缓冲两次。**

日志时序（以起播 26 秒为例）：
```
14:26:01.028  net-session opened        ← 从 0 秒打开连接
14:26:01.090  prebuffering -> playing    ← 第 1 次缓冲（0 秒处）
14:26:03.311  seek to 26000 ms           ← 2.2 秒后才 seek
14:26:03.722  prebuffering -> playing    ← 第 2 次缓冲（目标处）
```

**原因**：作者原有的 `EngineController::open()` 不支持指定起播位置，
必须先打开（从 0 缓冲），才能 seek（再缓冲）。

**尝试修复失败记录（重要，勿重蹈）**：
1. 第一次：改 `PlayerOptions` 加 `startSeconds` 传进 `seekSeconds_` → 用户实测缓冲两次仍在
2. 第二次：贯穿 4 层（EngineController → SourceOpenDesc → MediaFileSource → DemuxSession）
   在 `openContext` 后直接 `av_seek_frame` → **用户点播放直接闪退**
3. 第三次：改用作者 `resumeSession` 机制 + 本地 `positions` map（bili:bvid:cid 为 key）
   → **闪退**
4. 第四次：只改 `bilibiliPlay` 本地优先读取 + `flushBiliWatchProgress` 存本地 → **闪退**

**四次尝试中三次闪退，崩溃地址都在 `0x3AE5xx~0x3AEDxx`（2KB 内），
形态相同：`[bilibili] play` 成功 → `[open] headers=` → 0.37 秒后崩溃。**

无 pdb/cdb 无法符号化定位。**结论：这个区域存在未理解的问题，暂停尝试。**

### 播完后点播放条按钮 → 403（**已于 2026-10-25 修复**）

**根因**：

`togglePlayPause()` 里：
```cpp
if (liveKind == "url" && (transport == Ended || ...)) {
    openNetworkAddress(sourceLabel);   // 用旧 CDN 地址重开 → 地址已过期 → 403
    return;
}
```

B 站播放时 `liveKind = "url"`，所以播完点播放会复用过期地址。B 站 CDN 地址带时效签名，
过期返回 403 Forbidden。

**当初为什么难修**：这条分支在 `togglePlayPause()` 靠前的位置，直接 `return` 掉了，
所以任何加在它后面的 B 站判断都到不了。而且加在别处的尝试（改 `EngineController::open()`
支持起播位置）曾导致闪退。

**最终解法**：把 B 站判断**放到这条分支之前**，并按记住的 `bvid/cid/画质` 重新解析。

```cpp
// 函数开头，所有网络地址处理之前
if (biliSourceActive && !biliReplayBvid.isEmpty() && biliReplayCid > 0
    && (s.transport == T::Ended || !s.running)) {
    bilibiliPlayCid(biliReplayBvid, biliReplayCid, biliReplayQuality, /*fromStart=*/true);
    return;
}
```

从头播（`fromStart=true`）：播完时位置在结尾，沿用续播位置会立刻又结束。

**没有碰引擎**，只调整了 `togglePlayPause()` 的分派顺序，所以不存在当年那个闪退风险。

### 已知但接受的行为

- 全屏退出后窗口位置会变（作者原设计，用户决定保留）
- 播完重播需走上面两条替代路径
---


### 基线 v12（2026-10-23 22:00）

累计 12 个修复。新增：B 站观看历史上报。

### 第 12 个修复：观看历史上报

背景：Veyra 播放 B 站视频后，B 站历史记录不会更新——因为从未上报。
newBV 的做法是每 15 秒心跳一次；这里做成简化版（用户选择），
只在切换/打开/关闭时上报，避免每 15 秒一次的界面卡顿（B站请求在 GUI 线程同步执行）。

实现：

| 位置 | 作用 |
|---|---|
| tickSubtitles() 每 16ms | 播放中持续缓存 biliWatchLastPos / biliWatchDuration |
| bilibiliPlay() 开头 | 上报上一个视频的进度 |
| openPath() 开头 | 上报（覆盖本地文件/URL 切换）|
| ~QmlPlayerBridge() | 关闭程序时上报 |
| BilibiliApi.cpp 新增 | biliReportWatchProgress() -> POST /x/click-interface/web/heartbeat |

关键设计：
- 播到结尾（position >= duration - 1）上报 -1，B站标记为「已看完」（照搬 newBV）
- 缓存播放位置而非读实时 snapshot：flush 时引擎可能已拆 session，实时读不可靠
- 上报后清空状态，不会重复上报
- 失败静默（log::warn），绝不影响播放

端到端验证（真实数据）：
```
日志:  play BV1xbRhBKEfn -> watch progress reported played_time=13
       play BV1jfp76KEwB -> watch progress reported played_time=6
B站历史:
  BV1jfp76KEwB  progress=6     <- 与日志一致 OK
  BV1xbRhBKEfn  progress=13    <- 与日志一致 OK
```

踩过的坑（重要）：第一版用 biliWatchReported 标记防重复上报，但设 true 后无处重置，
导致从第二次播放起全部跳过。日志显示播了 5 个视频、0 条上报。
第二版改用「缓存位置 + 上报后清空状态」，才正确。

#### 基线 v12 哈希

```
veyra_qml_ui.exe              = 07D8F3BADE361E60
veyra_qml_ui.exe.p5           = 07D8F3BADE361E60
src/ui/QmlPlayerBridge.cpp    = B3B57616A243092C
src/media/BilibiliApi.cpp     = B3BD794131E014C8
qml/Veyra/Main.qml            = EEB0C08D9804A7F2
qml/Veyra/BilibiliPage.qml    = 6479A46EF1FC424C
src/media/DemuxSession.cpp    = 69E9092E1C128EDD
src/media/BilibiliResolver.cpp= 0B79E65B08E8F030
src/media/DanmakuOverlay.cpp  = EBA91C2B3FCFB48A
```

### 用户 cookie 状态

已 DPAPI 加密生效：662 字节，前缀 VBCK，密文中无 SESSDATA 明文。
（用户重新登录触发了加密写入）

### 简化版上报的已知限制

- 播放中不实时同步：只有切换/关闭时才上报
- 看一半直接关浏览器页（不关程序）-> 进度不更新
- 若需要实时同步，需加 15 秒 QTimer，但会造成周期性卡顿
---


### 基线 v10（2026-10-23 21:05）

累计 **10 个修复**。用户实测确认：B站视频正常播放、弹幕正常、全屏可操作、
画质显示正确、扫码切页不卡、启动不白发请求。

### 全部修复清单

| # | 位置 | 内容 | 用户可见 | 验证方式 |
|---|---|---|---|---|
| 1 | `Main.qml:172` | 离开 bili 页时 `videoHost.syncRect()` | ✅ **B站视频能播** | 用户实测 + 日志 `pre-open: 1280x720` |
| 2 | `DanmakuOverlay.cpp:238` | `draw` 开头 `active_.clear()` | ❌ 省内存 204MB/h→0 | 独立测试：3.41MB→0 |
| 3 | `QmlPlayerBridge.cpp:1609` | CDN 重试加 `biliOpenGen` 校验 | ❌ 消除时序隐患 | 独立测试：被顶掉→正确保留 |
| 4 | `BilibiliApi.cpp:587` | 删除冗余 owner 覆盖 | ❌ 隐患清理 | 真实数据：40 条零差异 |
| 5 | `Main.qml:231` | 全屏分支排除 `bili` | ✅ **全屏后列表可操作** | 用户实测 + 7 页面穷举 |
| 6 | `BilibiliPage.qml:1090` | 画质/分P ComboBox 加真绑定 | ✅ **显示与实际一致** | 用户实测(15分P) + Qt 断言 |
| 7 | `BilibiliPage.qml:59` | 启动改懒加载 | ✅ 启动不发请求 | 日志：启动请求数 0 |
| 8 | `BilibiliPage.qml:361` | 扫码轮询 `running: session && root.current` | ✅ 切页不卡 | 用户实测 + Qt 5 断言 |
| 9 | `BilibiliApi.cpp:587` | （同 4）| | |
| 10 | `DemuxSession.cpp:1366/1287` | `failed_` 在 recover/seek 成功时清除 | ⚠️ 需断网测，用户未测 | 独立测试 6 断言全过 |

#### 基线 v10 哈希

```
veyra_qml_ui.exe       = 264D6A72A55CF949
veyra_qml_ui.exe.p5    = 264D6A72A55CF949
qml/Veyra/Main.qml     = EEB0C08D9804A7F2
qml/Veyra/BilibiliPage.qml = 6479A46EF1FC424C
src/media/DemuxSession.cpp = 69E9092E1C128EDD
src/media/DanmakuOverlay.cpp = EBA91C2B3FCFB48A
src/ui/QmlPlayerBridge.cpp = C8CBFCFF0414A2E9
src/media/BilibiliApi.cpp = 3815BEC215AA192F
```

### 明确保留的作者设定（勿改）

1. **全屏退出后窗口位置变到上方** — `toggleFullscreen()` 只恢复 `visibility`，
   不记录 x/y。这是作者原设计，用户 2026-10-23 决定保留。
2. **`seek()` 在 `failed_ && !autoReconnect` 时直接返回 false** — 关闭自动重连时
    seek 也不偷偷重连，符合设置语义。第 10 个修复特意保留了这条。

### 尚未修复（按剩余价值排序）

| 问题 | 位置 | 影响 | 风险 |
|---|---|---|---|
| `bandwidthLow` 闸门失效 | `DemuxSession.cpp:611` | 弱网保护不触发（<0.33×码率）| 🔴 高（网络核心）|
| 同步网络阻塞 UI | `QmlPlayerBridge.cpp` bilibili* | 打开视频几十ms~数秒 | 🔴 高（架构）|
| Cookie 明文落盘 | `BilibiliResolver.cpp:239` | 安全问题 | 🟡 中（需重新扫码）|
| 弹幕 LRU 一次砍半预算 | `DanmakuOverlay.cpp:101` | 4K+200%字号时周期性掉帧 | 🟡 中 |
| 弹幕 2 秒前跳阈值过小 | `DanmakuOverlay.cpp:226` | UI 停顿>2s 被误判 seek，弹幕重排 | 🟢 低 |
| `biliRelated` 可用性 | — | 已修（第 4 项）| |

### 本轮审计驳掉的误报（重要，避免重复踩坑）

| 声称 | 实际 |
|---|---|
| P0：`std::format(L"...")` 编译错误 | 原文是窄字符串，编译通过 |
| P1：Cookie 读盘 4096 字节截断 | `while(ReadFile(...))` 循环读到 EOF，实测 18088 字节完整 |
| P1：`csrfForm` 空 token 返回 `csrf=` | 有 `if (token.empty()) return {};` |
| P1：`liveBackCap_` 永久放大 | 子代理自己撤回（进 Playing 即清零）|
| P2：`capacity`/`videoStarved_` | 子代理自己撤回（设计取舍）|

### 回滚方法（随时可用，已实测 3 次）

```bash
cd /d/DSH/.dsh-tmp/veyra-baseline
python restore.py          # 只看哪些不同
python restore.py --apply  # 执行恢复（不需要重新编译）
```

---


### 基线 v6（2026-10-23 20:35）

包含 5 个已验证的修复。用户实测确认：B 站视频正常播放、弹幕正常、无卡顿、
全屏后能正常看到 B 站列表并操作。

### 五个修复

| # | 位置 | 内容 | 用户可见 |
|---|---|---|---|
| 1 | `Main.qml:172` | 离开 bili 页时 `videoHost.syncRect()` | **能看见**：B站视频从 B 站页直接播放 |
| 2 | `DanmakuOverlay.cpp:238` | `draw` 开头 `active_.clear()` | 看不到（省内存 204MB/h → 0）|
| 3 | `QmlPlayerBridge.cpp:1609` | CDN 重试加 `biliOpenGen` 校验 | 看不到（消除时序隐患）|
| 4 | `BilibiliApi.cpp:587` | 删除冗余的 owner 覆盖行 | 看不到（隐患清理）|
| 5 | `Main.qml:231` | 全屏分支排除 `bili` 页 | **能看见**：全屏后 B站列表可正常操作 |

### 修复 5 的穷举验证（1920x1080 全屏）

| 页面 | 修复前 | 修复后 |
|---|---|---|
| **bili** | **1920x1080（盖满屏幕）** | **0x0（隐藏画面）** |
| home / set | 0x0 | 0x0 |
| min / pro / node / exp | 1920x1080 | 1920x1080 |

只有 bili 页变化，其余六个页面行为完全一致。

### 明确保留的作者设定（非缺陷，勿改）

**全屏退出后窗口位置会变到上方，不是原来位置。**

- 原因：`Main.qml:494-506` 的 `toggleFullscreen()` 只保存/恢复 `visibility`
  （`fullRestoreVisibility`），**从不记录窗口的 x/y 坐标**。
- 这是作者原设计，不是修 bug 引入的。
- 全部页面都这样（min/pro/node/exp 一样），不限于 bili 页。
- **用户 2026-10-23 明确决定：跟随作者设定，不修。**
- 如果将来要修：需要在进全屏前记录 `x`/`y`（可能含 width/height），
  退出时用 `Qt.callLater` 延迟一帧还原（Windows 上 `visibility` 切换与坐标设置有
  时序竞争，立刻设坐标可能被系统覆盖）。

#### 基线 v6 哈希

```
veyra_qml_ui.exe       = 6327EFAF9E6C31C0
veyra_qml_ui.exe.p5    = 6327EFAF9E6C31C0
qml/Veyra/Main.qml     = EEB0C08D9804A7F2
qml.p5/Veyra/Main.qml  = EEB0C08D9804A7F2
DanmakuOverlay.cpp     = EBA91C2B3FCFB48A
QmlPlayerBridge.cpp    = C8CBFCFF0414A2E9
BilibiliApi.cpp        = 3815BEC215AA192F
```

### 尚未修复的已知问题（按影响排序，供后续参考）

| 问题 | 位置 | 影响 |
|---|---|---|
| 同步网络阻塞 UI | `QmlPlayerBridge.cpp` bilibili* 全体 | 打开视频 1.5-6 秒卡顿 |
| 启动即发 3 次请求 | `BilibiliPage.qml:59-62` | 推迟首帧 |
| 扫码轮询切页不停 | `BilibiliPage.qml:344` | 每秒阻塞，最长 180 秒 |
| 画质下拉框显示与实际不符 | `BilibiliPage.qml:1086` | 换分P后显示不一致 |
| 全屏退出窗口位置变化 | `Main.qml:494` | **作者设定，已决定不修** |
| `bandwidthLow` 闸门失效 | `DemuxSession.cpp:611` | 弱网保护不触发（<0.33×码率）|
| `failed_` 永不复位 | `DemuxSession.cpp:865` | 长断网后 seek 救不回 |
| Cookie 明文落盘 | `BilibiliResolver.cpp:239` | 安全问题（需重新扫码）|

---


### 基线 v5（2026-10-23 20:20）

包含 4 个已验证的修复。用户实测确认：B 站视频正常播放、弹幕正常、无卡顿。

### 四个修复

| # | 位置 | 内容 | 可见效果 | 验证依据 |
|---|---|---|---|---|
| 1 | `qml/Veyra/Main.qml:172` | 离开 bili 页时 `videoHost.syncRect()` | **能看见**：B站视频从 B 站页直接播放 | 日志 `pre-open: 1280x720`（修复前 `0x0` + 报错）|
| 2 | `src/media/DanmakuOverlay.cpp:238` | `draw` 开头 `active_.clear()` | 看不到（省内存）| 独立测试：1分钟 3.41MB → 0 |
| 3 | `src/ui/QmlPlayerBridge.cpp:1609` | CDN 重试加 `biliOpenGen` 校验 | 看不到（消除时序隐患）| 独立测试：被顶掉 → 正确保留新源 |
| 4 | `src/media/BilibiliApi.cpp:587` | 删除冗余的 owner 覆盖行 | 看不到（隐患清理）| 真实数据：40 条零差异 |

### 修复 1 的根因（重要）

`bili` 页的 `videoRect()` 返回 `{width:0, height:0}`（该页是浏览器不是播放器）。
`openPath` 里 `emit navigate("min")` 是同步信号，但切页动画走 `showTimer` 延迟 150ms 才 `syncRect`。
`preOpen()` 在同一栈里立刻执行 → 读到 `0x0` → `main.cpp:1051` 报 `no valid client rectangle` 并 return
→ **原生视频窗口没有被摆放，播放失败**。

修法：把 `bili` 加入立即 `syncRect` 的页面列表（和 `home`/`set` 同类，都是没有画面的页）。

### 修复 3 的背景

原代码 `QTimer::singleShot(150, ...)` 的重试 lambda 无任何校验。
150ms 内换片源 → 重试把新源顶掉，且 `openPath` 已清空 `pendingHeaders` → 用无 Referer 的地址必 403。
修法：`openPath`/`openSourceUri` 递增 `biliOpenGen`，lambda 内比对，不一致即放弃。

### 基线哈希

```
veyra_qml_ui.exe       = 6327EFAF9E6C31C0
veyra_qml_ui.exe.p5    = 6327EFAF9E6C31C0
qml/Veyra/Main.qml     = A5F6305FC0029B9E
qml.p5/Veyra/Main.qml  = A5F6305FC0029B9E
DanmakuOverlay.cpp     = EBA91C2B3FCFB48A
QmlPlayerBridge.cpp    = C8CBFCFF0414A2E9
BilibiliApi.cpp        = 3815BEC215AA192F
```

### 回滚能力再次实测（v5）

1. 往 `BilibiliApi.cpp` 追加垃圾行 → md5 `3815bec2...` → `3b7bf470...`
2. `python restore.py` 检出「需恢复 1 个文件」
3. `python restore.py --apply` 恢复
4. md5 回到 `3815BEC215AA192F`，垃圾行消失 ✅

### 尚未修复的已知问题（按影响排序）

| 问题 | 位置 | 影响 |
|---|---|---|
| 同步网络阻塞 UI | `QmlPlayerBridge.cpp` bilibili* 全体 | 打开视频 1.5-6 秒卡顿，慢网更久 |
| 启动即发 3 次请求 | `BilibiliPage.qml:59-62` | 推迟首帧 |
| 扫码轮询切页不停 | `BilibiliPage.qml:344` | 每秒阻塞，最长 180 秒 |
| 全屏时 B 站页视频窗盖满屏幕 | `Main.qml:231` | 按 F11 后列表看不见 |
| 画质下拉框显示与实际不符 | `BilibiliPage.qml:1086` | 换分P后显示与实际不一致 |
| `bandwidthLow` 闸门失效 | `DemuxSession.cpp:611` | 弱网保护不触发（0.33×码率以下）|
| `failed_` 永不复位 | `DemuxSession.cpp:865` | 长断网后 seek 也救不回来 |
| Cookie 明文落盘 | `BilibiliResolver.cpp:239` | 安全问题（需重新扫码才修）|

---


### 基线 v2 更新（2026-10-23 19:15）

把 **`dovi-el-gpu` 日志刷屏修复**并入基线。

### 修复内容

`src/pipeline/EnhanceGraph.cpp:2528` 原判断条件有缺陷：

```cpp
if (p7BoundCount <= 3 || ...)   // p7BoundCount 恒为 0 → 恒为真 → 每帧打印
```

普通 B 站 HEVC 片源不满足 `p7Ready_ && doviElFrame_ && uploadElLuma()` 三条件，
`p7Bound` 恒为 false，计数器永远是 0，于是**每帧都写一行**
`[dovi-el-gpu] bound=0 frames=0 ready=0 el=0`。

修法：加 `p7Bound &&` 前置条件，只在真正绑定增强层时才打印。

### 实测效果

| 指标 | 修复前 | 修复后 |
|---|---|---|
| `dovi-el-gpu` 条数 | 5432（占 64%） | **0** |
| 日志总行数 | 8491 | **918** |
| 日志大小 | 1.29 MB | **0.20 MB** |
| `stalls` | 0 | 0（无退化）|
| `bandwidthLow` | false | false |
| `seek` 首帧 | 345ms | 未测（本次无 seek）|

#### 基线 v2 哈希

```
veyra_qml_ui.exe      = 54552D4A453A4520
veyra_qml_ui.exe.p5   = 54552D4A453A4520
EnhanceGraph.cpp      = 04CB44FA5BF8AEC9
```

### 回滚能力已实测验证

1. 故意在 `EnhanceGraph.cpp` 追加垃圾行 → md5 变为 `45490720...`
2. `python restore.py` 正确检出「需恢复 1 个文件」
3. `python restore.py --apply` 执行恢复
4. 恢复后 md5 回到 `04CB44FA5BF8AEC9`，垃圾行消失

**结论：这套基线是真的能用的，不是摆设。**

---



---

## 三、原始说明（保留原样）

### 这是干什么的

把这个时刻的代码和部署包完整存档。以后改坏了，一条命令就能恢复到能正常播放的状态。


### 目录内容

| 内容 | 位置 | 数量 |
|---|---|---|
| 源码快照 | `src-snapshot/` | 35 个文件 |
| 部署包快照 | `deploy-snapshot/` | 124 个文件（exe + 全部 QML）|
| 源码清单 | `manifest.json` | 含每个文件的 MD5 |
| 部署清单 | `deploy-manifest.json` | 含每个文件的 MD5 |
| 恢复脚本 | `restore.py` | 见下 |


### 怎么恢复

```bash
cd /d/DSH/.dsh-tmp/veyra-baseline

# 1) 先检查（不改任何文件）
python restore.py

# 2) 确认后执行恢复
python restore.py --apply
```

脚本只恢复 MD5 不一致的文件，一致的不动。恢复完直接启动程序即可，**不需要重新编译**（exe 也一起恢复了）。


### 基线包含的关键状态

### exe
```
veyra_qml_ui.exe      = 807E76FDC3F737C7
veyra_qml_ui.exe.p5   = 807E76FDC3F737C7
（.orig 作者原件未动 = 1C111E8724F529FC）
```

### 已包含的 B 站功能修复
- 收藏夹选择器（点收藏弹出选择夹子）
- 稍后再看 / 收藏状态修正（bvid 当 aid 的 bug）
- b23.tv 短链展开
- `pending*` 字段泄漏修复（本地视频不再继承 B 站音频地址）
- 相关推荐高度按实际列数计算
- 切分P时清理旧弹幕
- 顶部「B 站」图标改为 B 字

### 已回滚、不在基线内
- 弹幕类型过滤开关（滚动/顶部/底部）
- 弹幕默认字号 175%（基线是 100%）
- seek 门限自适应
- 起播测速选节点
- `Main.qml` 的 bili syncRect 修复


### ⚠️ 已知问题（基线里也存在）

1. **从 B 站页播放可能报错**
   `Main.qml:172` 原版 `if (prev === "home" || prev === "set")` 漏了 `bili`，
   而 bili 页 `videoRect()` 返回 0x0，导致 `pre-open` 拿到 0x0。
   修法：改成 `if (prev === "home" || prev === "set" || prev === "bili")`。
   （已验证有效，但用户要求回滚到 17:55 状态，故不在基线内）

2. **网络速率测量缺陷**
   `DemuxSession.cpp:553` 的 `serverRateLocked()` 用
   「解封装字节 ÷ av_read_frame 耗时」，衡量的是吐包速率而非网络带宽。
   实测对比：同一 FFmpeg 库同地址，ffmpeg.exe 32 倍速，节点 curl 61 MB/s，
   但 Veyra 日志显示 0.2 MB/s。
   **未修**，因为可能引发新的不稳定。


### 相关日志

```
D:\项目\Veyra-NV\Veyra-2.1.0-NVIDIA-win64-portable\logs\veyra-qml.log
```


### 注意

- 本快照在 `.dsh-tmp` 下，用户决定何时清理
- 不要删除 `src-snapshot/` 和 `deploy-snapshot/`，否则基线失效
- 基线只覆盖 B 站模块相关文件，不包含第三方库和构建产物
---


### 会话收尾（2026-10-23 21:30）

**用户决定：剩余问题不修，先用一段时间，发现问题再来说。**

### 最终状态：基线 v11，11 个修复

```
veyra_qml_ui.exe              = BCAE9C4458EAE928
veyra_qml_ui.exe.p5           = BCAE9C4458EAE928
qml/Veyra/Main.qml            = EEB0C08D9804A7F2
qml/Veyra/BilibiliPage.qml    = 6479A46EF1FC424C
src/media/DemuxSession.cpp    = 69E9092E1C128EDD
src/media/BilibiliResolver.cpp= 0B79E65B08E8F030
src/media/BilibiliApi.cpp     = 3815BEC215AA192F
src/media/DanmakuOverlay.cpp  = EBA91C2B3FCFB48A
src/ui/QmlPlayerBridge.cpp    = C8CBFCFF0414A2E9
```

### 用户当前环境

- 显示器：2560x1440，**120Hz**（用户自行设置，表现良好）
- 网络：实测可达 44 MB/s，B 站片源码率 0.15-2.6 MB/s
- 渲染：软件 UI（`[app] software UI`）
- GPU：RTX 5080
- 设置：缓冲档位 Auto，自动重连 true

### 已验证表现（用户确认）

| 项目 | 状态 |
|---|---|
| B 站视频播放 | ✅ 正常，4K 也行 |
| 弹幕显示 | ✅ 正常，不卡 |
| 拖动进度条 | ✅ 345ms 出首帧（60Hz 时测）|
| 全屏 | ✅ 视频窗正常，B 站列表可操作 |
| 画质/分P 显示 | ✅ 与实际一致 |
| 收藏夹选择器 | ✅ 可弹选择 |
| 稍后再看 / 收藏 | ✅ 可用 |
| b23.tv 短链 | ✅ 可解析 |
| 120Hz 下表现 | ✅ P50 延迟 4.9ms，零卡顿 |

### 已知但主动不修（用户决定）

1. **全屏退出窗口位置变到上方** —— 作者原设计，保留
2. **`bandwidthLow` 闸门失效** —— 用户网速快碰不到，改动风险高
3. **同步网络阻塞 UI** —— 用户网速快感知弱，工程量大
4. **弹幕 LRU 一次砍半** —— 需 4K+200%字号+密集弹幕
5. **弹幕 2 秒前跳阈值** —— 需 UI 停顿超 2 秒

### 待用户观察

- 长时间播放（10 分钟以上）是否卡顿
- Cookie 加密要等下次登录才生效

### 重要提醒（下次接手必看）

1. **改动全部未提交、未推送**，在工作区等验收后自行决定
2. **这套基线已实测回滚 3 次**，`python restore.py --apply` 可随时回到当前状态
3. **审计误报清单在上一节**，如果重新审计，先看那里避免重复劳动
4. **`Main.qml:172` 和 `:231` 是 bili 页的两处关键修复**，改动 QML 时不要回退掉


---



---
