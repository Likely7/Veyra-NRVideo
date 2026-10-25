# Veyra 交接文档 — 新开发环境（D:\Program\Veyra）

**生成时间**：2026-10-25 10:05（首次 09:40，本次补全部署与切换规则）
**状态**：环境已验证 —— 清空 build 后全量重建 236/236 成功，程序启动正常，双版本切换正常

---

## 一、先读这段：环境已迁移，原因在下面

**旧路径不能用了。** 原来在 `D:\项目\Veyra-NV\`，现在全部搬到 `D:\Program\Veyra\`。
旧目录已由用户手动删除，不要再去找它。

### 为什么搬

中文路径 `D:\项目\` 会让 CMake 的 Ninja 生成器**写不出 `CMakeFiles/rules.ninja`**，
导致 `build.ninja` 引用了 989 条未定义的编译规则，构建直接报
`unknown build rule 'CXX_COMPILER__...'`，而且 `cmake --fresh` 之后再也无法恢复。

实测对照（同一份源码、同一个 CMake 3.31.6）：

| 构建目录 | rules.ninja |
|---|---|
| `D:\项目\Veyra-NV\build`（中文路径） | ❌ 缺失 |
| `D:\Program\Veyra\build`（ASCII 路径） | ✅ 9309 行 / 989 条 rule |

**结论：这个项目必须在纯 ASCII 路径下构建。** 新路径已验证：清空 `build` 后全量重建 236/236。

---

## 二、目录结构

```
D:\Program\Veyra\                                    ← 开发环境
├── src\       约 1.3 GB  3186 文件   ← 源码 + .git（分支 custom，HEAD 926023e5）
├── deps\      159.1 MB    225 文件   ← FFmpeg 9.0.1（BtbN 版）
├── build\     约 320 MB              ← Ninja 构建目录（15 个 .lib + 4 个 .exe）
└── HANDOFF.md

D:\Program\Veyra-2.1.0-NVIDIA-win64-portable\        ← 唯一部署目录（用户实际运行）
├── 启动-P5修复版.cmd          ← 用户的 P5 修复版
├── 启动-作者完整版.cmd        ← 作者原版
├── veyra_qml_ui.exe           ← 当前生效（两个 cmd 会覆盖它）
├── veyra_qml_ui.exe.p5        ← 用户的版本（切换源）
├── veyra_qml_ui.exe.orig      ← 作者原件（切换源，永不直接运行）
├── shaders\YuvToLinearRgb.dxil.p5 / .orig
├── qml\Veyra\                 ← 当前生效的 QML（两个 cmd 会镜像它）
├── qml.p5\Veyra\              ← 用户的 QML
├── qml.orig\Veyra\            ← 作者的 QML
├── runtime_local\             ← 用户数据，见第五节
├── logs\veyra-qml.log
└── 全部 Qt DLL / FFmpeg DLL / 模型 / shaders
```

**旧目录 `D:\项目\Veyra-NV\` 已由用户手动删除**（2026-10-25 之后）。
中文路径会导致 CMake 写不出 `rules.ninja`，见第一节。

**`D:\Program\Veyra\package\` 已删除**（那是迁移早期复制的旧部署包副本，已进回收站）。

---

## 三、工具链确切位置

| 工具 | 路径 | 版本 |
|---|---|---|
| CMake | `C:\Program Files\CMake\bin\cmake.exe` | 3.31.6（官方版，非 VS 定制版）|
| Ninja | `C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe` | 随 VS |
| MSVC | VS 2022 Community，`vcvars64.bat` | 19.44.35229.0 |
| Windows SDK | 10.0.26100.0 | |
| Qt | `D:\Program Files\Qt\6.8.3\msvc2022_64` | 6.8.3 |
| FFmpeg | `D:\Program\Veyra\deps\ffmpeg\ffmpeg-n9.0.1-11-ge47273f4d9-win64-lgpl-shared-9.0` | 9.0.1 BtbN |
| DLSS SDK | `D:\Program\Veyra\src\third_party_local\nvidia\DLSS-SDK-310.7` | 310.7 |
| NVOF SDK | `D:\Program\Veyra\src\third_party_local\nvidia\Optical_Flow_SDK_5.0.7` | 5.0.7 |

**注意**：`D:\Program Files\Qt` 里带空格但**能用**（不是中文，不影响）。
项目自带的 `scripts/build.ps1` 假设 FFmpeg 在 `C:\veyra-deps\`，与我们实际位置不同，
**不要用那个脚本**，用下面第四节的命令。

---

## 四、构建命令（已验证可用）

### 完整配置（只在首次或改 CMakeLists 后需要）

写一个**纯 ASCII 的 .bat**（重要：不要用 `write` 工具写含中文的 bat，
cmd 按 GBK 读取会乱码；要么全 ASCII，要么用 Python 以 GBK 编码写）：

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

**`VEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON` 必须显式给**：不开的话 `veyra_ngx` 目标不生成，
而 `veyra_engine` 无条件链接它，会链不上。这是踩过的坑。

### 日常增量构建

```bat
@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
cd /d "D:\Program\Veyra\build"
"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe" veyra_qml_ui
```

实测（2026-10-25）：
- **全量**（清空 build 后）：236 个目标，**1 分 10 秒**
- **增量**（改一个 .cpp）：约 **9~10 秒**
- **无改动**：`[1/1] Copying QML`，1 秒内

**注意**：`ninja.exe` 不在默认 PATH，要用完整路径，或在 vcvars 之后
`set "PATH=%PATH%;C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja"`。

### 测试

```bat
ninja.exe veyra_realtime_preview_tests veyra_effect_chain_tests veyra_preset_library_tests
```
然后逐个运行 `build\veyra_*_tests.exe`，期望 `exit=0`。
**跑 preset_library_tests 前先删 `%TEMP%\veyra-preset-library-tests`**，否则会因残留数据假失败。

---

## 四之二、配置时最容易出错的地方

### 三个必须显式给的路径

`--fresh` 或新建目录后，这几个 CACHE 变量是空的，必须用 `-D` 补：

| 变量 | 值 | 漏了会怎样 |
|---|---|---|
| `VEYRA_FFMPEG_ROOT` | `D:\Program\Veyra\deps\ffmpeg\ffmpeg-n9.0.1-...` | `veyra_media` 等目标不生成，构建缺一大半 |
| `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR` | `ON` | `veyra_ngx` 不生成，`veyra_engine` 链接失败 |
| `VEYRA_DLSS_SDK_ROOT` | `...\third_party_local\nvidia\DLSS-SDK-310.7` | 上一个为 ON 时会 FATAL_ERROR |

### 配置成功的判据

```python
import os
os.path.exists(r'D:\Program\Veyra\build\CMakeFiles\rules.ninja')   # 必须 True
```
另外看 `CMakeCache.txt` 里 `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR:BOOL=ON`。

---

## 五、部署流程（验收前必须做）

部署目录是 **`D:\Program\Veyra-2.1.0-NVIDIA-win64-portable\`**（唯一的部署目录）。

用户规矩：**说「请打开看看」之前必须四处都部署**，否则界面和描述不一致。

### 四处部署（每次改代码后）

```
1. ...portable\veyra_qml_ui.exe       ← 构建产物 build\veyra_qml_ui.exe
2. ...portable\veyra_qml_ui.exe.p5    ← 同一份（必须一致！）
3. ...portable\qml\Veyra\*.qml        ← 源码 src\qml\Veyra\*
4. ...portable\qml.p5\Veyra\*.qml     ← 同上
```

- `*.orig` 是作者原件，**永远不动**
- 部署后必须报告：生效 exe 的 MD5、`.p5` 与生效一致、`.orig` 未变
- 启动验证：退出码 0，日志无 `Unexpected token` / `no root object` / `binding error`

### 双版本一键切换（用户的核心用法）

用户在部署目录里双击 `.cmd` 切换版本：

| 脚本 | 切到 | 用户数据目录 |
|---|---|---|
| `启动-P5修复版.cmd` | **用户的 P5 修复版**（`.p5` 系列）| `runtime_local\user-data-2.0.0-p5` |
| `启动-作者完整版.cmd` | **作者原版**（`.orig` 系列）| `runtime_local\user-data-2.0.0-author` |

每个脚本做三件事（全部用相对路径 `%~dp0`，换目录不用改）：

```bat
copy /y "veyra_qml_ui.exe.p5"  "veyra_qml_ui.exe"                 rem 1/3 exe
copy /y "shaders\YuvToLinearRgb.dxil.p5" "shaders\YuvToLinearRgb.dxil"   rem 2/3 shader
robocopy "qml.p5\Veyra" "qml\Veyra" /MIR /NFL /NDL /NJH /NJS /NP  rem 3/3 QML 镜像
start "" "veyra_qml_ui.exe" --data-dir "%~dp0runtime_local\user-data-2.0.0-p5"
```

**`/MIR` 是镜像**：切到作者版时，`qml\Veyra\BilibiliPage.qml`（用户独有的页面）会被移走；
切回 P5 版时从 `qml.p5` 复制回来。这是正确行为 —— 作者版本来就没有 B 站模块。

实测结果（2026-10-25 验证通过）：

| 动作 | exe | qml |
|---|---|---|
| 作者完整版 | `1c111e8724f529fc2bafc0d791f672b4` | 74 个，无 `BilibiliPage.qml` |
| P5 修复版 | 见当时构建产物 | 75 个，有 `BilibiliPage.qml` |

### 用户数据目录说明

| 目录 | 用途 | 状态 |
|---|---|---|
| `user-data-2.0.0-p5` | P5 版的数据（脚本启动时用）| **保留用户原数据** |
| `user-data-2.0.0-author` | 作者版的数据（脚本启动时用）| **保留用户原数据** |
| `user-data-2.0.0` | 不用脚本、直接双击 exe 时的默认目录 | 2026-10-25 已重置为作者初始 |
| `user-data-2.0.0-p5.BAK` | 2026-10-17 的历史备份 | 未动 |
| `user-data-ab-test` | 2026-10-17 的 A/B 测试残留 | 未动 |

### 作者发布新版本时的更新规则

**保持原来的规则不变**：作者的产物放 `.orig` 系列，用户的放 `.p5` 系列。

```
作者新 exe      → veyra_qml_ui.exe.orig
作者新 shader   → shaders\YuvToLinearRgb.dxil.orig
作者新 qml      → qml.orig\Veyra\（先清空再放）
```

然后用户的改动要重新合并到 `.p5` 系列（合并源码 → 构建 → 部署到 `qml.p5` / `exe.p5`）。

**注意**：切换脚本会覆盖 `veyra_qml_ui.exe`、`shaders\YuvToLinearRgb.dxil`、`qml\Veyra\`
这三个「当前生效」位置，这些位置不要手工维护，交给脚本。

---

## 六、我的开发记录在哪

**所有「我做了什么」的详细内容在 **（342 行，已提交进 git）：

| 想看什么 | 去 MY_WORK.md 的哪一节 |
|---|---|
| 当前 git 状态与统计 | 一、当前状态速查 |
| **13 个 PR 的完整列表**（3 已合并 / 1 已关闭 / 9 待处理）| 二、PR 一览 |
| **按日期的时间线**（杜比视界、B 站模块、A/B/C 修复等）| 三、时间线 |
| **14 条踩过的坑** | 四、踩过的坑 |
| 待办与未决事项 | 八、待办与未决 |

本文件（HANDOFF.md）只保留**怎么干活**：环境、构建、部署、验证。
「做过什么」全部去 MY_WORK.md，不在这里重复。

---

## 七、验证方法

### 代码状态校验

**旧的基线工具已失效**：`D:\DSH\.dsh-tmp\veyra-baseline\restore.py` 记录的是
已删除的旧路径 `D:\项目\Veyra-NV\`，不要再依赖它。

用 git 校验（简单可靠）：

```bash
cd D:\Program\Veyra\src
git status --porcelain     # 应为空
git log --oneline -1       # 应为 926023e5
```

### 环境健康检查（怀疑环境坏了时先跑这个）

```python
# 用 Python 判断，不要用 bat —— bat 里路径拼接容易写错（踩过坑）
import os
print(os.path.exists(r'D:\Program\Veyra\build\CMakeFiles\rules.ninja'))  # 必须 True
```
`rules.ninja` 缺失 = 环境坏了，重新配置即可（见第四节）。

### 功能验证要点

| 场景 | 期望 |
|---|---|
| 启动 | exit=0，日志出现 `[app] Veyra QML session started`，无 QML 错误 |
| 从 B 站列表点播放 | 界面不卡，跳到**专业模式** |
| 播慢 CDN 片源 | 画面能走，缓冲短暂停顿后恢复，**恢复后画面立即回来** |
| 播 60fps B 站视频 | 日志 `[fg-auto] pre-open: ... chosen=2`，只有一次 `graph initialized` |
| 双版本切换 | 见第五节表格的 MD5 与 qml 数量 |

### 日志

`D:\Program\Veyra-2.1.0-NVIDIA-win64-portable\logs\veyra-qml.log`

排查「卡」时先分清是哪一种（看日志里哪个线程没动静、哪个计数不涨）：

| 现象 | 看哪里 |
|---|---|
| 界面冻住 | 主线程日志的时间空档 |
| 一直缓冲 | `net-buffer` 的 `leadSec` / `targetSec` / `downloadMBps` vs `bitrateMBps` |
| 音画错位 | `audio-continuity` 的 `clockMs` vs `coverageMs` |

---

## 八、踩过的坑

**完整 14 条坑在 `src\MY_WORK.md` 第四节**，那里每条都有现象、原因和修法。

这里只列**每天都会遇到的 4 条**，其余去 MY_WORK.md 查：

1. **中文路径不能用于构建**。CMake 的 Ninja 生成器写不出 `rules.ninja`，
   构建报 `unknown build rule`。所有构建目录、临时脚本必须在纯 ASCII 路径。

2. **必须显式给 `VEYRA_ENABLE_EXPERIMENTAL_DLSSNR=ON`**。
   漏了 `veyra_ngx` 不生成，而 `veyra_engine` 无条件链接它，链不上。

3. **切 git 分支后编译要先清缓存**。否则 .obj/.lib 混用，出现
   `LNK4098`（CRT 冲突）或 `LNK2019`（找不到符号）。
   清 `CMakeFiles`、`*.lib`、`*.exe` 再全量重建（1 分 10 秒）。

4. **测试环境变量绝不能残留**。用 `env -u VAR ./app.exe` 包单条命令，
   演示前自查 `python -c "import os;print([k for k in os.environ if k.startswith('VEYRA_TEST')])"`
   必须为空。踩过：残留的限速变量让本地视频被当成网络源，首帧要 12 秒。

---
## 九、新会话第一步

```bash
# 1. 确认代码状态
cd D:\Program\Veyra\src
git log --oneline -3
git status --porcelain          # 应为空

# 2. 确认没有残留的测试环境变量（必须为空）
python -c "import os;print([k for k in os.environ if k.startswith('VEYRA_TEST')])"

# 3. 确认构建环境健康（关键：rules.ninja 必须存在）
python -c "import os; print(os.path.exists(r'D:\Program\Veyra\build\CMakeFiles\rules.ninja'))"

# 4. 增量构建（约 10 秒）
cd D:\Program\Veyra\build
ninja.exe veyra_qml_ui

# 5. 部署四处（见第五节）
# 6. 确认程序能跑（用干净环境）
cd D:\Program\Veyra-2.1.0-NVIDIA-win64-portable
env -u VEYRA_TEST_FORCE_DEMUX_SESSION -u VEYRA_TEST_SLOW_IO ./veyra_qml_ui.exe --exit-after 8000
# 日志在 logs\veyra-qml.log
```

**工作区约定**：本会话工作目录是 `D:\Program\Veyra`，
临时脚本一律放 `D:\Program\Veyra\.dsh-tmp\`。

**验证纪律**（用户 2026-10-25 明确要求）：测试要严谨，不能用被污染的环境去验证或演示。
详见第八节第 16、17 条。

---

## 十、用户偏好（必须遵守）

1. **所有思考和回复用简体中文**，代码注释也尽量中文。
2. **临时文件放当前工作区的 `.dsh-tmp\` 下**（本会话是 `D:\Program\Veyra\.dsh-tmp\`），
   不要散落到盘符根目录或用户目录。
3. **删除一律进回收站**（`Microsoft.VisualBasic.FileIO.FileSystem`），
   删除前先打印精确清单（绝对路径 + 大小）报给用户确认。
4. **验收前不许 push**，不许 `gh pr create` / `gh pr merge`。
   本地 commit、本地分支允许。
5. **说「请打开看看」之前必须四处部署**（exe / exe.p5 / qml / qml.p5），并报告哈希。
6. **`*.orig` 永远不动。**
7. **改一处 → 重新验证 → 再改下一处**，不要叠加改动。
8. **用户是小白但判断很准**，他说「感觉不对」往往真有问题，先查再解释。
9. **测试要严谨，不能被污染**（2026-10-25 用户明确要求）。
   测试用的环境变量不能残留；演示前必须确认环境干净；
   **给用户看的验证必须用和用户一样的启动方式**，不能用带测试参数的命令行冒充。
   详见第八节第 16、17 条。

---

## 十一、给下一位的提醒

1. **先跑一次增量构建确认环境**，再动代码。环境坏了先修环境，别在坏环境上改代码。
2. **不要相信「应该没问题」**。所有结论要有实测证据（日志行、哈希、退出码）。
3. **改 C++ 前想清楚**。构建崩了用户看到的是程序闪退。
4. **用户报「卡」时先分清是哪种卡**：
   - GUI 线程被网络阻塞 → 看主线程日志时间空档
   - CDN 慢于码率 → 看 `net-buffer` 的 `leadSec` / `targetSec` / `downloadMBps`
   - 音画时钟错位 → 看 `audio-continuity` 的 `clockMs` vs `coverageMs`
5. **上一轮最贵的教训**：排查 `rules.ninja` 时因脚本判断写错，在错误方向上耗了很久。
   **怀疑某个文件没生成时，先用 Python 的 `os.path.exists` 确认，不要用 bat 判断。**

---

**当前状态**：全量构建 140/140 通过，程序启动正常，工作区干净，
PR #71 已提交给作者等待回复。我的开发记录见 `src\MY_WORK.md`。
