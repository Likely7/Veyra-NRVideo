# MY_WORK — 我的 Veyra 开发记录

**最后更新**：2026-10-25
**说明**：这份文件记录我在 Veyra（作者 Likely7/Veyra-NRVideo）之上做的全部改动。
只记我自己的东西，作者原版的 583 个文档在 `src/docs/` 里，不动。

---

## 一、当前状态速查

```
仓库      D:\Program\Veyra\src          git 分支 custom
HEAD      c46140cc  Merge origin/main into custom
落后      作者主线 0 个提交（2026-10-25 已同步）
领先      作者主线 27 个提交
远端      fork = georgeg911/Veyra-NRVideo（我的）
          origin = Likely7/Veyra-NRVideo（作者的）

构建      D:\Program\Veyra\build        全量 140/140 通过
部署      D:\Program\Veyra-2.1.0-NVIDIA-win64-portable
          启动-P5修复版.cmd    → 我的版本，user-data-2.0.0-p5
          启动-作者完整版.cmd  → 作者原版，user-data-2.0.0-author
```

**统计**：本地提交 157 个，已提 PR 13 个（3 个已合并，1 个已关闭，9 个待处理）。

---

## 二、提给作者的 PR 一览

| # | 状态 | 日期 | 分支 | 标题 |
|---|---|---|---|---|
| **71** | 待处理 | 10-25 | `fix/network-buffer-clock-clean` | 慢速服务器缓冲时冻结媒体时钟（**中英双语**）|
| **62** | 待处理 | 10-17 | `feat/remove-hdr-metadata` | 移除 HDR10 静态元数据功能（**破坏性**）|
| **60** | 待处理 | 10-17 | `fix/actionable-errors` | 失败提示改成「告诉你怎么做」|
| **59** | 待处理 | 10-17 | `fix/hdr-display-peak` | 别拿没测量过的显示峰值缩放曲线 |
| **58** | 待处理 | 10-17 | `fix/log-and-robustness` | 清掉三个反复出现的 QML 日志错误 |
| **56** | 待处理 | 10-17 | `feat/dolby-vision` | 杜比视界 P5 转换 + P7 增强层合成 |
| **55** | 待处理 | 10-17 | `fix/hdr-title-memory-path` | 每片记忆的路径分隔符归一化 |
| **44** | ✅ 已合并 | 10-15 | `hdr-curve-metadata` | HDR 曲线：组合预设、白话旋钮、实时裁剪表、输出感知元数据 |
| **20** | ✅ 已合并 | 10-04 | `build-msvc-compat` | 适配新版 MSVC STL 宽窄字符串转换 |
| **19** | ✅ 已合并 | 10-04 | `hdr-static-tuning` | 静态 HDR 输出调优（曲线 + 元数据 + 七档预设 + 强度旋钮）|
| **15** | 待处理 | 10-03 | `home-clear-recent` | 首页清除最近记录 |
| **14** | ❌ 已关闭 | 10-03 | `hdr-source-route-and-brightness` | HDR 源上的 RTX Video HDR + 逐场景亮度 |
| **13** | 待处理 | 10-03 | `dovi-p5-fix` | 杜比视界 P5：转换 IPT-PQ-C2 基础层（原本显示成绿色）|

### 已合并的 3 个（说明方向对了）

- **#19 / #44** —— HDR 静态输出调优与曲线元数据，作者采纳
- **#20** —— MSVC 兼容性修复，作者采纳

### 特别注意

- **#62 是破坏性改动**（`feat(hdr)!`），作者很可能不接受，属于我的个人偏好
- **#71** 是唯一「中英双语注释 + 中英双语 PR 描述」的，作为以后的模板

---

## 三、时间线（做了什么）

### 2026-10-25 — 开发环境迁移 + PR #71

**环境迁移**（中文路径导致 CMake 生成失败）

- 旧路径 `D:\项目\Veyra-NV`（中文）→ 新路径 `D:\Program\Veyra`（纯 ASCII）
- 根因：CMake 的 Ninja 生成器在中文路径下**写不出 `CMakeFiles/rules.ninja`**，
  导致 989 条编译规则缺失，构建报 `unknown build rule`
- 实测对照：同一份源码、同一个 CMake 3.31.6，ASCII 路径正常生成 9309 行 rules
- 不需要升级 CMake（3.31.6 够用）

**PR #71 慢速服务器缓冲修复**

修三个独立缺陷（A/B/C），都属作者代码，不含 B 站逻辑：

| 项 | 位置 | 问题 | 修法 |
|---|---|---|---|
| **A** | `resumeTargetLocked()` | 带宽不足时目标 20 秒永远攒不满 | 压到 `startSeconds × 2` |
| **B** | `holding()` | 音频线程每 2–10ms 重算阶段，导致 pause/resume 抖动 | 只读 `phase_` |
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

- B 站播放的三个网络往返（上报进度/解析地址/读续播位置）移出 GUI 线程
- 现场日志实测：主线程原来静止 **22.9 秒**，修复后全程毫秒级
- 播放后从极简模式改为跳**专业模式**

### 2026-10-17～10-18 — 杜比视界 + HDR 系列（66+28 个提交）

**杜比视界支持**（PR #13 / #56）

| Profile | 处理方式 |
|---|---|
| **P5** | IPT-PQ-C2 基础层 → 正确转换（原本显示成绿色）|
| **P7** | 解码并合成增强层 |
| **P8** | 按 HLG 基础层播放（作者已有设计，不应用 RPU、不输出原生 DV）|

关键技术点：

- P5 用 IPT-PQ 色彩空间转换，不再假装是 HDR10
- P7 增强层合成（`DoviBlElCompose.h`）
- L1 动态元数据跟随：**只改高光起点、不动峰值上限**
  （上限是中灰 gamma 的锚点，动了会整体偏色）
- 跟随可加权「帧平均亮度」，阈值可调
- 与 mpv v0.41 / libplacebo v7.376 对拍验证：
  内容峰值 336 nit 远低于面板能力时，专业实现也**不做非线性压缩**，
  我们的「0.85 系数 + 内容峰值收口」在 HDR 直出场景下不是激进方案

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
- HDR 源路由与逐场景亮度（PR #14，后被关闭，被 #19/#44 的方案取代）

### 2026-10-04 — MSVC 兼容性（PR #20，已合并）

适配新版 MSVC STL 的宽窄字符串转换，并补 RemotePlay 裁剪时的条件编译。

---

## 四、踩过的坑（避免重犯）

### 构建环境

1. **中文路径不能用于本项目构建**。CMake 的 Ninja 生成器写不出 `rules.ninja`。
   所有构建目录、临时脚本必须在纯 ASCII 路径。

2. **不要用 `write` 工具写含中文的 .bat**。cmd 按 GBK 读，会乱码
   （实测 `'ram' 不是内部命令`，`format` 被截断）。
   要么全 ASCII，要么 Python 以 `encoding='gbk', newline='\r\n'` 写。

3. **`cmake --fresh` 是破坏性的**。会清空所有 `VEYRA_*` CACHE 变量
   （`VEYRA_FFMPEG_ROOT`、`VEYRA_ENABLE_EXPERIMENTAL_DLSSNR` 等）。
   改配置用 `-D` 逐个补，不要用 `--fresh`。

4. **必须显式给 `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON`**。
   不开的话 `veyra_ngx` 目标不生成，而 `veyra_engine` 无条件链接它，会链不上。

5. **在同一个 build 目录里切 git 分支后编译会污染缓存**。
   实测出现 `LNK4098`（CRT 冲突）、`LNK2019`（找不到 `FFmpegVideoDecoder::openSoftware`）。
   切分支后要编译，先清 `CMakeFiles`、`*.lib`、`*.exe` 再全量重建（1 分 10 秒）。

### 测试纪律（2026-10-25 用户明确要求）

6. **测试环境变量绝不能残留**。踩过：`VEYRA_TEST_FORCE_DEMUX_SESSION=1`
   和 `VEYRA_TEST_SLOW_IO=0.5` 留在 shell 里没清，导致给用户演示时
   **本地视频被当成网络源**（限速 0.35 MB/s 对码率 2.4 MB/s，首帧要 12 秒），
   用户几乎看不到画面，还以为是程序坏了。

   **规则**：优先用 `env -u VAR ./app.exe` 包单条命令（天然不残留）；
   演示前自查 `python -c "import os;print([k for k in os.environ if k.startswith('VEYRA_TEST')])"`
   必须为空。

7. **给用户演示必须用和用户一样的启动方式**。不能用带 `--output-cap` /
   `--fg-auto` / `--exit-after` 的命令行验证后就说「没问题」——
   那些参数会改界面状态、输出上限、补帧行为。

### Git / PR

8. **PR 分支必须从 `origin/main` 切**，不能从自己的开发分支切。
   踩过：从 `4d9165b5`（B 站模块提交）切出的 PR 分支带着 28 个无关提交，
   会被作者要求重做。正确做法：

   ```bash
   git checkout -b fix/xxx origin/main
   git cherry-pick <只含目标改动的提交>
   git rev-list --count origin/main..HEAD    # 必须是 1
   ```

9. **PR 的注释和描述一律中英双语**（用户 2026-10-25 明确要求）。

10. **GitHub 推送报证书错误是 CRL 检查失败，不是网络不通**。
    症状：`CRYPT_E_NO_REVOCATION_CHECK`，但 TCP 到 `github.com:443` 是通的。
    修复：`git config --global http.schannelCheckRevoke false`

### QML / UI

11. **统计 QML 文件数不能用 `ls *.qml`**。`Veyra` 下有 `sponsor` 子目录，
    顶层 58 个、含子目录 75 个。用 Python 的 `os.walk`。

12. **`robocopy /MIR` 会删除目标里的多余文件**。双版本切换时
    `qml\Veyra\BilibiliPage.qml`（我独有的页面）切到作者版会消失，
    切回我的版本会回来 —— 这是设计意图。

13. **`robocopy` 退出码不是「成功/失败」**：0=无变化，1=有文件复制，≥8 才是失败。

14. **在 bash 里调 `cmd //c` 跑带引号的命令容易转义出错**。
    写 `.bat` 文件再 `cmd //c xxx.bat`，不要在命令行里拼引号。

---

## 五、部署与双版本切换

### 四处部署（每次改代码后）

```
1. ...portable\veyra_qml_ui.exe       ← build\veyra_qml_ui.exe
2. ...portable\veyra_qml_ui.exe.p5    ← 同一份（必须一致）
3. ...portable\qml\Veyra\*.qml        ← src\qml\Veyra\*
4. ...portable\qml.p5\Veyra\*.qml     ← 同一份（必须一致）
```

`*.orig` 是作者原件，**永远不动**。

### 切换机制

| 脚本 | 切到 | 用户数据 |
|---|---|---|
| `启动-P5修复版.cmd` | 我的版本（`.p5` 系列）| `user-data-2.0.0-p5` |
| `启动-作者完整版.cmd` | 作者原版（`.orig` 系列）| `user-data-2.0.0-author` |

每个脚本做三件事（全用相对路径 `%~dp0`）：

```bat
copy /y "veyra_qml_ui.exe.p5"  "veyra_qml_ui.exe"
copy /y "shaders\YuvToLinearRgb.dxil.p5" "shaders\YuvToLinearRgb.dxil"
robocopy "qml.p5\Veyra" "qml\Veyra" /MIR /NFL /NDL /NJH /NJS /NP
```

### 作者发布新版本时

```
作者新 exe      → veyra_qml_ui.exe.orig
作者新 shader   → shaders\YuvToLinearRgb.dxil.orig
作者新 qml      → qml.orig\Veyra\（先清空再放）
```

然后我的改动重新合并到 `.p5` 系列（合并源码 → 构建 → 部署到 `qml.p5` / `exe.p5`）。

---

## 六、构建命令（可直接复制）

### 日常增量（约 10 秒）

```bat
@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=%PATH%;C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"
cd /d "D:\Program\Veyra\build"
ninja.exe veyra_qml_ui
```

### 完整配置（改 CMakeLists 后或环境坏了）

```bat
@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
chcp 65001 >nul
set "CMAKE=C:\Program Files\CMake\bin\cmake.exe"
set "NINJA=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
"%CMAKE%" -S "D:\Program\Veyra\src" -B "D:\Program\Veyra\build" -G Ninja ^
  -DCMAKE_MAKE_PROGRAM="%NINJA%" ^
  -DCMAKE_BUILD_TYPE=Release ^
  -DCMAKE_PREFIX_PATH="D:\Program Files\Qt\6.8.3\msvc2022_64" ^
  -DVEYRA_FFMPEG_ROOT="D:\Program\Veyra\deps\ffmpeg\ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0" ^
  -DVEYRA_DLSS_SDK_ROOT="D:\Program\Veyra\src\third_party_local\nvidia\DLSS-SDK-310.7" ^
  -DVEYRA_NVOF_SDK_ROOT="D:\Program\Veyra\src\third_party_local\nvidia\Optical_Flow_SDK_5.0.7" ^
  -DVEYRA_BUILD_QML_UI=ON ^
  -DVEYRA_ENABLE_NVOF=ON ^
  -DVEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON
```

**配置成功的判据**：
```python
import os
os.path.exists(r'D:\Program\Veyra\build\CMakeFiles\rules.ninja')   # 必须 True
```

### 工具链位置

| 工具 | 路径 | 版本 |
|---|---|---|
| CMake | `C:\Program Files\CMake\bin\cmake.exe` | 3.31.6（官方版）|
| Ninja | VS 自带 | — |
| MSVC | VS 2022 Community | 19.44.35229.0 |
| Qt | `D:\Program Files\Qt\6.8.3\msvc2022_64` | 6.8.3 |
| FFmpeg | `D:\Program\Veyra\deps\ffmpeg\ffmpeg-n9.0.1-...` | 9.0.1 BtbN |
| DLSS SDK | `...\third_party_local\nvidia\DLSS-SDK-310.7` | 310.7 |
| NVOF SDK | `...\third_party_local\nvidia\Optical_Flow_SDK_5.0.7` | 5.0.7 |

---

## 七、相关文档位置

| 内容 | 位置 |
|---|---|
| 杜比视界工作交接（370 行）| `src\docs\DOVI_WORK_HANDOFF.md` |
| 杜比视界 P5 验证（230 行）| `src\docs\DOVI_P5_VERIFICATION.md` |
| 基础层/增强层合成（250 行）| `src\docs\DOLBY_BL_EL_COMPOSITING.md` |
| DV PR 审查（91 行）| `src\docs\HDR_DOVI_PR_REVIEW_2026-10-04.md` |
| 补帧/FSR/DV 方案与状态 | `src\docs\FRAMEGEN_FSR_DOLBY_*.md` |
| P5 早期报告（610 行）| `D:\DSH\dolby-vision-p5-report.md` |
| B 站模块历史记录（1627 行）| `D:\DSH\.dsh-tmp\keep\veyra-history-README.md` |
| 操作手册（构建/部署/环境）| `D:\Program\Veyra\HANDOFF.md` |

---

## 八、待办与未决事项

| 项 | 状态 |
|---|---|
| PR #71 等作者回复 | 等待中，作者可能要求调整 |
| PR #13 / #56 / #62 等 6 个旧 PR | 都还开着，作者没表态 |
| B 站模块是否提 PR | 需先问作者意向（作者没有这个功能）|
| 自动选倍率是否提 PR | 属个人口味，需作者认可才提 |
| `D:\DSH\dolby-vision-p5-report.md` | 建议移进 `src\docs\` 跟 git 走 |

---

**说明**：这份文件只记我自己的改动。作者原版的 583 个文档（6.5 MB）在 `src\docs\`，
不要合并进来 —— 会造成 git 大冲突，且作者更新后你的合并版永远追不上。
