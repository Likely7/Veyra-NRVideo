

## 🛡️ 防污染方案（2026-10-18 定，给不懂开发的使用者）

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

### 十、作者合并后升级：必须逐项问用户，不许替他决定

> 用户明确要求：**作者合并 PR 时可能改动过实现，升级时必须让我选「跟作者」还是「按本地原来的」。**

#### 为什么必须问（不是礼貌，是数据）

实测冲突面（`origin/main` vs `custom` vs 两个 PR 分支）：

```
#56 改 27 个文件，#62 改 8 个，custom 改 47 个
三方重叠（合并时必冲突）：28 个文件
只在 PR 里（合并后归上游，本地无冲突）：0 个   ← 注意：一个都没有
只在 custom 里（本地独有，不受影响）：19 个
```

**关键事实**：`#56` 的 27 个文件**全部**也在 `custom` 里被改过。
这意味着一旦作者合并 #56，**每个文件都会冲突**，没有"自动跟随"的余地。

#### 决策分类（按文件性质，不是按文件数）

把 28 个冲突文件分成四类，每一类问一次，不要逐个问 28 次：

| 类别 | 文件 | 默认建议 | 问法 |
| --- | --- | --- | --- |
| **A. 纯新文件** | `DolbyVisionRpu.h`、`DolbyVisionP7.h`、`DoviP7Constants.h`、`DolbyVisionP5.h`、`DoviBlElCompose.h`、`DoviCurveAdapter.h` | **跟作者** | 「这些是新文件，上游有就看他的版本？他可能改过实现」 |
| **B. 共享数据结构** | `FramePacket.h`、`EnhancementSettings.h`、`GraphDescription.h`、`EngineController.h`、`VideoPresenter.h`、`MediaFileSource.h`、`IFrameSource.h`、`QmlPlayerBridge.h`、`FFmpegVideoDecoder.h`、`EnhanceGraph.h`、`DolbyVision.h` | **要看 diff** | 「这些是结构定义，两边都改过。要我列出他改了什么，你再决定？」 |
| **C. 实现代码** | `MediaFileSource.cpp`、`VideoPresenter.cpp`、`QmlPlayerBridge.cpp`、`EngineController.cpp`、`EnhanceGraph.cpp`、`FFmpegDemuxer.cpp`、`FFmpegVideoDecoder.cpp` | **要看 diff** | 同上 |
| **D. 界面与资源** | `ProPage.qml`、`SettingsPage.qml`、`i18n/catalog.json`、`YuvToLinearRgb.hlsl` | **跟作者 + 补回定制** | 「界面会被他的版本覆盖，你之前的 DV 卡片改动会丢。要我保住你的部分吗？」 |

#### 必须提供的选择（每个冲突给出三选一）

```
对这个文件，你要哪个？
  1) 跟作者（放弃我本地的改动）
  2) 按我本地的（覆盖他的版本）
  3) 混合（我列出他改了什么，你逐段选）
```

**默认不替用户决定。** 除非用户说「都跟作者」或「都按本地」，否则逐类问。

#### 操作步骤（先看清楚，再动手）

```powershell
# 1. 合并但不提交，让冲突摆出来
git checkout custom
git merge origin/main --no-commit --no-ff

# 2. 列出冲突文件（这就是要问的清单）
git diff --name-only --diff-filter=U

# 3. 对每个文件，先看作者改了什么（相对你分支的版本）
git diff feat/dolby-vision:工程文件路径 origin/main:工程文件路径

# 4. 用户选完之后再逐个解决并提交
git add <文件>
git commit
```

**如果冲突太多理不清**，可以整体放弃这次合并：

```powershell
git merge --abort
```

**回到合并前状态，什么都不丢。**

#### 一个重要的判断辅助

**作者如果重构了你的代码，优先跟作者**。理由：

- 他改可能是为了配合上游其他改动，硬保留你的版本会破坏别处
- 你要的功能大概率还在，只是实现方式变了
- 跟他的版本，以后合并阻力小

**只有这些情况才保留本地版本**：

- 上游**没有**这个功能（他拒收的部分，比如 P7 增强层）
- 你本地有**他没接受的定制**（比如 UI 布局、默认值）
- 他的改动**引入了 bug**（这时应该单独提 issue，而不是本地硬保留）

#### 升级后的核对清单

```powershell
# 功能还在吗
git cat-file -e HEAD:include/veyra/source/DolbyVisionRpu.h        # P7/P5 实现
grep -n "elLayerEnabled_" src/source/MediaFileSource.cpp          # 增强层开关
grep -n "active.hdrMetadata=defaults.hdrMetadata" include/veyra/engine/EnhancementSettings.h  # 元数据防护

# 编译
cmake --build build-nv

# 部署 + 冒烟
```

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

## 📌 提交与分支规矩（从本次起执行）

### 一、一个功能 = 一个提交

- **不要**在同一个提交里混两件不相干的事（改配色 + 修 bug）。
- **不要**"改完不满意再 revert 一条"——要么用临时分支试，要么在同一分支上 `--amend`。
- 试错请开 `trial/<用途>` 分支，成了再 `cherry-pick` 回来，扔掉临时分支。

### 二、提交信息写"为什么"，不是"改了什么"

文件列表 Git 自己会记。信息要回答：**为什么非改不可**，以及**验证到什么程度**。

```
✅ fix(hdr): stop scaling the curve against a display peak that was never measured

   The credibility check was a copy of `valid`, so a panel reporting 10000 nit
   scaled the whole curve by ten. Now computed for real.

❌ 改了一行
❌ fix bug
```

**破坏性改动加 `!`**（如 `feat(hdr)!: remove ...`），并在正文说清影响面。

### 三、合并上游前先看净差异

```powershell
git rev-list --count HEAD..origin/main   # 落后多少
git rev-list --count origin/main..HEAD   # 领先多少
git diff --stat origin/main..HEAD        # 真正的改动
```

**净差异稳定在几十个文件才对**。如果领先提交数远超文件数，说明历史里堆了撤销和试错，该清理了。

### 四、每次升级上游后必做的三件事

1. `git merge origin/main`，冲突**优先取上游**（定制改动另行补回）
2. 检查 `EnhancementSettings` 归一化里的防护是否还在（见下）
3. 全量编译 + 冒烟 + 部署，报告 `exe`/`qml` 哈希

### 五、HDR 元数据功能的防护（重要）

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

### 六、验证请求必须连 exe 一起部署

只换 QML 不换 exe，用户看到的界面与描述不符（已发生过）。部署清单：

- `veyra_qml_ui.exe` + `.p5` 备份（作者原件 `.orig` 永远不动）
- `qml\Veyra\*` → `qml.p5\Veyra\*` → `qml\Veyra\*`
- `shaders\*.dxil`（若有改动）
- 报告：生效 exe 哈希、`.p5` 是否一致、`.orig` 是否未变
- 启动验证：`exit=0` 且日志无 `Unexpected token` / `no root object`

### 七、删除一律进回收站

```powershell
Add-Type -AssemblyName Microsoft.VisualBasic
[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteDirectory($path,
  [Microsoft.VisualBasic.FileIO.UIOption]::OnlyErrorDialogs,
  [Microsoft.VisualBasic.FileIO.RecycleOption]::SendToRecycleBin)
```

删除前先打印精确清单（绝对路径 + 大小）并向用户确认。

### 八、升级必须改目录名

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


## ✅ 2026-10-17 AMD 光流（FidelityFX）从"根本编不进来"到编译通过：一条完整的排查链

### 现象
在「DLSS 帧生成 + 6X」下，一旦把「光流算法」选成 **AMD FidelityFX**，顶部补帧开关立刻被关掉。
作者版（同一 2.0.9 源码）却能开。日志里（`logs\veyra-qml.log`）一句话点明：

```
[ERROR] [amd-of] FidelityFX SDK not compiled into this build
[WARN ] [backend-recovery] initialization failed component=2 -> nr=false sr=false multiplier=1
[ui-fg-rollback] rejectedRevision=9 restoredBackend=0 restoredMultiplier=1
```

### 根因
`src/guidance/AmdOpticalFlow.cpp` 用 `#ifdef VEYRA_HAS_AMD_OF` 包裹整段实现；该宏由
`CMakeLists.txt` **L1310** 的条件定义：只有
`${VEYRA_FIDELITYFX_ROOT}/bin/ffx_sdk/ffx_opticalflow_x64.lib` **存在**才定义，
并链接 `ffx_opticalflow_x64.lib` + `ffx_backend_dx12_x64.lib`。
而我们本机没有这套库 → 宏没定义 → initialize() 直接走 `#else` 返回 false → 引擎拒绝提交 → 设置回退 → 补帧被关。

### 关键结论（最省时间的一条）
**官方三个发行包里都没有 `ffx_opticalflow_x64.lib`**：
* FidelityFX SDK **1.1.4**（449 MB）：只有 `ffx-api/bin/amd_fidelityfx_dx12.lib`（新 API），无经典光流库
* FidelityFX SDK **1.1.0**（331 MB）：同上
* FidelityFX SDK **1.0**（388 MB）：`bin/ffx_sdk/` 有 16 个库（fsr1/fsr2/cas/dof/denoiser…）但**没有 opticalflow**
→ 它**只能从源码编**。**别再为这个库去下发行包了**（本次白下了 1.17 GB）。

### 从源码编出来的完整步骤（可复用）
1. 解 1.1.4 发行包里的 `sdk/` 源码（864 个文件）；顶层 CMake **本来就有** `add_subdirectory(${FFX_COMPONENTS_PATH}/opticalflow)`（L235），
   且有 `option(FFX_OF "Build FFX Optical Flow API" ON)`。**不要自己在末尾再 add_subdirectory**（会让 configure 直接失败 ✗，本次踩过）。
2. SDK 的 CMake 里把 `/WX`（警告当错误）写死（`src/backends/dx12/CMakeLists.txt`、`src/components/fsr1|fsr2`），
   `-DCMAKE_CXX_FLAGS=/WX-` 覆盖不掉 → 直接把副本里的 `/WX` 改成 `/WX-`。
3. **CRT 必须一致**：本仓库是静态 CRT（/MT），SDK 默认 /MD → 链接报
   `LNK2038: RuntimeLibrary 不匹配：MD_DynamicRelease vs MT_StaticRelease`。
   修法：`cmake -S <sdk> -B <新目录> -G "Visual Studio 17 2022" -A x64 -DFFX_API_BACKEND=DX12_X64 -DFFX_OF=ON -DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`
   （`-DCMAKE_CXX_FLAGS=/MT` 之类不生效，必须用 `CMAKE_MSVC_RUNTIME_LIBRARY`）。
   验证：`dumpbin /directives xxx.lib | findstr DEFAULTLIB` 应看到 `libcpmt`/`LIBCMT`。
4. 编 `ffx_opticalflow_x64` 与 `ffx_backend_dx12_x64` 两个目标。
5. **产物不在构建目录**！SDK 把 `CMAKE_ARCHIVE_OUTPUT_DIRECTORY` 指回 **源码树**：
   `<sdk>/bin/ffx_sdk/*.lib`（这次就是因为在构建目录里找，白折腾了两轮 ✗）。
6. 把两个 `.lib` + 1.1.4 的 `sdk/include` 放到 `third_party_local/amd/FidelityFX-SDK/sdk/` 下的
   `bin/ffx_sdk/` 与 `include/` → 重新 cmake → `build.ninja` 里应出现 `VEYRA_HAS_AMD_OF`（本次 17 处）。

### 结果
整树编译 rc=0；exe 从 `C7E2693A…` 变为 **`74D1F7CB…`**；单元测试全绿（2055 项 0 失败）；
已部署（生效 exe = build = `.p5` = `74D1F7CB…`，作者版 `.orig` 仍 `7ADD56B9…`），启动退出码 0。

### 若以后还要重做（或在别的机器上）
只需要三步：**源码编两个 /MT 库 → 放进 `third_party_local/amd/FidelityFX-SDK/sdk/{bin/ffx_sdk,include}` → 重新 cmake + 编**。
源码构建目录可留在 `D:\DSH\.dsh-tmp\ffx114\sdkbuild\`（用完可删，库已进仓库）。

## ✅ 2026-10-17 光流算法：NVIDIA NVOF 与 AMD FidelityFX 双开，并实测 AMD 跑通

### 用户需求
「光流算法」选择器里 NVIDIA 与 AMD 两个都要能用。

### 做法
1. **NVIDIA NVOF**：SDK 本来就在（`third_party_local/nvidia/Optical_Flow_SDK_5.0.7`），
   只是开关是 OFF。重新配置 `-DVEYRA_ENABLE_NVOF=ON` 即可（零下载）。
2. **AMD FidelityFX**：本机缺 SDK（`third_party_local/amd` 不存在），
   用 **浅克隆 + blob 过滤 + 稀疏检出** 只取 `Kits/FidelityFX`：
   ```
   git clone --depth 1 --filter=blob:none --sparse --branch v2.3.0 \
     https://github.com/GPUOpen-LibrariesAndSDKs/FidelityFX-SDK.git <repo>/third_party_local/amd/FidelityFX-SDK-2.3.0
   git -C <该目录> sparse-checkout set Kits/FidelityFX
   ```
   **实测占用 154.6 MB**（比官方 minimal 包 ~125 MiB 的整包略大，因为 Kits 里含预编译库；但**不拉 Samples**）。
   放好之后 **不需要任何开关** —— CMake 里是 `if(EXISTS …/ffx_api_loader.h)` 守卫，`VEYRA_HAS_FSR` 会自动定义
   （`build.ninja` 里从 0 处变成 9 处，这就是"生效"的铁证）。

### 结果
* 整树编译 rc=0（147 步），测试全绿（hdr_tuning 214/0、preset_library 505/0、ui_i18n 0 failures、
  effect_chain 319/0、repair_contract 272/0、vfg_settings 683/0、lmxxf_nr 62/0）。
* 部署：生效 exe = build = `.p5`（`C7E2693A…`），`.orig` 仍是作者版（`7ADD56B9…`）；启动退出码 0，无残留进程。

### FSR 集成测试的运行姿势（重要，不然会误判）
* `veyra_fsr_dispatch_tests`：参数可选，传 `amd` 走 AMD 后端。
* `veyra_fsr_switch_tests`：**必须给一个输出目录**（`argc<2` 直接 `return 2`），第二个参数可选 `amd`。
* 两者都要求 **`runtime_local` 在 exe 旁边**（这是**跟 exe 走**的，不是跟工作目录！）。把包里的
  `runtime_local/amd/fidelityfx/*.dll` 复制到 `build-nv/runtime_local/amd/` 后：
  * `FSR_INDEPENDENT pass=1 samples=39 intermediate=39 meanError=0` ✓（默认与 `amd` 参数都通过）
  * `SWITCH_CASE cycle=1 backend=AMD-FSR multiplier=2 generated=38 pass=1` ✓
  → **AMD FidelityFX 光流在实机上确实跑通**，不是"编进去就算"。

### XeSS（第三个后端）现状：不可用
* 程序找 `runtime_local/intel/experimental/libxess_fg.dll`，而包里/`runtime_local/intel/` 下的
  `libxess_fg.dll` 是 **0 字节占位**；`third_party_local/intel` 根本不存在 →
  `SWITCH_CASE cycle=2 backend=XeSS pass=0`。
* 要启用需要真正的 Intel XeSS 运行时（SDK 或作者 runtime 包），本次不做。**与 NVIDIA/AMD 两个无关。**

## ✅ 2026-10-17 补记：让整树编译在 2.0.9 上全绿（两处 CMake 适配）

升级验证时只编了"主程序 + 8 个测试目标"（33 步）就通过了，但**整树编译（424 步）**在 2.0.9 上还需要两处适配：

1. **FSR 诊断探针缺 AMD SDK** —— `tools/fsr_probe/main.cpp`（69 处）与 `tools/fsr_upscale_probe/main.cpp`（35 处）
   直接 `#include` FidelityFX SDK 头，而上游是**无条件** `add_executable` 它们。
   本机 `third_party_local` 只有 `nvidia`（857 MB），没有 `amd`。
   **修法**：照 `veyra_gfx` 已有的守卫（`if(EXISTS …/ffx_api_framegeneration_dx12.h)`），
   把两支探针包进 `if(EXISTS "${VEYRA_FIDELITYFX2_ROOT}/api/include/ffx_api_loader.h")`。
   探针是**诊断工具**，播放器与测试都不需要它们；装了 SDK 就会自动参与编译。
   顺带核实：`VEYRA_FIDELITYFX_ROOT`（1.1.4）在 CMakeLists 里只见定义、没有使用点，**不需要**。

2. **一批测试目标漏 FFmpeg 头路径** —— `veyra_audio_timeline_tests`、`veyra_capture_audio_tests`、
   `veyra_audio_playback_rate_tests` 等编译时报 `NetworkInput.h(9): 找不到 libavformat/avformat.h`。
   上游依赖自己的构建环境把这些路径导出，本机没有。
   **修法**：在测试目标之前加一条全局 `include_directories(${FFMPEG_INCLUDE_DIRS})`（比逐个目标打补丁更彻底）。

### 结果
整树编译 **424 步、rc=0**，产出 **116 个测试可执行文件**。定制 exe 同步进程序目录并刷新 `.p5`，
作者版 `.orig` 保持不变；启动一次 **退出码 0** ✓。

### 依赖结论（供以后判断要不要装 AMD SDK）
* 播放器与全部测试**都不需要** FidelityFX SDK：主程序里 FSR 后台本来就有守卫（`VEYRA_HAS_FSR` 未定义即关闭）。
* 只有两支诊断探针需要；官方 2.3.0 release 的 "Minimal package" 标注 **~125 MiB**，
  我们真正需要的只是 `Kits/FidelityFX` 下的 `api/include`、`framegeneration/include`、`upscalers/include`（远小于 125 MiB，未实测）。
* 结论：**不装 SDK 也能让整树全绿** —— 已用上面的守卫实现。

## ✅ 2026-10-17 升级到上游 v2.0.9：PR #44 已被合并，HDR 实现正式交给上游

### 结论先行
上游 2.0.9 **已经合并了 PR #44** ✓ —— 提交历史里有 `55c81c1c Merge pull request #44`，其中包含
我们的 `b1dffdf6 界面卡片化` 与 `3ee17b62 元数据面板 12px 间距`。作者随后追加了一批适配提交
（`b571a44b Record PR44 HDR integration…`、`26db779c Keep custom HDR presets and unavailable guidance honest…`、
`6d5951dc Cover the HDR display curve after real SDR to TrueHDR conversion` 等）。
**所以这次升级的性质变了**：不再"守住我们的 HDR 实现"，而是"把 HDR 交给上游，只留非 HDR 定制"。

### 规模
上游 v2.0.8 → v2.0.9：**62 个提交、94 个文件、+5802/−350**。合并进 custom 后冲突 **8 个文件**。

### 冲突清单与处理（全部取上游 `--theirs`）
| 文件 | 冲突块 | 处理 |
| --- | --- | --- |
| include/veyra/engine/HdrOutputTuning.h | 1 | 取上游 |
| include/veyra/engine/VideoPresenter.h | 2 | 取上游 |
| include/veyra/ui/QmlPlayerBridge.h | 3 | 取上游 |
| qml/Veyra/ProPage.qml | 11 | 取上游（卡片化与 12px 间距已在上游 ✓） |
| src/engine/EngineController.cpp | 2 | 取上游 |
| src/engine/VideoPresenter.cpp | 6 | 取上游 |
| src/ui/QmlPlayerBridge.cpp | 25 | 取上游 |
| tests/unit/HdrOutputTuningTests.cpp | 3 | 取上游 |

**判定依据**：取完后这 8 个文件与 `v2.0.9` **逐字节一致**（`git diff --stat v2.0.9 -- <8 files>` 为空），
说明我们早期自研的 HDR 曲线/元数据实现已完全由上游承载、且没有丢失任何仍需要的定制。
合并提交：`38955db9`；回退点：tag `before-2.0.9-custom`（= `1fd3cb8d`）。

### custom 相对 2.0.9 仍保留的 19 个文件（我们真正独有的）
杜比视界 P5（`DolbyVisionP5.h`、`DolbyVision.h`、`DolbyVisionRpu.h`、`FramePacket.h`、`YuvToLinearRgb.hlsl`、
`MediaFileSource.cpp`、`EnhanceGraph.cpp`）、构建与工具（`CMakeLists.txt`、`tools/dovi_p5_probe/*`、
`tools/veyra-switch/make_switch.py`）、界面小定制（`HomePage.qml` 清空记录、`SettingsPage.qml`、`VSlider.qml`）、
本仓库材料（`CUSTOM_LEDGER.md`、两份 docs、`i18n/catalog.json`、`RepairContractTests.cpp`）。

### 新坑一：上游新增的两支 FSR 诊断工具在本机编不过
* 现象：整树编译在 `veyra_fsr_probe` / `veyra_fsr_upscale_probe` 处 `fatal error C1083`，
  缺 `third_party_local/amd/FidelityFX-SDK-2.3.0/.../ffx_api_loader.h`。
* 真因：`third_party_local` 下只有 `nvidia`，没有 `amd`；这两支工具是 **2.0.9 新增**且**无条件 include** 该头。
* 处理：验证时**只编需要的东西** ——
  `cmake --build build-nv --target veyra_qml_ui veyra_hdr_tuning_tests veyra_preset_library_tests veyra_ui_i18n_tests veyra_effect_chain_tests veyra_repair_contract_tests veyra_vfg_settings_tests veyra_lmxxf_nr_tests`
  一次通过（33 步）。**规矩：整树编译失败时先看是不是"缺某个可选 SDK 的探针工具"，不必为它阻塞升级。**

### 新坑二：三个测试的"运行姿势"（不然会误判为失败）
* `repair_contract_tests`：需要 **FFmpeg 的 bin** 在 PATH（`deps/ffmpeg/…/bin`），否则 `0xC0000135` 缺 DLL。
* `vfg_settings_tests`：**必须给一个绝对路径参数**当根目录（`argc != 2` 直接 `return 2`，且无任何输出）。例如传一个绝对目录：`D:\DSH\.dsh-tmp\vfg-settings-root`（记得先删掉再跑，跑完可清理）。
* `lmxxf_nr_tests`：结果写在 **stdout 最后一行**（`Lmxxf C ABI/identity GPU copy: 62 checks 0 failures.`），
  不要把"PASS=0"当失败；工作目录必须是构建目录。
* 另外：`pr19-hdr-persistence` 与 `veyra-preset-library-tests` 两个 `%TEMP%` 目录要在跑之前清掉（上一次的残留会让 `put()` 失败）。

### 本次验证数据（全部通过）
* 编译：主程序 `veyra_qml_ui.exe` + 8 个测试目标，`rc=0`。
* 测试：`hdr_tuning` **214/0**（2.0.8 是 213，上游多了 1 项集成检查）、`preset_library` 505/0、`ui_i18n` 0 failures、
  `effect_chain` 319/0、`repair_contract` **272/0**（原 265）、`vfg_settings` **683/0**、`lmxxf_nr` 62/0 —— 合计 **2055 项、0 失败**。
* 部署：程序目录改名为 `Veyra-2.0.9-NVIDIA-win64-portable`，改名前后哈希一致
  （exe `21722D52…`、`YuvToLinearRgb.dxil` `CCCAE8B7…`、`ProPage.qml` `0A39946A…`）。
* 切换脚本：`make_switch.py --apply` 重新生成 ✓ —— 着色器比对 49 个（顶层 30 + dis 19）差异仅 1 个
  （`YuvToLinearRgb.dxil`），界面 60 个文件差异 3 个（`HomePage.qml` / `SettingsPage.qml` / `VSlider.qml`），
  exe 与那 3 个界面文件 + 1 个着色器进入切换集合；已生成 `启动-P5修复版.cmd` / `启动-作者完整版.cmd` / `版本切换说明.txt`，生效设为定制版。
* 启动：定制版退出码 **0** ✓。日志有 2 条**上游自带**的 QML 告警（`DialogHost.qml:767` 与 `:1219`，
  `Unable to assign [undefined] to bool`）—— `DialogHost.qml` 不在我们的定制集合里，非致命，记录备查。

### 工具给出的待办提示
`make_switch` 报告「构建有、包里没有的着色器：`shaders\GenerateTestPattern.dxil`」（2.0.9 新增的测试图案着色器，
作者的包里也没带）。不影响播放；若后续需要"生成测试图案"功能，把它复制进包并纳入切换集即可。

## ✅ 2026-10-16 界面卡片化：复查、提 PR、以及两个新坑

### 做了什么
把 HDR 亮度曲线面板从平铺长列表重排为五张可折叠卡片（曲线图、读数与提示 / 一键选档 / 基础设置 / 高级设置：技术参数 / 记忆与自动化），
卡片直接复用作者自己的 `VAccordion` + `enabledSwitch: false`（因此边框、头部、折角、展开动画与色彩页一致），
行内提示统一 `VRow.hintBelow`，数值列收窄、长说明移到卡片底部小字。

### 提交方式（重要：fork 场景不能做"链式 PR"）
* 试着新建 PR、base 指向 PR #44 的分支时，GitHub 直接拒绝：
  `No commits between Likely7:hdr-curve-metadata and georgeg911:hdr-panel-cardify` / `Base ref must be a branch`。
* 原因：**PR 的 base 必须是上游仓库里的分支**，而 `hdr-curve-metadata` 只在我们的 fork 里。
  对 `main` 新建又会把 #44 重复一遍，所以正确做法是**把这批提交推到 #44 同一分支**（fast-forward），
  并在 #44 下留说明评论：https://github.com/Likely7/Veyra-NRVideo/pull/44#issuecomment-6197464565
* 撤销方式：`git push -f fork ced1d8c2:hdr-curve-metadata`。

### 提 PR 前必须剔除的两类"假差异"
1. **行尾噪声**：`include/veyra/ui/QmlPlayerBridge.h` 显示 1375 行差异，忽略行尾后**真实差异只有 1 行**
   （`git diff --ignore-cr-at-eol` 一眼看穿）。提 PR 时要 `git checkout <base-branch> -- <file>` 把它还原，
   否则 PR 里会出现整文件重写。**规矩：凡是 diff 行数与文件行数同量级，先怀疑行尾。**
2. **本仓库自有文件**：`CUSTOM_LEDGER.md` 这类个人台账不进对外 PR；`i18n/catalog.json` 则必须带上（纯新增，保证 i18n 测试绿）。

### 新坑：重复跑测试会被"上一次的残留"污染
* 现象：`veyra_hdr_tuning_tests` 212/1、`veyra_preset_library_tests` 504/1，稳定复现，
  失败行都是 `check(library.load() && library.put(entry), ...)` —— `load()` 成功但 `put()` 被拒。
* 真因：这两个用例**复用固定名字的临时目录**（`%TEMP%\pr19-hdr-persistence`、`%TEMP%\veyra-preset-library-tests`），
  上一次运行写下的文件让这一次的 `put()` 失败。`PresetLibrary.*` 与 `PresetLibraryTests.cpp` 相对 v2.0.8 **零差异**，
  `ninja: no work to do` 也说明二进制与源码一致 —— 所以**不是代码问题**。
* 处理：删掉 `%TEMP%` 下这两个测试目录再跑 → 立刻 **213/0 与 505/0**。
  **规矩：持久化类测试失败时，先清它自己的临时目录再判断，别急着改代码。**

### 本次验证结果（提 PR 的证据）
`qmllint` 0 error；应用启动退出码 0；`ui_i18n` 0 failures；`hdr_tuning` 213/0；`preset_library` 505/0；
逐项核对五张卡片的 glyph/hue/open/summary 与 `hintBelow`（12 处）；确认没有任何测试/工具/C++ 依赖我们改过的标签与 objectName（0 处）。

## ✅ 2026-10-13 HDR 亮度曲线「第二梯队」：人话滑杆、内容×环境两轴（保留旧七档）、每片记忆、开播自动选档、SDR 确认

用户要求第二梯队**五项全做**，并明确：**原七档预设必须保留**，新增「内容 × 环境」两轴，
**两套可切换**。

### 设计要点

* **两套预设写的是同一条曲线** —— 所以「旧/新预设」只是界面选择，存在**内部偏好键**
  `hdrPresetMode`（`qml-preferences.v1.json`），**不碰存档格式**（与 `hdrProfiles` 同机制）。
* **旧七档原样保留**（`kHdrTuningPresets` 未改），仅在新模式下隐藏。
* **新两轴** `kHdrContentPresets`（内容：`standard/dark/highlight/darkAndBright/punch`）×
  `kHdrEnvironmentIds`（环境：`bright/dark`）：
  * 内容基线直接沿用旧预设的曲线数字（两套系统对"暗场/高光"的理解一致）；
  * **环境轴只移动中灰**（白天 +45、夜晚 −18，clamp 100..500）——
    **绝不碰高光档**，这样两个轴不会互相抵消（有单元测试钉住这条不变量）。
* **人话滑杆**（`kHdrShadowLiftSteps` / `hdrBrightnessNits` / `kHdrHighlightSteps`）：
  暗部提亮 0~10、整体观感 0~10（中灰 100~500）、高光保护 关/弱/中/强。
  反向查找取**最近档**，所以用技术滑杆改过的曲线也能显示合理档位；
  **精确技术参数仍在「高级：技术参数」区**，信息不丢。
* **每片记忆** `hdrTitles`（有界列表，最多 60 部，先进先出）：**换片时先存旧片、再套新片**
  （与显示设备记忆同一套思路）；键用 `sourceLabel`（文件路径 / 串流标签）。
* **开播自动选档** `hdrAutoRecommend`（默认开）：仅在**当次没有影片记忆**时用推荐，
  且**绝不自己把曲线打开**（`settings().hdrCurve.enabled` 为假时不动）。
* **⑧ SDR 片源**：查代码确认曲线门控是 `hdrCurve.enabled && graph.hdrOutput()`
  （`VideoPresenter.cpp:269`），**只要求 HDR10 输出、不要求片源是 HDR** →
  「SDR 片源 + HDR10 输出」**本来就已经生效** ✓，本轮只补了界面说明
  （SDR 无真高光，只有暗部提亮与整体观感有意义），未新增代码路径。

### 踩到的坑

* **`std::string_view` 缺 include**：阶段 A 只在单元测试里编译过（测试文件恰好间接包含了
  `<string_view>`），**编译 exe 时才暴露** ✗。已在 `HdrOutputTuning.h` 补 `#include <string_view>`。
  → 教训：**改了公共头文件必须同时编译主程序**，只跑测试会漏。
* 脚本书写问题两处（函数签名不统一导致整批未写入；锚点撞车），已用"命中次数必须为 1"的
  自检拦下，未污染代码。

### 测试与验证

* `veyra_hdr_tuning_tests`：**175 PASS / 0 FAIL**（上一轮 137 → 本轮 +38：
  两轴不变量、未知 id 回退、8 种组合中灰范围、三根滑杆逐档往返、离网格取最近档）
* `qmllint`：`ProPage.qml` **0 error**
* 部署：exe **6,185,472 B**（`.p5` 相同）、`ProPage.qml` 146,145 B；
  **作者版备份未动**（exe 20,079,104 B / qml 134,692 B）
* 启动验证：**退出码 0**、只有 2 条既有 DialogHost 提示、无新增 QML 报错
* 新增界面 objectName：`list-hdrcurve-mode`、`-content`、`-environment`、
  `-dial-lift`、`-dial-brightness`、`-dial-highlight`、`-titlememory`、`-autorecommend`、`-forget-titles`

### 用户反馈后的两轮修正（同日）

1. **七档旧预设的位置**：初版把新界面块插在旧预设之前，旧七档 chips 落在了滑杆/记忆之后，
   读起来断层 ✗。用户要求"七档旧预设跟随『旧预设/新预设』位置下方" →
   已把 chips + 说明整体**移到「预设方式」行的正下方**（提交 `9ae6cc9`）。
   现在顺序：预设方式 → 旧七档（legacy）→ 内容/环境（axes）→ 人话滑杆 → 记忆/自动选档 → 高级技术参数。
2. **一键推荐跟随模式**（用户提问"推荐的是旧预设还是新预设"）：初版推荐只返回**旧档 id** 并应用旧档曲线，
   在 axes 模式下会出现档位对不上 ✗。现在按 `hdrPresetMode()` 分流（提交 `859b957`）：
   * legacy → 应用旧档（同以前）；
   * axes → 新增纯函数 `hdrAxesContentForPreset` 把结论映射到**内容**轴
     （`darkLift→dark`、`highlightGuard→highlight`、`punch→punch`、其余→`standard`），
     **环境沿用用户当前选择**，与手动在新模式下选档行为一致；
   * 界面提示按模式显示不同名称（旧模式"建议 暗场提亮"、新模式"内容：暗场多"）。
   * 补 2 项映射测试（含 `nullptr` 与纯环境档回退）。
3. **部署状态**：已部署（生效 exe **6,186,496 B** 与构建产物逐字节一致、`ProPage.qml` 146,650 B，
   作者版备份 20,079,104 B / 134,692 B 未动），启动验证退出码 0、2 条既有提示。
   测试 `hdr_tuning` **177 PASS / 0 FAIL**、`qmllint` 0 error。

> **踩坑记录（重要）**：把 PowerShell 脚本写到 `D:\DSH\.dsh-tmp\*.ps1` 时，若脚本内含中文且
> 文件是**无 BOM 的 UTF-8**，Windows PowerShell 会按 GBK 解析 → 中文变乱码、连字符串引号都被破坏 →
> 解析失败（后台等待部署任务因此连挂两次 ✗）。
> **结论：`.ps1` 里不要写中文**，或必须写带 BOM 的 UTF-8。同理，后台任务里不要用 `pwsh -File`
> 调另一个 pwsh（任务环境里没有 `pwsh` 命令 ✗），应直接 `& script.ps1`。

### 第三梯队

用户决定：**做 ① 与 ③，不做 ②（受限动态模式）**。

**③ 已完成并部署**（提交 `57363f9`）：

* **③a** 一键推荐不再重置「强度」与「显示设备峰值」（与「内容×环境」选档一致）。
* **③b** 面板内曲线可视化：采样由纯函数 `hdrCurveLuminance` 完成，它是
  `shaders/PresentBlit.hlsl` 中 `HdrCurveLuminance` 的**逐行移植**
  （暗部 `w²·(2^EV−1)` 提升 → 中灰幂律 `peak·(n/peak)^g` → 滚降 `start+(n−start)/(1+(n−start)/scale)`
  → 硬上限），所以图不会承诺 GPU 做不到的事。桥接属性 `hdrCurveGraph`（48 点对数扫描，
  0.5~4000 nit），QML 用 Canvas 画：虚线为参考对角、橙线为当前曲线（未启用转灰）。
  新增 7 项测试 → `hdr_tuning` **184 PASS / 0 FAIL**。

> 曲线公式速查（做任何曲线相关改动都要对照它）：
> `PresentBlit.hlsl` 的 `curve0 = {shadowLiftEv100/100, shadowRangeNits, midGrayNits, highlightStartNits}`、
> `curve1 = {max(1,cap−start)·rollOffPercent/100, peakCapNits}`；
> 门控 `flags & 16`，且只在 `graph.hdrOutput()` 且曲线 enabled 时置位；
> 输入乘 80 得 nit、输出除 80 回线性（`1.0 = 80 nit`）。

**① 实时裁剪表：语义核心已完成，GPU 管线部分尚未实现**

**已完成（提交见下）**：统计定义做成纯函数 `hdrCurveCoverage()` + `HdrCurveCoverage`
（`aboveCeiling` / `inRollOff` / `inLift` 三个桶与百分比），
把"被压 x% / 暗部 y%"的含义**固定在测试里**而不是藏在渲染循环里：7 项测试覆盖
（有效曲线计数、只有超出上限才算压平、滚降区、提升区、未启用曲线**不返回看起来像真的 0**、
无样本、0/负数/非有限值被忽略）→ `hdr_tuning` **191 PASS / 0 FAIL**。
调用方传入**已 resolve 的曲线**，因此显示峰值重算与强度拨盘都已包含在阈值里。

**待实现（GPU 部分）**：**GPU 探测与回读已实现并编译验证**（见下），仅剩"接界面"。

> **已落地的 GPU 部分（2026-10-13 第三轮）**：
> * `refresh()`：RTV 堆 **4 → 5**，第 5 个描述符给探测目标；目标是 **64×36、格式跟随交换链**
>   （因此可复用现有 present PSO；只解码 10-bit HDR10，scRGB 半浮点**跳过而不是猜**）；
>   回读缓冲用 READBACK 堆（64·36·8 = 18,432 B 上限）。
> * 探测绘制插在**主绘制正后方**：同一个着色器再画一次，**去掉曲线位(16)与 PQ 编码位(4)、保留解码位(32)**
>   → 线性值，CPU 侧 `nits = luma(Rec.2020)·80`；改动的状态（RTV／视口／裁剪区／根常量）**立即原样还原**。
>   仅在曲线启用且每 15 帧发一次，且 `probePending_` 时不再发（防重入）。
> * **非阻塞回读**：`ring.submitAndSignal()` 之后记 `ring.lastSignaledValue()`，
>   下次 present 时若 `fence->GetCompletedValue() >= probeFenceValue_` 才 `Map`（普通读取，**绝不等待 GPU**）。
> * `close()` 释放探测资源；`liveCurveCoverage()` 供引擎读取。
>
> **验证到的**：GPU 集成测试 `veyra_hdr_output_tuning_gpu_tests` **failures=0**（真渲染并检查 DXGI 像素）
> → **呈现路径没被改坏** ✓；日志可见 `coverage probe 64x36 format=24`（R10G10B10A2）创建成功 ✓，
> SDR 格式按设计静默 ✓；`hdr_tuning` 191 PASS / 0 FAIL ✓。
>
> **尚未验证**：探测绘制与回读**尚未被真正跑到**（集成测试里曲线未启用 → `curveFlag=0` → 不发探测）。
> 实机播放 HDR10 且曲线启用后，日志会出现
> `[present-probe] coverage aboveCap=… rollOff=… lift=… samples=2304`。
> **剩余工作**：**已完成**（presenter → `PlayerSnapshot` → 桥接 `hdrCurveCoverage` → 面板「实时裁剪」一行）。
> 面板显示"高光被压 x% · 滚降区 y% · 暗部提升区 z%"，未探测到时显示"播放中且曲线开启时才会测量"，
> 高光被压 >5% 时标"偏高"。
> **实机验证：用户已确认通过（2026-10-13）** ✓
> 1. 「实时裁剪」措辞正常（显示"未触及 / 未设上限"，不再是裸的 0.0%）✓
> 2. 把「峰值上限」拖到 300~400 后「高光被压」**变成非 0** ✓
>    → **整条测量链路（探测绘制 → CopyTextureRegion → PQ EOTF 解码 → 分桶）端到端确认可用** ✓
> 3. 切换显示设备后曲线配置**自动切换** ✓（第二梯队"一机一套配置"实机确认 ✓）
>
> 排查过程中连踩三层（都已在本文档与提交里记录）：
> ① `CopyResource` 纹理→缓冲（D3D12 非法）→ 回读恒 0；
> ② 探测承载线性值被 10-bit UNORM 钳在 80 nit → 高光桶数学上恒 0；
> ③ 探测 RTV 只在创建时写 slot 4，注册表重建后会失效（审计发现）。
> 教训：**跨格式/跨数值域的改动必须用标准参考点（如 PQ 的 100/1000 nit 码值）做单元测试**，
> 只靠"看起来合理"的推理会连着错三遍。

已侦察清楚的事实与插入点：

| 位置 | 现状 | ①要做什么 |
| --- | --- | --- |
| `VideoPresenter::refresh()` L108 | `hd.NumDescriptors=4`（3 个交换链缓冲 + 1 个合成目标，正好用满） | 扩到 **5**，第 5 个给探测目标 |
| `refresh()` L137-154 | 合成目标 `compositeTarget_` 的创建/建 RTV 可作模板 | 新建 **64×36 `R16G16B16A16_FLOAT`** 探测纹理 + RTV + `READBACK` 堆回读缓冲（64·36·8 = 18,432 B）+ 独立 fence |
| 呈现 pass（`pass_` / `dims[16]`） | 曲线常量在 L265-280 打包；`curveFlag` 依赖 `graph.hdrOutput()` | 用**同一个 PresentBlit 着色器**再画一次到探测目标，但**去掉曲线位(16)与 PQ 编码位(4)**、保留解码位(32) → 得到线性值，CPU 侧 `nits = value·80` |
| 回读时机 | 现成的 `readPresentedFrameForTest()` 会 `drainQueue()` 造成停顿 ✗ | **不可复用**：必须用独立 fence，只在 `GetCompletedValue() >= 目标值` 时 `Map`，否则跳过；每 ~15 帧发一次探测 |
| CPU 统计 | 无 | 对 2304 个像素算直方图 → 「高光被压 %」（超出生效硬上限，上限用 `hdrEffectivePeakCapNits`）与「暗部占比 %」（低于 `shadowRangeNits`） |
| 贯通 | — | `PlayerSnapshot` 加字段 → 桥接 `hdrCurveCoverage` 属性 → 面板一行小条 + 数字，约 2 次/秒刷新 |

**关键约束（别踩）**：探测**绝不能阻塞呈现**（这是当初放弃复用 `readPresentedFrameForTest` 的原因）；
探测目标是只读用途，不参与最终画面；作者版 `.orig` 备份不得改动。

### 推荐功能的后续计划（2026-10-14 用户决定：两项都放弃）

* 「用实测数据细化推荐」不做。原因：黑场片头会让「测前 N 秒取平均」毫无意义；
  而且 MaxFALL 本就是**最亮帧平均**（Maximum Frame-Average Light Level），
  正确的实测对照量是「帧平均的最大值」而不是时间平均 —— 这一点此前在界面文案里写错了，
  已改为「最亮帧平均」。
* 「攒日志校准阈值」也不做。原因（用户指出）：那样得到的阈值只贴合攒日志那个人的片单，
  随版本发给所有人并不合适。`hdr-recommend` 日志保留，但用途改为**诊断**。
* 结论：推荐阈值保持**基于标准与惯例的固定值**，并靠界面上的理由文案让人自己判断是否合理。

**② 受限动态模式：明确不做**（与显示器动态色调映射两级串联，历史上已因此关闭）。

## ✅ 2026-10-13 HDR 亮度曲线三项增强：一键推荐、一机一套配置、高光预警

用户提出"这个功能还能加什么、尽量傻瓜一键、动态模式是否可行"。**结论：不加动态模式**
（理由见下），改为加三项"零/低成本的傻瓜化"功能。

### 为什么不做动态模式（重要，别再重做）

台账 §8 记录过：我们**早就实现过逐场景自适应亮度**（强度/目标峰值/响应速度/过渡时间，
用图里已有的 64×36 直方图，不新增 pass、不回读），**已关闭**。原因至今成立：

1. **与显示器自带的动态色调映射两级串联** —— 两边都在按场景调，必然过冲、迟滞、忽明忽暗；
2. **静态恰恰是这个功能的卖点**（"nothing follows the scene, so the picture can never breathe"）；
3. 另外"整片分析"也做过并被用户自己否掉（要每 15 帧下载一帧，4K ≈16 MB/次、~64 MB/s，
   还占解码线程，只换来一次性选档建议）。

若将来仍要做，唯一站得住的做法是**受限动态**：逐镜头（复用已有场景切分）、**只动暗部提亮**、
慢时间常数 + 迟滞 + 幅度夹子、默认关、可用 A/B 立刻验证。

### 关键发现：实时裁剪表在硬解路径上做不了（③ 因此改设计）

原计划 ③ 是"用图里现成的 256 桶直方图算实时裁剪比例"。**实测否定**：

* 直方图只在 **CPU 路径**（RGB/packed/YUY2、NV12/P010 上传、采集）更新
  （`EnhanceGraph.cpp` 的两处 `analyzeLuma`，其一要求 `frame->data[0]`，其二要求 `planes[0]`）；
* **D3D12/D3D11 硬解走 GPU 纹理分支**，没有 CPU luma → 直方图**永远是空的**；
* 证据：208.9 MB 的真机日志里 `[scene]` 行为 **0**、`sceneCutCount` 也为 0，
  而日志里有多部 4K HEVC `hw=true` 播放。

所以实时表需要**新增一次 GPU 下采样 + 回读**（约 4 次/秒；`VideoPresenter::readPresentedFrameForTest`
证明回读机制存在，但用共享呈现环会 `drainQueue` 造成停顿，需独立环 + 非阻塞轮询）。
**本轮不做**，改为零成本的替代方案（见 ③）。

### 三项实现

**① 一键按片源选档**（`HdrOutputTuning.h` 纯函数 `recommendHdrTuning`）

用**容器层**元数据（母版峰值 / MaxCLL / MaxFALL）——硬解也有，零成本。判据顺序（**顺序很关键**）：

| 条件 | 选档 | 界面理由文案 |
| --- | --- | --- |
| 无元数据（抓屏/串流） | 不动作，`available=false` | 片源没有携带亮度信息 |
| `MaxFALL/MaxCLL ≤ 0.20` | `darkLift` | 整片偏暗、只有少量亮点 |
| 母版峰值 < 600 nit | `punch` | 母版峰值偏低（老片/转制片） |
| `MaxCLL ≥ 4000` 或比值 ≥ 0.45 | `highlightGuard` | 高光又亮又多 |
| 其余 | `standard` | 亮度分布均衡 |

> 踩过的坑：最初把"高光极端"判在"低母版峰值"之前，导致**老片永远到不了 punch 档**
> （低峰值片子的 FALL/CLL 比值天然偏高）。已把顺序改成 暗场 → 低峰值 → 高光极端 → 标准，
> 并由单元测试钉住。

**② 一机一套配置**（投影仪 / 电视各一套）

* 设备身份用 **EDID 派生的 `DISPLAY_DEVICEW.DeviceID`**（换线换口仍稳定），
  取不到时退回 GDI 设备名（`\\.\DISPLAY1`）；都是廉价 Win32 调用，不走 DXGI 枚举。
* 存储用**内部偏好键** `hdrProfiles`（`qml-preferences.v1.json`，与 `lastWindow` 同机制，
  不经 `setPreference` 校验）→ **不碰存档格式**。
* 切换逻辑 `syncHdrDisplayProfile()`：显示设备 id 变化时，先把当前曲线存回**旧设备**，
  再套用**新设备**的档案；首次见到的设备直接记下当前设置。
* 钩子挂在桥接侧 `impl_->poll()` 之后（`Impl::poll` 内没有 `this`，最初插错位置导致编译失败）。
* 界面：一行"已记住 N 台显示设备" + 「清除记忆」按钮。

**③ 高光预警**（替代实时裁剪表）

用片源自身的 `MaxCLL`（没有则母版峰值）与**曲线生效后的硬上限**比较
（`hdrEffectivePeakCapNits` 走同一套 `resolveHdrTuning`，含显示器峰值重算与强度拨盘）。
界面显示"片源最亮 X nit，曲线硬上限 Y nit：上限以上的高光会被压平 / 上限已覆盖片源峰值"。
**不改画面**，纯只读。

### 测试与验证

* `veyra_hdr_tuning_tests`：**133 PASS / 0 FAIL**（原 124，新增 9 项：6 项推荐判据 + 3 项硬上限）
* `veyra_preset_library_tests` 505/0、`veyra_effect_chain_tests` 315/0、`veyra_repair_contract_tests` 265/0
* `qmllint` 对 6 个定制 QML 文件 **0 error**（warning 均为既有）
* 新增界面控件 objectName：`list-hdrcurve-auto`、`list-hdrcurve-forget`
* **未做**：真机素材实测（部署时用户播放器正在运行，见下）

### 部署与两个后续修正（2026-10-13 当晚）

**首轮部署时实测到两个问题，都已修完：**

1. **界面"显示设备峰值"报 10000 nit 是假值**（用户截图发现：峰值 10000、全屏 10000、黑位 0.000）。
   `VideoPresenter.cpp:199` 在"自动"时直接把系统报告值 clamp 后当真实峰值用 → **预设按 1000 nit 写的
   高光数字被 ×10 缩放**，"高光保护"那档的硬上限被放大到 10000 nit，**保护形同虚设**。
   修法：**不可信的报告直接不用** —— 判据 `峰值 ≥ 4000 且黑位 ≤ 0.001` 视为 EDID 上限而非测量值，
   此时不改 baseline（0 = 沿用预设自带的 1000 nit 参考），并在界面那行文案里说明
   "系统报告不可信（报 N nit 且黑位 0.000）：已按 1000 nit 参考值计算"。
2. **我引入的除零崩溃（0xC0000094）**：`hdrPeakWarning()` 里 `cap*100/sourcePeak`，
   当**片源没有元数据（peak=0）而曲线带硬上限（cap>0）**时除以 0 ✗ —— QML 启动时就会求值它，
   所以**一启动就崩**（启动验证抓到，退出码 0xC0000094）。
   修法：把这段算术抽成 `HdrOutputTuning.h` 的**纯函数 `hdrPeakWarning(...)`** 并加保护，
   桥接只做搬运；补 4 项单元测试（含"片源无元数据时保持惰性"的回归用例）。
   **教训：新加的界面属性若有除法，必须走纯函数 + 单元测试，不能只靠 qmllint 与编译**。

**已验证并部署**（用户关闭播放器后）：

* 生效 exe = **6,167,040 B**（`.p5` 相同；`.orig` 仍是作者 2.0.7 的 20,079,104 B ✓）
* `qml\Veyra\ProPage.qml` = 源码版本 ✓（`qml.orig` 仍是作者版 ✓）
* 启动验证：**退出码 0**、无 crash、无会话损坏，只有 2 条既有 DialogHost 提示，`SetHDRMetaData` 0 次
* 测试：`veyra_hdr_tuning_tests` **137 PASS / 0 FAIL**（含 4 项预警测试）

### 部署状态

**已完成**（见上）。切换集合仍只有 exe + 6 个 QML 文件，作者版 `.orig` / `qml.orig` 未动 ✓。

### 三项实现（初版记录）

## ✅ 2026-10-15 HDR 亮度曲线 + HDR 元数据：与作者对齐、修掉我自己的三个坑、PR #44

**已发起 PR**：https://github.com/Likely7/Veyra-NRVideo/pull/44
（分支 `hdr-curve-metadata`，**从上游 tag `v2.0.8` 直接开出**，10 文件 / +1664 −18；
后追加 `ced1d8c2` —— 点亮跟随最后点击那个系统）

### 从 `custom` 剥出干净 PR 分支的做法（可复用）

1. `git checkout -b <pr> v2.0.8` —— **绝不基于 custom**（那样会把 19 个无关定制一起带上）
2. 逐文件：`git checkout v2.0.8 -- <file>` 回到上游原文 → `git diff v2.0.8 custom -- <file>` 取补丁
   → 按 hunk 判断（含 HDR 关键字 → 保留）→ `git apply --recount`（**位置由 git 保证**，不靠手写匹配）
3. 每次都做**三重复核**：文件**行数**是否合理、有无**重复行**、**夹带关键字是否为 0**
4. 最后在分支上**独立编译 + 跑全套测试** —— 只有它能证明补丁自洽完整
   （本次：编译 exit 0、`hdr_tuning` 213 checks、GPU/effect_chain/preset_library/ui_i18n/vfg_settings/lmxxf 全绿）

### 三个坑（我犯的，务必别再犯）

1. **块级回退把文件改坏** ✗：用 difflib 按"块"回退非 HDR 改动时对齐错位，`EngineController.cpp`
   出现**重复行**（编译不过）。**教训**：整块替换不安全 → 用 **hunk 级 `git apply`**。
2. **补丁拼接留空行** ✗：`split("\n")` 的末尾空元素混进 hunk → `git apply` 直接失败。
   **教训**：用 `splitlines()`。
3. **误丢 hunk** ✗：保留清单漏了「曲线生效时会按曲线后的输出上报」这句 → 元数据面板提示语被丢掉。
   **教训**：保留清单**宁宽勿窄**，并把**丢弃的块逐块打印**出来人工过一眼。
   另有一处**误判**：把 `resizeSourceColor` 当成 DoVi 内容回退掉了 —— 它其实是我们的 HDR 源数值采集。

### 一致性清理（PR 前）

* **注释译成英文**（代码库约定）；界面 `qsTr` 字符串**保持中文**（上游的源串也是中文）
* **objectName 无需改名** ✓：上游自己就用 `list-hdr-curve` / `list-hdrcurve-` / `list-hdrmeta-`，
  我们的 `list-hdrcurve-*` **正是作者的前缀**（我一度想改成 `list-hdr-curve-*`，查证后放弃 ✗）
* **推送前置**：fork 里缺 `.github/workflows` → **必须**先 `gh auth refresh -s workflow`，
  否则 `git push` 会被 GitHub 拒绝（这次用户已授权 ✓）

### HDR 元数据（恢复上游实现 + 三处对齐）

* 当年为修"桌面卡顿（悬停掉帧）"把 `applyHdrMetadata` 整段移除；**2.0.8 实测不卡**（同一台 60Hz 投影仪 ✓）
  → 用**上游原文**恢复（36 行），并保留我们的 ③①：
  * **③** 片源非 HDR 且未手动填值 → 不发（此前会虚报兜底 **1000 nit**）
  * **①** `clampMetadataToCurve()`：上报值按**曲线真正执行的上限**收敛（复用已测的 `hdrEffectivePeakCapNits`）
  * **④** 面板加一句说明
* 「HDR 元数据」折叠面板**加回界面**（此前连 UI 一起去掉 → 引擎恢复了却无法启用 ✗）

### 预设点亮：从"按数值匹配"到"跟随最后点击"

用户实测：两套预设写**同一条曲线**（「不补偿」时 自由组合 == 整档预设），所以一条曲线会**同时**
被两套认定 → 切视图时另一套也亮，看起来像"两个模式同时生效"。

* 第一次尝试：给点亮条件加"当前模式"前缀 → 但**点档位不切模式** ✗ → 反而变成
  "点自由组合的档，整档预设那一行亮" ✗
* 第二次：点任意档位**同时切换视图** ✓
* 第三次（最终 ✓）：新增偏好 **`hdrAppliedFrom`（最后生效来源）**，**与 `hdrPresetMode` 分开** ——
  只在"真正应用"时更新（点档位 ✓、点一键推荐 ✓），**切视图不动它** ✓
  → 只点亮**最后点过的那一套**，另一套整行不亮 ✓（用户已确认"就是我想要的效果"✓）
* UI 文案：**整档预设 / 自由组合**（原 七档旧预设 / 内容×环境）；小标题「**选档方式**」；
  两套各有一行状态（**当前生效：整档预设「X」** / **当前组合：内容 X · 环境 Y** + 等价说明一行）
* **自伤二连**（都修好了）：复制 `Q_PROPERTY` 时漏了 **NOTIFY 信号声明** → 编译 `C3861` →
  改为复用已有的 `preferencesChanged`；以及 **`WRITE` 子句被改漏**（指向 `setHdrPresetMode`）→ 已修正。
  **教训**：复制属性行必须**逐字段**核对（READ / WRITE / NOTIFY 三处）。

---
## ✅ 2026-10-15 升级到上游 v2.0.8：上游已并入我们的 HDR 曲线基础与 #29 开关修复

**版本信息**：上游 tag `v2.0.8` = `33b22b91`（57 提交、125 文件）；下载包
`D:\下载\DLSS5\Veyra-2.0.8-test-NVIDIA-win64-portable` 与 tag **逐字节一致** ✓
（ProPage / VSwitch / HomePage / Main 四文件规范化行尾后哈希相同）。
回退点 tag `before-2.0.8-custom` = `3d798df`。合并提交 **`626c3a9d`**。

### 上游这版最重要的两件事（对我们是好消息）

1. **HDR 亮度曲线的基础已被上游收下** —— `v2.0.8` 的 `ProPage.qml` 里已有
   `HDR 亮度曲线` 面板，注释文字与我们仓库当年的写法一致。所以我们的定制
   **不再是与上游分叉的独立实现，而是在上游基础之上的增量**（两轴、人话滑杆、
   实时裁剪表、一键推荐、曲线图、每片记忆）。`HdrOutputTuning.h` 上游**未改动** ✓。
2. **上游采纳了我们提的 GitHub #29（开关绑定 bug）** —— `VSwitch.qml` 现在是受控写法
   `onTapped: sw.toggled(!sw.checked)`，注释里直接写着 `(GitHub #29)`。
   本次合并据此**取上游版本**，并确认上游也补了 `DialogHost.qml` 本地复选框的回写 ✓。

### 冲突（17 个交集文件 → 5 个冲突文件）

| 文件 | 处理 |
| --- | --- |
| `qml/Veyra/VSwitch.qml` | 取上游 ✓（就是我们的修法 + 引用 #29） |
| `qml/Veyra/DialogHost.qml` | 取上游 ✓ |
| `tests/qml/quick/tst_components.qml` | 取上游 ✓（按新契约改了测试） |
| `i18n/catalog.json` | 上游 2307 条为基 + 按 `zh` 补回我们独有 71 条 = **2378 条** ✓ |
| `src/ui/QmlPlayerBridge.cpp` | 只 1 处真冲突：上游 `emitChainChanged()`（等价写法）+ 保留我们的
两行 `syncHdrDisplayProfile(); syncHdrTitleProfile();` ✓ |

**核验**：定制增量 **20/20 存活** ✓（HDR 曲线增量、元数据移除、中文串、首页清空、VSlider、
DoVi P5、P5 探针目标、CMake `/STACK` ✓）；编译 exit 0 ✓；可运行测试套件**全绿** ✓
（hdr_tuning 207、GPU 23、preset_library 505、effect_chain 319、repair_contract 265、
ui_i18n 0 failures、vfg_settings 683、capture 两项、lmxxf 62 checks 0 failures）。

### 本次踩到的三个坑（都记下来，下次别再犯）

1. **`QmlPlayerBridge.cpp` 的 blob 一直是 CRLF**（7665 个 CR 字节），而上游是 LF →
   合并时 git 认为**整个文件每一行都不同**，产生 `L1..L15623` 的单个巨型冲突 ✗。
   修法：提交 `89a2184` 只把行尾规范成 LF（不改内容）→ 巨型冲突立刻塌缩成 2 个正常块 ✓。
   **教训**：用脚本编辑文件后，提交前应校验"暂存 blob 里 CR=0"，否则下次合并还会炸。
2. **`git merge --abort` 之后工作区并未真的干净**（当时输出就提示"工作区有改动"），
   我却在那个状态下做了"行尾规范化"并提交，把 **7665 + 7955 行的拼接结果**（15623 行）
   当成了正常内容 ✗。修法：提交 `c8fc3f43` 从回退点取回正确内容并保持 LF ✓。
   **教训**：abort 之后必须先用 `git status --porcelain` 确认干净；规范化提交后必须核对
   **行数是否合理**（15623 对 7~8 千行的文件是明显的红线）。
3. **诊断脚本用了子串匹配造成误报** ✗：`list-hdrcurve-auto` 也匹配
   `list-hdrcurve-autorecommend`，把两个不同控件判成了"重复 UI"，
   白排查了两轮。**教训**：计数要用精确匹配（带尾引号或词边界）。

### 环境限制（非本次改动引入）

* `veyra_qml_quick_tests` 在本机**无法运行**：构建路径含中文（`D:\项目\...`），
  该测试程序把路径读成 `D:\???\...` 而报 does not exist。若要跑它，需把构建目录放到
  纯 ASCII 路径下。
* `veyra_lmxxf_nr_tests` **必须在 build 目录里运行**，否则会出现 5 条假 FAIL
  （换到 build 目录即 exit=0、62 checks 0 failures）。

### 打包与部署（按差异生成，未照抄 2.0.7）

**差异实证**（我们构建 vs 作者 2.0.8 包 vs 现 2.0.7 目录，逐文件 SHA256）：
**29 个 `.dxil` 里只有 `YuvToLinearRgb.dxil` 有差异** ✓
（我们 `CCCAE8B709DC` vs 作者 `BBF173AA6C40`）；`PresentBlit_ps.dxil`（曲线所在）、
`PresentMotion.dxil` 等**全部与作者包一致** ✓ → **两个切换脚本的清单无需改动** ✓
（仍是 exe + `shaders\YuvToLinearRgb.dxil` + `qml\Veyra\`），脚本里也不含版本号，
文案无需更新 ✓。这一点与台账 903 行"PresentBlit_ps 需纳入脚本"的旧说法不同，
原因是 **2.0.8 上游自己就带曲线**，两侧该着色器已一致。

**执行结果**：`robocopy 作者包 → 程序目录 /E /XD logs runtime_local`（rc=3 ✓，<8 视为成功）
→ 铺好作者原版 `.orig` / `qml.orig`（**逐一核对 exe.orig 仍是 20,254,720 B 未被动过** ✓）
→ 放入我们的构建：exe `E8518D15…`（6,345,728 B，`.p5` 同哈希 ✓）、
`YuvToLinearRgb.dxil` `CCCAE8B7…`、界面 60 文件（`qml` = `qml.p5` = 仓库，逐文件一致 ✓）。
目录**原地改名** `Veyra-2.0.7-…` → **`Veyra-2.0.8-NVIDIA-win64-portable`** ✓，
六个关键文件**改名前后哈希完全一致** ✓。

**启动验证（从简，只启动一次）**：定制版数据目录 exit **0** ✓、作者版数据目录 exit **0** ✓、
日志无新增 ERROR ✓；`runtime_local` 下三个数据目录（`user-data-2.0.0` / `-author` / `-p5`）
**全部保留**，无需清理 ✓（2.0.8 的 `VEYRA_PRESET_LIBRARY 12` 与 `CHAIN_SESSION 7` 均未变）。

---
## ✅ 2026-10-13 升级到上游 v2.0.7：3 处冲突、存档格式升版但向下兼容、修掉测试栈溢出

作者发布 **v2.0.7**（tag `v2.0.7` = `5975fa0`；`origin/main` 仅比它多 1 个文档提交）。
相对 v2.0.6：**120 个提交、284 个文件**，主题为 **VFG 优化**、**Blackmagic/DeckLink 采集**、
**播放列表**、AMD NR/FSR，以及大量新测试（`veyra_decklink_tests`、`veyra_playlist_*` 等）。

> 用户下载的是作者自测包 `Veyra-2.0.7-test-r2-NVIDIA-win64-portable`。**已按字节核对：**
> 包内 10 个 `qml/Veyra/*.qml` 在规范化换行后与 tag `v2.0.7` **完全一致** → 该测试包就是
> v2.0.7 tag 的构建产物，可以放心按 tag 合并。

### 本轮做法与结果

* 回退点 tag **`before-2.0.7-custom` = `2615c79`**；合并提交 `8301f29`，其后 `ab9cde2` 是栈修复。
* **交集 12 个文件**（2.0.6 只有 4 个）：`CMakeLists.txt`、`i18n/catalog.json`、
  `include/veyra/pipeline/FramePacket.h`、`qml/Veyra/{DialogHost,HomePage,ProPage,SettingsPage}.qml`、
  `shaders/YuvToLinearRgb.hlsl`、`src/engine/{EngineController,VideoPresenter}.cpp`、
  `src/pipeline/EnhanceGraph.cpp`、`tests/unit/RepairContractTests.cpp`。
* **3 处冲突**，全部解决为"两边都保留"：
  1. `FramePacket.h`：我们的 `DolbyVisionP5 dolbyVisionP5` + 上游的 `bool fullRangeChroma`
     （Elgato 非 MK.2 采集卡的"limited luma + full chroma"）。
  2. `EnhanceGraph.cpp`：常数数组**同时**保留我们的 `+kDolbyVisionP5ConstantCount`
     和上游的 `resolved.fullRangeChroma?2.0f` 范围表达式。
  3. `i18n/catalog.json`（16 个冲突块）：**以上游 v2.0.7 的 2245 条为基，补回我们独有的 19 条 = 2264 条**
     （判据按 `zh` 字符串本身，**不能按 `(zh,src)`** —— 上游会调整 `src` 字段，会造成大量假阳性）。
     我们的 19 条里有部分是**已删除的 HDR 元数据功能留下的死字符串**，保留在翻译表里无害。
* **上游 2.0.7 未改** `VideoPresenter.cpp` 的 `applyHdrMetadata` 函数体 → 我们的空操作（移除 HDR 元数据）
  **零冲突存活** ✓；`VSwitch.qml` / `VSlider.qml` 上游也未改 ✓。

### ⚠️ 存档格式升版（本轮的新情况）

上游把两份格式都升了一级：**预设库 11 → 12**（新扩展名 `.dlss-sr`）、**链会话 6 → 7**。
但**读端向下兼容 4~6**（`if(data.starts_with("VEYRA_CHAIN_SESSION 6")…)`，校验放宽到 `version <= 7`），
所以**旧数据目录可以继续用，本轮不需要清数据目录**（与 2.0.5 那次不同）。
实测：用旧 `runtime_local\user-data-2.0.0-p5` 启动，日志里 `损坏` / `[ui-session]` 均为 **0 条** ✓。

### ⚠️ 修掉一个合并后暴露的栈溢出（上游测试的问题）

`veyra_repair_contract_tests` 在 v2.0.7 后以 `0xC00000FD`（STATUS_STACK_OVERFLOW）退出，
崩溃点在 `explicit consumer motion choice wins` 之后。诊断过程与结论：

* 先用 `setvbuf(stdout,nullptr,_IONBF,0)` 让输出**无缓冲**，才定位到真实崩溃位置
  （默认缓冲下最后一行是**假的**，会误导）；
* 用 `editbin /STACK:16777216` 临时加大栈后**265 项全绿** → 确认是默认 1 MiB 栈不够，**不是逻辑错误**；
* 实测尺寸：`EnhancementSettings=7832 B`、`FramePacket=312 B`、`FrameBatch=568 B`，
  **本仓库新增的 `DolbyVisionP5` 仅 112 B，不是主因**；2.0.6 时该套件能在 1 MiB 内跑完，
  2.0.7 的测试与结构体增长后越界。
* 修法照抄上游对同类测试的既有做法：`target_link_options(veyra_repair_contract_tests PRIVATE /STACK:33554432)`
  （上游已给 `live_presentation` 8 MiB、`preset_library`/`hdr_tuning` 各 32 MiB）。

### ⚠️ 两个"长期既有失败"其实是**状态相关的非幂等测试**

`preset_library` 的 `DLSS presets: save original K library` 与 `hdr_tuning` 的
`persist: save existing v10 Flow entry` 长期被记作"既有失败"。本轮查明：

* 它们把文件写到**固定的 `%TEMP%` 目录**且**不清理** ——
  `%TEMP%\veyra-preset-library-tests`、`%TEMP%\pr19-hdr-persistence`。
* 实测：清干净后**第一遍 505 PASS / 0 FAIL、124 PASS / 0 FAIL 全绿**；
  **同一个目录再跑第二遍就各失败 1 项**。→ 是**测试自身的幂等性问题**，与合并无关。
* **以后跑这两个套件前先删这两个临时目录**，否则会把假失败当成回归。

### 测试结果（全部在清理 `%TEMP%` 之后）

| 套件 | 结果 |
| --- | --- |
| `veyra_preset_library_tests` | **505 PASS / 0 FAIL** |
| `veyra_hdr_tuning_tests` | **124 PASS / 0 FAIL**（原"既有失败"实为上面的幂等问题） |
| `veyra_effect_chain_tests` | 315 / 0 |
| `veyra_repair_contract_tests` | **265 checks / 0 failures**（加栈后） |
| `veyra_vfg_settings_tests` | **683 checks, 0 failures** |
| `veyra_capture_color_tests` | 270 PASS，`failures=0` |
| `veyra_capture_format_selection_tests` | `failures=0` |
| `veyra_lmxxf_nr_tests` | 62 checks / 0 failures |
| `veyra_ui_i18n_tests` | 0 failures（验证我们手工合并的 catalog 有效） |

⚠️ **运行姿势（累计，别再踩）**：
1. `veyra_vfg_settings_tests` 必须传一个**不存在**的绝对目录（已存在则 `create_directories` 返回 false → exit 2 且**无任何输出**）。
2. `veyra_lmxxf_nr_tests` 必须**在 build 目录下**运行（它用 `absolute("lmxxf-test-runtime")` 按 cwd 找假运行时 DLL）。
3. 运行前把 `deps\ffmpeg\...\bin` 加进 `PATH`；`veyra_ui_i18n_tests` 还需要 **Qt bin** 与**程序目录**。
4. `preset_library` / `hdr_tuning` 前先清 `%TEMP%\veyra-preset-library-tests`、`%TEMP%\pr19-hdr-persistence`。

### 部署

* 新包覆盖当前程序目录（`robocopy /E`，**排除 `logs`**，保留 `runtime_local\` 下的
  `user-data-2.0.0-p5` / `-author` 数据目录）。
* 切换集合（差异工具实测）：`veyra_qml_ui.exe`（作者 20079104 B / 定制 6154752 B）、
  `shaders\YuvToLinearRgb.dxil`（作者 18204 / 定制 20596）、**6 个界面文件**
  （`DialogHost`/`HomePage`/`ProPage`/`SettingsPage`/`VSlider`/`VSwitch`）。
  着色器比对从 44 升到 **48** 个（上游新增 4 个 VFG/Flow 相关着色器），界面文件从 58 升到 **59** 个；
  `shaders\PresentBlit_ps.dxil` 仍与作者版**逐字节相同**，无需切换。
* 程序目录改名 `Veyra-2.0.6-…` → **`Veyra-2.0.7-NVIDIA-win64-portable`**，改名前后哈希一致 ✓。
* 启动验证（唯一一次）：退出码 0，仅 2 条既有 `DialogHost.qml` 提示（行号因上游加行变为 603/1046），
  `SetHDRMetaData` **0 次** ✓。
* **未做**：真实素材播放检查（本机仍无测试片段）。

### 保留的定制（上游 2.0.7 仍无）

1. Dolby Vision Profile 5 转换（作者继续暂缓 PR #13）
2. HDR 元数据提示功能**已移除**（上游仍保留该功能；issue #28 尚无回应）
3. `VSwitch` 受控化修复（上游仍是点击即赋值；issue #29 尚无回应）
4. 首页「清空」最近列表按钮、`VSlider` 的 `keyStepValue`
5. 双版本切换工具 `tools/veyra-switch/` 与本文档 / 构建说明

## ✅ 2026-10-07 升级到上游 v2.0.6：零冲突合并，定制仍 25 个文件

作者发布 **v2.0.6**（tag `v2.0.6` = `e68c0c8`）。相对 v2.0.5：**9 个提交、86 个文件、+4016 行**，
主题是 **VFG（视频插帧）诊断与优化**、**Blackmagic 采集支持**、AMD NR/FSR 计划，以及一大批
`scripts/acceptance/` 验收脚本。

### 对外的两个 issue（2026-10-07 发出）

1. **[#28] HDR 元数据提示导致卡顿** —— `IDXGISwapChain4::SetHDRMetaData`（`applyHdrMetadata` 里两处调用）
   生效时，界面上任何叠加元素（悬停提示）出现都会让**视频与系统鼠标光标一起卡**；60Hz 投影仪复现、
   120Hz OLED 电视无感（两台不同设备，未分离"刷新率"与"面板处理管线"，issue 里如实写明）。
   附了 4 组 A/B 对照、逐帧数据（present 中位 0.37 ms、0 帧超 16.7 ms、调用本身 0~1 µs）、
   微软官方弃用原文，并指出**预设 `metaOn=true` 会强制打开它**这个诱因。
   本仓库的处置是**彻底移除该功能**（见下条）。
2. **[#29] VSwitch 受控化** —— 点击时的命令式赋值 `sw.checked = !sw.checked` 会**销毁父级的声明式绑定**，
   导致"手动拨过一次后，选预设时小字显示已启用、开关却还是关的"。本仓库已修
   （`VSwitch.qml` 改为只发信号 + `DialogHost.qml` 唯一那处本地复选框补回写 + 回归用例
   `test_switch_follows_external_model_change`），并把可直接 `git apply` 的两处补丁贴在 issue 正文里。

**两件都只提了 issue，没有提 PR** —— 原因：fork `georgeg911/Veyra-NRVideo` 里**没有** `.github/workflows`，
而作者仓库有 `star-history.yml`；基于作者 `main` 建的分支会把这个文件带进 fork，GitHub 会以
"OAuth 令牌缺少 `workflow` 权限"拒绝推送。

* 日后若要提 PR：先运行 `gh auth refresh -s workflow`（gh 在 `D:\DSH\.dsh-tmp\gh\bin\gh.exe`），
  再推分支即可。**PR 分支已备好但未推送**：分支 `fix/vswitch-controlled`，基线 `origin/main`（`96a7c8d`），
  提交 `23050d1`，改动 3 文件 +37/-5，`qmllint` 0 错误；补丁文件 `D:\DSH\.dsh-tmp\vswitch.patch`。
* 上游 46 处 `VSwitch` 使用点已审计：45 处是"绑定 + `onToggled`"（受控写法，无需改），
  1 处是 `DialogHost.qml` 的纯本地复选框（`preset-save-default`，需补回写）；
  `Main.qml` 的运动探针由代码**直接赋值** `probeSwitch.item.checked`，不受影响。

### 本轮做法与结果

* 回退点 tag **`before-2.0.6-custom` = `dec90f6`**；合并提交 `c9b41f3`（`Merge tag 'v2.0.6' into custom`）。
* **零冲突**：与我们的定制交集只有 **4 个文件**（`CMakeLists.txt`、`qml/Veyra/DialogHost.qml`、
  `src/engine/EngineController.cpp`、`src/pipeline/EnhanceGraph.cpp`），且全部自动合并成功。
  逐项复核增量未丢：P5 探针目标 7 处、VSwitch 回写 1 处、中文串 1 处、EnhanceGraph P5 4 处。
* **两个关键文件上游没动** → 我们的定制零冲突存活：
  * `src/engine/VideoPresenter.cpp`（`applyHdrMetadata` 空操作；确认 `SetHDRMetaData(` 0 处）
  * `qml/Veyra/ProPage.qml` / `SettingsPage.qml`（HDR 元数据界面已删）
  * `qml/Veyra/VSwitch.qml`（受控化修复）
* 上游 2.0.6 **仍然带着 HDR 元数据功能**（`SetHDRMetaData` 3 处、界面「HDR 元数据」2 处），
  所以"移除它"继续是本仓库独有的定制，将来若作者也删掉，冲突自然消失。
* **链会话版本仍是 6**（`PresetLibrary.cpp` 2.0.6 未改动）→ **不需要清数据目录**，与 2.0.5 那次不同。

### 测试（全绿；两个套件的运行姿势要记住）

| 套件 | 结果 |
| --- | --- |
| `veyra_preset_library_tests` | 482 PASS / 0 FAIL |
| `veyra_hdr_tuning_tests` | 123 PASS / 1 FAIL（既有：`persist: save existing v10 Flow entry`，与本仓库改动无关） |
| `veyra_effect_chain_tests` | 264 PASS / 0 FAIL |
| `veyra_repair_contract_tests` | 264 PASS / 0 FAIL |
| `veyra_vfg_settings_tests` | **331 checks, 0 failures** |
| `veyra_capture_color_tests` / `veyra_capture_format_selection_tests` | failures=0 |
| `veyra_lmxxf_nr_tests` | 0 FAIL |

⚠️ **下次别再踩的三个坑**：

1. `veyra_vfg_settings_tests` 必须传一个**不存在的绝对目录**（`create_directories` 对已存在目录返回
   false → 直接 `return 2`，且**没有任何输出**，很容易误判成崩溃）。
2. `veyra_lmxxf_nr_tests` 必须**在 build 目录下**运行 —— 它用
   `std::filesystem::absolute("lmxxf-test-runtime")` 按**当前工作目录**找假运行时 DLL，
   在别处运行会报 5 项 FAIL（"old function table negotiated" 等），其实全是找不到 DLL。
3. 运行测试前把 `deps\ffmpeg\ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0\bin` 加进 `PATH`，
   否则 `repair_contract` / `capture_color` 会以 `0xC0000135`（STATUS_DLL_NOT_FOUND）退出。

### 部署

* 新包覆盖当前程序目录（`robocopy /E`，**排除 `logs`**，保留 `runtime_local\` 下的
  `user-data-2.0.0-p5` / `-author` 数据目录）。
* 切换集合（差异工具实测）：`veyra_qml_ui.exe`、`shaders\YuvToLinearRgb.dxil`、
  **6 个界面文件**（`DialogHost` / `HomePage` / `ProPage` / `SettingsPage` / `VSlider` / `VSwitch`）。
  `shaders\PresentBlit_ps.dxil` 仍与作者版**逐字节相同**（7448 B），无需切换。
* 程序目录改名 `Veyra-2.0.5-…` → **`Veyra-2.0.6-NVIDIA-win64-portable`**，改名前后哈希一致。
* 切换说明由工具重新生成为 `NVIDIA 2.0.6`。
* 启动验证（唯一一次）：退出码 0，仅 2 条既有 `DialogHost.qml` 提示（行号因上游加行从 528/971
  变为 554/997），`SetHDRMetaData` **0 次**。
* **未做**：真实素材播放检查 —— 本机测试片段在早先清理时已删除，需要时请用户提供 HDR10 片源。

## ✅ 2026-10-07 升级到上游 v2.0.5：HDR 改用上游实现，本仓库定制收敛到 19 个文件

作者发布 **v2.0.5**（tag `v2.0.5` = `4957672552`），其中**已经包含本仓库的静态 HDR 输出调优**
（原 PR #19，作者做了适配：HDR10 输出改为先解 PQ 到线性 nits、映射后再编码 PQ；
i18n 按 (zh, ctx) 重合并；metadata 生命周期加固）。

### 本轮做法（与上一节"分叉"结论一致）

* **26 个被双方都改过的文件一律取上游 v2.0.5**，包括那些"自动合并"成功的 —— 否则会把
  本仓库与上游的两套 HDR 字段并在一起。
* **移除本仓库的 HDR 那套**：预设库版本号由 `tuning?12 + .vfg` 回到上游的 `tuning?11 + .hdr-output`；
  `PresetLibrary.cpp` 现在与上游**完全相同**。
* **4 个文件以上游为基、精确补回我们的增量**：`CMakeLists.txt`（P5 探针目标）、
  `src/pipeline/EnhanceGraph.cpp`（P5 的 4 处）、`tests/unit/RepairContractTests.cpp`（P5 契约测试）、
  `i18n/catalog.json`（上游 2049 + 我们独有 29 = 2078 条，按 (zh,src) 去重）。
* 回退点：tag `before-2.0.5-custom` 之前的 `a34e792`（合并后为 `a3b5e6f`）。

### 保留的定制（上游 2.0.5 均无）

1. Dolby Vision Profile 5 转换（**上游仍无；作者在 2.0.5 集成计划里继续暂缓 PR #13**）
2. 首页「清空」最近列表按钮（`qml/Veyra/HomePage.qml`）
3. `VSlider` 的 `keyStepValue`（整数参数方向键可逐格调整）
4. 双版本切换工具 `tools/veyra-switch/` 与本文档 / 构建说明

### 一个容易踩的坑（安全检查抓到）

`clearRecent` 看似是本仓库的"首页清空"，**其实上游 v2.0.4/v2.0.5 的 QmlPlayerBridge 本来就有**，
本仓库的独立改动只在 `HomePage.qml`。判断"某文件是否含我们的独有内容"的判据必须是
**「我们有、上游 2.0.5 没有」**，否则会把上游自带的东西误判成我们的。

### 结果

* 剩余定制：**19 个文件**（相对 v2.0.5）；真正影响产物的只有 exe、`YuvToLinearRgb.dxil`、
  以及 `qml/Veyra/` 下 2 个文件。**`PresentBlit_ps.dxil` 不再需要切换** ——
  我们的构建产出与作者版逐字节相同（7448 B）。
* 编译一次通过；五个单元测试全绿（repair_contract 从 252 升到 **264** 项，
  因为上游新增的跨厂商 FG 用例与我们的 P5 契约测试并存）。
* 旧链会话是 v9，而上游 2.0.5 的会话版本上限是 **6** → 升级后**必须清掉旧数据目录**，
  否则新版会判为损坏并弹「模式会话损坏，已拒绝覆盖原文件」。已清除 `-p5` 与 `-author` 两个目录。
* 由于 `PresetLibrary.cpp` 已与上游一致，**两个版本的存储格式现已相同**，
  分开数据目录从此只是保险措施（若想共用设置，可以去掉 .cmd 里的 `--data-dir`）。

## ⚠️ 上游已合并 PR #19 / #20（2026-10-06），HDR 实现已分叉 —— 升级前必读

作者于 **2026-10-06 15:03 UTC 把 PR #19（HDR 静态调优）与 #20（MSVC 构建适配）合并进 `main`**。
合并提交：`2e3d239794`（#20）、`357c13679c`（#19），其后 `578d63c3a0` 是收尾文档提交。
**他没有写评论，而是先适配再合并**（合并提交标题：*Merge PR #19: adapt static HDR output tuning
to current NR/Flow contracts*），并把过程写进了 `docs/PR19_PR20_INTEGRATION_PLAN_2026-10-06.md`。

### 作者适配时改了什么（会影响我们）

1. **冲突 3 处**（catalog / PresetLibrary / VideoPresenter），按"主线 NR/Flow 格式优先"逐项解决。
2. **HDR10 域处理被改写**：他判断"HDR10 图输出已经是编码好的 PQ BT.2020，而原 PR 当成线性 scRGB"，
   改为"仅调优时把 PQ 解码到线性 nits → 按亮度映射 → 保持 signed RGB 比例 → 再编码 PQ；
   scRGB 直接在线性域处理"。**这一条需要我们在自己的 fork 上复核**（见下）。
3. **i18n 按 (zh, ctx) 重合并**：我们那份合并漏了 off/play 的 ctx，导致"关闭"被翻成 Close。
4. metadata：按 source epoch / SDR / close 清空，同 epoch 保留可选源字段，校验 MaxFALL ≤ MaxCLL，
   失败限频并记录 HRESULT，resize 重发。
5. 新增 6 个验收脚本 `scripts/acceptance/pr19-pr20-*.py`，并跑了一套很完整的验证
   （HDR 124 项、预设 482 项、GPU 23 项、7 例 AMD identity、真子进程双层 NR+HEVC、
   UI 四种导出各 60 帧、七档 HDR UI + 真进程重启恢复等）。

### 两套实现已经分叉（升级时必须二选一，不能简单 merge）

| 项目 | 上游 main（作者适配后） | 本仓库 custom |
| --- | --- | --- |
| 预设库版本号 | **11** | **12** |
| 扩展名 | **`.hdr-output`** | **`.vfg`** |
| 读取调用 | `readRendering(in,g,version>=7,version>=11)` | `readRendering(in,g,version>=7,false,false,version>=12,version>=12)` |
| HDR10 域处理 | PQ 解码 → 线性 nits → 映射 → 重编码 PQ | 在线性 nits 域（原有实现） |

**因此下次升级（上游只要 ≥ 本次合并）时的正确做法是：**

1. **HDR 曲线 / 元数据 / 七档预设这一整块改用上游实现**，删除我们这套（否则两套曲线代码、两个版本号方案并存）；
2. **只保留本仓库独有且上游没有的东西**：Dolby Vision Profile 5 转换（PR #13 仍被作者暂缓）、
   首页清空（PR #15 无回复）、双版本切换机制与 `tools/veyra-switch/`；
3. 上游版本号回到 **11 + `.hdr-output`**，我们不再占用 12；升级后**用户现有存档需要迁移**
   （我们的 v12 存档上游读不了，反之亦然）—— 或者按用户已同意的做法，设置不迁移、重新配。

### 网络注意

2026-10-06 实测本机 **git 访问 github.com 超时**（`Failed to connect to github.com:443`），
`git fetch` 拿不到上游 main；只有 `gh` 的 API 通道可用（`gh pr view` / `gh api` 正常）。
要拉上游新版源码前，先确认网络能访问 github.com。

### 其它 PR 状态

* **#13（Dolby P5）**：作者 10-03 给了 3 点静态评审，我们当天回复了修复与证据；此后作者无新回复。
  他在集成计划里明确写了"现有 PR13/14 继续暂缓" → **是他主动暂缓，不是被忽略**。
* **#15（首页清空）**：无任何回复，仍 OPEN。
* **#14**：我们自行关闭（其中两块内容已放弃，HDR 部分改为 #19）。

## 升级到上游新版本的标准流程（2026-10-06 用户指定，必须照此执行）

用户说「作者出新版了，更新一下」时，按下述步骤做，**不要多做也不要少做**：

1. **不新建目录**。始终在当前程序目录（`D:\项目\Veyra-NV\Veyra-<当前版本>-NVIDIA-win64-portable`）**原地更新**。
   用户可能已经下载好新包（例如 `D:\下载\...\Veyra-2.0.x-NVIDIA-win64-portable.zip`），
   直接解压并覆盖到当前目录即可，不要解压成一个并列的新目录。
2. **作者完整版 = 直接覆盖**（不要搞复杂流程）：
   * 用新包覆盖当前目录（保留 `runtime_local\`，那里有 FSR/XeSS 运行时）；
   * 覆盖后不要手工逐个备份 —— 交给下面第 4 步的工具统一生成 `.orig`。
3. **定制版 = 按新版正常重新升级并编译**：
   `git fetch origin --tags` → `git merge v<新版本>` 到 `custom` → 解决冲突 → 编译 → 跑单元测试。
4. **切换脚本 = 用差异生成，不要照抄上一版**（用户明确要求）：

       python tools/veyra-switch/make_switch.py --package <程序目录> --build <构建目录> --repo <源码仓库> --apply

   它**递归**比对三组产物 —— `veyra_qml_ui.exe`、`shaders\**\*.dxil`（含 `shaders\dis\`
   子目录，共 44 个：顶层 25 + dis 19）、`qml\Veyra\**` —— **只把真正不同的文件纳入切换集合**，
   然后生成 `.orig` / `.p5` 备份、两份 `.cmd` 与 `版本切换说明.txt`，最后把生效设为定制版。
   前提：跑之前程序目录必须处于**作者原版**状态（刚被新版本覆盖）。
   ⚠️ 旧脚本只处理顶层 2 个着色器，`shaders\dis\` 里 19 个**从未纳入过切换**；2.0.4 只是恰好
   字节一致（同源码 + 同编译器 = 确定性输出）才没暴露问题。所以每次都必须实测比对，不能假定。
5. **顺手把程序目录改成新版本号** —— 这是升级流程的一部分，**不需要用户单独提**：
   `Veyra-<新版本>-NVIDIA-win64-portable`。改名前后各用哈希核对一遍生效文件，
   避免中途出问题留下不一致。用户已确认同意改名；目录名与版本号不一致会造成混淆
   （作者自己的 `GPU-PACKAGE.txt` 标题就长期写着旧版本号，是个反面例子）。
6. **验证从简**（用户明确嫌慢）：
   * 编译 + 跑单元测试（快）；
   * 文件核对用哈希/大小（快，不启动程序）；
   * **只启动程序一次**做真实素材功能检查，不要每验证一步就启动一遍并固定等待。
7. 冲突处理与踩到的坑写进本文件。

> **2026-10-04 迁移**：工作根从 `D:\项目\Veyra2` 迁到 `D:\项目\Veyra-NV`，部署目标改为作者的 NVIDIA 包
> `D:\项目\Veyra-NV\Veyra-2.0.4-NVIDIA-win64-portable`（基线仍为上游 v2.0.3 / `f82f649`，与本包同一提交）。
> 构建目录为 `build-nv`，并新开 DLSS SR / DLSS FG / NVOF。
> **2026-10-06 升级到上游 v2.0.4**：作者发布 2.0.4（tag `v2.0.4`），相对 2.0.3 有 **146 个提交**。以 `git merge v2.0.4` 合并进 `custom`，冲突 5 个文件（i18n/catalog.json、EngineController.cpp、PresetLibrary.cpp、VideoPresenter.cpp、EffectChainTests.cpp），全部解决。
> 回退点：tag `before-2.0.4-custom`（合并前的完整状态）。
> 
> **合并时踩到的坑（重要，下次升级必看）**：预设库的**版本号被两侧同时占用**。上游 2.0.4 把 
> 8/9/10 分给了 NR correction / correctionExtended / flow，而本仓库早期把 10/11 用于 HDR 调优、
> 8/9 用于已删除的逐场景亮度管理。git 自动合并后**写入用 10、读取用 12**，导致写出的存档自己
> 读不回来（`save()` 的自检 `parse` 拒绝），`veyra_preset_library_tests` 出现 20+ 失败。
> 修法：本仓库的 HDR 调优**改用版本 12**，且以下四处必须一致 —— 
> (1) `serialize()` 的版本计算 `tuning?12`；(2) 预设库 tuning 的**写入**判断 `writeRendering(...,version>=12)`；
> (3) 预设库 tuning 的**读取**参数 `tuning/tuningPlus = version>=12`（同时不再读 extended）；
> (4) 扩展名规则只给 **7 和 12** 加 `.vfg`（8/9/10 归上游，加了会让测试按基路径读不到）。
> chain session 的格式是引擎自己的（vfg=5 / extended=6,7 / tuning=8 / tuningPlus=9，上限 9），与预设库无关，未改动。
> 
> **部署目录改名**：`Veyra-2.0.3-NVIDIA-win64-portable` → `Veyra-2.0.4-NVIDIA-win64-portable`
> （原地升级 2.0.4 内容后改名，不再保留第二个程序目录）。切换脚本用 `%~dp0` 相对路径，改名不受影响。
> 
> **切换脚本增强**：两个 `.cmd` 都加了「运行中预检」（`tasklist` 检测 `veyra_qml_ui.exe`）。
> 起因：程序在运行时替换 exe 会失败，脚本会走到一半（exe 没换、着色器/界面已换）而留下**版本不一致**的安装，
> 且 `pause` 会在隐藏窗口下挂住。现在检测到程序在运行就**在动任何文件之前**退出。
> 
> 验证：五个单元测试全绿（preset_library / hdr_tuning / effect_chain / repair_contract 252 / vfg_settings 331）；
> 真机播放 4K HDR10 素材输出 `PQ-BT2020-RGB10`、元数据发送 `masteringPeak=1000 maxCLL=9978`。
> 旧 `runtime_local\user-data-2.0.0` 按用户要求清除（回到默认设置）。
> 
> **2026-10-06 修复：两个版本必须各用自己的数据目录**（用户报告：切到作者版后弹出
> 「当前修改尚未保存：模式会话损坏，已拒绝覆盖原文件」）。
> 
> 原因：本仓库给**链会话**（模式会话）扩展了字段并占用了版本 8/9（HDR 调优），
> 而数据目录是**全部用户状态的唯一根**（链会话 / UI 会话 / 界面偏好 / 预设库 / last-applied）。
> 两个版本共用 `runtime_local\user-data-2.0.0` 时：定制版写出 `chain-session.v1.vfg`（版本 9，
> 比上游多出 HDR 调优字段），作者版的 `ChainSessionStore::decode` 不认识这些字段，字段错位 →
> 解析失败 → 判定文件损坏 → 按其安全策略**拒绝覆盖**，于是弹出那个提示（日志标签 `[ui-session]`，
> 消息文本来自 `src/engine/PresetLibrary.cpp` 的 `ChainSessionStore::save()`）。
> 注意 `ChainSessionStore::load()` 会优先选 `.vfg` 变体，所以作者版一定会读到我们写的那份。
> 
> 修法（**只改脚本，不动代码**）：`tools/veyra-switch/make_switch.py` 生成的两份 `.cmd` 各自带
> `--data-dir`，把状态彻底隔离：
> 
>     定制版  runtime_local\user-data-2.0.0-p5
>     作者版  runtime_local\user-data-2.0.0-author
> 
> 代价是两个版本的设置各自独立 —— 但格式不同，本来也无法互通。
> **务必用这两个 .cmd 启动**；直接双击 `veyra_qml_ui.exe` 会退回共用的默认目录，可能再次触发。
> 
> 实测：修复前作者版日志中该提示 57 次；修复后作者版 0 次并自建 `-author` 目录，定制版 0 次。
> 
# Veyra 个人定制改动清单（CUSTOM LEDGER）

> 本文件是个人自用定制的唯一台账，随 `custom` 分支一起维护。
> 上游作者不使用本文件；未来合并上游新版本时，以本文件 + git 提交历史为准移植定制。
> 工作区：`D:\项目\Veyra-NV\Veyra-NRVideo`；安装/测试目录：`D:\项目\Veyra-NV\Veyra-2.0.4-NVIDIA-win64-portable`（2026-10-04 随 2.0.3 升级改名）；
> 构建目录：`D:\项目\Veyra-NV\build-nv`（Ninja，指向本目录源码；从 `D:\项目\Veyra` 复制下来的旧 `build\`
> 缓存里记录的是旧路径，不能直接复用）。
> 工具链：VS2022 Community（MSVC 14.44，含 CMake/Ninja）；Qt 6.8.3 MSVC2022_64 在 `D:\Program Files\Qt\6.8.3\msvc2022_64`；Python 3.14；git。

## 1. 基线与分支模型

- **上游仓库**：https://github.com/Likely7/Veyra-NRVideo （remote 名 `origin`，只读参考，不推送）
- **gold 基线（本地 tag `gold`）**：当前＝上游 `v2.0.2`（提交 0dafc57）。
  - **2026-10-04：`custom` 已合并上游 `v2.0.3`（tag `v2.0.3` = 354b1c6）并部署完成；
    `gold` 按规则仍未移动**，等本人明确指示后再 `git tag -f gold`。
  - 下次再移植时请把 `--onto` 指到那时的新版 tag（不要再用 `gold`，它已落后一个版本）。
  - 含义："已验证可正常使用"的回滚点。
  - **规则：只有本人明确说"更新为 gold"后才移动 `gold` 标签，禁止擅自推进。**
- **工作分支 `custom`**：基于 `gold`，所有个人改动都在此分支，**一个功能一个提交**，提交信息以【定制】开头并写清改了什么、为什么。
- `main`：跟踪上游，仅用于 `git fetch` 后对照，不直接在上面改东西。

## 2. 交付可行性地图（重要）

### 免编译 —— 可直接给改动文件、覆盖到安装目录即可生效
| 路径（仓库/安装目录一致） | 内容 | 安装位置 |
| --- | --- | --- |
| `qml/Veyra/*.qml`、`qml/Veyra/*.js` | 全部界面、布局、交互、QML 内中文文案 | `<安装目录>/qml/Veyra/` |

> QML 由 `engine.loadFromModule("Veyra","Main")` 从 exe 旁 `qml/` 明文加载（qt.conf: QmlImports=qml），
> 外置文件优先，因此改 qml 无需重新编译。已核实安装目录 `qml/Veyra/` 有 54 个明文文件。

### 需编译 —— 改动最终链接进 `veyra_qml_ui.exe`（作者发布版 16.44 MB；本地裁剪编译版约 5.3 MB，因关闭 RemotePlay/Moonlight/Xbox）
- 所有 C++（`.cpp/.h`）：引擎、来源、输出、字幕解析逻辑、文件对话框过滤器等。
- `i18n/catalog.json`：经 `qt_add_resources` 编译进 exe（界面非中文翻译）。
- `shaders/*.hlsl`：发布版是预编译 `*.dxil`（安装目录 shaders 全为 .dxil，改 shader 需 dxc）。

## 3. 已确认的技术现状（与需求相关）

- **媒体类型**：`openPath()` 不做扩展名白名单拦截，直接交 FFmpeg 探测；
  拖拽打开、或对话框选"所有文件 (*)"已能打开 FFmpeg 支持的任意格式。
  唯一"看不到"的限制是打开对话框过滤器（`QmlPlayerBridge.cpp:4209`），
  现仅列：mp4/mkv/mov/avi/webm/ts/m2ts/jpg/jpeg/png/bmp/webp（**改这里需编译**）。
- **字幕兼容性根因**：外挂字幕读取 `readTextFile()`（`src/engine/Subtitles.cpp:50`）
  仅处理 UTF-16LE BOM、UTF-8 BOM，其余一律按 UTF-8 严格解码；
  **无 BOM 的 GBK/GB2312/Big5 字幕会解码失败（打不开）或乱码**（修复需编译）。
  外挂过滤器（`QmlPlayerBridge.cpp:3328`）：srt/ass/ssa/vtt。
- **杜比视界 P5（已实现真正的 IPT-PQ-C2 转换，提交见第 4 节 #3）**：
  - 原拒绝点：`DolbyVision.h` 的 `route()` 只放行 P7、带兼容层的 P8/P10，**P5 判 Unsupported**。
  - 实测样本（Wednesday S02E01 Netflix WEB-DL）ffprobe 结论：hevc **Main10 / yuv420p10le（10bit，硬解可）**、
    color_range=**pc（full）**、matrix/transfer/primaries **全 unknown（颜色藏在 RPU）**、
    DOVI profile5 level6 rpu1 el0 bl1 **compat0**。
  - **同一文件不同时间点的 RPU 实测**（t=90/600/1800/3000s）：`ycc_to_rgb_matrix`、`rgb_to_lms_matrix`
    全片恒定；**reshape 曲线逐场景刷新（pivots 每次都不同，`scene_refresh_flag=1`）**，所以必须逐帧取。
  - **为什么"当 HDR10 打开"必然偏绿**：P5 基础层是 IPT-PQ-C2，不是 BT.2020 YCbCr。把它按 BT.2020 NCL
    full-range PQ 解码，画面整体偏绿、鸭子变粉色（本机 libplacebo 对照实拍帧验证；鸭子本色是黄色）。
  - **正确管线**（逐帧 RPU → 着色器）：
    `基础层 Y/Cb/Cr → 逐分量 RPU 分段多项式整形 → RPU ycc_to_rgb → ST2084 EOTF → RPU rgb_to_lms
     → 固定 HPE LMS→BT.2020（两者线性相邻，主机端合成一个矩阵）→ ST2084 OETF → 原有 HDR10/SDR 链路`。
  - **精度取证**（`D:\DSH\.dsh-dvtmp`，用 deps 里带 libplacebo 的 ffmpeg 作标准答案，
    `-vf libplacebo=apply_dolbyvision=1:color_trc=smpte2084` 输出 PQ/BT2020 逐像素对比）：
    精确曲线 **0.00067**（PQ 域平均绝对误差）；投影到 `{1,√x,x}` 三系数 **0.00177**（本实现采用）；
    只做矩阵不做整形 0.041；完全不转换（现在会绿的那条路）——鸭子变粉、画面发绿。
  - 常量预算：着色器根常量上限 60（`GpuPassUtils.h`），yuv pass 原用 32，本次追加
    **28 个**（`12 基础 + 20 调色 + 28 P5`=60，正好到顶）。整形曲线用三系数拟合就是为了塞进这个预算，
    精确分段多项式需要常量缓冲/描述符堆改造，代价更大且收益仅 0.001。若将来要上精确曲线，
    先看 `include/veyra/pipeline/DolbyVisionP5.h` 的通道布局注释。
  - 其它 profile 完全不受影响：只有 `dv.profile==5 && baseLayer && RPU 真的带颜色块` 时才置
    `DolbyVisionP5::active`（即着色器里的 `dv6.w`），否则走原来的路径。
  - **占位整形（重要坑）**：本文件 t≈0～1.95s 以及 t≈9.4～10s 的 RPU 给的是"平坦"整形
    （luma `poly_coef=0 0`、chroma `4194304 0`，即常量 0 与 0.5）。照字面应用会把整帧压成黑；
    实测 libplacebo 在这些帧用的是**首个有效整形**（t=1 帧套 t=2 的曲线，逐像素误差 0.00032）。
    流式解码器拿不到未来曲线，所以本实现取"**保留上一个有效整形**，还没有用过 identity"，
    与 libplacebo 只在开头约 2 秒（本来就是从黑场淡入）有肉眼难辨的差别。
  - 硬解：HEVC 走 D3D11VA（Main10/P010 支持）；若硬解失败，软件已有 `fallbackToSoftware` 自动回退。
  - **已实测**（2026-10-03）：替换便携目录 exe + `shaders/YuvToLinearRgb.dxil` 后打开该 MKV，
    日志出现 `profile 5 base layer converted as IPT-PQ-C2` 与常量取证行（矩阵与 ffprobe 推导值
    逐位一致），输出 `HDR10 RGB10 PQ/BT2020`；从 0:00 起播时占位整形被正确忽略
    （`curve=[0,0,1]` + 占位日志）。

### 3.1 这个样本到底是不是"真 P5"（结论与证据）

- **是真 P5**。判定依据：把基础层按 BT.2020 NCL YCbCr 解码（"当 HDR10 播"）→ 画面发绿、
  鸭子变粉；按 IPT-PQ-C2 管线解码 → 与 libplacebo（`apply_dolbyvision=1`）逐像素一致
  （t=90s 帧误差 0.00067）。两条路的差异是数量级的，不是参数微调。
- 它的"不像真 DV"之处在于：容器 VUI 完全没有颜色标签（颜色全在 RPU）、单层无增强层、
  RPU 整形曲线逐场景变化且开头带占位曲线——这些正是 P5 的正常形态。
- 想自己复核：`ffprobe -read_intervals 90%+#1 -show_frames -show_entries frame=side_data_list`
  看 `ycc_to_rgb_matrix` / `pivots` / `poly_coef`；`ffmpeg -vf libplacebo=apply_dolbyvision=1:
  color_trc=smpte2084` 出标准答案。
- 对照图：`D:\DSH\.dsh-dvtmp\p5-before-after.png`（绿/本修复/libplacebo 三联）。

### 3.2 着色器实测（`tools/dovi_p5_probe`，本仓库新增诊断工具）

"日志里常量对了"不等于"画面对了"，所以补了一个真正跑 GPU 的探针：它用 FFmpeg 软解一帧、
读同一份 RPU、按播放器的方式打包 60 个根常量、加载**编译出来的那份**
`YuvToLinearRgb.dxil`（不是副本），dispatch 后回读工作纹理，输出 scRGB 线性 BT.709
（1.0 = 80 nits，即 `EnhanceGraph::convertInput` 在 scRGB 输出位下的结果）。设了 scRGB 位就绕开
色调映射，比较的是颜色转换本身。

```powershell
# 需要把 deps 的 bin 放进 PATH（运行时 FFmpeg DLL）
$env:PATH="D:\项目\Veyra-NV\deps\ffmpeg\ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0\bin;$env:PATH"
D:\项目\Veyra-NV\build-nv\veyra_dovi_p5_probe.exe `
  --input "<那个 MKV>" --time 90 --out D:\...\probe_dv.raw
D:\项目\Veyra-NV\build-nv\veyra_dovi_p5_probe.exe `
  --input "<那个 MKV>" --time 90 --out D:\...\probe_nodv.raw --no-dovi   # 对照组
```

把探针输出按播放器工作空间的逆变换还原回 PQ/BT.2020，与 libplacebo 标准答案逐像素比对
（t=90s 帧，3840×2160，829 万像素）：

| 运行 | PQ 域平均绝对误差 | 说明 |
| --- | --- | --- |
| **开启 P5 转换（本次实现）** | **0.00175**（p95 0.0038，max 0.023） | 与独立解码器一致 |
| 关闭 P5 转换（=`--no-dovi`，即改动前行为） | **0.03957** | 发绿，差 23 倍 |
| 亮部区域（鸭子）平均 PQ RGB | shader `[0.4501,0.4592,0.4420]` vs 参考 `[0.4504,0.4596,0.4426]` | 逐通道几乎相同 |

对照组是关键：同一帧、同一着色器、只改 `dv6.w` 一个开关，误差就差 23 倍——说明这个测量确实
量到了颜色转换本身，而不是别的环节。视觉对照见 `gpu_vs_libplacebo.png`（左=GPU 实跑，右=libplacebo）。

0.00175 与 3.1 节预估的"投影三系数 0.00177"吻合，说明残差全部来自曲线投影近似，没有实现误差。

探针还带 `--sdr`（清掉 scRGB 位），跑的是 SDR 显示器下真正走的那条 BT.2390 色调映射路径：
`--sdr` 输出同样是"黄鸭 + 中性木地板"，而 `--sdr --no-dovi`（改动前行为）仍然是发绿 + 粉鸭。
所以用户要的两条输出路由（HDR10 / SDR）都实测过，见 `P5-before-after.png` 四联图。

#### 3.2.1 评审复测与工具扩展（2026-10-03，作者 review PR #13 之后）

作者那条 review 要"工具 + 复现命令 + 样本信息 + 参考解码器版本 + 参考结果"都能被复查，于是：

- 探针加了三个**不需要 GPU** 的模式，断言可以直接复跑：`--scan <n>`（逐帧打印 RPU 结论，
  含占位帧）、`--history`（同一帧用"全新 seek / 从 0 顺序解码 / 先播后面再回退 seek"三种方式
  到达，逐个矩阵与曲线比对）、`--sdr`（见上）。
- 计算那些数字的**对比脚本也进了仓库**：`tools/dovi_p5_probe/compare_raw.py`
  （`pq` = 探针 scRGB 输出 ↔ 16 位 PQ 参考；`f2` = 两次探针输出互比；`u2` = 两个 16 位 raw 互比）。
- 完整记录：**`docs/DOVI_P5_VERIFICATION.md`**（样本识别、libplacebo 命令、五类检查的命令与结果、
  测试结果与断言名）。

复测数字（t=95s，命令与参考命令见上面那份文档；0.00175 是首轮 t=90s 用另一套参考设置量的）：

| 检查 | 结果 |
| --- | --- |
| 连续 1500 帧（约 62s）逐帧 RPU | 可用 1500/1500，缺元数据 0，不支持 0，占位 119（0–1.0s、9.3–12.3s、43.3–44.0s、48.3–48.4s） |
| 历史无关（t=0.5 占位帧 / t=95 普通帧） | 两条都是 `history independent=1`，三种到达方式参数完全一致 |
| HDR10 输出 vs libplacebo | 平均绝对误差 **0.00354**（p95 0.015），亮区逐通道差 <0.0003 |
| 同帧对照组（`--no-dovi`，= 改动前行为） | 0.05410，差约 15 倍 |
| SDR 路由（`--sdr`） | 转换照常生效（与对照组平均差 0.034），**0% 像素溢出**（对照组 0.81%） |
| 非 P5 回归（HDR10-only 文件，新 shader vs 改动前 shader） | **99.9972% 逐位相同**，全部落在 1 个 FP16 步长内（max 4.9e-04） |
| 硬解路径 | 播放器跑同一文件 20s，无一条"拒绝"日志，打包矩阵与探针同帧值一致 |
| 契约测试 | `veyra_repair_contract_tests` **249/249**（旧断言"P5 应为 Unsupported"已按新契约替换） |

**本次修复推翻了原先的一条语义**：占位（平坦）整形不再"保留上一个有效曲线"，而是**用单位映射**，
且 `open()`/`seek()` 都清状态——这样同一帧的结果与播放历史无关（作者 review 的第 3 点）。

## 4. 定制功能登记表（按提交顺序追加）

| # | 功能 | 类型(免编译/需编译) | 涉及文件 | 提交 | 状态 |
| --- | --- | --- | --- | --- | --- |
| 1 | 构建适配：新版MSVC wstring→UTF8、ps5SendLoginPin 补条件编译 | 需编译 | src/sink/MfVideoEncoder.cpp、src/ui/QmlPlayerBridge.cpp | 617cd13 | 已编译 |
| 2 | 杜比视界 P5 按 HDR10 兼容播放（route 放行 + 颜色归一化） | 需编译 | include/veyra/source/DolbyVision.h、src/source/MediaFileSource.cpp | d6906b6 | **已被 #3 取代**（只会打开，颜色仍偏绿） |
| 3 | 杜比视界 P5 真正按 IPT-PQ-C2 转换（RPU 整形 + RPU 矩阵 + HPE，逐帧；占位整形**用单位映射**，与播放历史无关） | 需编译 | include/veyra/pipeline/{FramePacket,DolbyVisionP5}.h、include/veyra/source/{DolbyVision,DolbyVisionRpu}.h、src/source/MediaFileSource.cpp、src/pipeline/EnhanceGraph.cpp、shaders/YuvToLinearRgb.hlsl | e19e586（占位语义在 c9b2359 修正） | 已编译 + 已部署 + 日志实测通过 |
| 4 | `tools/dovi_p5_probe` 诊断工具：离线跑真实 dxil 并回读，用于验证 P5 颜色（后续扩展见 #6） | 需编译（仅工具） | tools/dovi_p5_probe/main.cpp、CMakeLists.txt | 见下方提交 | 已实测：对独立解码器误差 0.00175，对照组 0.03957 |
| 5 | P5 评审修复：无可用 RPU 参数时**拒绝**而不是退回普通 YUV；只在转换真正生效时报告；`usable` 判定 + 契约测试 | 需编译 | include/veyra/source/{DolbyVision,DolbyVisionRpu}.h、src/source/MediaFileSource.cpp、tests/unit/RepairContractTests.cpp | c9b2359（PR #13）、custom 727ee52 | 已编译；`veyra_repair_contract_tests` 249/249 |
| 6 | 探针免 GPU 模式（`--scan`/`--history`）+ 对比脚本 + 验证记录文档 | 需编译（仅工具） | tools/dovi_p5_probe/{main.cpp,compare_raw.py}、docs/DOVI_P5_VERIFICATION.md | 78a5961、f08934b（PR #13）、custom 263153d | 已复跑：1500/1500 帧可用、`history independent=1`、比较脚本复现同数字 |
| 7 | 首页「最近」列表加「清空」：**右对齐**、与「继续上次」右侧的「开始」同款主按钮（`VButton` + `primary` + `trash` 图标），右边缘与「开始」逐像素对齐；先确认再调用桥接里已有的 `clearRecentFiles()`，只清历史不删文件 | QML 免编译（i18n 复用已有「清空」条目，目录无净增） | qml/Veyra/HomePage.qml | 898ccd1、cf555be、12e2f87 | 实测：`start.right=1712.0`、`clear.right=1712.0`（delta 0）、按钮同宽 74.0/73.5；确认框完整；清空后 ui-session 记录 8 → 0 ；**已提上游 PR #15**（纯 QML） |
| 8 | **HDR 亮度管理**（逐场景自适应：强度 / 目标峰值 / 响应速度 / 过渡时间；只在 HDR 输出路径 + PQ 片源上运行，用图里已有的 64×36 亮度直方图测量，不新增 pass、不回读）。默认**强度 50%**，增益夹子 **[0.5, 3.2]** | 需编译 | include/veyra/pipeline/{HdrSceneMapping.h,EnhanceGraph.h}、include/veyra/engine/HdrBrightnessSettings.h、src/pipeline/EnhanceGraph.cpp、shaders/YuvToLinearRgb.hlsl（打包进 toneMapParams.z/w）、src/ui/QmlPlayerBridge.cpp、qml/Veyra/{ProPage,SettingsPage,NodePage}.qml、i18n/catalog.json | 特性本身在 PR #14；**滑块失效修复** custom `c5ca763`、默认 50% `5ab549c`（PR #14 上对应 `e33bfc6` 修复、`2fc83e3` 夹子/默认 60、`2a9f7eb` 默认 50，三条提交信息均为中文） | **该功能已于 2026-10-04 删除**，由 #9（静态曲线）与 #10（静态元数据）取代：逐场景增益与显示器自带动态映射叠加后，暗场提不动、亮场又被压 |
| 9 | **静态 HDR 亮度曲线**：暗部抬升 (0.01EV) / 抬升范围 (nit) / 中灰 (nit) / 高光起点 (nit) / 滚降强度 (%) / 峰值上限 (nit)。暗部抬升带权重 `w=saturate(1-L/range)²`，**亮部按构造不受影响**（单一曝光做不到）；中灰用「以峰值为锚的幂函数」`gamma=log(mid/peak)/log(203/peak)`，中灰变了而峰值不变；高光起点后软压缩 + 可选硬上限。**只作用于预览**（PresentBlit 在传输函数编码前，flags bit 16），不碰导出与工作帧 | 需编译 | include/veyra/engine/HdrOutputTuning.h、shaders/PresentBlit.hlsl、include/veyra/pipeline/GpuPassUtils.h（根签名常量数改为可配）、src/engine/VideoPresenter.cpp、src/engine/{EffectChain,PresetLibrary}.cpp、include/veyra/engine/{EffectChain,EnhancementSettings,GraphDescription}.h、src/pipeline/EnhanceGraph.cpp、src/ui/QmlPlayerBridge.{h,cpp}、qml/Veyra/{ProPage,SettingsPage}.qml、i18n/catalog.json | f67d657（根签名/live/部署修复：5ec3c86、bbe5038） | **已部署；用户实测有效果** |
| 10 | **HDR10 静态元数据**：母版峰值 / MaxCLL / MaxFALL（0 = 跟随片源），用 `IDXGISwapChain4::SetHDRMetaData` 发给显示器；只在值变化时发送、离开 HDR10 时清空、交换链重建后重发；色度坐标 BT.2020 + D65，母版黑位默认 0.005 nit。HDR10 元数据是静态的（逐场景动态需要 HDR10+/DV，本仓库不支持），「动态」仍由显示器负责 | 需编译 | include/veyra/gfx/PresentSink.h、src/gfx/PresentSink.cpp、src/engine/VideoPresenter.cpp、include/veyra/engine/HdrOutputTuning.h | f67d657、a3f7e70 | 已部署；日志实测 `[hdr-metadata] sent masteringPeak=… maxCLL=…` |
| 11 | **七档一键预设**（标准 / 暗场提亮 / 高光保护 / 暗亮双修 / 明亮环境 / 暗室观影 / 强化冲击）：一次写入曲线 + 元数据全部参数，当前档位高亮、旁边一行文字解释用途，滑块一微调就显示「自定义」 | 需编译 | src/ui/QmlPlayerBridge.cpp（预设表 + `applyHdrTuningPreset`）、qml/Veyra/{ProPage,SettingsPage}.qml、i18n/catalog.json | 22c4abb（chips 落盘修复 e897138，参数放宽 bbe5038） | **已部署；用户实测 7 档全部有效果** |
| 12 | **构建适配（已单独提上游 PR #20）**：新版 MSVC STL 拒绝 `std::string(wstring.begin(),wstring.end())` 这种窄化构造（而且该写法会把每个 `wchar_t` 截断成 `char`，非 ASCII 的 MFT 名称与错误信息在日志里是乱码），改为 `narrowUtf8()`（`WideCharToMultiByte(CP_UTF8)`）替换 4 处；`ps5SendLoginPin` 在 `VEYRA_ENABLE_REMOTEPLAY` 未定义时仍引用后端，补上条件编译。 | 需编译 | src/sink/MfVideoEncoder.cpp、src/ui/QmlPlayerBridge.cpp | custom `617cd13`；面向上游的干净版本在分支 `build-msvc-compat`（`979ee3f`） | **已提 PR #20**（+19/-5，MERGEABLE） |
| 13 | **HDR 调优第二期·强度旋钮**（0-200%）：一个旋钮缩放整档效果，按每个参数的中性值插值（抬升/范围→0、中灰→203、滚降→100、**高光起点反向**、峰值上限不跟）。关键设计：设置里存的是**基准**，强度与峰值存在旁边、由引擎在应用时解析（`resolveHdrTuning`），所以转旋钮不会让界面掉出当前档位，滑块也始终在编辑基准 | 需编译 | include/veyra/engine/HdrOutputTuning.h、src/pipeline/EnhanceGraph.cpp、src/engine/PresetLibrary.cpp、src/ui/QmlPlayerBridge.{h,cpp}、qml/Veyra/{ProPage,SettingsPage}.qml、i18n/catalog.json、tests/unit/HdrOutputTuningTests.cpp | `539157f`、`9463f92` | 已部署；实机验证 strength=150 重启后仍在 |
| 14 | **HDR 调优第二期·显示器峰值**：`PresentSink::queryDisplayPeak()` 读 `IDXGIOutput6::GetDesc1()` 的峰值/全屏/黑位；7 档的高光参数按书写基准 1000 nit **等比缩放**（600 nit 屏上「高光保护」450→270、上限 600；2000 nit 屏上 900/2000），**暗部与中灰刻意不缩放**（它们描述内容与观看环境）；0 = 自动用系统报告值，界面显示系统报告的三个数 | 需编译 | include/veyra/gfx/PresentSink.h、src/gfx/PresentSink.cpp、include/veyra/engine/HdrOutputTuning.h、src/ui/QmlPlayerBridge.{h,cpp}、qml/Veyra/{ProPage,SettingsPage}.qml、i18n/catalog.json | `539157f`、`9463f92` | 已部署；实机验证 peak=600 重启后仍在 |
| 15 | **HDR 调优第二期·整片一次性分析**：复用引擎已有的 256 桶 luma 直方图（零额外开销）累积，带 BT.2390 PQ→nit 换算，`recommendHdrPreset` 六条可解释规则；跨线程经 `PlayerSnapshot` 发布，界面**播放时进度条自动填充**（无需点开始），满了显示「建议：X（暗部占…、中位…、最亮处…）」+「应用建议」一键应用 | 需编译 | include/veyra/engine/HdrOutputTuning.h、include/veyra/pipeline/EnhanceGraph.h、src/pipeline/EnhanceGraph.cpp、include/veyra/engine/EngineController.h、src/engine/EngineController.cpp、src/ui/QmlPlayerBridge.{h,cpp}、qml/Veyra/{ProPage,SettingsPage}.qml、i18n/catalog.json | `539157f`、`9463f92` | 已部署；界面效果待用户实测 |

**#8 的"重新武装"规则（别改回去）**：映射目标只在三种情况下重算 —— ①检测到场景硬切、
②功能刚开启后的第一帧、③**设置本身发生变化**。第 ③ 条是 `c5ca763` 补的：在那之前只有
①②，于是同一个镜头内拖滑块完全无效（演唱会片一个镜头几分钟，四个参数看起来全惰性），
而「响应速度」那条分支只是朝**旧目标**滑动、从未真正生效。现在的语义：
`响应速度>0` → 目标每帧跟随测量值（画面跟随场景亮度）；`=0` → 只在这三种情况下跳变
（同一镜头内不跟随测量抖动，画面不会呼吸）。日志行是
`[hdr-brightness] sceneCut=<bool> settingsChanged=<bool> frame=… p90Nits=… gain=…`。

**#8 与显示器自带动态色调映射共存（2026-10-03 结论，已关闭）**：理论上是两级串联（都在按场景
改亮度），但**用户实测同时开启没有冲突**。因此：不做界面提示、不做「目标峰值」自动对齐面板实测
峰值 —— **这条不再作为待办**，以后也不用再提。

**#7 的两个 QML 坑（换布局时别再踩）**：

1. **不要用 `RowLayout` 放「Flow + 右对齐按钮」**：`Flow` 的隐式宽度是所有 chip 的宽度，
   `RowLayout` 的隐式宽度随之变大 → 把整列撑宽 → 按钮落到网格右边缘之外，与「开始」错位
   （实测偏右约 190px）。
2. **按钮必须是 `Flow` 的兄弟节点**：写进 `Flow` 内部会报
   `Cannot specify anchors for items inside Flow` 和
   `Cannot anchor to an item that isn't a parent or sibling`，而且不报错时它也只是"排在 chip 后面"，
   不是右对齐。当前实现：`Item` 包一层，`Flow` 锚 left/right(= 按钮左边)/top，
   按钮锚 `right: parent.right; rightMargin: 10`（10 = 继续上次卡片内 RowLayout 的右边距）。

**RTX Video HDR（作者功能）这块：现在与上游 `main` 一致。** 我们曾往这里加过两样东西，都已按用户决定撤回：

1. **「HDR 片源也转换」开关** —— custom `95ee5cd`（PR #14 `e7879e0`），删除理由见 §8 开头；
2. **源峰值 / SDR 参考白 / 曝光 / 高光滚降 四个 HDR→SDR 调参** —— custom `a6f5cb2`。
   它们只服务「HDR → SDR」方向：SDR 片源走 RTX Video HDR 时**根本不会被读**（SDR 输入走
   BT.1886 / BT.709 / sRGB 分支，不碰 `toneMapParams`），留在界面上只会误导。
   `include/veyra/engine/VideoHdrSettings.h`、`shaders/HdrToSdr.hlsli`、`shaders/RgbToLinear.hlsl`
   已与上游**逐字节一致**；我们新增的 `include/veyra/pipeline/HdrToSdrConstants.h` 已删除；
   `EnhanceGraph` 的常量打包也回到上游（`toneMapParams = 源峰值 / 203 / 0 / 0`），
   只保留逐场景亮度管理对 `z/.w` 的借用（HDR 输出路径才覆盖，与上游默认 0/0 不冲突）。
   **存储层**：预设库 v7 / 用户预设 v28 里那五个数字仍被读取并校验后丢弃（版本号不再写出），
   老预设 / 老会话照常可读 —— 已用旧版 exe 生成的 `VEYRA_PRESET_LIBRARY 7` 档案验证过。

## 5. 未来上游出新版本时的移植流程（给未来的自己/助手）

**2026-10-04 实录：2.0.2 → 2.0.3（已按下面的流程做完）**

* 先在临时分支用 `git merge v2.0.3` 试演（比 rebase 更适合评估冲突面）：上游 143 个改动
  文件里 **141 个自动合并**，只有 **2 个文件 12 处冲突**，且全在存储序列化层 —— 作者 2.0.3
  用同一段代码存 **VFG 帧生成**，与本仓库的 **HDR 亮度管理** 撞了版本号。
* 解法：以上游新结构为基准、把亮度块重放在 VFG 之后 ——
  载荷 `v7=VFG（上游）/ v8=+亮度 / v9=+亮度+过渡`；会话 `v5=VFG（上游）/ v6=+亮度 / v7=+亮度+过渡`；
  `writeRendering/readRendering` 合并（字段顺序：VFG → 亮度四字段 → 过渡一字段）；
  版本上限仍用本仓库的 `maxVersion` 机制（编辑器载荷显式传 9）；
  `.vfg` 版本探测扩展到载荷 7/8/9 与会话 5/6/7。
  `PresetStore.cpp` 直接采用上游版（本仓库那个“读 5 个已撤回数字”的 v28 块丢弃 —— v28 现在是上游的 VFG quality）。
* 跟随上游删除了自建的 `testEditorPayloadVersions`（上游 2.0.3 用 `testNodeEditorSession` /
  `testEditorPresetStorage` / `testEditorGlobalSettings` 重构了这部分覆盖；生产侧的 `maxVersion` 修复保留）。
* 验证：**编译一次通过**；`preset_library` / `effect_chain` / `repair_contract`(251 检查) /
  `vfg_settings`(331 检查，上游新增、正好覆盖 VFG 序列化) / `video_hdr`(TrueHDR 硬件路径 op=0/1/2)
  / `hdr_enhancement`(`HDR_SHADER_IDENTITY=1`) **全绿**；实机冒烟三条路径正常
  （原生 HDR + `[hdr-brightness] sceneCut=false settingsChanged=true gain=0.880->0.940`、
  SDR→`[video-hdr] loaded nvngx_truehdr.dll`）。
* **旧档案必须重置一次**：`ui-session.v1`(v1) 与 `chain-session.v1`(`VEYRA_CHAIN_SESSION 5`
  ＝本仓库格式，与作者的 VFG-v5 撞号) 新版都读不了；新版的做法是**拒绝覆盖原文件**（安全）。
  升级时已把两者改名为 `*.pre203` 留档，整个用户数据目录备份在
  `D:\DSH\.dsh-tmp\upgrade-203\user-data-backup-203`。
* 上游 2.0.3 **没有改任何 shader**，所以 `.p5` 的 dxil 无需更新（沿用 2026-10-03 那批）。
* 安装目录已改名为 `Veyra-2.0.4-NVIDIA-win64-portable`（2026-10-04）；两个切换脚本用的是 `%~dp0`
  相对路径，改名不受影响。`启动-P5修复版.cmd` 与 `版本切换说明.txt` 里关于「HDR 片源也转换」
  的过期文字、以及 2.0.2 的版本号说明都已更新（说明书里那一整节删除，改为「关于 HDR 片源」
  的新行为说明：HDR 片源走原生 HDR + 亮度管理，RTX Video HDR 只服务 SDR 片源）。
* 安装目录文档一致性：`README.md` / `README_CN.md` / `THIRD_PARTY_NOTICES.md` 已从仓库复制为 2.0.3 版；
  新增《本机构建说明.txt》，说明这是「2.0.3 源码 + 本机定制」的构建，并注明 `package-manifest.json` /
  `release-runtime-manifest.json` 仍是作者 2.0.2 发布包的原文件（程序不读它们、仅供人工核对，
  故意不改以免伪造作者发布包的校验信息）。
* 遗留提示：`DialogHost.qml:528/972` 的 `Unable to assign [undefined] to bool` 是**上游长期存在**的
  告警（日志里最早出现在 2026-10-03，升级前就有；来自 PS5/Xbox 串流对话框的属性初始化，
  而本机构建根本不编译串流），与本仓库定制和本次升级无关。


```bash
git fetch origin --tags
git rebase --onto vX.Y.Z gold custom     # 有冲突逐个解决: git add -> git rebase --continue
# 本人确认新版没问题、且明确指令后再: git tag -f gold <提交>
```

- 移植时对照本文件第 4 节逐条核对；上游若重构同一文件，先保留上游新结构，再最小化重放定制。

## 6. 交付打包约定

- 免编译改动：只打包本次变化的 `qml/` 等外置文件，保持目录结构，解压到安装根目录覆盖。
- 需编译改动：交付单个新 `veyra_qml_ui.exe`（本地裁剪版约 5.6 MB）覆盖到安装根目录；作者原版已备份为 `veyra_qml_ui.exe.orig`，出问题可还原。
- **着色器也算交付物**：改了 `shaders/*.hlsl` 就必须同时交付对应的 `shaders/<名字>.dxil`
  （运行时从 exe 旁 `shaders/` 明文加载，不编译进 exe）。
  本次：#3 交付 `veyra_qml_ui.exe` + `shaders/YuvToLinearRgb.dxil`，两者都要覆盖；
  旧 shader 已备份 `shaders/YuvToLinearRgb.dxil.orig`。
  只换 exe 不换 dxil 会按旧的 32 常量布局读，画面必错；只换 dxil 不换 exe 则 DV 分支永不开启。

### 6.1 双版本一键切换（本机安装目录现状）

本机**不编译串流功能**（见 6.2），所以安装目录里同时保留两个版本，用两个 `.cmd` 切换：

| 安装目录文件 | 内容 | 哈希(前16) |
| --- | --- | --- |
| `启动-P5修复版.cmd` | 切到 P5 修复版并启动 | 脚本，GBK 编码 |
| `启动-作者完整版.cmd` | 切到作者完整版并启动 | 脚本，GBK 编码 |
| `版本切换说明.txt` | 给用户的说明（UTF-8 BOM） | — |
| `veyra_qml_ui.exe` / `.p5` | P5 修复版程序（**2026-10-04：上游 2.0.3 + 全部定制**，含删除旧亮度管理、静态曲线、HDR 元数据、七档预设、会话持久化修复） | `.p5` = `93554F4AF8B01EE5`；生效 exe 需重启后才会变成它 |
| `veyra_qml_ui.exe.orig` | 作者原版程序 —— **仍是 2.0.2 原版**（我们没有纯 2.0.3 原版 exe，需要单独构建才有），所以「切回作者完整版」等于回 2.0.2 | `EAFB7511F00321EE` |
| `veyra_qml_ui.exe.p5prev` | 上一版 P5 程序（纯备份，不参与切换） | `18748EB6A347AB9F` |
| `shaders\YuvToLinearRgb.dxil` / `.p5` | P5 修复版着色器（2026-10-03 22:0x 重编：撤回 HDR→SDR 参数化后与上游语义一致，里层只保留 P5 与亮度管理的借用） | `3AF4D82751538D21` |
| `shaders\PresentBlit_ps.dxil` / `.p5` | **曲线所在的着色器**（2026-10-04 新增）。它一度没部署，导致曲线完全不生效；现已纳入两个切换脚本 | `72D1D82B0DB74291` |
| `shaders\RgbToLinear.dxil` | RGB/packed 输入转换着色器 —— **本次也变了**（撤回参数化，已与上游一致）。它不在切换脚本的复制清单里，所以只覆盖当前安装目录这一份即可 | `7C971313C59CD122` |
| `shaders\YuvToLinearRgb.dxil.orig` | 作者原版着色器 | `A23C55FAADAAAF67` |
| `shaders\YuvToLinearRgb.dxil.p5prev` | 上一版 P5 着色器（纯备份） | `A2B88E6DA2B6D43D` |
| `qml.p5\Veyra\` | P5 修复版界面（**55 个文件**，2026-10-04 与 `qml\Veyra` 一同更新为「上游 2.0.3 + 本仓库定制」，新增上游的 `EffectSupport.js`） | 相对 `qml.orig`（2.0.2 原版）差 HomePage/ProPage/NodePage/SettingsPage/VSlider + 上游 2.0.3 的全部界面改动 |
| `qml.orig\Veyra\` | 作者原版界面（54 个文件，逐字节原样） | 与 `HEAD:qml/Veyra` 的 git 对象哈希一致 |

**HDR 调优第二期的存储约定（#13-#15）**：tuning 段在末尾**追加**两个数（强度、显示器峰值），所以载荷 v10→**v11**、会话 v8→**v9**；读取端按版本判断，旧档案照常读、新字段保持默认。这是**追加式**升级，没有复用任何旧号段（第 8 号功能的教训：单边比较 `>=` 会让下一个版本号的档案被误读）。

**「整片分析」已删除（2026-10-04，用户决定）**：#15 的整片分析功能经用户实测后判断边际价值低 —— 7 档旁的用途说明本身就在做推荐，而它要每 15 帧下载一帧（4K 约 16 MB/次、~64 MB/s，还占解码线程），换来的只是一次性选档建议；覆盖面又只限本地影片。已全部删除（界面、桥接属性、PlayerSnapshot 字段、引擎累积器与下载采样、分析器类、测试、i18n），播放路径恢复零额外开销。**#13 强度旋钮与 #14 显示器峰值保留**，持久化仍是载荷 v11 / 会话 v9。

**`YuvToLinearRgb.dxil` 事件的准确结论（2026-10-04，已查清）**

事实（用 dxc 现编验证过）：
* **构建产物是正确的**：`dxc -T cs_6_0 -E main -O3 -Qstrip_debug -Qstrip_reflect` 编译当前
  `shaders/YuvToLinearRgb.hlsl` 得到 **20580 B / `3AF4D82751538D21`**，与安装目录里下发的那份**完全一致**；
  源码里 P5 的路径是活的（`dv6.w > 0.5 ? DoviP5ToPqBt2020(y,uv) : YuvToRgb(y,uv)`）。
* 那次事故的真实原因：`build-nv\shaders\` 里躺着一份 **18196 B / `A23C55FAADAAAF67`** 的**陈旧产物**
  （等于作者原版，是升级时带来的、**从未被重编过**）。我误以为"构建产物才是最新"，用它覆盖了安装目录里
  正确的那份 → 用户立刻看到 P5 发绿。

**教训（比"别覆盖"更准确）**：当构建产物与下发版本不一致时，**先弄清哪边是对的**，方法是
**删掉构建产物、强制重编一次**，再看结果 —— 而不是假设"构建的一定更新"。

**部署 dxil 的可执行校验**：
1. 涉及某个 shader 时，先 `cmake --build build-nv --target <shader 目标>` 强制重编（或删产物逼它重编）；
2. 比对哈希，已知基线：`YuvToLinearRgb.dxil` = `3AF4D82751538D21` / **20580 B**，
   `PresentBlit_ps.dxil` = `72D1D82B0DB74291`；
3. **替换任何 dxil 之后必须实测对应功能**：曲线 → 开曲线看画面；P5 → 播一段 P5 看是否偏绿。


### 迁移后新增：启用 VFG（视频插帧）

`VfgBackend` 是**编译期开关**：`CMakeLists.txt` 里只有当 `VEYRA_CUDA_DRIVER_INCLUDE_DIR/cuda.h` 存在时才定义 `VEYRA_HAVE_CUDA_DRIVER_HEADERS=1`，否则 `VfgBackend.cpp` 里所有实现都走 `#else` 分支、`runtimeAvailable()` 直接返回 false，界面因此把 VFG 显示为不可用。

本机系统没有 CUDA Toolkit，但 **Optical Flow SDK 5.0.7 的样本目录里自带一份 CUDA driver API 头**：
`third_party_local/nvidia/Optical_Flow_SDK_5.0.7/NvOFFRUC/NvOFFRUCSample/cudart_header/cuda.h`（771 KB，`CUDA_VERSION 11040`）。把它指给该变量后重新配置并构建，`VfgBackend.cpp` 重编通过，实机探针确认 `componentList` 里 `NVIDIA VFG` 的 `loaded=true`。

运行时 DLL 不需要我们提供（作者 NVIDIA 包的 `runtime/nvidia-vfg/` 自带 `nvVFXVideoFrameGeneration.dll`(197.7 MB)、`cudart64_12.dll`、`NVCVImage.dll` 等）。

**VFG 使用结论（2026-10-04 实测收尾）**

* **两个版本都正常**：同一素材、同样设置下，本仓库定制版与作者版都真实提交了插帧帧
  （日志 `present-cost generated=true`：定制版 11 次、作者版 5 次）。
* **倍数别用 6X**：4K 24fps × 6 = 144fps，超过 120Hz 显示器，且每帧共享缓冲 380 MB；
  实测 6X 那次触发 **48 次** `non-monotonic PTS` 时间线重置（插帧刚预热就被拆），
  而 2X/4X 时为 **0 次**。**建议 2X–4X**。
* **改完设置要等几秒**：每次改动都会重建管线并重新预热（日志里 `epoch` 递增），
  期间 `generated` 为 false 属正常。
* **诊断插帧时必须用带音轨的素材**：用纯视频片段（`-an`）测试会得到 `generated` 恒为 0
  的假阴性 —— 插帧调度依赖媒体时钟，缺音轨时排不上。这一点踩过坑，A/B 对比因此一度失真。
* **相关修复**：`applyHdrMetadata` 原先逐帧读取源的母版/CLL 值，而这些侧数据并非每帧都有，
  导致值在真实值与回退值之间跳动、几乎每帧都 `SetHDRMetaData`。已改为保持最后一次有效值
  （sticky），同一片段 12 秒内发送次数由多次降为 **1 次**，且报文改为真实值
  （`maxCLL=9978 maxFALL=867`）。
* **VFG 的编译开关**见上一节：`VEYRA_CUDA_DRIVER_INCLUDE_DIR` 指向含 `cuda.h` 的目录。

### 6.1.1 每次部署的检查清单（2026-10-04 两次事故后加，别再漏）

一次「改完代码 → 部署」必须核对下面全部，缺一项就可能出现「功能看着在、实际不生效」：

1. **exe**：`build-nv\veyra_qml_ui.exe` → 安装目录的 `veyra_qml_ui.exe` 与 `.p5`。注意 exe 的哈希**每次链接都不同**（PE 头/版本资源里有时间戳），别用哈希判断版本是否一致 —— 只要每次都执行复制动作、并核对文件时间戳即可。（dxil 是确定性的，可以比对哈希。）
2. **dxil**：先比对，再**按功能**决定是否替换。教训一：`PresentBlit_ps.dxil` 一直没部署，曲线代码根本不在运行的程序里 → 该换的要换。教训二（2026-10-04）：`YuvToLinearRgb.dxil` 的构建产物是**对的**（20580 B / `3AF4D82751538D21`），但 `build-nv` 里可能躺着**从未重编过的陈旧产物**（18196 B / 作者原版）—— 用它就会发绿。所以换之前先强制重编并比对大小/哈希。**凡是替换了任何 dxil，都要实测对应功能**（曲线 → 开曲线看画面；P5 → 播一段 P5）。
3. **QML**：`qml\Veyra` 与 `qml.p5\Veyra` 都要与仓库一致（55 个文件）。
4. **moc**：改了带 `Q_OBJECT` 的头文件后，先删 `build-nv\veyra_qml_bridge_autogen` 再构建 —— AUTOMOC 有时不重新运行，新属性在 exe 里根本不存在。
5. **根常量**：给某个 pass 增加 root constants 时，必须同时改 `GraphicsPass::create` 的 `constantCount`（默认 8），否则新参数**传不进 shader**（曲线就是这么「完全不生效」的）。
6. **live 字段**：新设置若标称 live（不触发重建），必须同时在 `EnhanceGraph::applySettings()` 里更新 `desc_`，否则 `graph.xxx()` 永远返回建图时的值。
7. **切换脚本**：新增了依赖某文件的定制后，把该文件纳入两个 `.cmd`（示例：`PresentBlit_ps.dxil`），否则切回来静默失效。
8. **版本号**：加序列化字段时，「旧字段的读取条件」必须是**区间**（如 `v>=6 && v<=7`），不能用 `==` 或单边 `>=` —— 后者会让下一个版本号的档案被误读，甚至被判「会话损坏」而**拒绝保存**（2026-10-04 用户遇到的就是这个：连 NR 开关都存不下来）。

切换要换**四样**：`veyra_qml_ui.exe`、`shaders\YuvToLinearRgb.dxil`、`shaders\HdrSceneReduce.dxil`
（HDR 亮度管理要用，见 §8 对应的定制项）、`qml\Veyra\`（54 个文件）。
exe、着色器、界面必须成对，否则会出现"界面里有开关但引擎不认"这类错配。
脚本用 `copy /y` 换前两样，用 `robocopy /MIR` 同步界面 —— **只同步 `qml\Veyra\` 这个子目录**，
绝不能对整个 `qml\` 做 `/MIR`（那会删掉 Qt 自带的 `qml\QtQuick\`、`qml\QtQml\` 等）。
`.cmd` 用 **GBK(936)** 写，否则 cmd 窗口里中文会花屏；cmd.exe 按当前代码页逐字节解析，
改这两个文件时别存成 UTF-8。

**程序正在运行时不能覆盖 `veyra_qml_ui.exe`**（Windows 占用，`copy /y` 会报
"正由另一进程使用"）。所以更新部署的顺序是：只把新构建写成 `.p5` / `.probe.exe`，
`veyra_qml_ui.exe` 留给切换脚本在下次启动时覆盖；期间若直接双击 `veyra_qml_ui.exe`
拿到的是上一版。真要立刻同步，先关掉程序再补一次复制。

界面切换实测（2026-10-03）：跑完 `启动-作者完整版.cmd` 后，生效的 exe/着色器/ProPage/Main.qml
三者哈希分别等于 `.orig` / `.orig` / `qml.orig`；跑 `启动-P5修复版.cmd` 后三者均等于 `.p5` / `.p5` / `qml.p5`。

实测（2026-10-03）：
- `启动-作者完整版.cmd` + 那个 DV MKV → 生效文件等于 `.orig`，日志 `route=1`（Unsupported）
  随后 `source-state ... failed=true`，**文件打不开** —— 正是作者原版行为。
- `启动-P5修复版.cmd` + 同一个 MKV → 生效文件等于 `.p5`，日志 `route=2` + P5 转换行 +
  `[present] output=HDR10 RGB10 PQ/BT2020`，正常播放。
- 当前默认停在 **P5 修复版**（看片用）。

重装便携包后要恢复这套切换：把上表这些程序/着色器/脚本放回安装根目录、`shaders\`，并恢复
`qml.p5\` 与 `qml.orig\` 两个目录即可（脚本内容简单，照上表写即可，全文见 git 提交里的本文件历史）。

### 6.2 为什么不做"单 exe 全功能版"

`VEYRA_ENABLE_REMOTEPLAY / MOONLIGHT / XBOX` 需要本机完全没有的第三方源码与库：

- RemotePlay：chiaki-ng `0e16950` + 递归子模块 + 2 个补丁，**还要一份干净副本**给
  `verify-chiaki-stage.py` 校验来源；另外 Opus / OpenSSL / nanopb / Jerasure+gf-complete /
  curl / json-c / libevent / miniupnpc / SDL3 3.4.14(静态) 全来自 vcpkg 树，还要 pkg-config。
- Moonlight：moonlight-common-c `f900dd4` + 子模块 enet/nanors + 静态 OpenSSL。
- Xbox：vcpkg `x64-windows-static` 的 libdatachannel + libjuice + usrsctp（含作者的 overlay port 补丁）。

已核实本机（`C:\Users\user`、`C:\Program Files`(x86)、`D:\`两层、`D:\项目`、`D:\share`、
C:/D: 顶层目录名）没有 vcpkg / chiaki / moonlight-common-c / libdatachannel / libjuice /
usrsctp / libcrypto.lib / libssl.lib / pkg-config.exe，也没有作者的
`Veyra-2.0.0-dependency-source.zip`(538 MB)；作者锁文件里记的路径是 `E:/项目/Veyra/deps/...`，
而本机只有 C: 和 D: 两个盘。所以全功能版必须联网下载依赖源码 + 装 vcpkg + 编十几套库（数小时）。

**注意**：`VEYRA_ENABLE_DLSS_SR / DLSS_FG / NVOF` 三个开关在本仓库里**只有声明、从未被使用**
（全仓库搜过，排除 docs/台账后只有 CMakeLists 第 53–55 行），是死开关，开不开都一样。
真正影响功能的只有 `REMOTEPLAY / MOONLIGHT / XBOX`（本机 OFF）与 `EXPERIMENTAL_DLSSNR`（本机 ON）。

## 7. 构建环境与依赖路径（本机，仅供编译，不进 Git）

- 生成器：Ninja（VS 自带）；构建目录 `D:\项目\Veyra-NV\build-nv`（**新项目请新建构建目录**，
  从 Veyra 复制来的 `build\` 缓存里 `CMAKE_HOME_DIRECTORY` 仍是 `D:/项目/Veyra/...`，直接用会编旧路径的源码）；
  configure 前先加载
  `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\Launch-VsDevShell.ps1 -Arch amd64`。
- FFmpeg 开发包（头+导入库，运行时仍用作者 patched DLL）：
  `D:\项目\Veyra-NV\deps\ffmpeg\ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0`（已补 `share/ffmpeg/FindFFMPEG.cmake`）。
  这个包的 `bin\ffmpeg.exe` 带 **libplacebo**，是本次验证 P5 颜色的标准答案来源，别删。
- nv-codec-headers n13.0.19.0：`third_party_local/nvidia/nv-codec-headers-13.0`（含 include/ffnvcodec）。
- DLSS/NGX SDK **v310.7.0**（对齐作者 310.7 接口，来自公开 github.com/NVIDIA/DLSS）：
  `third_party_local/nvidia/DLSS-SDK-310.7`。
- Optical Flow SDK **5.0.7**（已安装，来自 NVIDIA Developer 完整包）：
  `third_party_local/nvidia/Optical_Flow_SDK_5.0.7`（含 NvOFInterface/nvOpticalFlowCommon.h、nvOpticalFlowD3D12.h）。
- FidelityFX / XeSS 的 `third_party_local\{amd,intel}` 本机不存在；`CMakeLists.txt` 用
  `if(EXISTS ...)` 保护，缺了只是不编那部分，configure 不会失败。
- 本次实际 configure（`build-nv`，Release，Ninja）：
  ```powershell
  cmake -S D:\项目\Veyra-NV\Veyra-NRVideo -B D:\项目\Veyra-NV\build-nv -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DVEYRA_BUILD_QML_UI=ON -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON -DVEYRA_ENABLE_D3D12_DEBUG=OFF `
    -DVEYRA_ENABLE_DLSS_FG=OFF -DVEYRA_ENABLE_DLSS_SR=OFF -DVEYRA_ENABLE_NVOF=OFF `
    -DVEYRA_ENABLE_MOONLIGHT=OFF -DVEYRA_ENABLE_REMOTEPLAY=OFF -DVEYRA_ENABLE_XBOX=OFF `
    -DVEYRA_DLSS_SDK_ROOT=.../third_party_local/nvidia/DLSS-SDK-310.7 `
    -DVEYRA_NVOF_SDK_ROOT=.../third_party_local/nvidia/Optical_Flow_SDK_5.0.7 `
    -DVEYRA_FFMPEG_ROOT=D:/项目/Veyra-NV/deps/ffmpeg/ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0 `
    -DCMAKE_PREFIX_PATH="D:/Program Files/Qt/6.8.3/msvc2022_64"
  cmake --build D:\项目\Veyra-NV\build-nv --target veyra_qml_ui -j 16
  ```
  （`D3D12_DEBUG=OFF` 对齐作者 x64-release preset；上一轮定制版是默认 ON。）
- 日志：`<安装目录>\logs\veyra-qml.log` 追加写，很大（>100 MB），排查时按偏移量取新增部分，
  别整文件读进上下文。打开含 DV 的文件后应能看到 `[source-dovi]` 三行：profile/route、
  转换说明、常量取证。

## 8. 定制功能：HDR 片源先转 SDR，再用 RTX Video HDR —— **已于 2026-10-03 删除**

> **现状：这个功能已经删掉了**（custom `95ee5cd`，PR #14 上 `e7879e0`）。
> 用户实测后认为效果不好，改为**按片源分工**：SDR 片源走 RTX Video HDR，HDR 片源走
> 原生 HDR10 / 杜比视界输出 + §4 #8 的 HDR 亮度管理。
>
> 删除原因（分析结论，依据代码）：NGX TrueHDR 的接口契约就是 **RGBA8/sRGB 输入**
> （`TrueHdrBackend.cpp` 里有硬校验：非 `R8G8B8A8_UNORM` 直接拒绝，参数里也没有
> "输入是 HDR"的选项），所以要让它运行，**必须先做一次 "HDR → 8bit sRGB" 的转换**。
> 于是动态范围在链路最前端（输入转换）就被 BT.2390 压到 SDR 白点、中间的 NR / 超分 /
> 补帧全在 SDR 空间作业、进模型前还要过一次 8 位量化，最后靠模型的逆映射把高光"猜"
> 回来；而该网络训练在真正的 SDR 内容上，把它接到 HDR 片源属于借用 —— **效果不好是
> 结构性的，不是参数没调好**。
>
> 被删的东西：`VideoHdrSettings::convertHdrSource` / `convertsHdrSource()`、
> `hdrWorking()` 与 `convertVideoHdr()` 里的条件、图重建与热更新判定、桥接的
> `hdrSource` 参数、日志与状态文案、`VideoExportJob` 的对应警告、列表页与节点页的开关、
> i18n 四条文案、测试里的设置。
> **保留**：HDR→SDR 的四个调参（源峰值 / SDR 参考白 / 曝光 / 高光滚降）——它们仍服务于
> "HDR 片源 → SDR 输出"这条路（默认值逐位等于作者原硬编码行为）。
> **存档兼容**：预设库 v7 / 用户预设 v28 里那个字段继续写 0 占位、读取时校验后丢弃，
> 老预设与老会话照常可读，格式与行数不变（没有提升版本号）。
> 实测：HDR 片源 `working=linear-BT709-scRGB-1=80nits`、无 `[video-hdr]` 运行，
> 状态显示"原生 HDR 输入（RTX Video HDR 只处理 SDR 片源）"；SDR 片源仍正常创建 TrueHDR；
> 预设库 / 效果链测试全绿。

下面保留功能存在期间的实现与实测记录，只作参考，**不要照它改回来**。

**需求**：作者的 RTX Video HDR 只在 `!hdrInput` 时运行，所以 HDR10 / 杜比视界片源走原生 HDR10，
RTX Video HDR 用不上。希望 HDR 片源也走 RTX Video HDR（用户明确选"方案一"：新参数对所有
HDR→SDR 场景生效；新开关默认关；改动最小、不破坏作者原有行为）。

**做法**：不新增处理阶段，而是让**输入转换**在这一模式下先把 HDR 降到 SDR，工作空间随之变成
SDR，后面那段现成的 SDR→HDR 就能跑起来。核心只有两行判定
（`include/veyra/pipeline/EnhanceGraph.h`）：

```cpp
bool hdrWorking() const {return hdrInput&&hdrOutput&&!videoHdr.convertsHdrSource();}
bool convertVideoHdr() const {return videoHdr.enabled&&hdrOutput&&(!hdrInput||videoHdr.convertHdrSource);}
```

`hdrWorking()` 一变 false，输入转换就自动做 BT.2390 色调映射，下游 NR/SR/补帧/输出全都照旧。

**新增参数**（都在 `VideoHdrSettings`，默认值逐位还原作者原来的硬编码值）：

| 字段 | 默认 | 范围 | 说明 |
| --- | --- | --- | --- |
| `convertHdrSource` | false | 开关 | HDR 片源也走这条路 |
| `sourcePeakNits` | 0 | 0 或 400–4000 | 0 = 自动（沿用 MaxCLL→母版→1000 兜底） |
| `sdrWhiteNits` | 203 | 80–400 | SDR 白点，原来是写死的 `203.0` |
| `exposureEv100` | 0 | −200–200 | 曝光，1/100 EV |
| `shoulderPercent` | 100 | 50–150 | 高光滚降；100 = 原来的固定拐点 |

YUV 通路的曝光/肩部塞进 `toneMapParams.z/.w`（原本空着，**根常量一个都没加**，仍是 60）；
RGB 通路的 3 个值追加在调色块之后（`kHdrToSdrConstantCount = 3`，该 pass 28→31，上限 60 够用）。
`HdrToSdr()` 加两个入参，默认值下是恒等运算（`exp2(0)==1`、除 1.0、no-op clamp），所以作者原有
HDR→SDR 输出行为逐位不变。

**踩到的两个坑（都已修，都会静默失效，值得记）**：

1. `EngineController.cpp:192` 用 `sameVideoConfiguration()` 决定 `settings.revision`，那里只对比了
   `videoHdr.enabled`。新开关不在其中 → revision 不变 → 走"热更新参数"那条路 → 而
   `EnhanceGraph::applySettings()` 明确拒绝它（要重建图）→ **整个变更被丢掉，界面开关点了没反应**。
   修：在 `sameVideoConfiguration` 里把 `convertHdrSource` 也算进图形状，并且
   `requiresGraphRebuild` 与 `applySettings` 各加一条（前者触发重建，后者拒绝热更新）。
2. `ChainSessionStore` 的内嵌预设库写出去是 v7，但它的解码走
   `PresetLibrary::parse(..., preserveLegacy=true)`，而版本上限原本写死 `preserveLegacy?3:7` →
   **会话每次保存都校验失败**（日志 `[ui-session] 模式会话编码校验失败，未保存`）。修：给 `parse`
   加独立的 `maxVersion`（0 = 保持原判定），链会话显式传 7。副作用：开着新开关时写出的会话
   文件作者原版读不了（会退回默认且拒绝覆盖，不损坏原文件），把开关关掉再切回作者版即可。

**数据兼容**：`PresetStore` 升到 v28、`PresetLibrary` 升到 v7，都是**追加**字段并只在新版本读，
老预设/老设置/老会话照常可读，新字段落回默认值（关）。

**只有两处必须重建图**（会有一个约 2 秒的停顿）：新开关本身（要创建 TrueHDR 功能、改工作空间）；
4 个转换参数是热更新，拖滑块不重建。

**实测（2026-10-03，RTX 5080 + 那个 DV P5 样本，用临时 QML 钩子调用界面同一个桥接函数）**：

- 钩子后日志出现：`[video-hdr] loaded nvngx_truehdr.dll` → `[hdr-tone-map] ... targetPeakNits=203
  exposureEv=0.00 shoulderPercent=100` → `[hdr-route] ... working=linear-BT709-SDR-relative
  output=PQ-BT2020-RGB10 toneMap=BT2390-luminance+neutral-ray-gamut-compression (HDR source converted
  for RTX Video HDR)` → `[video-hdr] evaluate 3840x2160`，即 **HDR→SDR→RTX HDR→HDR10** 全链路跑通。
- **开 2X 补帧同时生效**：`Applied revision=5 ... multiplier=2` + `[present-cost] generated=true`。
- **参数与开关会存档并在重启后恢复**：把 contrast 改成 199、SDR 白点改成 250 后重启，日志直接是
  `targetPeakNits=250` + `contrast=199` + SDR 工作空间，无需再点开关。
- **对照组（全新数据目录、开关关）**：`working=linear-BT709-scRGB toneMap=none`、
  `video-hdr`/`hdr-tone-map` 各 0 次、输出 HDR10 —— 与作者原行为一致。
- 日志里没有指向 `ProPage.qml`/`NodePage.qml` 的 QML 报错；只有作者原有的
  `DialogHost.qml:528/972/1343/1380 Unable to assign [undefined] to bool`（两个版本都在，非本次引入）。

**已知小限制**（都如实告诉用户）：补帧开启时输出封装固定 HDR10（作者原设计），
所以"HDR 输出模式"选择器在此模式下无效；`forceSdrPreview`（SDR 预览）会让整条路不执行；
`useHdrPreview` 仍要求显示器 HDR 处于开启状态。

## 9. 杜比视界 P5 的上游 PR 与评审回合（2026-10-03）

**上游状态**（决定以后怎么移植，务必先看这里）：

| PR | 分支 | 内容 | 状态 |
| --- | --- | --- | --- |
| [#13](https://github.com/Likely7/Veyra-NRVideo/pull/13) | `dovi-p5-fix`（6ed64e9 → c9b2359 → 78a5961 → f08934b） | 本文件 §4 的 #3/#4/#5/#6（P5 转换 + 探针 + 评审修复 + 验证记录） | OPEN / MERGEABLE，已回复作者 review |
| [#14](https://github.com/Likely7/Veyra-NRVideo/pull/14) | `hdr-source-route-and-brightness`（10 提交，分支尖 `e7879e0`） | 构建适配 + HDR→SDR 四个调参（曝光/肩部/SDR 参考白/源峰值）+ §4 #8 的 HDR 亮度管理（含滑块失效修复、夹子 0.5–3.2、默认强度 50%）+ 会话持久化修复；**§8 的「HDR 片源也转换」已按用户决定删除**（`e7879e0`） | OPEN / MERGEABLE；亮度与删除那几条提交信息是**中文**（按用户要求） |
| [#15](https://github.com/Likely7/Veyra-NRVideo/pull/15) | `home-clear-recent`（4d5802a，基于上游 `main`） | 本文件 §4 #7：首页「最近」列表的「清空」按钮（纯 QML + 2 条 i18n，不动 C++） | OPEN / MERGEABLE，1 提交、2 文件 +74/−17 |

#15 特意做成**纯 QML**（`clearRecentFiles()` 上游本来就有），作者不需要编译任何东西，
把 `qml/` 覆盖到安装目录即可验证；上游若接受，`custom` 这边的 #7 就变成"已在上游"。
它的内容就是 `custom` 上 #7 那三个提交（898ccd1、cf555be、12e2f87）**压成一个提交**的净结果，
另外把 2 条 i18n 按上游目录的排序重新插入。

两个 PR 都基于上游 `main`，`custom` 里的对应实现与它们**逐文件相同**（P5 相关 4 个文件 +
`tools/dovi_p5_probe/*` + `docs/DOVI_P5_VERIFICATION.md`），已同步提交：custom `727ee52`、`263153d`。

**作者 review 的三点 + 我们的改法**（提交 c9b2359，已同步到 custom）：

1. 参数不可用时**拒绝**：`read()` 每帧清状态、只用本帧 RPU 重建；没 RPU／颜色块为空／用了本解码器
   无法复现的整形方式（MMR）→ 带原因停止，不再退回普通 YUV（那条路必然发绿）。状态文案只由新的
   `DolbyVisionP5Build::usable` 决定，**只有真正在转换时**才说"按 RPU 转换"。
2. `RepairContractTests` 的旧断言（P5 → Unsupported）按新契约替换，并补：空颜色块、不支持映射方式、
   占位帧与播放历史无关。当前 249/249。
3. 占位整形改为**单位映射** + `open()/seek()` 清状态 → 同一帧的结果与到达方式无关（见 §3.2.1 实测）。

**移植注意**：

- 上游若合并 #13：删掉本文件 §4 的 #3–#6 行（改记"已在上游"），只保留 `custom` 相对上游的**增量**
  （目前没有增量：两边同一份代码），并把 `.p5`/`.orig`/`.p5prev` 的哈希按新构建刷新。
- #13 的分支在**关闭串流选项**的配置下编译还需要 #14 第一个提交里的 QML `#ifdef`
  （`VEYRA_ENABLE_REMOTEPLAY=OFF` 时 `ps5SendLoginPin` 不编译）+ `MfVideoEncoder` 的 wstring→UTF8；
  本机验证时是临时打上、验完撤掉的。作者若要求合并，把 #14 的那一条搬过来即可。
- 本仓库**没有 CI**（无 `.github/workflows`，PR 上不会出现检查项）；所有数字都是本机跑出来的，
  命令与结果见 `docs/DOVI_P5_VERIFICATION.md`。

**待补**（本文件还没登记、但 `custom` 里已经有的定制）：预设库 v8/v9 与会话 v5/v6 的字段表
（HDR 亮度管理已在 §4 #8 登记，会话持久化的坑见 §8）。下次整理台账时一并补上。

## 2026-10-07 悬停卡顿定性：与 HDR 元数据提示的交互（并改掉预设的强制开启）

**症状（XGIMI 60 Hz 投影）**：鼠标经过播放条图标（播放速度/字幕/音轨/全屏）时，视频与
**系统鼠标光标一起卡一下**；关闭「HDR 元数据」后消失；在 LG 42 C4（120 Hz）上感觉不到。

**用户亲自做的 A/B 对照（决定性）**：

| 提示弹窗 | HDR 元数据 | 结果 |
| --- | --- | --- |
| 显示 | 开 | 卡 |
| 显示 | 关 | 不卡 |
| `VEYRA_TEST_NO_TOOLTIP=1` 抑制 | 开 | 不卡 |
| 抑制 | 关 | 不卡 |

即**两者同时存在才卡**，是交互效应。

**应用侧全部排除（逐帧实测）**：
* `SetHDRMetaData` 调用耗时 **0~1 µs**；`refresh()` 8~11 µs；
* 47 个 SEND/CLEAR/REFRESH/SINK 事件，**没有一个**夹着 >60 ms 的帧间隔（11~45 ms，正常）；
* present 单帧耗时中位 0.37 ms，**0 帧** >16.7 ms；
* 帧间隔中位 42.0 ms、p99 44.0 ms（24 fps 标称 41.7 ms），整段仅 2 次大间隔且都在 seek 时刻；
* 提示从"独立顶层窗口"改为窗口内就地渲染（探针证实 same window）后**照样卡**。

**结论**：卡点在应用**提交之后**（DWM 合成 / 显示器-投影仪的处理管线）——应用无法让系统
光标卡，而光标确实卡，正说明这一点。应用侧唯一的杠杆就是**那个静态元数据提示本身**。

**改动（本仓库）**：预设不再强制打开元数据提示。预设原本六个档全是 `metaOn=true`，于是
"选一档预设"就等于"开始卡"。现在 `applyHdrTuningPreset` 用 `hdrPresetMetadata()` 保留用户
对开关的选择，预设只负责曲线与亮度基准；`hdrPresetMatches` 改为忽略开关位（否则选档后
界面无法高亮当前档）。`hdrMetadata.enabled` 只控制那一次 `SetHDRMetaData`，**不改变输出
格式或色彩空间**，所以关掉它不损失画质（曲线照常生效）。

**未决**：60 Hz 显示设备在"元数据提示 + 界面叠加"下的停顿无法从应用侧消除；
用户可在投影仪上关闭动态色调映射/运动补偿做进一步确认，或在需要时手动打开该提示
（例如在 120 Hz 的 LG C4 上）。

### 2026-10-07 最终处置：彻底移除 HDR 元数据提示功能

按用户要求执行（"彻底移除、只改 P5 版、尽量少碰作者原版"）：

* `VideoPresenter::applyHdrMetadata` 改为**空操作**（签名与调用点都不动，改动仅限函数体）——
  不再调用 `SetHDRMetaData`。理由见函数内注释与 2026-10-07 的定性条目：该 API 已被微软
  明确弃用（不保证送达显示器、各显示器处理不一致），且在 60 Hz 显示设备上，只要它处于
  生效状态，界面上任何叠加元素（悬停提示）出现都会让整个桌面（视频与系统光标）漏帧。
* 界面：`ProPage.qml` 与 `SettingsPage.qml` 里的「HDR 元数据」开关与三个参数（母版峰值 /
  MaxCLL / MaxFALL）整块删除。
* 结构体、序列化格式（`.hdr-output` v6 / 预设库 v11）与桥接属性**一律不动**，避免破坏
  存档与预设库兼容性；预设表里的 `metaOn` 字段保留但已无作用。
* 为定位该问题临时加入的全部插桩与脚本已清除：`VideoPresenter.cpp`、`apps/veyra-qml/main.cpp`
  恢复到作者原版（`754af6d`），并删除程序目录里的 PresentMon 与各诊断脚本/CSV。

保留：HDR 亮度曲线（曲线与 HDR10 PQ 输出完全不受影响）、VSwitch 受控化修复、
清单里原有的定制（HomePage 清空按钮、VSlider keyStepValue）。

## ✅ 2026-10-17 杜比视界 P3-2 / P3-3：逐帧 L1 接进呈现曲线，并做成真开关

### 做了什么
1. **P3-2a..d（e1d39c93）**：`VideoPresenter` 新增 `latestDovi_` + `setFrameDovi()`；
   `EngineController` 在帧处理完后把 `pkt.dovi` 交过去（暂停重绘路径也交）；
   `present()` 里用 `doviCurveFromLevel1` 得到 effective 曲线，**只替换 dims[8..13]**，
   `probeCurve_` / 显示端元数据仍用静态曲线；保留帧复用比较改用 effective 曲线。
2. **P3-3**：`EnhancementSettings::doviFollow`（live）+ `describeStages()` 写入
   `EnhanceGraphDesc::doviFollow` + `applySettings` 里 off→on/on→off 打一行 `hdr-dovi`；
   UI 在「杜比视界设置」卡里加「跟随动态元数据」开关（VSwitch，objectName `list-hdr-dovi-follow`），
   曲线没开时给出提示；文案进 i18n 四语。
3. 持久化走**已验通道**：`qml-preferences.v1.json` 的 `doviFollow`（全局兜底，启动时经
   `commitHdrTuning()` 应用）+ `hdrTitles[].doviFollow` / `hdrProfiles[].doviFollow`（每片/每显示）。
4. 顺手修掉 P1 的一个真 bug：`MediaFileSource` 里 `out.dovi = doviFrameInfo;` 原本写在提取**之前**，
   每帧带的是上一帧的 L1（曲线慢一帧、首帧无值）。已移到提取之后。
5. i18n：新增 4 条本功能文案；另补齐 P2b 遗留的 8 条杜比视界文案（杜比视界设置 / 未检测到片源 /
   RPU 有 / 无 RPU / P5 三条状态）。`python -B scripts/i18n/extract.py --check` →
   `missing={zh-TW:0, en:0, ja:0} placeholder problems=0`。

### 走错又退回的一段（教训写在 handoff 里）
先把 `doviFollow` 放进 `ChainGlobalSettings` 并把预设库提到 v14、会话提到 v9，再手写 v9 会话当夹具。
实测：会话被拒（`[ERROR] [ui-session] 模式会话损坏、无法读取或版本不支持`），且 bridge 的 `revalidate()`
会把 tuning 块打回默认。已全部回退（`git checkout` 三个文件），改用偏好通道。

### 关键发现
`EnhanceGraph::initialize()` 里 `desc_=desc` 会整体覆盖描述 → 任何新 live 字段都必须写进
`describeStages()`（`include/veyra/engine/GraphDescription.h`），否则图形重建时丢值。
这正是第一版只在 `applySettings` 里赋值时的现象：同一份数据目录两次运行，一次 follow=1 一次 follow=0。

### 实测证据（p8_30s.mkv / p5_30s.mkv，本机 RTX 5080，HDR10 输出）
| 用例 | 开关 | follow | applied | highlightStartNits |
|---|---|---|---|---|
| 空数据目录（曲线关） | off | 0 | 0 | 0 |
| 曲线开、跟随关 | off | 0 | 1 | 2000（= 用户值，画面不变） |
| 曲线开、跟随开（偏好驱动） | on | 1 | 1 | 286（0.85×336 nit） |
| 同一段第 200 帧 | on | 1 | 1 | 141（0.85×165.9 nit）/ cap 166 |
| P5 片源、跟随开 | on | 1 | 0 | 277（0.85×326.2 nit），P5 转换路径不受影响 |

全部 `--exit-after` 退出码 0；日志无 `Unexpected token` / `no root object` / `Type … unavailable`。
单元/回归：hdr_tuning 0 失败、preset_library 0 失败、ui_i18n 0 失败、repair_contract 280/0、
effect_chain 全过、vfg_settings 683/0（后两类需先移走 %TEMP% 里的测试残留目录）。
`veyra_settings_transaction_tests` 本机 8 轮 HDR 比对 `passed=0`；用 `git stash` 在无 DV 改动的基线上复测
同样 passed=0 → 与本功能无关。

### 未做
- P3-4 与 mpv（gpu-next）同片对拍观感
- P4 HDR10 元数据一致性（跟随 DV 时显示端元数据是否按片段上报）
- 未推送任何提交

### 2026-10-17 补记：P3-3 修正（上限不是盖子）与 P3-4 对拍
- **用户实测反馈"跟着开画面都有变化"是正确指控**：第一版跟随把 `peakCapNits` 也写成帧峰值，
  而它就是中灰 gamma 的锚点（g=log(mid/peak)/log(203/peak)）。用用户真实档位（mid 175 /
  start 450 / cap 1200）算：1 nit 掉 78%、100 nit 掉 18%、1000 nit 被硬夹掉 61%，
  逐帧还会呼吸。修正为只改 `highlightStartNits` 且单向下调（不削弱用户已设的压缩），
  3 条契约测试锁死（上限原样、1~203 nit 完全一致、低起点保留）。提交 4f24d9bd。
- 滚降区间（dims[12]）在跟随时按内容自身峰值收口，`dims[13]` 硬上限仍是用户值，
  否则高显示峰值会把坡度拉平到等于没压。
- P3-4 与 mpv v0.41 / libplacebo v7.376 对拍（p8_30s.mkv）：mpv 识别 DV profile 8，
  HDR10 PQ 直出同款 swapchain；libplacebo 的 tone_map 是线性 `a*x+b`，无 BT.2390/
  样条/Reinhard 痕迹 → 面板能力足够时**专业实现也不压**。我们同一帧 336 nit 处 -0.62%，
  1~320 nit 0.00% → 策略一致，不需要调 0.85 系数。
- 顺带确认产品截图不含呈现曲线（`EngineController` 保存的是 `videoFrameResource` 工作帧，
  HDR 时存 .jxr），所以"跟随/曲线"的效果不能靠截图对拍，只能靠曲线数学 + 运行日志。
