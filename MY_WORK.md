# MY_WORK — 我的 Veyra 开发记录与规则

**最后更新**：2026-10-25
**说明**：这份文件合并了原来的 `CUSTOM_LEDGER.md`（规则）与 `MY_WORK.md`（记录）。
前六部分是**现状与记录**，第七部分是**行动规则**（遇到情况该怎么做）。

作者原版的 610 个文档在 `src/docs/` 里，保持不动 —— 合并进来会造成 git 大冲突，
且作者每次更新都会让你的合并版落后。

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


## 📖 本文内部导航

| 你想做什么 | 看哪一节 |
|---|---|
| 看当前 git 状态、构建部署位置 | 一、当前状态 |
| 看我提过哪些 PR、什么状态 | 二、PR 一览 |
| 看我什么时候做了什么 | 三、时间线 |
| **哪些已知问题还没修？能不能修？** | **一、当前状态 → 已知限制** |
| 查踩过的坑（构建/测试/Git/UI）| 四、踩过的坑 |
| 部署和双版本切换（命令细节）| 五、部署与切换 → **详细命令看 HANDOFF 第五节** |
| **改东西要不要提 PR？提 PR 有什么规矩？** | **七、行动规则** |
| **升级上游、合并冲突、删文件怎么做？** | **七、行动规则** |
| **改了功能后它静默失效了？** | **七、规则十八之三「清理和恢复必须成对」** |
| **开关自己跳回关/自己变了？** | 同上 → 「清理只能动数据，不能动用户意图」 |
| **改了段判断/解析逻辑，怕改错？** | 七、规则十八之四「改解析逻辑前先写例子试」 |
| **要改一个共用属性/全局行为？** | 七、规则十八之五「改共用属性前先查它被谁用」 |
| **某个命令/设置点了没反应？** | 七、规则十八之六「两端都加日志证明发没发到」 |
| **发现某功能「缺失」/和作者版不一样？** | 七、规则十八之七「先查是不是有意关闭」 |
| **要重新 configure 构建？** | 七、规则十八之八「参数逐项对照 HANDOFF」 |
| 相关文档在哪 | 六、文档索引 |

---

## 一、当前状态

### 固定不变的（记这些）

```
仓库      D:\Program\Veyra\src          git 分支 custom
远端      fork   = georgeg911/Veyra-NRVideo（我的）
          origin = Likely7/Veyra-NRVideo（作者的）

构建      D:\Program\Veyra\build
部署      D:\Program\Veyra-2.1.0-NVIDIA-win64-portable
          启动-P5修复版.cmd    → 我的版本，user-data-2.0.0-p5
          启动-作者完整版.cmd  → 作者原版，user-data-2.0.0-author
```

### 会变的（用命令查，不要信文档里的数字）

**HEAD 和提交数每次都变，文档里写死必然过期。用下面三条命令查实时状态：**

```powershell
cd D:\Program\Veyra\src
git log --oneline -1                          # 当前 HEAD
git rev-list --count custom..origin/main      # 落后作者几个提交（应为 0）
git rev-list --count origin/main..custom      # 领先作者几个提交
git status --porcelain                        # 工作区是否干净（应为空）
```

**PR 状态用 `gh` 查，不要抄文档**：

```powershell
gh pr list --repo Likely7/Veyra-NRVideo --author georgeg911 --state all
```

### 本地定制（上游没有）

| 功能 | 状态 | 关键文件（核查用）|
|---|---|---|
| 杜比视界 P5 IPT-PQ 转换 | 稳定 | `shaders/YuvToLinearRgb.hlsl`、`include/veyra/pipeline/DolbyVisionP5.h` |
| 杜比视界 P7 增强层 | 实验性（默认关）| `include/veyra/pipeline/DolbyVisionP7.h`、`src/source/MediaFileSource.cpp` |
| 杜比视界跟随 L1 | 稳定（默认关）| `include/veyra/engine/DoviCurveAdapter.h` |
| 首页最近列表清空 | 稳定 | `qml/Veyra/HomePage.qml` |
| QML 日志错误修复 | 稳定 | `qml/Veyra/VRow.qml`、`DialogHost`、`VMenu` |
| 后端失败提示可操作 | 稳定 | `src/engine/EngineController.cpp` |
| 会话目录诊断 | 稳定 | `src/engine/PresetLibrary.cpp` |
| **B 站模块** | 稳定 | `qml/Veyra/BilibiliPage.qml`、`src/media/BilibiliApi.cpp` |
| **自动选择补帧倍率** | 稳定 | `include/veyra/engine/DisplayFgBudget.h`、`src/ui/QmlPlayerBridge.cpp` |

**核查某个功能还在不在**：

```powershell
git cat-file -e HEAD:include/veyra/pipeline/DolbyVisionP7.h   # 存在则输出空
grep -n "elLayerEnabled_" src/source/MediaFileSource.cpp
```

### 已删除（合并上游后可能复活，需重新删）

| 功能 | 删除日期 | 防护 |
|---|---|---|
| **HDR10 静态元数据**（`SetHDRMetaData`）| 2026-10-18 | `EnhancementSettings` 归一化 |
| HDR 片源转 SDR + RTX Video HDR | 2026-10-03 | 无 |

### 已知限制（别再试，或要试先想清楚）

| 问题 | 原因 | 现状 |
|---|---|---|
| **串流 / 采集卡功能「缺失」** | **有意不构建**：`VEYRA_ENABLE_MOONLIGHT` / `REMOTEPLAY` / `XBOX` 保持默认 OFF，`DECKLINK_SDK_DIR` / `MAGEWELL_SDK_DIR` 留空 | **不是遗漏，用户明确说用不到**（2026-10-25 重申）。**不要当成 bug 去「修」**。依赖源码也不在树里。需要这些功能时用「作者完整版」脚本切到 `.orig`（作者原 exe）|

| ~~**B 站续播闪一下开头 + 缓冲两次**~~ | 作者的 `EngineController::open()` 不支持指定起播位置，只能「先打开再到 0 秒 seek」 | **已解决 / 非缺陷**（2026-10-25）：「闪一下开头」是「先打开再 seek」的**必然结果，不是 BUG**（用户确认，不再处理）。真正的 bug 是「点了根本不跳」，已修好（原来 5/5 全丢，现在准确跳转）。**曾尝试 4 次改引擎，3 次闪退** —— 这次的修复没碰引擎 |
| ~~**播完后点底部播放条 → 403**~~ | `togglePlayPause()` 复用已过期的 B 站 CDN 地址（带时效签名）| **已解决**（2026-10-25）：改记 `bvid/cid/画质` 重新解析拿新地址。**B 站分支必须排在「通用网络地址」分支之前**，否则会先拿过期地址重连 |
| **B 站页内播放 + Esc 退出** | 原生视频窗口永远画在 QML 之上；全屏时 Qt 的 `Shortcut` 收不到 Esc | **已放弃**，相关改动已回滚 |
| **投币 / 各种 `-401`** | **不是我们的 bug**：B 站服务端风控，网页端也会拦 | **B 站行为，不算缺陷**（2026-10-25 明确）。不修 |
| **搜索框回车无响应** | 焦点链问题：控件拿不到 `activeFocus`，按键不进控件也不进 eventFilter。`BilibiliPage.qml:879` 的 `onAccepted` **写了但不触发** | **未解决**（2026-10-25 复测确认仍在）。**已试并失败 4 种方案**，详见 `docs\BILIBILI_MODULE_HISTORY.md` 第一节 |
| **选 4K 却只有 1080p** | **不是 bug**：B 站 4K 要大会员。日志写 `qn=126 not permitted for this account, served qn=112` | **无法修**（权限问题）。界面「输入 3840x2160」是**超分目标**，不是片源分辨率 |
| **专业模式播放前约 2.7 秒透出桌面** | **作者原版架构**：`blankVideo()` 用 `SetWindowRgn(hwnd, 0×0)` 把原生视频窗口裁掉，直到新片源出帧；底下 Qt 的 `vwrap` 是空的，于是透出桌面 | **决定不修**（2026-10-25）。只影响观感，不影响功能 |
| **收藏夹计数 125、实际只列出 121** | **不是我们的 bug**：`/x/v3/fav/resource/list` 不返回已失效的收藏，而 `media_count` 含它们 | **接受**（2026-10-25）。实测 `noBvid` 恒为 0、`apiCount=125` 而逐页累计 121，证明服务端就没发那 4 条；补 `order=mtime&type=0&tid=0` 也拿不回来 |

**「透出桌面」的时序**（`logs/veyra-qml.log`，2026-10-25 实测）：

```
07.705  blank video until the new source presents (opening)   ← 空白期开始
08.598  source-file] opened                                  ← 开网络流 0.89s
09.956  ngx] NR adapter 初始化                                ← NGX 初始化 1.36s（最慢）
10.192  graph] initialized                                   ← 建管线
10.449  present-cost                                          ← 首帧上屏，空白期结束
        合计 2.74 秒
```

**如果要修**（当时评估过，选择了不修）：需要 C++ 把 `g_videoBlanked` 暴露成
QML 属性（`apps/veyra-qml/main.cpp` 里有这个状态，但只在 C++ 内部），然后
QML 用它切换 `vwrap.color`。**只改 QML 无效**：`veyra.running` 在片源打开时
（08.598）就变 true，比首帧上屏早 1.85 秒，遮罩会提前消失。

**续播的实现方式（已定案，不要再当 bug 修）**：

当前实现见 `src/ui/QmlPlayerBridge.cpp` 的 B 站续播分支：

```cpp
// 必须等「新会话已经建好」才发 seek —— 否则打在正在被拆掉的旧会话上，永远丢
const bool biliSessionIsNew = s.sessionId != 0
    && s.sessionId != impl_->biliResumeAfterSession;
if (impl_->biliResumeAt >= 0.0 && s.running && s.duration > 0 && biliSessionIsNew) {
    seekTo(at);   // ← 此时已经从 0 秒开始播了几帧，所以会「闪一下开头」
}
```

**「闪一下开头」是「先打开再 seek」的必然结果，不是缺陷**（2026-10-25 用户确认）。
要消除它只能改作者的核心接口：

```cpp
// 现在
engine.open(window, path, options);
// 需要
engine.open(window, path, options, startPositionMs);
```

涉及 `EngineController::open()` 签名、`run()` 的初始 seek、音视频时钟初始锚点。
**改动侵入性大，前 3 次尝试都闪退，已放弃。不要再试。**

---

## 二、PR 一览

### 已合并（3 个，说明方向对了）

| # | 日期 | 分支 | 标题 |
|---|---|---|---|
| **44** | 10-15 | `hdr-curve-metadata` | HDR 曲线：组合预设、白话旋钮、实时裁剪表、输出感知元数据 |
| **20** | 10-04 | `build-msvc-compat` | 适配新版 MSVC STL 宽窄字符串转换 |
| **19** | 10-04 | `hdr-static-tuning` | 静态 HDR 输出调优（曲线 + 元数据 + 七档预设）|

### 待作者决定（9 个，作者回复前不要动）

| # | 日期 | 分支 | 标题 | 规模 |
|---|---|---|---|---|
| **71** | 10-25 | `fix/network-buffer-clock-clean` | 慢速服务器缓冲时冻结媒体时钟（**中英双语**）| +55/−2 |
| **62** | 10-17 | `feat/remove-hdr-metadata` | 移除 HDR10 静态元数据（**破坏性**）| +4/−216 |
| **60** | 10-17 | `fix/actionable-errors` | 失败提示改成「告诉你怎么做」| +31/−2 |
| **59** | 10-17 | `fix/hdr-display-peak` | 别拿没测量过的显示峰值缩放曲线 | +51/−2 |
| **58** | 10-17 | `fix/log-and-robustness` | 清掉三个反复出现的 QML 日志错误 | +19/−3 |
| **56** | 10-17 | `feat/dolby-vision` | 杜比视界 P5 转换 + P7 增强层合成 | +2210/−40 |
| **55** | 10-17 | `fix/hdr-title-memory-path` | 每片记忆的路径分隔符归一化 | +9/−3 |
| **15** | 10-03 | `home-clear-recent` | 首页清除最近记录 | 小 |
| **13** | 10-03 | `dovi-p5-fix` | 杜比视界 P5：转换 IPT-PQ-C2 基础层（原本显示绿色）| 中 |

### 已关闭（1 个）

| # | 日期 | 分支 | 原因 |
|---|---|---|---|
| **14** | 10-03 | `hdr-source-route-and-brightness` | 方案被 #19/#44 取代 |

---

## 三、时间线

### 2026-10-25 — 环境迁移 + PR #71

**环境迁移**（中文路径导致 CMake 生成失败）

- 旧路径 `D:\项目\Veyra-NV`（中文）→ 新路径 `D:\Program\Veyra`（纯 ASCII）
- 根因：CMake 的 Ninja 生成器在中文路径下**写不出 `CMakeFiles/rules.ninja`**，
  989 条编译规则缺失，构建报 `unknown build rule`
- 实测：同一份源码、同一个 CMake 3.31.6，ASCII 路径正常生成 9309 行 rules
- 不需要升级 CMake

**PR #71 慢速服务器缓冲修复**

修三个独立缺陷（A/B/C），都属作者代码，不含 B 站逻辑：

| 项 | 位置 | 问题 | 修法 |
|---|---|---|---|
| **A** | `resumeTargetLocked()` | 带宽不足时目标 20 秒永远攒不满 | 压到 `startSeconds × 2` |
| **B** | `holding()` | 音频线程每 2–10ms 重算阶段，pause/resume 抖动 | 只读 `phase_` |
| **C** | `nowMs()` | 音频耗尽就把时钟交给墙钟，缓冲后画面回不来 | 网络 hold 期间继续冻结 |

实测（60s 1080p / 10.8 Mbps，限速 45%）：

```
server 0.59 MB/s < bitrate 1.30 MB/s
playing -> buffering ... target=6.00s    (改前 20s)
15 秒缓冲期间时钟只前进 301ms，与 coverage 差 ±32ms（改前约 15 秒）
```

### 2026-10-24 — B 站模块 + 自动选倍率

**B 站模块**（`55e65704`，37 文件 +8025 行）

收藏夹、稍后再看、搜索、扫码登录、续播、弹幕、无限滚动、观看历史上报、
Cookie DPAPI 加密（`VBCK` 前缀）。

**自动选择补帧倍率**（`4d9165b5`）

- 独立开关「自动选择倍率」，只在 DLSS/VFG 下出现（FSR/XeSS 隐藏）
- 规则：`need = ceil(上限 / (源帧率 × 1.02))`，取后端可选的最接近档位
- 打开前用 DASH `frame_rate` **预决策**，避免播放中重建 GPU 管线（省 2 秒卡顿）
- 拿不到帧率时退回首帧后决策

**播放健壮性**（`926023e5`，后被拆为 PR #71）

- B 站播放的三个网络往返移出 GUI 线程
- 现场日志实测：主线程原来静止 **22.9 秒**，修复后全程毫秒级
- 播放后从极简模式改为跳**专业模式**

### 2026-10-17～10-18 — 杜比视界 + HDR 系列（94 个提交）

**杜比视界支持**（PR #13 / #56）

| Profile | 处理方式 |
|---|---|
| **P5** | IPT-PQ-C2 基础层 → 正确转换（原本显示成绿色）|
| **P7** | 解码并合成增强层（覆盖率仅 5.9%，多数帧需基础层）|
| **P8** | 按 HLG 基础层播放（作者已有设计，不应用 RPU、不输出原生 DV）|

关键技术点：

- P5 用 IPT-PQ 色彩空间转换，不再假装是 HDR10
- P7 增强层合成（`DoviBlElCompose.h`）
- L1 动态元数据跟随：**只改高光起点、不动峰值上限**
  （上限是中灰 gamma 的锚点，动了会整体偏色）
- 跟随可加权「帧平均亮度」，阈值可调
- 与 mpv v0.41 / libplacebo v7.376 对拍：内容峰值 336 nit 远低于面板能力时，
  专业实现也**不做非线性压缩**，我们的「0.85 系数 + 内容峰值收口」不是激进方案

**HDR 曲线与元数据**（PR #19 / #44 / #55 / #59 / #62）

- 静态输出调优：亮度曲线 + 七档预设 + 强度旋钮 + 显示器峰值
- 组合预设、白话旋钮、实时裁剪表、输出感知元数据
- 修：每片记忆的路径分隔符归一化
- 修：别拿没测量过的显示峰值缩放曲线
- 移除 HDR10 静态元数据功能（**破坏性，作 PR #62**）

### 2026-10-03 — 起步阶段（50 个提交）

- Dolby Vision P5 首次修复（显示绿色 → 正确转换）
- HDR 静态调优初版
- 首页清除最近记录
- HDR 源路由与逐场景亮度（PR #14，后被关闭，被 #19/#44 取代）

### 2026-10-04 — MSVC 兼容性（PR #20，已合并）

适配新版 MSVC STL 宽窄字符串转换，并补 RemotePlay 裁剪时的条件编译。

---

## 四、踩过的坑

### 构建环境

1. **中文路径不能用于本项目构建**。CMake 的 Ninja 生成器写不出 `rules.ninja`，
   构建报 `unknown build rule`。所有构建目录、临时脚本必须在纯 ASCII 路径。

2. **不要用 `write` 工具写含中文的 .bat**。cmd 按 GBK 读，会乱码
   （实测 `'ram' 不是内部命令`，`format` 被截断）。
   要么全 ASCII，要么 Python 以 `encoding='gbk', newline='\r\n'` 写。

3. **`cmake --fresh` 是破坏性的**。会清空所有 `VEYRA_*` CACHE 变量
   （`VEYRA_FFMPEG_ROOT`、`VEYRA_ENABLE_EXPERIMENTAL_DLSSNR` 等）。
   改配置用 `-D` 逐个补，不要用 `--fresh`。

4. **必须显式给 `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON`**。
   不开的话 `veyra_ngx` 不生成，而 `veyra_engine` 无条件链接它，会链不上。

5. **在同一个 build 目录里切 git 分支后编译会污染缓存**。
   实测出现 `LNK4098`（CRT 冲突）、`LNK2019`（找不到 `FFmpegVideoDecoder::openSoftware`）。
   切分支后要编译，先清 `CMakeFiles`、`*.lib`、`*.exe` 再全量重建（1 分 10 秒）。

### 测试纪律（2026-10-25 用户明确要求）

6. **测试环境变量绝不能残留**。踩过：`VEYRA_TEST_FORCE_DEMUX_SESSION=1`
   和 `VEYRA_TEST_SLOW_IO=0.5` 留在 shell 里没清，导致给用户演示时
   **本地视频被当成网络源**（限速 0.35 MB/s 对码率 2.4 MB/s，首帧要 12 秒），
   用户几乎看不到画面还以为是程序坏了。

   **规则**：优先用 `env -u VAR ./app.exe` 包单条命令（天然不残留）；
   演示前自查 `python -c "import os;print([k for k in os.environ if k.startswith('VEYRA_TEST')])"`
   必须为空。

7. **给用户演示必须用和用户一样的启动方式**。不能用带 `--output-cap` /
   `--fg-auto` / `--exit-after` 的命令行验证后就说「没问题」。

### Git / PR

8. **PR 分支必须从 `origin/main` 切**，不能从自己的开发分支切。
   踩过：从 `4d9165b5`（B 站模块提交）切出的分支带着 28 个无关提交。
   正确做法：

   ```bash
   git checkout -b fix/xxx origin/main
   git cherry-pick <只含目标改动的提交>
   git rev-list --count origin/main..HEAD    # 必须是 1
   ```

9. **PR 的注释和描述一律中英双语**（用户 2026-10-25 明确要求）。

10. **GitHub 推送报证书错误是 CRL 检查失败，不是网络不通**。
    症状：`CRYPT_E_NO_REVOCATION_CHECK`，但 TCP 到 `github.com:443` 是通的。
    修复：`git config --global http.schannelCheckRevoke false`

11. **在 bash 里调 `cmd //c` 跑带引号的命令容易转义出错**。
    写 `.bat` 文件再 `cmd //c xxx.bat`，不要在命令行里拼引号。

### QML / UI

12. **统计 QML 文件数不能用 `ls *.qml`**。`Veyra` 下有 `sponsor` 子目录，
    顶层 58 个、含子目录 75 个。用 Python 的 `os.walk`。

13. **`robocopy /MIR` 会删除目标里的多余文件**。双版本切换时
    `qml\Veyra\BilibiliPage.qml`（我独有的页面）切到作者版会消失，切回会回来 ——
    这是设计意图。

14. **`robocopy` 退出码不是「成功/失败」**：0=无变化，1=有文件复制，≥8 才是失败。

---

## 五、部署与双版本切换

> **完整流程见 `HANDOFF.md` 第五节**（四处部署、切换脚本、用户数据、作者更新规则）。
> 这里只记**为什么这么做**，具体命令不重复。

### 三条关键原则

1. **必须四处都部署**：`veyra_qml_ui.exe`、`exe.p5`、`qml\Veyra\`、`qml.p5\Veyra\`。
   只换 exe 不换 shader/QML 会造成版本不一致 —— **2026-10-19 血的教训**（见规则十九）。

2. **`*.orig` 永远不动**：那是作者原件，切换脚本的切换源。

3. **两个用户数据目录各管一套**：
   - `user-data-2.0.0-p5`（我的版本）、`user-data-2.0.0-author`（作者版）
   - 分开是为了避免 chain-session 格式差异导致「模式会话损坏」

### 切换脚本做什么

| 脚本 | 切到 | 用户数据 |
|---|---|---|
| `启动-P5修复版.cmd` | 我的版本（`.p5` 系列）| `user-data-2.0.0-p5` |
| `启动-作者完整版.cmd` | 作者原版（`.orig` 系列）| `user-data-2.0.0-author` |

每个脚本换三样东西（全用 `%~dp0` 相对路径）：`exe` + `shaders\*.dxil` + `qml\Veyra\`。

**切换到作者版时 `BilibiliPage.qml` 会消失、切回会回来** —— 这是 `/MIR` 镜像的正确行为，
作者版本来就没有 B 站模块。

**作者发布新版本时的更新规则**：见 `HANDOFF.md` 第五节末尾（作者的放 `.orig` 系列，
我的放 `.p5` 系列）。这里不重复。

---

## 六、文档索引

整理后只剩 **5 份主文档**（2026-10-25 从 10 份精简）：

| 内容 | 位置 | 行数 |
|---|---|---|
| **本文件**（记录 + 全部规则）| `src\MY_WORK.md` | 927 |
| **操作手册**（构建/部署/环境/验证）| `src\HANDOFF.md` | 395 |
| **杜比视界完整技术档案** | `src\docs\DOLBY_VISION.md` | 936 |
| **B 站模块开发史**（基线 v2→v17，含历史问题与处理结论）| `src\docs\BILIBILI_MODULE_HISTORY.md` | 1771 |
| **技术排查档案**（13 个专题的排查链）| `src\docs\HISTORY.md` | 1699 |

### 已合并/删除的文档（2026-10-25）

| 原文件 | 行数 | 去向 |
|---|---|---|
| `docs/DOVI_WORK_HANDOFF.md` | 370 | → `DOLBY_VISION.md` 第一节 |
| `docs/DOVI_P5_VERIFICATION.md` | 230 | → `DOLBY_VISION.md` 第二节 |
| `docs/DOLBY_BL_EL_COMPOSITING.md` | 250 | → `DOLBY_VISION.md` 第三节 |
| `docs/MERGING.md` | 141 | → 本文件第七条规则十 |
| `docs/VeyraNV_LOCAL_BUILD_NOTES.md` | 45 | → 本文件第六节构建命令 |
| `CUSTOM_LEDGER.md` | 567 | → 本文件（20 条规则）|
| `D:\DSH\dolby-vision-p5-report.md` | 610 | 已被 `DOLBY_VISION.md` 覆盖，删除 |
| `D:\DSH\.dsh-tmp\keep\veyra-history-README.md` | 1627 | → 迁入 `docs\BILIBILI_MODULE_HISTORY.md` |

**作者原版文档**：`src\docs\` 下另有作者自己写的 600+ 个文档，保持不动。

---

## 七、行动规则

> **背景**：我不是开发者，本地改功能凭感觉判断好坏，提了 PR 之后才发现有 bug 又回头删功能。
> 担心自己的改动污染上游，怕作者不小心合并了出问题。
> 这些规则的目标：**让「本地随便玩」和「给上游提什么」彻底分开**。

### 核心原则：三条线，别混

| 线 | 在哪 | 可以随便改吗 | 会上游吗 |
|---|---|---|---|
| **玩票区** | `custom` 分支 | ✅ 随便改、随便删、随便试 | ❌ 永不上游 |
| **候选区** | `fix/*` `feat/*` 分支 | ⚠️ 只改一件事 | ✅ 检查通过后提 PR |
| **干净区** | `origin/main` | ❌ 不直接碰 | — |

**一句话**：`custom` 是我的游乐场，`origin/main` 是别人的家。
**从游乐场里搬东西去别人家之前，必须先在候选区单独放好、洗干净。**

### 一、改动先分三色，决定能不能提

每次想改东西，先问自己：

| 颜色 | 判据 | 处理 |
|---|---|---|
| 🟢 **绿** | **上游的 bug**（他写错了、崩了、日志报错）| 可以提 PR |
| 🟡 **黄** | **行为/观感**（画面效果、默认值、UI 排版、文案）| **先问作者**，他同意再提 |
| 🔴 **红** | **删功能 / 加功能 / 改架构** | **默认只留本地**，不提 |

**判断口诀**：

> 「这是**修好他的 bug**，还是**我想让他按我的喜好改**？」
> 修 bug → 可以提　｜　我的喜好 → 只留本地

**实例对照**：

- #55 修路径分隔符匹配 → 🟢 绿（他的 bug）
- #58 修三个日志错误 → 🟢 绿（他的 bug）
- #60 改错误提示文案 → 🟢 绿（可读性，几乎无争议）
- #59 修假峰值缩放 → 🟡 黄（**会改变画面**，虽是真 bug）
- #71 缓冲时钟修复 → 🟢 绿（他的 bug，附实测数据）
- #56 杜比视界全套 → 🔴 红（新功能，+2210 行，本地未完全验证）
- #62 删 HDR 元数据 → 🔴 红（**删他的功能**，只有他明确要才提）

### 二、永远不要从 `custom` 提 PR

```powershell
# ❌ 错：从 custom 开分支，必然夹带杜比视界、删元数据、改过的 UI
git checkout -b fix/something custom

# ✅ 对：从 origin/main 开，只 cherry-pick 目标提交
git checkout -b fix/something origin/main
git cherry-pick <那个提交>
git rev-list --count origin/main..HEAD   # 必须是 1
```

**2026-10-25 实测**：从 `4d9165b5` 切出的 PR 分支带了 28 个无关提交。

### 三、提 PR 前必跑检查

```powershell
# 1. 确认分支只含 1 个提交
git rev-list --count origin/main..HEAD

# 2. 确认改动文件范围对
git diff origin/main..HEAD --stat

# 3. 确认不含不该有的内容（如 B 站代码）
git diff origin/main..HEAD | grep -c "bilibili"    # 应为 0

# 4. 确认能编译
ninja.exe veyra_qml_ui
```

### 四、PR 要小、要单一、要能解释

- 一个 PR 只解决一个问题
- 提交信息写「为什么」，不是「改了什么」
- **注释和描述一律中英双语**（2026-10-25 用户要求）

### 五、不确定的改动，先写进本文件，别急着提

拿不准是 bug 还是个人偏好时，先记在这里，用一段时间再判断。

### 六、已经提了但发现有问题

1. **在 PR 分支上改**，不改 `custom`：

   ```bash
   git checkout fix/xxx
   # 改代码
   git commit -am "..."
   git push fork fix/xxx          # PR 自动更新
   ```

2. 如果问题严重，可以在 GitHub 上关闭 PR，或在 PR 里说明

### 七、定期自查：本地和上游差多少

```powershell
git fetch origin
git rev-list --count custom..origin/main     # 落后多少
git rev-list --count origin/main..custom     # 领先多少
```

### 八、已提 PR 的处置状态

作者自行决定是否合并，**不合并也没关系**。作者回复前不要动它们
（不要关闭、不要催促、不要反复推）。

### 九、作者合并了我的 PR 之后，怎么继续

1. 先 `git fetch origin` 拉最新的 `origin/main`
2. 对比自己的 `custom` 分支，看哪些改动已被上游吸收
3. 已被吸收的，从 `custom` 里删掉对应改动（避免重复）
4. 记录到本文件第一节「已进上游」

### 十、任何 PR 被合并后升级：逐项问用户，不许替他决定

升级涉及冲突、功能取舍时，**必须逐项问用户**：
- 这个功能上游已吸收，我删掉本地版本？
- 这个功能上游没动，我保留？
- 这个冲突两边都有改动，用哪边？

**不许自己判断「应该没影响」就替用户决定**（2026-10-18 用户明确要求）。

### 十一、升级上游时的安全顺序

```powershell
# 1. 先存档当前状态（提交或 stash）
git status                    # 必须干净
git tag backup-before-upgrade

# 2. 拉取上游
git fetch origin

# 3. 合并
git merge origin/main

# 4. 有冲突就停下来问用户

# 5. 合并后立刻验证
ninja.exe veyra_qml_ui
```

### 十二、一个功能 = 一个提交

不要把多个不相关的改动塞进一个提交，否则 cherry-pick 时没法只拿一部分。

### 十三、提交信息写「为什么」，不是「改了什么」

```
❌ 错：fix: 改了 DemuxSession.cpp
✅ 对：fix(playback): 服务器缓冲时保持媒体时钟冻结
        现场日志显示时钟跑了 16 秒而 coverage 停在原处
```

**中英双语**（2026-10-25 要求）。

### 十四、合并上游前先看净差异

```powershell
git diff custom..origin/main --stat    # 不是 custom...origin/main
```

### 十五、每次升级上游后必做三件事

1. 检查「已删除」的功能有没有复活（见第一节）
2. 重新构建并部署四处
3. 双版本切换测一遍

### 十六、HDR 元数据功能的防护（重要）

HDR10 静态元数据是我删掉的功能，**合并上游后一定会复活**。

检查方法：

```powershell
grep -rn "SetHDRMetaData" src/ include/ | head -3
```

如果出现了，说明复活了，需要重新删（对应 PR #62）。

### 十七、验证请求必须连 exe 一起部署

说「请打开看看」之前，**必须四处都部署**（exe / exe.p5 / qml / qml.p5），
否则用户看到的界面和描述不一致。部署后报告哈希。

**验证要从简**（用户明确要求）：编译 + 跑相关测试 + 哈希核对，**只启动一次程序**。
用户多次抱怨「太慢 / 等太久」，不要用固定 `sleep` 反复启动 GUI 来「验证」。

### 十八、删除一律进回收站

```powershell
Add-Type -AssemblyName Microsoft.VisualBasic
[Microsoft.VisualBasic.FileIO.FileSystem]::DeleteFile($path,
  [Microsoft.VisualBasic.FileIO.UIOption]::OnlyErrorDialogs,
  [Microsoft.VisualBasic.FileIO.RecycleOption]::SendToRecycleBin)
```

删除前先打印精确清单（绝对路径 + 大小）确认。

### 十八之二、子代理只能读，不能改文件（2026-10-25 教训）

**子代理和我一样有写文件的能力。光在指令里写「不要修改文件」是软约束，靠它自觉。**
要真正拦住，必须用权限预设。

**两种控制手段，缺一不可**：

| # | 手段 | 强度 | 怎么做 |
|---|---|---|---|
| 1 | **软约束** | 弱 | 指令里写死「只读，不要修改任何文件」|
| 2 | **硬约束** | 强 | 用任务板派活并设 `permission: "read-only"`，子代理物理上无法写入 |

**各调用方式能否强制只读**：

| 方式 | 能否强制只读 | 说明 |
|---|---|---|
| `subagent` | ❌ 不能 | 没有权限参数，只能靠提示词 |
| **`task_board_create` + `task_board_run`** | ✅ **能** | 卡片有 `permission` 字段，设 `read-only` 后写操作直接被拒 |
| `workflow` | ❌ 不能 | `agent()` 只支持 label/phase/model，无权限控制 |

**所以审计/复检类任务的正确派法**：

```
task_board_create(
  title: "审计 XXX",
  prompt: "...",
  permission: "read-only"     ← 关键，不要省
)
task_board_run(taskId)
```

**踩过的坑**：让子代理复检 5 份文档（只给了软约束），它顺手把发现的问题直接改了；
与此同时主会话也在改同一批文件 —— 两边撞车，子代理的提交把主会话的**半成品**一并提交，
导致提交信息与内容不符（说「改了 2 处」实际含 3 处），事后要靠 `git reset --soft`
合并重写历史才理清。

**另外两条**：

1. **纯命令核对**（`grep`/`ls`/`head`/`gh`）不需要子代理，主会话里并行发命令更快。
   这类任务的结论是确定的，换哪个模型、重跑几次都是同一结果。
2. **子代理适合**：需要读大量文件、需要独立上下文判断的任务（大范围代码审查、
   跨文件一致性分析）。这类任务才值得付启动和转述的开销。

### 十八之三、清理和恢复必须成对（2026-10-25 四次同型错误）

**改清理逻辑时，必须同时确认「恢复路径」存在。** 只清不恢复 = 功能静默消失。

**2026-10-25 一天内犯了三次，全是同一个毛病：**

| # | 改了什么 | 只做了前半段 | 后果 |
|---|---|---|---|
| 1 | B 站扫码登录异步化 | worker 把 `qrKey`/`qrUrl` 带回来，tick 里**忘了写回 `impl_`** | 二维码白框、点完立刻退回原页 |
| 2 | 开弹幕异步化 | worker 只带回**条数**，忘了创建叠加层 + `setItems` | 弹幕功能完全失效，开关怎么点都没字幕 |
| 3 | `playSelected()` 换分P | 关掉了旧弹幕，**忘了按新分P重新加载** | 一点播放弹幕就消失，得回详情页再点一次开关 |

**共同点**：改的时候只想着「这里该清掉什么」，忘了清掉之后还要把它恢复。

**自查方法**：写完一段「清理/关闭/重置」的代码，立刻问自己三句：

1. 这个状态是谁打开的？
2. 我清掉之后，谁负责重新打开？
3. 如果没人负责，是不是应该在**同一条路径**里补上？

**代码里的典型陷阱位置**：

| 位置 | 陷阱 |
|---|---|
| `openPath()` 的 `!keepBilibiliStream` 分支 | 清掉一堆状态，但 B 站播放会走 `keepBilibiliStream=true`，别的地方补 |
| `playSelected()` 开头 | 关弹幕给「换分P」让路，必须在这条路径末尾重开 |
| worker 产出要写回 `impl_` 的方法 | worker 不能写 `impl_`，tick 里必须显式接住 |
| 异步化的「动作后处理」 | 网络取数据只是一半，后面的创建/清洗/提交不能丢 |

#### 补充：清理只能动「数据」，不能动「用户意图」

这是同一个毛病的第四种表现，2026-10-25 当天第三次复发后总结出来的。

**区分两类状态**：

| 类型 | 例子 | 换分P/换视频时 |
|---|---|---|
| **数据** | 弹幕条数、弹幕内容、续播秒数、画质列表 | ✅ **该清就清** |
| **用户意图** | 弹幕开关开着还是关着、用户选的画质档、自动倍率开关 | ❌ **绝不能动** |

**踩过的具体例子**：切换分P 时，弹幕加载失败的分支里写了 `root.danmakuOn = false`。
结果用户切到一个**没有弹幕的分P**，或网络偶发失败一次，开关就自己跳回「关」，
看起来像「换个分P 就把弹幕功能关了」。**实际上用户从没关过它。**

**判断口诀**：

> 「这个值是用户点出来的，还是程序算出来的？」
> 用户点出来的 → 只有用户能改　｜　程序算出来的 → 程序随意更新

**四类触发点，处理方式不同**：

| 触发 | 数据怎么办 | 开关怎么办 |
|---|---|---|
| 用户点开关 | 加载或清空 | 跟着用户的点击 |
| 切分P / 换视频 | 清掉旧的，按新的重载 | **保持不变** |
| 加载失败 | 清空 | **保持不变**，只提示原因 |
| 播放开始 | 重载新的 | **保持不变** |

### 十八之四、改解析逻辑前先写例子试（2026-10-25）

**编译通过只说明语法对，不说明逻辑对。** 改字符串解析、状态判断这类代码时，
先花两分钟写几个小例子跑一遍，确认结果对，再动 C++。

**2026-10-25 一次具体翻车**：修 `fav:<文件夹id>` 被当成页码的问题，连改两次全错。

| 次数 | 写法 | 错在哪 |
|---|---|---|
| 1 | `head.startsWith("fav:")` | `head` 已经是 `"fav"`，永远不含冒号 → 恒为假 |
| 2 | `head.indexOf(':') == head.lastIndexOf(':')` | 判断反了，带页码时也判成 id |
| 3 | `head == "fav"` | ✅ 先写 11 个用例脚本验证，一次通过 |

**成本对比**：

| 做法 | 代价 |
|---|---|
| 改完直接编译 + 开程序测 | 一次 5–10 分钟，而且我不一定看得出是哪一步错了 |
| **先写小脚本跑用例** | **2 分钟，立刻知道对不对** |

**写法**：改那段逻辑的等价脚本（Python 就行），把典型输入列进去，跑一遍看输出：

```python
for k in ['fav:203714961', 'fav:203714961:2', 'fav:0', 'fav:0:2',
          'popular:2', 'search:猫:3', 'qualities:BV1:123', 'detail:BV1']:
    print(k, '->', parse(k))
```

**适用场景**：字符串切分、前缀判断、页码/ID 提取、状态机转移、条件分支。

### 十八之五、改「共用属性」前先查它被谁用（2026-10-25）

**改一个 C++ 属性前，先 `grep` 它在 QML 里被多少处引用。** 用的人越多，越不能改
它的语义——那是作者原有页面在依赖它。

**2026-10-25 的具体教训**：为了让 B 站视频播完后按钮显示「播放」而不是「暂停」，
我把 `paused()` 改成「Paused 或 Ended 都算暂停」。这个属性被**作者原有的 7 处界面**
共用（极简底栏、专业模式、节点模式、导出页、两处音轨标记、暂停动画）。结果本地
视频播完后的按钮图标也跟着变了 —— **等于改了作者的原功能**。用户当场指出。

**正确做法**：新加一个专用属性，默认值保持原行为，只在自己的页面接上。

```cpp
// 共用属性：一个字都不动
bool paused() const { return t == Paused; }
// 新属性：只给我的功能用，别人看不到
bool biliEndedReadyToReplay() const { return biliSourceActive && ...; }
```

```qml
// 组件里加可选属性，默认 false = 原行为
property bool biliEnded: false
name: (!bar.biliEnded && veyra.running && !veyra.paused) ? "pausefill" : "playfill"
```

**判据（问自己）**：

| 问题 | 若不是 → 别改 |
|---|---|
| 这个属性是不是只有我的页面在用？ | 共用属性不能改语义 |
| 改了之后，本地文件 / 采集 / 串流的结果会变吗？ | 会变就不能改 |
| 能不能新加一个默认关闭的开关？ | 能加就别改原属性 |

**顺带限制**：我给 B 站加的功能，**只允许影响 B 站页**。作者的本地播放、采集、
串流、专业模式、节点页、导出页，一处都不能变。

### 十八之六、命令不生效时，两端都加日志证明「发没发到」（2026-10-25）

**「我调用了」和「对方收到了」是两件事。** 怀疑某个命令没生效时，在**发送端**和
**接收端**各加一条日志，看它们是否配对 —— 比盯着界面猜快得多。

**2026-10-25 的实例**：B 站续播点了没反应。在 `seekTo()`（发送端）和引擎取走
`seekSeconds_` 处（接收端）各加一条日志，一次运行就定性：

```
手动 seek：seek-call → seek-drain 同毫秒内     3/3 成功
续播 seek：只有 seek-call，完全没有 seek-drain  5/5 失败
```

**结论直接出来**：seek 发给了引擎，但引擎永远没取走。于是查「为什么没取走」，
发现是打在正要被拆掉的旧会话上。

**如果没有这两条日志**，只能看到「界面没跳」，会误以为是 seek 逻辑或网络源的问题 ——
之前几轮就是这么走偏的。

**写法**：

| 位置 | 日志内容 |
|---|---|
| 发送端入口 | 参数值 + 当前上下文（position / duration / transport / sessionId）|
| 接收端消费点 | 收到的值 + 序号 |

**判据**：

| 日志组合 | 结论 |
|---|---|
| 两端都有 | 命令送达，问题在执行或后续 |
| 只有发送端 | **没送达**，查为什么（时机、覆盖、目标对象已失效）|
| 都没有 | 根本没调用到那一步 |

**用完删掉**，只保留一条带上下文的关键日志（如本例的 `ui-resume` 现在带
`session=` 和 `dur=`，这次正是它让差异可见）。

### 十八之七、判断「功能缺失」前先查是不是有意关闭（2026-10-25）

**看到某个功能不可用、或二进制对比发现「少了东西」，第一件事不是修，而是查它是不是
被有意关掉的。**

**2026-10-25 的翻车**：我拿自己的构建和作者原版 exe 做二进制特征比对，发现
Moonlight（106→21）、PS5 RemotePlay（70→20）、Xbox（201→23）的字符串大幅减少，
当场报告「我的构建缺三大串流功能」。**用户回我：这些我早就说过用不到、不构建。**

**错在两点**：

1. **没有先查文档**里有没有「有意关闭」清单（当时文档里确实没写，这是第二层错）
2. **把「和作者原版不一样」直接等同于「我漏了」** —— 根本身就是有意裁剪的

**现在的正确流程**：

| 步骤 | 做什么 |
|---|---|
| 1 | 查 `HANDOFF.md` 的「**有意关闭的功能**」表，看它在不在里面 |
| 2 | 不在表里 → 再查 `git log -- CMakeLists.txt`，确认开关的历史状态 |
| 3 | 只有确认「本来该有、确实没有」才报缺 |

**已确认的有意关闭项**（详见 `HANDOFF.md`）：Moonlight / Sunshine 串流、PS5 RemotePlay、
Xbox 串流、DeckLink 采集、Magewell 采集。理由：**用户用不到**，且依赖源码不在树里。

**还有两个「死选项」不要动**：`VEYRA_ENABLE_DLSS_SR` / `VEYRA_ENABLE_DLSS_FG` 在
CMakeLists 里只有 `option()` 声明、源码零引用，显示 OFF 不影响 DLSS 功能。

### 十八之八、构建参数必须逐项对照手册，不能凭记忆（2026-10-25）

**`HANDOFF.md` 第四节就是唯一权威的配置命令**，照抄，不要凭印象增减参数。

**2026-10-25 的翻车**：我照着 `HANDOFF.md` 的配置命令构建，而那份命令里**漏了
`-DVEYRA_CUDA_DRIVER_INCLUDE_DIR`** —— 于是 VFG 在编译期被静默禁用，用户报「VFG 灰了」。
旧说明文件里其实写着这条参数，我读到了却没往构建脚本里加，还顺手把它从新文档里删了。

**这类错误的共同点：构建成功、程序能跑、只是某个功能悄悄不见了。**

**排查手法**（这次有效）：把当前配置里所有 `VEYRA_*` 参数打印出来，逐个问「它控制什么、
现在这个值对不对」，而不是只看构建有没有报错。

**其中一个最阴险**：
```cmake
if(EXISTS "${VEYRA_CUDA_DRIVER_INCLUDE_DIR}/cuda.h")   # 路径空 → 条件假 → 宏不定义
  target_compile_definitions(veyra_pipeline PRIVATE VEYRA_HAVE_CUDA_DRIVER_HEADERS=1)
endif()
```
```cpp
bool VfgBackend::runtimeAvailable(...){
#ifdef VEYRA_HAVE_CUDA_DRIVER_HEADERS
    ...
#else
    return false;    // ← 静默禁用
#endif
}
```

### 十九、切换脚本必须覆盖全部二进制差异（2026-10-19 血的教训）

**只换 exe 不换 shader / QML** 会造成版本不一致，界面和实际行为对不上。

切换脚本必须同时换：

1. `veyra_qml_ui.exe`
2. `shaders\*.dxil`（如果两边不同）
3. `qml\Veyra\*.qml`

**判断依据**：

```powershell
# 两个包的 exe 是否相同
certutil -hashfile veyra_qml_ui.exe.p5 MD5
certutil -hashfile veyra_qml_ui.exe.orig MD5

# shader 是否相同
certutil -hashfile shaders\YuvToLinearRgb.dxil.p5 MD5
certutil -hashfile shaders\YuvToLinearRgb.dxil.orig MD5
```

**只要有一处不同，切换脚本就必须覆盖它。**

**如果将来要用工具重新生成切换脚本，必须「差异反推」**（来自 2.0.5 时代的经验）：

> 跑生成工具之前，**程序目录必须处于作者原版状态**（用 `*.orig` / `qml.orig` 还原），
> 否则工具算出来的差异是错的 —— 它会把「已经是定制版」当成基线来比。

现在脚本由手工维护，这条只在需要重新生成时适用。

### 二十、升级必须改目录名

换新版 Veyra 时，**部署目录名要跟着版本号改**（如 `Veyra-2.1.0-...`），
不要沿用旧名字 —— 否则两个版本混在一起分不清。

改名后同步更新：
- 切换脚本里的路径（都是 `%~dp0` 相对路径，通常不用改）
- 本文件第一、五节里的部署路径
- `HANDOFF.md` 里的路径

---

## 七之二、分支历史清理手法（2026-10-18，可复用）

### 为什么清理

`custom` 领先上游 263 个提交，但净差异只有 46 个文件。中间大量提交是「改完又撤销」：

```
7 对 feat/fix + 后置 Revert（补帧页排版 5 次、seek 重建 1 次、显示峰值 1 次）
HDR 元数据功能来回三轮：移除（2.0.6）→ 加回（16ee455d）→ 再移除（本次）
大量调试/诊断中间提交（diag(...)、wip(...)）
```

后果是合并上游时冲突面变大、回溯问题要翻几百条提交、`git log` 读不出所以然。

### 怎么做（绝不 `rebase -i`）

263 个提交交互式重排风险太高。改用这个手法：

1. `git checkout -B <new> origin/main` —— 从上游开干净分支
2. 按**功能分组**，每组把文件从旧 HEAD 检出：`git checkout <old-head> -- <files>`
3. 每组用一段说明写清楚「为什么」，然后提交
4. **逐文件哈希校验**：`git diff --name-only <old-head>..HEAD` 必须为空
5. `git diff --shortstat` 必须与清理前**逐字节一致**
6. 全量编译 + 冒烟，再 `git branch -f custom <new>`

**三重备份**（重写历史前必须做）：

```powershell
git bundle create <path>\custom-full.bundle --all
git tag backup/custom-before-cleanup <old-head>
# 外加全部源文件 SHA256 快照（事后比对用）
```

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


## 八、待办与未决事项

| 项 | 状态 |
|---|---|
| PR #71 等作者回复 | 等待中，作者可能要求调整 |
| PR #13 / #56 / #62 等 8 个旧 PR | 都还开着，作者没表态 |
| B 站模块是否提 PR | 需先问作者意向（作者没有这个功能，属🔴红）|
| 自动选倍率是否提 PR | 属个人口味（🟡黄），需作者认可 |
| `D:\DSH\dolby-vision-p5-report.md` | 建议移进 `src\docs\` 跟 git 走 |

---

**说明**：这份文件记录我自己的改动与规则。作者原版的 610 个文档（6.5 MB）在
`src\docs\` 保持不动 —— 合并进来会造成 git 大冲突，且作者更新后你的版本永远落后。
