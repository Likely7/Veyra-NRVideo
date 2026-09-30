# 历史交接方案：前端迁移 · 桌面端接手（2026-09-26，已废止）

> 本文件只保留历史桌面会话记录，不得按其中固定 staging、旧门槛或旧命令继续操作。最新完整路线图见 [2.0.0 完整升级与恢复总方案](UI_V2_0_0_MASTER_PLAN_2026-09-27.md)，进度见 `UI_MIGRATION_EXECUTION_2026-09-25.md`；先 P0，再优先收口原 R5.3，范围包含完整 UI 和新增功能。当前代码安全锁未解除，本轮未恢复无人值守。

# 前端迁移 · 桌面端接手方案（历史内容）

> 本文件是交接文档。代码库在 `C:\Users\123\Desktop\Veyra DLSS Video Player`，分支 `codex/ui-qml-migration-20260925`，交接存档 `checkpoint/ui-mig-wip-handoff`。

## 0. 目标（用户原话）

> **「前端必须做好，现在的可以说丑爆，完全不符合设计稿」**

用户已批准的设计稿在 **`prototypes/ui-redesign-2026-09-25/`**。它是唯一权威。最终界面要和它**一致**：布局、层级、控件样式、间距、动画。

**不要靠记忆或印象做界面。每一屏都要对着设计稿做，并截图比对。**

## 1. 环境与命令（都已验证可用）

### 构建
```powershell
# 构建 Qt 前端（CMake 独立构建目录，不动产品构建树）
& "C:\Users\123\Desktop\Veyra DLSS Video Player\scripts\build-qt-probe.ps1" -Targets veyra_qml_ui
```

- 构建目录：`E:\项目\Veyra\build\qt-probe-20260925`
- Qt：`E:\项目\Veyra\deps\qt\6.8.3\msvc2022_64`（含 windeployqt）
- 构建同时会把 `qml/` 拷到 exe 旁边

### 运行与截图（**关键工具**）
```powershell
# 把 exe 和 QML 同步到运行目录
$app='E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app'
Copy-Item 'E:\项目\Veyra\build\qt-probe-20260925\veyra_qml_ui.exe' $app -Force
Copy-Item 'C:\Users\123\Desktop\Veyra DLSS Video Player\qml\Veyra\*' "$app\qml\Veyra\" -Recurse -Force

# 截图某一页（用 PrintWindow，即使窗口不在最前也能截到）
& 'C:\Users\123\Desktop\Veyra DLSS Video Player\tools\qt_probe\capture-window.ps1' -Page pro -Clip 'C:\Users\123\Desktop\Veyra DLSS Video Player\loop\local\fixed_clips\test_av_1080p.mp4'
# 输出：E:\项目\Veyra\logs\ui-qml-migration-20260925\qml-shots\qml-<page>.png
# -Page 可选：home / min / pro / node / exp / set
```

**踩过的坑（都写在脚本注释里了，别重复走）**：
- 用 `CopyFromScreen` 截屏会抓到 IDE 或任务栏缩略图，不是应用窗口。**必须用 `PrintWindow`**（脚本已改好）。
- `Get-Process.MainWindowHandle` 会返回 136×40 的任务栏预览。脚本改为**枚举进程所有顶层窗口取最大的**。
- 带空格的路径传给 `Start-Process -ArgumentList` 必须**加引号**，否则被拆开，应用报"文件不存在"。
- `.ps1` 文件**必须有 UTF-8 BOM**，否则中文路径乱码成 `E:\椤圭洰\`。
- PowerShell 里注释是 `#` 不是 `//`，用 `//` 会让 `param()` 块失效。
- `$args` 是 PowerShell 保留变量，不能赋值。

### 看设计稿（浏览器）
```powershell
# 设计稿总览
start "C:\Users\123\Desktop\Veyra DLSS Video Player\prototypes\ui-redesign-2026-09-25\index.html"
```
设计稿有 17 屏，见 `board.js` 里的 `FRAMES` 表（**唯一权威的屏清单**）。顶部工具栏可跳到任意一屏。

## 2. 当前状态（诚实版）

### 能跑的部分（已验证）
- 六个页面都能加载，**零 QML 错误**
- 视频能播放：日志确认 `video window 852x512 parent=true`、`opened 1920x1080`
- 专业页、极简页截图看着**结构对**
- 主窗口承载原生 D3D12 视频窗口 —— 已实测 499/499 次 Present 成功、0.205 ms

### 已知坏掉的部分

**① `VGroup` 行重叠（最要紧，挡住三页）**
导出页、设置页右侧的分组行**堆在同一位置**。实测数据：
```
children=5  innerH=0  innerIH=0  groupH=6
  [0 y=0 h=50] [1 y=0 h=50] [2 y=0 h=50] [3 y=0 h=50] [4 y=0 h=0]
```
子项各自高度正确，但**全部 `y=0`**，且内层 layout 的 `implicitHeight` 是 **0** —— 容器没在做布局。
`VAccordion` 里同样的行**排布正常**，所以差异在 `VGroup` 这个容器。
**我已改过 `VGroup`/`VCard`/`VRow` 好几轮都没修好，原因仍未知。**
最小复现已写好但**没跑过**：`E:\项目\Veyra\tmp\ui-qml-migration-20260925\min\Test.qml`

**② 截图比对几乎没做** —— 只比过专业页和极简页。

**③ 界面还很糙**：图标用 emoji（设计稿是线性 SVG 图标）、缺屏、缺状态。

### 后端缺口（决定界面能做到什么程度）

**这是硬约束，界面做得再漂亮也绕不过**：

| 计划项 | 状态 | 对界面的影响 |
|---|---|---|
| S2.2 独立调色 pass（多实例） | **未做** | 节点模式里调色不能放链首/链尾 |
| S2.3 可排序执行器 | **未做** | **节点模式自由排序、设计稿核心卖点全空** |
| S2.4 节点链运行 | **未做** | 依赖 S2.3 |
| S2.5 导出补齐 | **未做** | 导出的队列、剪辑范围、码率控制、自定义分辨率全没有 |
| S2.6 小项补齐 | 部分 | 音频输出设备、画面比例、缩略图、播放位置记忆没有 |

**工程纪律（项目规则，必须遵守）**：引擎没有的能力，界面要**明说"尚未接入"**，不许画一个改了没反应的假控件。现有 QML 里到处是这种诚实的占位说明 —— **不要删掉它们去糊一个假界面**。

## 3. 工作方式建议（针对"桌面端能看见屏幕"）

1. **先修 `VGroup`**：跑那个最小复现，一次定位。别再猜。
2. **然后逐屏对着设计稿做**：打开设计稿 → 看 QML → 截图 → 比对 → 改。**每一屏都留下比对截图**。
3. **一次只改一个变量**，改完立刻截图。这是我上次最大的错误 —— 一次改三处，结果不知道哪处起作用。
4. **不要在没看到界面的情况下改界面。** 你看得见屏幕，这是你最大的优势，用足。

## 4. 优先级

| 优先级 | 任务 |
|---|---|
| P0 | 修 `VGroup` 行重叠（挡三页） |
| P0 | 逐屏截图比对，先比 `f-home` / `f-min` / `f-pro` / `f-node` 四主屏 |
| P1 | 把 emoji 图标换成设计稿的线性图标（`icons.js` 里有 SVG 路径可抄） |
| P1 | 补齐缺的屏和状态（`f-min169` 等） |
| P1 | 专业页：链式条单行不换行、仪表卡补迷你折线图 |
| P2 | 导出页/设置页布局修好后的精细比对 |
| P2 | 色彩页缺曲线画布、混色器、色轮、校准、LUT（**需要新控件，也可能需要后端**） |
| P3 | 节点模式：链路由、节点弹开、耗时条 |

## 5. 项目红线（务必读 `AGENTS.md`）

- **不许删除用户文件**，不许把 SDK/DLL/模型提交进 Git
- **不许把"没验证"说成"完成"**：没跑过就写"未执行"
- 引擎没有的能力，界面写"尚未接入"，**不做假控件**
- 每步打存档标签：`git tag -a checkpoint/ui-mig-xxx`
- 大产物写到 `E:\项目\Veyra\`，不散落桌面

## 6. 关键文件

| 文件 | 作用 |
|---|---|
| `qml/Veyra/Theme.qml` | 设计令牌，**逐值抄自设计稿 `app.css`**，改样式先看这里 |
| `qml/Veyra/V*.qml` | 15 个组件，与设计稿 CSS 类一一对应 |
| `qml/Veyra/Main.qml` | 外壳：页面切换、顶部 dock、视频窗口矩形计算 |
| `qml/Veyra/*Page.qml` | 六个页面 |
| `qml/Veyra/DialogHost.qml` | 七个对话框 |
| `include/veyra/ui/QmlPlayerBridge.h` + `src/ui/QmlPlayerBridge.cpp` | QML 唯一的后端接口 |
| `apps/veyra-qml/main.cpp` | 入口：创建/挂载原生视频窗口 |
