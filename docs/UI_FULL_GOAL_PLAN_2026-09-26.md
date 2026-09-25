# 前端迁移 + 后端升级 · 目标模式全量计划（2026-09-26）

分支 `codex/ui-qml-migration-20260925`（起点 `1651608`）。授权依据：AGENTS.md 2026-09-25 条（QML 迁移 + 引擎改造，单分支，每步打标签）。
**不在本计划内（需用户另行授权）：push、合并 main、Release、S5 打包。** 无人值守期间一律不做。

权威：设计稿 `prototypes/ui-redesign-2026-09-25/`（17 屏 = `board.js FRAMES`）。主方案 `UI_MIGRATION_MASTER_PLAN_2026-09-25.md`，进度记 `UI_MIGRATION_EXECUTION_2026-09-25.md` 与 `WORKLOG.md`。

## 0. 续接指针（每步完成后更新本表）

| 阶段 | 状态 | 最后标签 | 备注 |
|---|---|---|---|
| G0 工具与基线 | 进行中 | checkpoint/ui-mig-g0.4 | G0.2 设计参考在 `logs\ui-qml-migration-20260925\design-ref\`（shotpage 默认关过渡，`?motion=1` 保留动画）；G0.3 测试开关逐项截图于 `goal\g0.3\`；G0.4 对比图在 `compare\`；下一步 G0.5 |
| G1 基础（缓动/背景/字体/图标/组件动效） | 未开始 | | |
| G2 外壳 | 未开始 | | |
| G3 四主屏 | 未开始 | | |
| B1 小项 + 桥接补齐 | 未开始 | | |
| B2 导出补齐（S2.5） | 未开始 | | |
| B3 独立调色 pass（S2.2） | 未开始 | | |
| B4 可排序执行器（S2.3） | 未开始 | | |
| B5 节点链运行（S2.4） | 未开始 | | |
| G4 其余屏（对话框/导出/设置） | 未开始 | | |
| W 收尾（逐屏对齐/测试/静态界面） | 未开始 | | |

### 待用户决定（遇到难选项记在这里，按默认继续）

| # | 问题 | 采用的默认 |
|---|---|---|
| D1 | 极简模式播放条压边需独立顶层窗口 | 做独立小窗，PresentMon 对比显示/隐藏；有可复现退化则退回“条在画面下方”并记录 |
| D2 | 画幅跟随是全部片源还是仅极简 | 仅极简模式 |
| D3 | 帧率读数 | 只显示“提交 fps”（用户 §6 决定），设计稿的“显示 fps”删除 |
| D4 | Geist 字体 | 若本机可取得 OFL 原件则放 `E:\项目\Veyra\deps\fonts`、运行目录加载、NOTICES 标注；取不到则用 Noto Sans SC / Consolas 并记录偏差 |
| D5 | 播放条毛玻璃 | 原生视频上无法取样模糊，用 rgba(22,22,26,.86) 近似，记录偏差 |
| D6 | 调色“效果”组（纹理/清晰度/去雾） | 设计稿没有 → 不显示 |
| D7 | AMD FSR 补帧 / NVIDIA FSR4 | 保持隐藏 / 已撤回 |
| D8 | 节点画布算法 | 按 `pages-node.js` 手工移植（同一 GPLv3 项目原型，无第三方许可问题） |

## 1. 通用规则（每一步都适用）

每步写成：**做什么 / 改哪些文件 / 怎么验证 / 标签 / 失败怎么办**。

- 标签：前端 `checkpoint/ui-mig-g<阶段>.<步>`，后端 `checkpoint/ui-mig-b<阶段>.<步>`。
- 证据：`E:\项目\Veyra\logs\ui-qml-migration-20260925\goal\<步骤>\`。
- 每步：构建 → 运行 → 截图并**看图** → 与设计稿并排比对 → 提交 → 标签 → 更新本表、执行记录与 WORKLOG。
- 一次只改一个变量；看不到效果就不下结论。未跑的写“未执行”。
- 引擎没有的能力界面写“尚未接入”，不画假控件。
- 所有产物写 `E:\项目\Veyra\`；只对子进程设 TEMP/TMP。
- 单个测试 ≤ 300 秒。

### 引擎改动的回归门槛（B 阶段每步必跑）

| 门槛 | 命令 | 判据 |
|---|---|---|
| 构建 | `scripts/build-ui-migration.ps1` + `scripts/stage-ui-migration.ps1` | exit 0 |
| 哈希 | `scripts/hash-ui-migration.ps1 -Out <证据>\hash` | 17 项与 `hash-baseline.json` 逐帧一致（新增用例单独记录） |
| 性能 | `scripts/perf-ui-migration.ps1 -Out <证据>\perf -Compare E:\项目\Veyra\tests\ui-qml-migration-20260925\perf-baseline.json` | 提交 P95 不劣化 >10%、GPU 就绪 P95 不劣化 >5%，两次复跑确认 |
| 交付 | `scripts/gates/delivery.ps1 -Root . -BuildDirectory E:\项目\Veyra\tests\ui-qml-migration-20260925\app -OutputDirectory <证据>\delivery -FixtureRoot loop\local\fixed_clips` | PASS |
| 单元 | `scripts/run-unit-ui-migration.ps1 -Out <证据>\unit` | 无新增失败（基线 pass=44 fail=33 skip=3） |
| 真实画面 | 新路径抓帧的 mean/std/min/max | 非黑、非常数 |

### 停工与回退

- 哈希不一致：修或回到上个标签；**S2.3 第一步哈希不一致不得进入第二步**。
- 性能/延迟可复现退化：旧路径留在开关后面，新路径默认关。
- 新路径崩溃：放到默认关闭的开关后，记录并继续下一项。
- 运行组件身份不符：停该项，记录实际值。
- 构建修不好：回退到上个标签。
- 难选项：记入“待用户决定”，按默认继续。

## 2. G0 工具与基线

| 步 | 做什么 | 文件 | 验证 |
|---|---|---|---|
| G0.1 | 执行记录对账（S1.4/S1.5/S1.6 标签名、S3.0b 旧行、s4.1–s4.8 无行）；登记 B6（`resetColourParameter` 把“混合 50 / LUT 强度 100”重置为 0） | 执行记录 | 文档 diff |
| G0.2 | 用无头 Edge 按 `shot2.html?f=<id>` 以精确尺寸渲染 17 张设计参考 | `tools/qt_probe/shoot-design.ps1` | 17 张 PNG 尺寸正确，看图 |
| G0.3 | 应用测试开关：`--page --tab --dialog --aspect --dock-pinned --size WxH --reduced-motion --slow-animations N` | `apps/veyra-qml/main.cpp`、桥 | 每个开关截图 |
| G0.4 | `shoot-all.ps1`（17 屏）与 `compose-compare.ps1`（左设计右实现）入库 | `tools/qt_probe/` | 17 张对比图 |
| G0.5 | 动效采样：设计端用 CDP 暂停动画按时间点截图；QML 端 `--slow-animations` + 定时截图，输出帧带与 CSV | `tools/qt_probe/` | 至少 dock、页面切换、开关三组对照 |
| G0.6 | C++ `tst_easing`：Theme 曲线采样对 CSS cubic-bezier / `linear()` 求解，容差 0.01 | `tests/unit/QmlEasingTests.cpp`、CMake | 修前失败（证明能抓到 bug），修后通过 |
| G0.7 | 全部门槛跑一次作为本轮基线 | — | 结果存 `goal\g0.7\` |

## 3. G1 基础

- **G1.1 缓动**：Theme 改成 6 值组 `[c1x,c1y,c2x,c2y,1,1]`；`--spring` / `--spring-soft` 的 `linear()` 停靠点逐段移植为分段三次（每段控制点 1/3、2/3）；`--out` 为单段。20 处用法统一引用。验证：G0.6 通过；动效采样 dock 回弹出现超调。
- **G1.2 背景**：径向渐变 + 0.045 噪声层（着色器或平铺噪声图），消除色带。
- **G1.3 字体**：按 D4。
- **G1.4 图标**：由 `icons.js` 生成 `qml/Veyra/IconData.js`（名称 → path d），新组件 `VIcon`（Shape + PathSvg，16px、描边 1.7、圆头圆角）；替换全部 emoji。NOTICES 已有 Lucide 条目，补充本次来源。
- **G1.5 组件动效**：按 §8 清单逐项（按钮/胶囊/开关/分段/滑块/弹层/勾选/遮罩/对话框/折叠/子组/标签提示）。
- **G1.6 Qt Quick Test**：组件交互冒烟（开关切换、分段索引、滑块取值、折叠高度）。

## 4. G2 外壳

- G2.1 顶部 dock：12px 热区、离开 450ms 收起、固定模式常开、指示器弹簧移动、提示气泡。
- G2.2 页面切换：旧页 `sink` 220ms → 150ms 后新页 `rise`（`[data-in]` 逐项 45ms 延迟）；视频 HWND 只在动画结束移动一次，只 `ResizeBuffers` 一次；切页不重开片源、不提交设置、不增 revision。
- G2.3 视频窗口同步由 16ms 常驻计时器改为几何变化驱动。
- G2.4 拖放打开、快捷键（与 AppShell 对齐：空格、F11/Alt+Enter/Esc、←→10 秒、↑↓音量、V 看原画、B/Z/X/T/Y 字幕、Ctrl+O、Ctrl+E、全屏 Ctrl+L）、`PlaybackPowerGuard`、导出徽标、toast 动效。
- G2.5 全屏：独立顶层控制栏、自动隐藏、Ctrl+L 锁定；PresentMon 对比。

## 5. G3 四主屏

- **首页**：标志呼吸、4 张片源卡（弹簧悬停）、继续上次、最近。
- **极简**：封面（桥接图像提供者）、画幅跟随（D2）、窗口高度弹性、`cineIn` 用子窗口矩形动画、`barIn`、压边播放条（D1）、seek/peek/播放键动效、字幕/音轨菜单、`f-min169`。
- **专业**：头部（片源下拉、格式标签、列表/节点、截图、预设菜单）、vbar、三块仪表（信号 / GPU 分段柱 / 提交 fps + 折线 ≤4Hz）、页签切换动效、单行处理顺序条、SR/NR 叠层/保护/HDR/补帧行、补帧页、完整调色页、声音页、显示页（对比、缩放）。
- **节点**：默认位置与布局存储、连线、落位推开、拖到连线插入、拖远断开、右键菜单搜索、分隔条、统计条（无“显示 fps”）、分段耗时条。B4/B5 落地前运行相关处明确“尚未接入”。

## 6. 后端

- **B1（S2.6 + 桥接）**：字幕叠加窗口移到共享代码并由 QML 宿主（含四档描边）；WASAPI 输出设备与下混；画面比例 适应/原始/填充；屏幕捕获缩略图与选项接线；偏好持久化（播放位置、默认解码、截图目录、外观）；封面图像提供者；按阶段 / 按 NR 实例 GPU 计时；折线环形缓冲；NR 逐层实时更新；VHDR / 补帧细项；完整调色 API（曲线、混色器、黑白、色轮、校准、LUT、组旁路、组还原、撤销/重做、复制/粘贴、按住看原图、色彩预设）并修 B6。每项有单元测试或自检。
- **B2（S2.5 导出）**：自定义分辨率（跟随预设 = SR 目标）、剪辑入出点（首个完整帧起、音频同步裁剪）、顺序队列、CBR/VBR/CQ（NVENC 与 MF）、ETA、按冻结预设导出。验证分辨率、时长、音画同步、码率偏差、取消。
- **B3（S2.2）**：独立调色 pass，RGBA16F，最多 6 实例，各自表与 LUT；链首单调色保持融合路径（哈希一致）；融合 vs 独立在同位置容差内；6 实例同时。
- **B4（S2.3）**：第一步只实现现有两种顺序且哈希一致后删除 `nrBeforeSr` 分支；第二步自由顺序、补帧在输出端、光流每源帧一次；200 条随机合法链、20 条性能；画质“未验证”。
- **B5（S2.4）**：未连接节点不分配资源；列表/节点两套配置；切换重建；50 次切换无泄漏（显存与句柄回基线）；切回列表恢复原设置。

## 7. 收尾

W1 17 屏逐屏对齐（同状态对比图）；W2 测试补齐；W3 静态界面（播放时界面不动、读数 2–4Hz、减少动画全局生效、UI 命令 P95 ≤16ms、切页对提交间隔 ≤100ms）；标签 `checkpoint/ui-mig-s2-done` 与全量回归。S5 等用户验收。

## 8. 动效清单（设计 → QML）

曲线：spring = CSS `linear()` 弹簧；spring-soft 同；out = `cubic-bezier(.2,.8,.2,1)`。减少动画：全部时长 0，无限动画停止。

| ID | 元素 | 触发 | 时长 | 曲线 | 变化 | QML 做法 |
|---|---|---|---|---|---|---|
| M1 | dock | 热区进入/离开 | 550 | spring | y -120% → 8px | Behavior y |
| M2 | dock 把手 | dock 展开 | 200/400 | spring | 透明度，scaleX .4 | Behavior |
| M3 | logo | 悬停/按下 | 200/400 | spring | 背景，按下 .9 | Behavior scale |
| M4 | dock 按钮 | 按下 | 450 | spring | .86 | Behavior scale |
| M5 | 提示气泡 | 悬停 | 150/300 | spring | .85→1 | Behavior |
| M6 | dock 指示器 | 切页 | 550 | spring | x | Behavior x |
| M7 | 页面进入 | 切页 | 600，逐项 45ms | spring-soft | 透明 0，y 16，scale .98 | 按索引延迟 |
| M8 | 页面离开 | 切页 | 220 | out | 透明 0，scale .985 | |
| M9 | 按钮 / 胶囊 | 按下 | 400 | spring | .95 | |
| M10 | 警告点 | 常驻 | 1200 循环 | ease-in-out | 透明 .35 | SequentialAnimation |
| M11 | 开关 | 切换 | 250 / 500 | spring | 底色；圆钮 x 14，按下宽 18 | |
| M12 | 分段 | 选择 | 500 | spring | 指示条 x、宽 | |
| M13 | 滑块圆钮 | 悬停/拖动 | 350 | spring | 1.25 | |
| M14 | 弹层 | 打开 | 150 / 450 | spring | .9→1，y 8→0 | |
| M15 | 选项勾 | 选中 | 150 / 400 | spring | .4→1 | |
| M16 | 遮罩 | 对话框 | 200 | 线性 | 透明 | |
| M17 | 对话框 | 打开 | 550 | spring | .9→1，y 14→0 | |
| M18 | 首页标志 | 常驻 | 4500 循环 | ease-in-out | 阴影 20→30，scale 1.02 | MultiEffect |
| M19 | 片源卡 | 悬停/按下 | 500 | spring | y -4 / .97 | |
| M20 | 极简窗口高度 | 进入极简 | 700 | spring-soft | 高度 | 窗口高度动画，结束时一次 resize |
| M21 | 极简画面 | 进入 | 800 | spring-soft | 上下各 12% 裁切展开 | 子窗口区域 |
| M22 | 播放条 | 进入 | 800，延迟 120 | spring | 透明 0，y 30，.94 | |
| M23 | 播放条按钮 | 按下 | 400 | spring | .86 | |
| M24 | 进度条 | 悬停 | 300 | spring | 高 3→6，拇指 0→1 | |
| M25 | peek | 悬停 | 150/400 | spring | .85 | |
| M26 | 播放键 | 悬停/按下/切换 | 450/400 | spring | 1.06/.88；图标 scale .4 旋转 -40° | |
| M27 | GPU 柱 | 出现 | 800，120+i×60 | spring-soft | 宽 | |
| M28 | 折叠卡 | 展开 | 500 / 200 / 450 延迟 60 | spring-soft / spring | 高 0→内容；内容 y -6 | |
| M29 | 折叠箭头 | 展开 | 450 | spring | 90° | |
| M30 | 折叠卡 flash / new / bye | 定位/新增/删除 | 900 / 600 / 300 | out / spring / out | 光环；.85 y -8；.9 | |
| M31 | 页签重绘 | 切页签 | 460，i×30 | spring-soft 曲线 | y 10 | |
| M32 | 设置重绘 | 切分区 | 480，i×40 | (.22,1.25,.36,1) | y 12 | |
| M33 | 节点诞生/消失/flash | 增删 | 600 / 220 / 900 | spring / out / out | .6 / .7 | |
| M34 | 节点落位 | 松手 | 500–580 | spring | x,y | |
| M35 | 适配视图 / 自动排列 | 按钮 | 620 / 600+660 | spring-soft / spring | 变换 | |
| M36 | 右键菜单 | 打开 | 120 / 400 | spring | .85→1 | |
| M37 | 色板 | 选中 | 400 | spring | 1.12 | |
| M38 | 旋转图标 | 加载中 | 800 循环 | 线性 | 360° | RotationAnimator |
| M39 | 音轨均衡条 | 选中 | 1000 循环，错相 | ease-in-out | 高 25%↔100% | |
| M40 | 耗时条分段 | 数据 | 600 | spring-soft | 宽度比例 | |
| M41 | 预设行删除 | 删除 | 240 | out | .9 透明 | |
| M42 | toast | 提示 | 进出 | spring | y、透明，2600 后消失 | |
