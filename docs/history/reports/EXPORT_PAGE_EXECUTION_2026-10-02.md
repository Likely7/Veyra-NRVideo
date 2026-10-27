# Veyra 导出优化执行与验收记录

2026-10-02 用户授权目标模式完成导出优化。分支 `codex/export-page-20261002`，工作区 `E:/项目/Veyra/worktrees/export-page-20261002`，起点 `09392c4d66b267cc75269b068aaffe3189830d37`。产品改动和本机定向验收已完成，没有提交、合并、推送或发布。

## 实现

- 左侧添加支持多选及拖入，添加仅入队。支持点击预览、单项移除、清空、拖动排序。点击开始后串行导出当前批次；后加入的文件留到下一批。等待项可调整顺序或移除，当前执行项不能移除。各文件独立保留剪辑/轨道选择；开始时冻结本批设置。
- 右侧设置可滚动，底部操作固定可达，分辨率改为有边界的下拉框。小窗口切换文件/预览/设置页签；Main 与 ExportPage 共用布局计算，原生视频随页签隐藏。局部深色控件，不改变公共控件。
- MP4/MKV 与 H.264/HEVC 分开选择。默认全部音轨和内嵌字幕，也可逐文件选择或关闭。兼容轨道复制压缩包，保留声道、语言、标题和 default/forced。文字字幕必要时转换并标明样式损失；MKV 保留 ASS 字体附件。“全部”不限轨数；只有手动选择上限 64 条。
- 取消中/收尾状态持续轮询，回收 worker 进程后才发布终态。每次原子预留专属临时文件，取消只清理自己的文件。旧 partial 不阻塞重试；正式文件不覆盖。清理失败保留准确路径。已经安全保存的结果不因迟到的取消变成失败。
- 视频整批全部成功只提示一次；取消、失败、混合结果不播放成功音。图片批次采用同一规则。默认启用 Windows MessageBeep，可关闭并持久保存；实际可闻性受系统音量和声音方案控制。

新增 ExportQueue、QmlExportQueueModel、ExportStreams；复用现有单 worker、GPU 增强链和 D3D12 NVENC，没有另造播放/增强流水线、视频像素回读或修改媒体运行库。

## 隔离与产物

不可变基线 `E:/项目/Veyra/archives/export-page-20261002-start/scope-baseline.json`，SHA256 `d135f9ad55aacddc3ca5cf6f0771991b863306ebb5931d6e00c0a5aac9d4daae`，1109 文件。每轮 `python -B scripts/acceptance/export-page-control.py` PASS。未写其他工作区、旧 guard/baseline，未切换其他 checkout、stash/reset 或终止其他任务进程。其他 Agent 自行产生的变化不算本任务改动。

| 用途 | 绝对路径 |
|---|---|
| 最终可运行版本 | `E:/项目/Veyra/test-packages/export-page-20261002/veyra_qml_ui.exe` |
| 构建 | `E:/项目/Veyra/build/export-page-20261002` |
| 媒体、输出、截图、隔离配置 | `E:/项目/Veyra/tests/export-page-20261002` |
| 构建/测试日志 | `E:/项目/Veyra/logs/export-page-20261002` |
| 子进程 TEMP/TMP | `E:/项目/Veyra/tmp/export-page-20261002` |
| stage 工具临时目录 | `E:/项目/Veyra/tmp/ui-migration-stage/{baseline-app,candidate-ui-001,export-page-20261002}` |
| 增量源码归档 | `E:/项目/Veyra/archives/export-page-20261002-final` |

MSVC 2022、Qt 6.8.3、项目既有 patched FFmpeg（PS5 slice 补丁不变）；系统 FFmpeg 8.1.1 仅生成/检查媒体。SDK/runtime/媒体不进入源码 Git。最终包 AMD/Intel 目录由 staging 链接转为逐文件哈希一致的独立副本，不依赖中间 staging，见 `final-runtime-copy.json`。保留既有 PreviewView 整数转 float 编译警告，不宣称零警告。

最终包可执行文件 SHA256 `4fe74b404f7d577ff212ca20cdf0f8fb0b793904c8c9598325ee3650a3be0310`，与构建相同；52 份 QML 源文件逐一一致，Main 中测试 Loader 已恢复移除。见 `final-audit.json`。中间 staging 删除命令被自动审批以 `blocked by policy` 拒绝，没有执行删除；`baseline-app`、`candidate-ui-001`、`engine-app` 及其日志全部保留，见 `cleanup-deferred.json`。没有通过其他工具绕过拒绝。

构建：`python -B scripts/acceptance/export-page-build.py <唯一日志名> <目标...>`，目标为 veyra_qml_ui、veyra_export_workflow_tests、veyra_export_queue_tests、veyra_export_worker_failure_tests、veyra_qml_quick_tests、veyra_qml_data_tests、veyra_qml_easing_tests。最终相关日志 `build-final-review-001.log`、`build-boundary-fix-001.log`、`build-boundary-test-001.log`，exit 0。

## 验收证据

日志名相对于上述 logs 目录。驱动 `python -B scripts/acceptance/export-page-tests.py <模式> <唯一标识> [素材]`；单次测试子进程上限 290 秒。

| 场景 | 本机结果及日志 |
|---|---|
| 准备/暂停/编码取消，同路径重试，worker 终态比退出早 1200ms | `lifecycle-final-001.log` PASS；foreign partial 保持 24 字节，最终 20 帧完整解码 |
| 保存前取消、清理遇占用、残留不阻塞重试、保存后取消 | `lifecycle-boundary-003.log` PASS；错误 32 和临时路径保留，安全落盘结果保持成功 |
| 20 文件仅添加、稳定 ID、拖排、独立 trim/选轨、冻结批次、清空 | `queue-unit-003.log` PASS |
| 真实 C/A/B 顺序，晚加 D 留下一批，混合失败不报全成功 | `queue-001.log` PASS；4 份 probe 各 30 帧、2 音轨、2 字幕 |
| 实际页面/bridge/model/worker，预览切换不打断导出，逐文件 trim 恢复 | `production-ui-final-001.log` PASS；截图及 profile 在 tests 下同名目录 |
| 提示音、静音、取消、失败、图片成功/冲突/取消 | 同上：视频整批成功一次声音请求、静音成功零次；图片整批成功一次；其余零次；关闭偏好保存 |
| 720×260、1280×720、1366×768、1920×1080，鼠标拖排/菜单边界 | `qml-1-full-001-results.log` 29/29，含本页 8 项；最终 200% `qml-2-final-001-results.log` 8/8 |
| 100/125/150/200% DPI | `qml-1-002`、`qml-1.25-002`、`qml-1.5-002`、`qml-2-002` 的 results 各 8/8；截图复核 |
| MP4/MKV × H.264/HEVC，双音轨、多字幕 | `mp4-001`、`mkv-001`、`mp4-hevc-001`、`mkv-hevc-001` 导出/probe/decode 通过 |
| mov_text→ASS、SRT→mov_text | `mkv-convert-001`、`mp4-convert-001` PASS |
| 0.7–1.7s 剪辑，跨入点文字字幕与出点截断 | `mkv-trim-001`、`mp4-trim-001`：10 帧；字幕 0–0.600s、0.800–1.000s，文字保留 |
| AAC 起始样本及压缩包完整性 | `mkv-002`、`mp4-002`、`track-content-001.json`：284 包内容相同；输入/输出各 144384 解码样本 |
| ASS 和字体附件 | `mkv-font-001`：ASS 包 SHA256 相同；TTF 1045720 字节，SHA256 b3658eadae55e682b5f69eb64c439c1ecc8f196c0bb8d4756d145d13bc86476a 相同 |
| 5.1 FLAC+多 AAC，无音轨，30s 稀疏字幕 | `mkv-six-001`、`mp4-silent-001`、`mkv-sparse-001` probe/视频音频解码通过 |
| 65 条音轨全部保留 | `mkv-sixty-five-001`：65 条，codec/声道/语言匹配，视频和全部音频解码通过 |
| 单独选轨、全部不保留 | `mkv-selected-001`、`mp4-none-001` PASS；选定字幕包内容一致 |
| PGS 整片复制 | `mkv-pgs-004-subtitle-packets.json`：输入/输出各 2 包 SHA256 相同，240 帧视频解码通过 |
| PGS→MP4、PGS 剪辑、不支持压缩 PGS | `mp4-pgs-reject-001`、`mkv-pgs-trim-reject-001`、`mkv-pgs-reject-005` 明确失败，无假成功正式文件 |
| 颜色导出链、数据隔离、QML 曲线 | `color-chain-003.log` 11/11（直接导出/worker 解码像素相同）；`qml-data-003.log` PASS；`qml-easing-003.log` failures=0 |

真实视频导出使用 RTX 5070、NVENC D3D12。NR/SR/FG 算法未修改，没有由无增强/颜色链测试推断全部增强、AMD/Intel 实机或 HDR 通过。未人工听音。Qt contentItem 截图不包含独立 HWND 视频像素；真实预览和区域（636×532 at 282,56）有生产日志，不把截图黑色区域当成画面验证。整体 QML 套件保留旧 ProPage 测试的 veyra stub 警告，本页独立/真实测试没有对应 QML 错误。

## 失败记录和修复

1. MKV AAC 原始负时间 priming 包被丢弃，同时保留 CodecDelay，头部被重复扣除。完整导出保留 priming，剪辑清除原始 initial_padding；随后 payload/解码样本一致。
2. 官方 PGS 小样本使用了本机 patched 库不支持的轨道压缩。解复用只打印 Unsupported encoding type，自动 pgs_frame_merge 丢包但返回成功。早期 mkv-pgs-001/002/003 仅轨道头通过，**字幕内容失败，不算验收证据**。增加 PGS 包结构检查明确拒绝；以系统 FFmpeg 无损重封装为普通 PGS 后，产品两包内容相同。没有替换媒体库。
3. 故障注入发现通用 Cancelled 文案覆盖了临时文件删除失败路径；去掉覆盖后 lifecycle-boundary-003 通过。
4. 首次 QML 断言错误要求可滚动内容全部装进 viewport，且误以为截图 save 返回布尔值。改为边界/可达性与 PNG 检查。Windows 原生样式拒绝自定义选择器，本页使用 Basic 与深色滚动条/勾选框。
5. 边界测试起初错误等待进程回收前 100%（现有上限 99.9%），改以正式文件存在且进程仍活着观测。颜色链初测缺数据目录确认变量，补 VEYRA_TEST_COLOR_CHAIN_DATA_ROOT=本任务 engine-app/runtime_local 后通过。失败日志保留，没有覆盖。

PGS 来源 [FFmpeg samples](https://samples.ffmpeg.org/sub/PGS/supsample.mkv)，23644 字节，SHA256 e6c8f93f57d0371603704d7e7b16933e6c4c5df669da42b42a2a84de881e0f27；本机 Arial 字体和样本只用于本地回归，不进入交付包。

## 明确限制

- **PGS/VobSub 等图片字幕只支持整片保留，带剪辑时明确拒绝该轨道组合。** 可以整片导出或排除此字幕轨道，没有伪装精确裁剪支持。
- 旧 MKV 中媒体库不支持的轨道压缩需要先无损重封装或排除该轨；不能静默丢字幕。字体附件只保留到输出，不安装。
- 音轨直接复制以包为边界，不承诺任意采样点精确剪辑；文字字幕显式裁剪入/出点。
- 节点链离线导出沿用现有不支持提示，本次不扩展其执行能力。
- 这是独立本地测试版本，源码留在本分支工作树待审查整合，没有新 Release。

## 2026-10-02 Claude 验收与修正

用户要求复核 Codex 本轮导出改动并合并 main。复核结论：取消回收、专属临时文件、队列冻结、MKV/多轨/字幕路径设计正确，原有验收全部重跑通过。发现并修正一处默认行为问题：

- **“保留全部”遇到当前封装装不下的轨道时整项失败。** 队列默认音轨/字幕均为“全部”、封装默认 MP4；常见 MKV 片源带 PGS 图片字幕（或剪辑时带任何图片字幕）会在预检阶段直接失败，比旧版（只带一条音轨、不带字幕）更容易导不出来。现改为：“全部”只表示“当前封装能装下的全部”，装不下的轨道跳过，在队列项说明和完成消息中列出（“部分轨道当前封装装不下，未保留”），音轨全被跳过时额外提示“导出的视频将没有声音”；用户手动选中的轨道装不下仍明确报错。从“全部”切换到手动选择时不再把被跳过的轨道带进手动选择。
- 页面轨道说明同步更新。

重跑（日志在 `E:/项目/Veyra/logs/export-page-20261002/*-claude1*`）：mp4、mkv、mkv-trim、lifecycle、lifecycle-boundary、queue、regression、ui、qml(1x 8/8) 全部 PASS；新增 `mp4-pgs-skip`、`mkv-pgs-trim-skip`（全部策略：成功导出、PGS 被跳过、结果消息含“未保留”）及改为手动选择的 `mp4-pgs-reject`、`mkv-pgs-trim-reject`（明确失败、无正式文件）PASS。
