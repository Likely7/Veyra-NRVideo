# 实时录制（处理后画面）施工方案 — 2026-10-20

## 一、需求

用户要求给软件增加录制功能：

1. 录到**处理后的画面**（即 NR / 超分 / 补帧之后的画面，不是源画面）；
2. 设置页可设置**录制位置、码率**等；
3. 专业模式在**截图按钮旁边**增加录制按钮，带**合适动画**与**3 秒倒计时**；
4. 完成后构建新的**测试版本**交付用户实测。

## 二、设计取舍

### 2.1 录什么：呈现的那一帧

录制挂在实时呈现点上：每次 `presenter.present()` 真正提交了画面，就把同一帧交给编码器。
因此录到的是用户当时看到的那一帧，包含 NR、超分与补帧的结果。

- 真实帧来自增强图的输出纹理（`graph->videoFrameResource(slot)`）；
- 补帧生成的帧来自生成池（`graph->generatedFrameResource(slot)`）；
- **XeSS 自行持有交换链**时，生成帧不在增强图里，录制只取真实帧。这与导出路径的限制一致
  （导出侧同样拒绝 XeSS 补帧纹理），不假装能录到没有的像素。

### 2.2 用哪个编码器：复用产品导出编码器

`LiveRecorder` 直接使用 `sink::openVideoEncoder()`，即 N 卡的 NVENC（D3D12 零拷贝）与
A 卡的 Media Foundation 硬件 MFT，与视频导出同一条路径、同一套能力边界：

- HDR 输出固定使用 HEVC Main10（H.264 不接受 10bit），与导出一致；
- 非 N 卡的 HDR 会被明确拒绝（系统编码器只有 8bit），不会偷偷降级成 SDR。

**没有另写一套编码逻辑**，也没有引入新的运行库。

### 2.3 不卡播放：写文件不在播放线程

编码器在实时线程上工作（它需要增强图的纹理），但**容器写入放在独立线程**：

- 编码器回调把码流包放进**有界队列**（240 包，高水位 160）；
- 写文件线程负责 `avio_open` / `avformat_write_header` / `av_interleaved_write_frame` /
  `av_write_trailer`；
- 队列到高水位时**丢帧并计数**，而不是阻塞播放。磁盘慢只影响录制，不影响播放。

另外给编码器增加了可配置的**排空超时**（`EncoderConfig::drainTimeoutMs`）：导出可以等
10 秒，录制只等 2 秒，避免慢 GPU 把播放卡在编码器后面。

### 2.4 文件与封装

- 容器 MP4，时间基 1/1000000，PTS 取自真实时钟，所以录出来的时长与节奏是对的；
- 文件名 `Veyra-YYYYMMDD-HHMMSS.mp4`，目录在设置里选，默认「视频/Veyra」；
- 颜色元数据按 HDR/SDR 分别写 BT.2020/PQ 或 BT.709。

### 2.5 生命周期

| 事件 | 行为 |
| --- | --- |
| 用户点录制 | 界面走 3 秒倒计时（动画），结束后向引擎发请求 |
| 引擎收到请求 | 在引擎线程打开编码器与文件，开始录制 |
| 播放中 | 每次呈现提交后编码这一帧 |
| 用户停止 / 会话结束 / **设置变更（重建增强图）** | 关闭文件（写尾索引）并汇报路径 |
| 编码器或写文件失败 | 关闭录制，界面给出明确原因 |

**设置变更会结束录制**：增强图重建会换掉所有纹理，录制器注册的 NVENC 资源随之失效，
所以必须先收尾再重建，并明确告诉用户，而不是产出一个头尾不匹配的文件。

### 2.6 本版边界（明确不做）

- **不含音频轨**：只录画面，本版不做音频混流。
- 只出 MP4，不出 MKV。
- 暂停时不继续写入新帧（文件时间轴因此有间隔），本版不做暂停段的重复帧填充。
- 录制不改变播放节奏统计口径（UI 33ms / 250ms 节奏不变）。

## 三、改动清单

| 文件 | 改动 |
| --- | --- |
| `include/veyra/engine/LiveRecorder.h`、`src/engine/LiveRecorder.cpp` | 新增：录制器（编码器 + 封装 + 写文件线程 + 有界队列） |
| `include/veyra/sink/VideoEncoder.h` | `EncoderConfig` 增加 `drainTimeoutMs` |
| `src/sink/NvencD3D12Encoder.cpp`、`src/sink/MfVideoEncoder.cpp` | 使用可配置排空超时 |
| `include/veyra/engine/EngineController.h`、`src/engine/EngineController.cpp` | 录制请求/快照 API、呈现点编码钩子、重建与会话结束时收尾 |
| `include/veyra/ui/QmlPlayerBridge.h`、`src/ui/QmlPlayerBridge.cpp` | 录制属性与方法、偏好项（位置/码率/帧率/编码）、轮询发布状态 |
| `qml/Veyra/ProPage.qml` | 截图按钮旁的录制按钮：红点呼吸动画、3 秒倒计时与数字弹跳 |
| `qml/Veyra/SettingsPage.qml` | 录制设置分区 |
| `i18n/catalog.json` | 30 条新文案，四语齐备 |
| `CMakeLists.txt` | 挂载 `src/engine/LiveRecorder.cpp` |

## 四、验收

1. 构建：`veyra_qml_ui` + QML 冒烟 + UI 契约 + 翻译检查目标通过；
2. `i18n/extract.py --check` 无缺译、无占位符问题；
3. QML 冒烟测试会真实实例化改过的 ProPage，语法或属性错误会直接失败；
4. 打包成与 2.1.0 同一套运行组件的本地测试包（只换 EXE 与 QML，运行组件字节不动），
   冷启动验证通过后交付用户实测；
5. **实卡录制效果、时长、画质与丢帧由用户实测确认**，本轮不宣称通过。
