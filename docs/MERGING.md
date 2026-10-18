# 合并冲突决策流程

> 从 `CUSTOM_LEDGER.md` 第 10 条拆出，内容一字未改。
> 触发时机：升级上游时 git 报冲突，或作者合并了你的 PR。

### 十、任何 PR 被作者合并后升级：逐项问用户，不许替他决定（通用规则）

> 用户明确要求：**作者合并 PR 时可能改动过实现，升级时必须让我选「跟作者」还是「按本地原来的」。**

#### 适用范围：所有 PR，不只是 #56 / #62

**这条规矩对所有被作者合并的 PR 生效**，包括：

- 以后新提的 PR（`fix/*`、`feat/*`）
- 作者主动采纳、或他自己实现的同类改动
- 甚至作者**没有**采纳你的 PR，但自己改了同一批文件

**判断依据不是「这是不是我提的 PR」，而是「合并时这个文件两边都改过」。**

只要一个文件在**上游新版本**和**本地 custom** 里都变了，就必须问用户。
不管这个改动起初来自你的 PR、作者的提交，还是别人的贡献。

#### 为什么必须问（不是礼貌，是数据）

以 #56 / #62 为例实测冲突面（`origin/main` vs `custom` vs 两个 PR 分支）：

```
#56 改 27 个文件，#62 改 8 个，custom 改 47 个
三方重叠（合并时必冲突）：28 个文件
只在 PR 里（合并后归上游，本地无冲突）：0 个   ← 注意：一个都没有
只在 custom 里（本地独有，不受影响）：19 个
```

**关键事实**：`#56` 的 27 个文件**全部**也在 `custom` 里被改过。
这意味着一旦作者合并 #56，**每个文件都会冲突**，没有"自动跟随"的余地。

#### 怎么找出「哪些文件要问」（通用做法）

每次升级上游（不管是不是因为合并了你的 PR），都先跑这三步：

```powershell
git fetch origin

# 1. 本地相对新上游改了哪些文件（这些是可能冲突的候选）
git diff --name-only origin/main..custom

# 2. 其中哪些在新上游里也被改过（相对上次同步点）
git diff --name-only <上次的 origin/main>..origin/main

# 3. 两个列表的交集，就是必须问用户的文件
```

**简化判定**：直接尝试合并，让 git 告诉你：

```powershell
git checkout custom
git merge origin/main --no-commit --no-ff
git diff --name-only --diff-filter=U    # 冲突文件清单
```

**这个清单里的每一个，都要问。** 一个都不许自己解决。

#### 决策分类（按文件性质，不是按文件数）

把冲突文件分成四类，每一类问一次，不要逐个问几十次。

（下表以 #56/#62 的实际文件举例；**以后遇到别的 PR，按同一套分类逻辑套用**，
不要求文件完全相同——分类依据是「这是新文件 / 数据结构 / 实现 / 界面」）：


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
