# 2026-09-16 字幕系统重做（多格式 / 内嵌轨 / 双语 / 样式 / 延时 / 自动对齐）

用户指令："字幕你列出来的功能全部都加上吧……先修字幕，其他不动"。本轮只改字幕相关
代码，未触碰 4K HEVC 解码路径、播放/导出链路与其它功能。

## 1. 交付内容

### 1.1 引擎（`include/veyra/engine/Subtitles.h`, `src/engine/Subtitles.cpp`）

- **外挂格式**：SRT（原有）、**ASS/SSA**、**WebVTT**；按扩展名 + 内容嗅探，UTF-8(BOM)/
  UTF-16LE，16MB 上限。
- **ASS/SSA 解析**：`[Script Info]` 的 PlayResX/Y（用于 `\pos` 换算）、`[V4+ Styles]`/
  `[V4 Styles]`（字体、字号、Primary/Outline/Back 颜色 **BGR→ARGB**（ASS 的 00 表示不透明）、
  粗斜体、描边、阴影、对齐、边距）、`[Events]`（时间、样式索引、`\N`/`\n` 换行、
  `{\...}` 覆盖块剥离、`\an` 与 `\pos` 提取）。注释行（`Comment:`）跳过。
- **内嵌字幕轨**（Matroska/MP4…）：`loadEmbeddedSubtitleTracks()` 用 FFmpeg 逐轨解码
  （subrip/ass/ssa/mov_text/webvtt）。2026-10-04 修正旧记录：FFmpeg 的 `AVSubtitleRect.ass`
  是 `ReadOrder,Layer,Style,Name,ML,MR,MV,Effect,Text`，固定八个头部分隔符；旧实现把
  正文逗号继续当头部分隔符，Issue #18 的逗号截断/多行丢失确实存在。现在只剥离
  ASS rectangle 的八字段头部，plain text 不剥头，正文逗号及换行保留。五条真实
  外置/内嵌 SRT/ASS/VTT 轨道的 30 个作者 cue 逐字及时间比对通过，三行 overlay
  栅格/实际播放器字幕通过；证据见 PLAYBACK_SMOOTHNESS_PLAN_2026-10-04.md。
  图形字幕（PGS/DVB）本构建**没有解码器**：轨道照样列出并标注"不支持"，不静默消失。
- **索引查找**：`rebuildIndex()` 建立按开始时间排序的数组，`cuesAt()` 二分 + 回扫，
  取代原来的"每帧线性扫描全部 cue"（长字幕文件不再白烧 CPU）；同一时刻多条 cue
  （ASS 常见）会一起返回。
- **自动对齐（实验）**：`alignSubtitleToAudio()` 解码音轨（8kHz 单声道，最多 30 分钟）→
  20ms 网格 RMS 包络（带跨帧进位，时间轴不漂移）→ 相对阈值（峰值 90 分位的 20%）得到
  "人声活动" → 与字幕活动做 ±30 秒互相关，**全局 F1 分母 + 至少 50% 重叠门限**，避免
  "把字幕推出分析范围、只靠少数匹配拿高分"的假阳性。

### 1.2 渲染（`apps/veyra/ui/SubtitleOverlay.h/.cpp`）

重写为带样式的多行渲染：字体/字号/颜色/描边（宽度/颜色）/阴影/背景条/对齐（numpad 1-9）/
边距/`\pos` 定位；主字幕在下、副字幕在上（双语）；按内容签名缓存，只在内容或尺寸变化时
重绘。修掉了旧实现的一处隐患：函数级 static GDI+ 对象会在 `GdiplusShutdown` 之后析构
（本轮实测触发了 0xC0000005 退出崩溃）。

### 1.3 播放器接线（`apps/veyra/ui/AppShell.cpp` 等）

- 打开文件时：先找同目录同名 `.srt/.ass/.ssa/.vtt` 自动加载，再枚举内嵌轨；
  主字幕优先中文轨，其次第一条可用轨；"副字幕"开关记住后会挑另一条轨。
- 字幕菜单（字幕按钮）分区：开关、载入外挂、重新扫描内嵌、**主字幕轨列表**、
  **副字幕列表**、**延时 -1s/-50ms/归零/+50ms/+1s**、**自动对齐**、
  **字号 ±**、**描边开关**、**背景条开关**、**位置上/下移**、**字体切换**（雅黑/黑体/宋体/等线/Arial/Segoe UI）。
- 快捷键：`B` 字幕开关、`Z`/`X` 延时 ∓50ms（`Shift` 为 ∓1 秒）、`T` 循环主字幕轨、
  `Y` 循环副字幕轨。
- 偏好持久化：`ui-preferences` 升到 v3（描边/背景条/双语开关/边距/字体索引），旧 v1/v2
  文件仍可读。
- 调试/脚本开关（也是本轮验证手段）：`--subtitle-primary N`、`--subtitle-secondary N`、
  `--subtitle-offset-ms N`、`--subtitle-font-size N`、`--subtitle-no-outline`、
  `--subtitle-background`、`--subtitle-auto-align`。

## 2. 验证证据（本机 RTX 5070）

| 场景 | 素材 | 结果 |
| --- | --- | --- |
| MKV 内嵌 SRT 轨 | `out/format-matrix/a16-subs.mkv` | `track codec=subrip cues=3` → 屏幕文本 `测试字幕第一行/第二行/第三行`，exit 0，457 帧 |
| MKV 内嵌 ASS 轨 | 同上 | `codec=ass cues=2`，覆盖块剥离为 `ASS 字幕测试 带样式` |
| 外挂同名 ASS 自动加载 | `out/format-matrix/extauto.mp4` + `.ass` | `外挂 · extauto.ass cues=2`；`\an8` 顶部对齐与多行文本生效 |
| 双语 | `a16`（主 SRT + 副 ASS） | 同一帧输出 `测试字幕第二行 \| 移动定位字幕` |
| 延时快捷键 | `Z,Z,X,B` | `offsetMs=-50 → -100 → 0`，`B` 关闭字幕；状态栏逐条回显 |
| 自动对齐（真值 -3000ms） | `out/format-matrix/align-fixture.mp4` + 人为后移 3 秒的 SRT | `shift=-3000ms score=0.949 ok=true` |

回归：修复合同测试 **180 项 0 失败**（新增 10 项字幕解析/索引），预设往返 PASS，
UI 合同 PASS，delivery 短测 PASS `logs/delivery/48d9555716234d7ca3e1ad254c78c37c/result.json`。

## 3. 未做（如实，含原因）

- **OpenSubtitles 在线搜索/下载**：需要 API key（且 OpenSubtitles 要求注册应用 + 用户级
  key）、联网行为与隐私说明。没有凭据不能实现，也不该硬编码第三方 key——需要你决定是否
  申请 key、用哪家接口（OpenSubtitles / SubDL / Assrt），我拿到 key 再接。
- **libass 级 ASS 特效**：`\move`/`\t`/`\clip`/`\blur`/卡拉OK `\k`、矢量绘图等未实现；
  当前是自绘的"样式类"ASS（字体/颜色/描边/阴影/对齐/边距/`\an`/`\pos`）。完整特效需要引入
  libass（ISC，但要连带 freetype/fribidi/harfbuzz 依赖链并改渲染到它的位图输出）。
- **PGS/DVB 图形字幕**：本机 FFmpeg 未编这两个解码器；要支持需要重建 FFmpeg 并做位图叠加。
- **音轨选择**（上一份清单里的第 8 项）：属于音频管线改造，不在"字幕"范围，本轮按"其他不动"
  未做。
