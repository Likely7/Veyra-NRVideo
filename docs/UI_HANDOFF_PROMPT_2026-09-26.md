# 给桌面端 Claude 的提示词（复制下面横线内的全部内容）

---

Veyra DLSS Video Player 的前端迁移，需要你把 Qt/QML 界面**做到和设计稿一致**。我先说明现状，请不要跳过任何一节。

## 你在哪

代码库：`C:\Users\123\Desktop\Veyra DLSS Video Player`
分支：`codex/ui-qml-migration-20260925`
交接存档：`checkpoint/ui-mig-wip-handoff`

**先读这个文件，它写了全部环境、命令、已踩的坑和当前状态：**
`docs/UI_HANDOFF_DESKTOP_2026-09-26.md`

**再读项目的强制规则：** `AGENTS.md`（红线：不许假完成、不许假控件、每步打存档标签、大产物写 `E:\项目\Veyra\`）。

## 目标

用户已批准的设计稿在 `prototypes/ui-redesign-2026-09-25/`，**它是唯一权威**。当前 QML 界面的问题是：**"丑爆，完全不符合设计稿"**。你要把 17 屏逐屏做到和它一致：布局、层级、控件样式、间距、动画、图标。

设计稿的屏清单见 `prototypes/ui-redesign-2026-09-25/board.js` 里的 `FRAMES` 表。用浏览器打开 `index.html` 可以逐屏看。

## 最重要的一件事：你有屏幕，我没有

上一轮是我（终端里的 Claude）做的，**最大的失败是我很长一段时间看不到界面**——截屏抓到的是编辑器或任务栏缩略图。所以我一直在盲改，改了十几轮都没修好一个布局 bug。

**你最大的优势就是能看见屏幕。请用足它**：
- 每改一次就跑截图脚本，**看图**再判断
- 一次只改一个变量，不要一次改三处
- 不要靠读源码推理"应该是什么效果"，**去看实际效果**

截图命令（已修好，能截到不在最前的窗口）：
```powershell
$app='E:\项目\Veyra\tests\ui-qml-migration-20260925\qml-app'
Copy-Item 'E:\项目\Veyra\build\qt-probe-20260925\veyra_qml_ui.exe' $app -Force
Copy-Item 'C:\Users\123\Desktop\Veyra DLSS Video Player\qml\Veyra\*' "$app\qml\Veyra\" -Recurse -Force
& 'C:\Users\123\Desktop\Veyra DLSS Video Player\tools\qt_probe\capture-window.ps1' -Page pro -Clip 'C:\Users\123\Desktop\Veyra DLSS Video Player\loop\local\fixed_clips\test_av_1080p.mp4'
```

构建：
```powershell
& "C:\Users\123\Desktop\Veyra DLSS Video Player\scripts\build-qt-probe.ps1" -Targets veyra_qml_ui
```

## 第一件事：修 `VGroup` 行重叠

导出页和设置页右侧的分组行**堆在同一个位置**。实测数据：子项各自高度正确，但全部 `y=0`，内层 layout 的 `implicitHeight` 是 0 —— 容器没在做布局。`VAccordion` 里同样的行排布正常，所以问题在 `VGroup` 这个容器本身。

我改过 `VGroup`/`VCard`/`VRow` 好几轮都没修好，原因未知。

**最小复现已写好但没跑过**，先跑它，一次定位：
`E:\项目\Veyra\tmp\ui-qml-migration-20260925\min\Test.qml`
（可以用 `E:\项目\Veyra\deps\qt\6.8.3\msvc2022_64\bin\qml.exe` 直接跑）

## 第二件事：逐屏对照设计稿

修好上面的 bug 后，逐屏做。**每一屏都要留下设计稿截图和实现截图的比对记录**。

已知的具体差距（不完整，你要自己对着看）：
- 图标用的是 emoji，设计稿是线性 SVG 图标（`prototypes/ui-redesign-2026-09-25/icons.js` 里有现成路径）
- 专业页：设计稿的"处理顺序"链式条是**单行**，现在换行了；仪表卡缺**迷你折线图**
- 极简模式：设计稿是控制条**横跨画面下沿**。因为原生视频窗口永远盖在 QML 之上，我改成了条在画面下方。**设计稿自己注明要"独立小窗口"才能做压边**——这是个真问题，你可以尝试做独立窗口，但要先确认它不会增加呈现延迟
- 缺屏：`f-min169`（16:9 状态）等
- 色彩页缺**曲线画布、混色器、色轮、校准、LUT**
- 节点模式：缺链路由连线、节点被压住时弹开

## 必须知道的硬约束

**后端有一大块没做**，所以设计稿里很多控件**没有东西可接**：

| 未做的后端 | 挡住什么 |
|---|---|
| S2.2 独立调色 pass | 节点模式里调色不能放链首/链尾 |
| **S2.3 可排序执行器** | **节点模式自由排序——设计稿核心卖点** |
| S2.4 节点链运行 | 依赖 S2.3 |
| S2.5 导出补齐 | 导出队列、剪辑范围、码率控制、自定义分辨率 |
| S2.6 小项 | 音频输出设备、画面比例、缩略图、播放位置记忆 |

**工程纪律（必须遵守）**：引擎没有的能力，界面要**明说"尚未接入"**，**不许画一个改了没反应的假控件**。现有 QML 里到处是这种诚实占位 —— **不要删掉它们去糊一个好看的假界面**。这是用户的硬性要求。

如果你判断某个控件**必须有后端支持**，那就在报告里写清楚"这需要后端 X"，而不是假装它能用。

## 交付要求

1. 每完成一步，打存档标签：`git tag -a checkpoint/ui-mig-<步骤>`
2. 报告要**如实**：没验证的写"未执行"，不许写成"完成"
3. 逐屏比对要有**截图证据**
4. 遇到"做不了"的，写清楚**为什么做不了**（后端缺？Qt 限制？），不要含糊过去

## 请先做

1. 读 `docs/UI_HANDOFF_DESKTOP_2026-09-26.md` 和 `AGENTS.md`
2. 跑一次构建 + 截图，**亲眼看看现在什么样**
3. 打开设计稿，**亲眼看看应该什么样**
4. 修 `VGroup` bug（先跑最小复现）
5. 然后逐屏对照，从首页/极简/专业/节点四主屏开始
