## 📖 目录（想找什么，先看这里）

**日常场景**：

| 你想做什么 | 看哪条 |
| --- | --- |
| 我在本地改了个功能，想给作者 | 1 → 2 → 3 |
| 我提了 PR，但发现有问题 | 6 |
| 作者合并了我的 PR，我要升级 | 9 → 10 |
| **升级时一堆冲突文件，不知道怎么办** | **10** |
| 我要升级到新版了 | 11 → 19 |
| **合并后 HDR 元数据又冒出来了** | **16** |
| 我要删文件 | 18 |
| 改完要请人验证 | 17 |

**全部规矩**（两部分共 19 条）：

| # | 规矩 | | # | 规矩 |
| --- | --- | --- | --- | --- |
| 1 | 改动先分三色 | | 11 | 升级上游的安全顺序 |
| 2 | 不从 `custom` 提 PR | | 12 | 一个功能 = 一个提交 |
| 3 | 提 PR 前跑检查脚本 | | 13 | 提交信息写"为什么" |
| 4 | PR 要小要单一 | | 14 | 合并前看净差异 |
| 5 | 不确定的先写台账 | | 15 | 升级后必做三件事 |
| 6 | 提了 PR 发现问题 | | 16 | HDR 元数据防护 |
| 7 | 定期自查差多少 | | 17 | 验证前部署 exe |
| 8 | 已提 PR 的处置 | | 18 | 删除进回收站 |
| 9 | 作者合并后怎么继续 | | 19 | 升级改目录名 |
| 10 | **合并冲突逐项问** | | | |

---

## 📊 当前状态（2026-10-18）

### 本地定制（上游没有）

| 功能 | 状态 | 说明 |
| --- | --- | --- |
| 杜比视界 P5 IPT-PQ 转换 | 稳定 | 修掉偏绿，着色器 `YuvToLinearRgb.hlsl` |
| 杜比视界 P7 增强层 | 实验性（默认关） | 覆盖率仅 5.9%，多数帧需基础层参考 |
| 杜比视界跟随 L1 | 稳定（默认关） | 含平均亮度模式，借 libplacebo st2094 |
| 首页最近列表清空 | 稳定 | — |
| QML 日志错误修复 | 稳定 | VRow 循环 / DialogHost / VMenu |
| 后端失败提示可操作 | 稳定 | 按后端给恢复建议 |
| 会话目录诊断 | 稳定 | 错误带路径与系统原因 |

### 已删除（合并上游后可能复活，需重新删）

| 功能 | 删除日期 | 防护 |
| --- | --- | --- |
| **HDR10 静态元数据**（`SetHDRMetaData`）| 2026-10-18 | `EnhancementSettings` 归一化强制关闭；卡片 UI 需手动再删（见规矩 16）|
| HDR 片源转 SDR + RTX Video HDR | 2026-10-03 | 无 |

### 已进上游

暂无。（PR #55/#56/#58/#59/#60/#62 均待作者决定）

### 待作者决定

| PR | 内容 | 规模 |
| --- | --- | --- |
| #55 | 路径分隔符匹配修复 | +9/−3 |
| #56 | 杜比视界 P5+P7+跟随 | +2210/−40 |
| #58 | 三个 QML 日志错误 | +19/−3 |
| #59 | 假峰值缩放曲线 | +51/−2 |
| #60 | 后端/会话失败提示 | +31/−2 |
| #62 | 删除 HDR10 静态元数据 | +4/−216 |

> 作者自行决定是否合并，**不合并也没关系**。作者回复前不要动它们。

### 分支现状

```
分支   : custom
提交   : 17 个（2026-10-18 从 263 压缩而来，内容逐字节一致）
落后上游: 0
```

---


## 第一部分 · 防污染方案（给不懂开发的使用者）

> 背景：用户不是开发者，本地改功能时凭感觉判断好坏，提了 PR 之后才发现有 bug、
> 又回头删功能。担心自己的改动污染上游，怕作者不小心合并了出问题。
> 本方案的目标：**让「本地随便玩」和「给上游提什么」彻底分开**。

### 核心原则：三条线，别混

| 线 | 在哪 | 可以随便改吗 | 会上游吗 |
| --- | --- | --- | --- |
| **玩票区** | `custom` 分支 | ✅ 随便改、随便删、随便试 | ❌ 永不上游 |
| **候选区** | `fix/*` `feat/*` 分支 | ⚠️ 只改一件事 | ✅ 检查通过后提 PR |
| **干净区** | `origin/main` | ❌ 不直接碰 | — |

**一句话**：`custom` 是你的游乐场，`origin/main` 是别人的家。
**从游乐场里搬东西去别人家之前，必须先在候选区单独放好、洗干净。**

### 一、改动先分三色，决定能不能提

每次想改东西，先问自己：

| 颜色 | 判据 | 处理 |
| --- | --- | --- |
| 🟢 **绿** | **上游的 bug**（他写错了、崩了、日志报错） | 可以提 PR |
| 🟡 **黄** | **行为/观感**（画面效果、默认值、UI 排版、文案） | **先问作者**，他同意再提 |
| 🔴 **红** | **删功能 / 加功能 / 改架构** | **默认只留本地**，不提 |

**判断口诀**：

> 「这是**修好他的 bug**，还是**我想让他按我的喜好改**？」
> 修 bug → 可以提　｜　我的喜好 → 只留本地

**实例对照（你已提的 6 个）**：

- #55 修路径分隔符匹配 → 🟢 绿（他的 bug）
- #58 修三个日志错误 → 🟢 绿（他的 bug）
- #60 改错误提示文案 → 🟢 绿（可读性，几乎无争议）
- #59 修假峰值缩放 → 🟡 黄（**会改变画面**，虽是真 bug）
- #56 杜比视界全套 → 🔴 红（新功能，+2210 行，本地未完全验证）
- #62 删 HDR 元数据 → 🔴 红（**删他的功能**，只有他明确要才提）

### 二、永远不要从 `custom` 提 PR

```powershell
# ❌ 错：从 custom 开分支，必然夹带杜比视界、删元数据、改过的 UI
git checkout -b fix/xxx custom

# ✅ 对：从上游开，只放这一件事
git checkout -B fix/xxx origin/main
```

**为什么**：`custom` 领养了 11 个提交的定制。从它开分支，PR 里会混进
杜比视界、主页清空、UI 改动等一大堆东西，作者根本没法审。

### 三、提 PR 前必跑检查脚本

已经放进仓库：`tools/check_pr_ready.py`

```powershell
python tools/check_pr_ready.py fix/你的分支名
```

它检查六项：

1. **分叉点**——是不是从 `origin/main` 开的（不是从 `custom`）
2. **本地专有文件**——有没有夹带 `CUSTOM_LEDGER.md`、`DOVI_WORK_HANDOFF.md` 等
3. **改动规模**——超过 800 行会提醒拆分
4. **是否删除上游文件**——删功能要作者同意
5. **本地专有开关**——`doviP7Compose` / `doviFollow` 等字段是否泄漏
6. **提交信息质量**——太短（"fix"、"wip"）会提醒

**只有输出「结论：可以提 PR」才提，否则先修。**

### 四、PR 要小、要单一、要能解释

| 指标 | 建议 |
| --- | --- |
| 文件数 | ≤ 10 |
| 行数 | ≤ 300 |
| 提交数 | 1~3 |
| 能一句话说清吗 | 必须能 |

**太大就拆**。#56（27 文件 / 2210 行）就是因为一次做完 P5+P7+跟随，
作者很难审——**如果拆成三个 PR，被接受的概率大得多**。

### 五、不确定的改动，先写进本地台账，别急着提

台账（本文件）就是用来记「我试了什么、结果如何、为什么撤销」的。
**本地记录没有成本，提错的 PR 会消耗作者的信任。**

### 六、已经提了但发现有问题，怎么办

```
1. 立刻在 PR 里回复说明问题（诚实比掩饰好）
2. 能修就修，推新提交到同一分支
3. 修不了就请求关闭 PR：# <PR号> 有问题，请先不要合并
4. 涉及已合并的：单独提一个 revert PR，说清原因
```

**绝不能**：悄悄 force-push 覆盖已被人看过的提交，那会让作者困惑。

### 七、定期自查：本地和上游差多少

```powershell
git rev-list --count HEAD..origin/main     # 落后多少
git rev-list --count origin/main..HEAD     # 领先多少
git diff --shortstat origin/main..HEAD     # 真正的差异
```

**健康状态**：领先提交数与净差异规模匹配（比如 11 个提交 / 46 个文件）。
**不健康**：领先 200 个提交但只差 40 个文件 → 历史里堆了撤销和试错，该清理。
（2026-10-18 就清理过一次：263 个提交压成 11 个，内容逐字节一致。）

### 八、已提 PR 的处置状态（2026-10-18 用户决定，不要再动）

| PR | 内容 | 状态 |
| --- | --- | --- |
| #55 | 修路径分隔符匹配（title memory） | 打开，等作者 |
| #56 | 杜比视界 P5 + P7 + 跟随（27 文件 / +2210） | **用户已与作者沟通，交作者决定** |
| #58 | 修三个日志错误 | 打开，等作者 |
| #59 | 修假峰值缩放曲线 | 打开，等作者 |
| #60 | 后端/会话失败提示可操作 | 打开，等作者 |
| #62 | 删除 HDR10 静态元数据（8 文件 / −216） | **用户已与作者沟通，交作者决定** |

**明确结论**：用户已就 #56 与 #62 与作者联系过，作者自行决定是否合并，
**不合并也没关系**。这两项**不需要再修改、拆分、关闭或评论**。

**以后不要再做的事**：

- 不要试图"帮我清理"这些 PR（合并/关闭/改写）
- 不要因为 #56 太大就主动拆分重提
- 不要因为 #62 是 breaking change 就主动撤回
- 作者回复之前，这 6 个 PR 保持原样即可

**如果作者回复了**（提意见或要求修改），再按他的意见处理；他没有要求就什么都不做。

### 九、作者合并了我的 PR 之后，怎么继续

> 假设：作者合并了 #56（杜比视界）和 #62（删元数据）。
> 这时**情况变了**：这些代码成了上游的一部分，不能再往旧 PR 分支加东西。

#### 一个必须理解的差别

| | 合并前 | 合并后 |
| --- | --- | --- |
| 代码住在哪 | 你的 `feat/dolby-vision` 分支 | **上游 `main`** |
| 新功能往哪加 | 旧 PR 分支继续 push | **从最新上游开新分支** |
| 旧 PR 分支 | 还有用 | **可以删了** |

**核心陷阱**：作者合并后，他可能会**改写你的提交**（squash、fixup、调整）。
如果这时你还往旧分支 push，两边历史就分叉了——你的改动会被孤立。

#### 正确流程（合并确认后立刻做）

```powershell
# 1. 拉上游最新
git fetch origin

# 2. 确认你的东西真的进去了（不是"他说合并了"，是代码真的在）
git log --oneline origin/main --grep="dovi" -20
git log --oneline origin/main --grep="metadata" -10
grep -n "DolbyVisionRpu\|active.hdrMetadata=defaults" include/veyra/engine/EnhancementSettings.h

# 3. 把本地 custom 接到新上游上
git checkout custom
git merge origin/main

# 4. 这时必然有冲突（你在 custom 上也改过这些文件）
#    解决原则：优先取上游，你本地独有的定制（如 UI 改动）再补回

# 5. 编译 + 冒烟 + 部署，确认没坏

# 6. 删掉已合并的旧 PR 分支
git branch -D feat/dolby-vision feat/remove-hdr-metadata
```

#### 之后的每一步新功能

```powershell
# 永远从"最新上游"开分支，不是从旧分支
git checkout -B feat/dovi-next origin/main

# 只改这一件事
git commit
python tools/check_pr_ready.py feat/dovi-next   # 检查
git push fork feat/dovi-next
gh pr create --head georgeg911:feat/dovi-next
```

#### ⚠️ 千万别做的事

| 别做 | 为什么 |
| --- | --- |
| 继续往已合并的旧分支 push | 历史分叉，你的提交会被孤立 |
| `git push --force` 到已合并分支 | 同上，而且作者会困惑 |
| 在 `custom` 上做新功能然后从它提 PR | 夹带全部定制（见方案第二条） |
| 合并后不验证就继续开发 | 上游可能改写了你的代码，行为可能变 |

#### 一个重要判断：上游的版本和你的不一样怎么办

作者合并时可能**改动了你的实现**（简化、重命名、整合进他的结构）。

**这时**：

1. **先读上游的代码**，搞清他怎么写的
2. **以他的实现为基础**开发新功能——不要试图把你的旧代码再塞回去
3. 如果你觉得他的改动有问题，**单独提一个小 issue 或 PR 讨论**，别直接覆盖

**心态**：合并意味着代码归他维护了。你从"作者"变成"使用者 + 贡献者"，
按他的形状来，冲突最少。

#### 如果作者只合并了一部分

比如合并了 #56 的 P5，但 P7 增强层他不要（因为覆盖率只有 5.9%）：

```powershell
# 你的 custom 上还有 P7，上游没有
git checkout custom
git merge origin/main     # P5 会和上游合并，P7 保留为本地定制

# 确认 P7 还在
grep -n "elLayerEnabled_\|DolbyVisionP7" src/source/MediaFileSource.cpp
```

**这没问题**——本地保留他没要的部分，是完全正常的用法。

### 十、任何 PR 被合并后升级：逐项问用户，不许替他决定

> **详细流程见 [`docs/MERGING.md`](docs/MERGING.md)。**

核心：合并时只要有文件两边都改过，就逐个问用户「跟作者 / 按本地 / 混合」，
**不许自己决定**。判断依据是「这个文件两边都改过」，不是「这是不是我提的 PR」。


### 十一、升级上游时的安全顺序

```
1. 先备份：git bundle create .../custom-full.bundle --all
2. 再合并：git fetch --tags && git merge v<新版本>
3. 冲突优先取上游，定制改动看情况补回
4. 检查 EnhancementSettings 里的元数据归一化防护还在（见规矩五）
5. 全量编译 + 冒烟 + 部署
6. 报告哈希
```

**注意**：合并上游**不会**把本地定制推上去，方向是反的——合并只是把上游的东西拿下来。
**只有 `git push` 才会推。所以合并永远安全，推才需要谨慎。**

---

## ✅ 2026-10-18 分支历史清理：263 个提交压成 9 个，并定下提交规矩

### 为什么清理

`custom` 领先上游 263 个提交，但净差异只有 46 个文件。中间大量提交是"改完又撤销"：

```
7 对 feat/fix + 后置 Revert（补帧页排版 5 次、seek 重建 1 次、显示峰值 1 次）
HDR 元数据功能来回三轮：移除（2.0.6）→ 加回（16ee455d）→ 再移除（本次）
大量调试/诊断中间提交（diag(...)、wip(...)）
```

后果是合并上游时冲突面变大、回溯问题要翻几百条提交、`git log` 读不出所以然。

### 怎么做的（可复用的手法）

**绝不 `rebase -i`**（263 个提交交互式重排风险太高）。改为：

1. `git checkout -B <new> origin/main` —— 从上游开干净分支
2. 按**功能分组**，每组把文件从旧 HEAD 检出：`git checkout <old-head> -- <files>`
3. 每组用一段说明写清楚"为什么"，然后提交
4. **逐文件哈希校验**：`git diff --name-only <old-head>..HEAD` 必须为空
5. `git diff --shortstat` 必须与清理前**逐字节一致**
6. 全量编译 + 冒烟，再 `git branch -f custom <new>`

**三重备份**（重写历史前必须做）：

- `git bundle create <path>/custom-full.bundle --all`
- `git tag backup/custom-before-cleanup <old-head>`
- 全部源文件 SHA256 快照（用于事后比对）

后悔一条命令：`git reset --hard backup/custom-before-cleanup`。

### 结果

```
263 提交 → 9 提交
净差异不变：46 文件 +6438 / −278
逐文件哈希：与清理前完全一致
编译：318 目标全绿
```

9 个提交按功能分组（docs / tools / UI 修复 / 元数据移除 / HDR 曲线 / DV 跟随 / P5 / P7）。

---

## 第二部分 · 提交与升级规矩

### 十二、一个功能 = 一个提交

- **不要**在同一个提交里混两件不相干的事（改配色 + 修 bug）。
- **不要**"改完不满意再 revert 一条"——要么用临时分支试，要么在同一分支上 `--amend`。
- 试错请开 `trial/<用途>` 分支，成了再 `cherry-pick` 回来，扔掉临时分支。

### 十三、提交信息写"为什么"，不是"改了什么"

文件列表 Git 自己会记。信息要回答：**为什么非改不可**，以及**验证到什么程度**。

```
✅ fix(hdr): stop scaling the curve against a display peak that was never measured

   The credibility check was a copy of `valid`, so a panel reporting 10000 nit
   scaled the whole curve by ten. Now computed for real.

❌ 改了一行
❌ fix bug
```

**破坏性改动加 `!`**（如 `feat(hdr)!: remove ...`），并在正文说清影响面。

### 十四、合并上游前先看净差异

```powershell
git rev-list --count HEAD..origin/main   # 落后多少
git rev-list --count origin/main..HEAD   # 领先多少
git diff --stat origin/main..HEAD        # 真正的改动
```

**净差异稳定在几十个文件才对**。如果领先提交数远超文件数，说明历史里堆了撤销和试错，该清理了。

### 十五、每次升级上游后必做的三件事

1. `git merge origin/main`，冲突**优先取上游**（定制改动另行补回）
2. 检查 `EnhancementSettings` 归一化里的防护是否还在（见下）
3. 全量编译 + 冒烟 + 部署，报告 `exe`/`qml` 哈希

### 十六、HDR 元数据功能的防护（重要）

该功能已在本次彻底移除，但**上游 main 上有**。以后合并上游会把它带回来：

```cpp
// include/veyra/engine/EnhancementSettings.h
// HDR10 static metadata is gone... A session saved while it existed must not
// resurrect it.
active.hdrMetadata = defaults.hdrMetadata;
```

**这行必须保留**——它保证即使上游代码回来，运行时也强制关闭、永不发送 `SetHDRMetaData`。
如果上游把**卡片 UI** 也带回来，需要再删一次界面（`ProPage.qml` 的 `list-hdr-metadata`、
`SettingsPage.qml` 的 `hdrmeta-enabled`）。

**为什么必须删**：它的 `SetHDRMetaData` 在 flip-model 交换链上会让 DWM 重新合成；
全屏时每次窗口区域更新（悬停 tooltip、弹出面板）都会重新应用它。**60Hz 下一次重组就是一帧**，
表现为鼠标划过图标就卡、亮度反复脉动。120Hz 有余量所以看不出来——同一台机器上，60Hz 投影仪必现、
120Hz 电视无感。

### 十七、验证请求必须连 exe 一起部署

只换 QML 不换 exe，用户看到的界面与描述不符（已发生过）。部署清单：

- `veyra_qml_ui.exe` + `.p5` 备份（作者原件 `.orig` 永远不动）
- `qml\Veyra\*` → `qml.p5\Veyra\*` → `qml\Veyra\*`
- `shaders\*.dxil`（若有改动）
- 报告：生效 exe 哈希、`.p5` 是否一致、`.orig` 是否未变
- 启动验证：`exit=0` 且日志无 `Unexpected token` / `no root object`

### 十八、删除一律进回收站

```powershell
Add-Type -AssemblyName Microsoft.VisualBasic
[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($path,
  [Microsoft.VisualBasic.FileIO.UIOption]::OnlyErrorDialogs,
  [Microsoft.VisualBasic.FileIO.RecycleOption]::SendToRecycleBin)
```

删除前先打印精确清单（绝对路径 + 大小）并向用户确认。

### 十九、升级必须改目录名

升级后程序目录**必须**改成新版本号（`Veyra-<新版本>-NVIDIA-win64-portable`）。
这是升级流程的一部分，**不需要用户单独提**。

```
1. 下载作者包
2. 原地解压覆盖（保留 runtime_local\，那里有 FSR/XeSS 运行时）
3. ★ 目录改名 Veyra-<新版本>-NVIDIA-win64-portable
4. git fetch --tags → git merge v<新版本> → 解决冲突 → 编译
5. python tools/veyra-switch/make_switch.py --package <程序目录> --build <构建目录> --repo <源码仓库> --apply
6. 哈希核对 + 启动一次验证
```

**为什么不能省**：

- 目录名是**版本标识**。不改就会出现「目录写着 2.0.9、实际跑着 2.1.0」
- 切换脚本按目录名/版本判断，名字不对会出错
- 排查问题时给的路径与日志里的版本号对不上，无法定位
- 多个版本并存对比时，分不清哪个是哪个

**改名前后各用哈希核对一遍生效文件**，避免中途出问题留下不一致状态。
作者自己的 `GPU-PACKAGE.txt` 标题长期写着旧版本号，就是反面例子。

**注意**：只改目录名，不要新建并列目录（台账 §升级流程 第 1 条）。

---

---

## 📚 相关文档

| 文档 | 内容 |
| --- | --- |
| [`docs/HISTORY.md`](docs/HISTORY.md) | 全部历史技术记录（按日期倒序）|
| [`docs/MERGING.md`](docs/MERGING.md) | 合并冲突决策流程（规矩 10 的详细版）|
| [`docs/DOVI_WORK_HANDOFF.md`](docs/DOVI_WORK_HANDOFF.md) | 杜比视界工作交接 |
| [`docs/VeyraNV_LOCAL_BUILD_NOTES.md`](docs/VeyraNV_LOCAL_BUILD_NOTES.md) | 本地构建说明 |
