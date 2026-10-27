# AVerMedia Multichannel Audio 插件 → Veyra 5.1 采集可行性调研

日期：2026-09-17
调研对象：`C:/Users/123/Downloads/AVerMediaMultichannelAudio-1.0.2-windows-x64-Installer.exe`
目标硬件：AVerMedia GC553G2（USB VID `07CA` / PID `2553`，见第 2 节证据）
本次性质：**只调研，不改代码**。未构建、未运行产品、未连接采集卡、未做任何实机验收。

---

## 0. 结论

**技术上可行，但不是"把 OBS 插件搬进 Veyra"。真正缺的只有一个环节：把采集卡切到"非 PCM 直传"模式。**

拆开看是三步：

| 步骤 | 谁在做 | Veyra 现状 |
|---|---|---|
| 1. 命令采集卡把 HDMI 音频以非 PCM（压缩位流）方式从 UAC 端点送出 | AVerMedia 预编译 `avt_device_opener.dll` → Realtek `RTICE_SDK_x64.dll` / `RTK_IO_x64.dll`（**无源码**） | **完全没有** |
| 2. 从音频端点抓原始字节 | `avt_dshow_core.dll`（DirectShow：设备 → 回调滤镜 → Null Renderer） | **已有等价实现**（`CaptureCardSource` 的 DirectShow 音频图 + `BitstreamAudioSink`） |
| 3. 把位流解成 6 声道 PCM | 插件内 FFmpeg `spdif` demuxer → AC-3/E-AC-3 解码 → `obs_source_output_audio` | **已有等价实现**（`BitstreamDecoder`，本地已用真实编码夹具逐声道验证过） |

所以这一次的工程量不是"重写 5.1 链路"，而是 90% 卡在"第 1 步缺源码"这一件事上。

现实建议：**先做第 8 节的阶段 0 实验（不改代码）**，它能一次性回答两个决定性问题：切换后端点长什么样、切换状态是否会保持。这两个答案直接决定后面能不能只写几十行嗅探代码就收工。

---

## 1. 证据与身份（可复核）

### 1.1 安装包

| 项 | 值 |
|---|---|
| 大小 | 4,040,208 字节 |
| SHA-256 | `7D28EA1749D576F9F1DAD154CDF8D6B57E3E5CF61B088090A93A3BF87B97D252` |
| 打包器 | Inno Setup（`Setup data version 6.1.0 (unicode)`） |
| 版本 | 1.0.2.0，AVerMedia TECHNOLOGIES, Inc. |

### 1.2 包内文件（解包后逐个哈希）

| 文件 | 大小 | 版本/来源 | SHA-256 |
|---|---|---|---|
| `obs-plugins/64bit/AVerMediaMultichannelAudio.dll` | 44,544 | 1.0.2，插件本体 | `4B939A78F0106E0864D94FC603A59382EEF95EBF9AFB6F71592913F8AA0B02F5` |
| `obs-plugins/64bit/AVerMediaMultichannelAudio.pdb` | 1,191,936 | Jenkins 构建符号 | `AD8943A7C17057A9DE40940470674B70A2D4E0FB6451BC64DB29AEB452F4CE1E` |
| `obs-plugins/64bit/avt_device_opener.dll` | 64,512 | 1.0.2，设备切换+厂商 SDK 包装 | `100A3D2E465292BA68A999B464D6F5C0F7237C1B6A503BBBB2A92B6FFC3882F1` |
| `obs-plugins/64bit/avt_dshow_core.dll` | 33,280 | 1.0.1，DirectShow 抓取 | `02F08B3307BE0029A8B60EACEC2A56D787E3C082A75570C51D1648FDC5E4AA54` |
| `data/obs-plugins/AVerMediaMultichannelAudio/avmcallback.ax` | 53,760 | 1.0.0.16（2015），DirectShow 回调滤镜 | `E59265C73391BDBA2ADD6496EBFE3BACCD819DEDE096FA6F4ADFF0D0AE07B1B8` |
| `.../RTICE_SDK_x64.dll` | 184,832 | Realtek，无版本资源 | `22A4275407E10087C061CD7FA0D88C81BDAB1F89468BDF18C8DE5700C4A8B19E` |
| `.../RTK_IO_x64.dll` | 5,621,248 | Realtek，无版本资源 | `8EBB912BD710E8697BD72D2C6C0BC740A39B0F87FB1CB5EF3B3E867994633D17` |
| `.../msvcr110.dll` | 849,360 | VC++ 2012 CRT | `AE996EDB9B050677C4F82D56092EFDC75F0ADDC97A14E2C46753E2DB3F6BD732` |
| `tmp/LICENSE`（Inno 临时） | 18,383 | **GPLv2 全文** | `892D0A23E7C96C84FC1445ED792CBA6687413861B6D16D6A9A2E0480EBB52092` |

解包目录（临时，不是仓库）：`%TEMP%\averma-mca-extract-01\`。
包内不含任何 `.inf` / `.sys` / 服务注册 —— **没有内核驱动**，纯用户态组件。
安装脚本（仓库里的 `.iss.in` 模板）把文件装进 OBS 目录并写 `HKLM\SOFTWARE\AVerMedia\OBS_Plugins\AVerMediaMultichannelAudio`（`Path`/`FilePath`/`Version`/`Language`）；模板里**没有** `regsvr32` 条目，所以 `avmcallback.ax` 到底靠什么注册（或是否根本不需要注册）本轮**未确认**——不影响 Veyra，因为 Veyra 用自己的 sink，不需要这个滤镜。

### 1.3 上游开源仓库

| 项 | 值 |
|---|---|
| 仓库 | `github.com/AVerMedia-Technologies-Inc/obs-MultichannelAudio`（约 100MB 以内，默认分支 `main`） |
| GitHub 识别许可证 | `GPL-2.0` |
| 仓库内 `buildspec.json` 版本 | 1.0.5（安装器是 1.0.2） |
| 最后推送 | 2025-02-03 |
| 仓库内 `avt_device_opener/bin/avt_device_opener.dll` | 65,024 字节，`7ECCF391AC25E932BD36961FB278EA69F8501B2E557C3381CD290AF374F37E6E`（**与安装器不是同一构建**） |
| 仓库内 `avt_dshow_core/bin/avt_dshow_core.dll` | 33,792 字节，`D8E8616185AAB69E1AA71F5D1D1E3EF45E5B2FAC897AD08F7295955995196539`（同上） |
| 源码范围 | Windows/Mac 的 OBS source、FFmpeg 解码线程、`avt_*` 的**头文件**；`avt_*` 的实现只有预编译 DLL/.lib，**没有 .cpp** |
| 安装器独有、仓库没有 | `RTICE_SDK_x64.dll`、`RTK_IO_x64.dll`、`avmcallback.ax`、`msvcr110.dll` |

---

## 2. 插件实际做了什么（逐步可验证）

### 2.1 它是 OBS 音频源

`AVerMediaMultichannelAudio.dll` 导入 `obs.dll` 的 `obs_register_source_s` / `obs_source_output_audio` / `obs_properties_*` / `blogva` 等 38 个符号，导出 `obs_module_load` 等 8 个。源码里注册的 source id 是 `avt_audio_dshow_source`，显示名取自 `AVerMedia.DolbyAudio.DisplayName`（`data/.../locale/en-US.ini` 里是 `AVerMedia Multichannel Audio`）。

→ 这个 DLL 只能在 OBS 进程里跑，**不能直接塞进 Veyra**。

### 2.2 真正有价值的两个模块

`avt_device_opener.dll`（C++ 类导出，MSVC 名字修饰）字符串里能直接读到它解析的厂商 SDK 函数名：

```
RTK_IO_x64 | RTICE_SDK_x64
initialize | uninitialize | setDevice | setPort | closePort
AT_Audio_Get_HdmiRX_AudioFormat
AT_Audio_CSR_Get_Send_Non_Pcm_Data_On_off
AT_Audio_Send_Non_Pcm_Data
AT_USB_Get_SerialNum
DeviceOpener::EnableNonPcmDataByVendorSdk, force setNonPcmOnOff(1)
```

它通过 `Qt6Core.dll` 的 `QLibrary` 动态加载 `RTICE_SDK_x64.dll` / `RTK_IO_x64.dll`，用 SetupAPI 按 `vid_`/`&pid_`/`-Audio` 找设备，然后对这些函数 `resolve` + 调用。

`avt_dshow_core.dll` 字符串里带 `vid_07ca` 与 `gpid_2553`（GC553G2 的 VID/PID），以及：

```
AudioWdmCaptureDevice::Initialize / BuildCaptureFilter / BuildCallbackFilter / BuildNullRendererFilter / ConnectGraph
AVerMedia Callback Filter | Null Renderer | Capture Filter
```

`AvtDShowAudioSampleStruct.h` 里回调结构只有三项：`dwChannels / dwBitsPerSample / dwSamplingRate`。

### 2.3 数据链

`AVerMediaMultichannelAudio.dll` 导入 `avcodec-61 / avformat-61 / avutil-59`（FFmpeg 7.x）与 `av_find_input_format`，并且字符串里有 `spdif`、`av_probe_input_buffer2`、`av_read_frame`、`avcodec_receive_frame`、`obs_source_output_audio %lu %d %d`。

它的激活顺序（源码 `src/Win/AVerMediaAudioDShowInput.cpp`）：

```
Deactivate() → deviceOpener.StopChecking()
Activate()   → deviceOpener.SwitchDeviceThenDetectAudioFormat(设备名/路径)
             → AudioDevice::ResetGraph / UpdateDevice / ConnectFilters / SetCallback / Start
             → deviceOpener.StartChecking()          // 监控线程，源音频格式变化时重切
OnAudioData(buf) → if (deviceOpener.IsAudioFormatNonPcm())
                       FfmpegAudioDecode::OnEncodedAudioData(buf)   // spdif 解包 → AC-3/E-AC-3 解码
                   else
                       (#if 0 死代码，什么都不输出)
```

结论（高置信度，源码直接可读）：

1. 卡被切成"非 PCM 直传"后，USB 音频端点送出来的是 **IEC 61937 封装的压缩位流**，不是 6 声道 PCM。
2. 插件靠 **软件解码** 把 AC-3/E-AC-3 还原成 5.1 PCM，再交给 OBS。
3. `StartChecking()` 的存在说明：HDMI 源在 PCM ↔ Dolby 之间切换时，需要**周期性重切**，不是一次性设置。

### 2.4 一个必须实测的不确定点（中等置信度）

插件在**每一个回调**里都要调用 `IsAudioFormatNonPcm()`（读芯片的 HDMI RX 音频格式）才知道手上这堆字节是不是压缩流。如果 DirectShow 媒体类型本身能区分，这个调用就是多余的。

**推论：切换后 UAC 端点的媒体类型很可能仍是 2ch/48k/16bit PCM，只是载荷变成了位流。**

这条如果成立，直接决定了 Veyra 的实现方式（见 4.2）。它是本次调研里最关键的待验证假设，第 8 节阶段 0 就是为了打掉它。

---

## 3. 硬边界（来自 AVerMedia 官方 FAQ，2026-09 抓取）

1. 支持的型号只有 **GC553G2 / GC553PRO / GC575**（仓库 README 明写）。
2. GC553G2 需要 **固件 V1.0.7.7 或更高**；GC575 需要 `GC575FwUpdateTool_1.1.6.12`。
3. **源必须输出 Dolby 位流**。官方原文：LPCM 只支持 2.0，"support multichannel 5.1 (e.g., PS5, Xbox series X) while NOT support 5.1 LPCM"。→ **PS5 必须设成 Dolby Digital / Dolby Atmos，设成 Linear PCM 拿到的还是 2.0。**
4. 官方建议在 Streaming Center 里先看到 "Multichannel detected"，再把 Audio 设成 5.1。
5. 主机菜单里通常不出 5.1，要进游戏（官方原文：Multichannel 5.1 audio is typically not output when staying in the console menu）。
6. 拿到的是 **解码后的 5.1 PCM**，Atmos 对象（JOC）在这一层就丢了。想保留对象只能走接收端直通，那是另一条链路（Veyra 的 `BitstreamAudioSink` 已具备，但和内部增强链路互斥）。
7. 全链路仅 Windows。

---

## 4. Veyra 现状对照

### 4.1 已经有的（可直接复用，不用重写）

| 能力 | 位置 | 状态 |
|---|---|---|
| DirectShow 音频抓取 + 三态选择（自动 / 强制 PCM / 位流优先） | `src/source/CaptureCardSource.cpp` | 已实现，本机三种模式实测跑过（无位流类型的卡） |
| 位流分类（AC-3 / E-AC-3 / TrueHD / DTS / DTS-HD） | `include/veyra/sink/BitstreamAudio.h` | 已实现 |
| IEC 61937 解包 + FFmpeg 解码到 float PCM + 真实声道数/采样率 | `src/sink/BitstreamAudio.cpp` | 已实现；用真实编码夹具验证过 6 声道与逐声道幅度 |
| 6 声道采集协商、side/back 掩码、10ms 协商 | `NativeCaptureSink` / `CaptureAudioSession` | 已实现（声道路由 1–8ch 校验） |
| 输出端不支持时矩阵降混、设备热插拔、端点丢失恢复、PCM 停滞重连 | `CaptureAudioSession` / `AudioInputRecovery` | 已实现并有回归 |
| 共享处理图（不是为采集单独开一条音频链路） | `FrameSource → EnhanceGraph → FrameSink` | 已实现 |

### 4.2 缺的三个（真实的工程缺口）

**缺 1：切卡的代码，一行都没有。**
现有 UI 帮助文本明确写着"设备本身的音频格式不受影响"——Veyra 今天从不改变采集卡的音频模式。

**缺 2：2ch PCM 标签流上的位流识别。**
当前位流分支只在"设备**枚举出压缩媒体类型**"时才走（`classifyBitstreamSubtype(subtype.Data1)`：`0x92` / `0x2000` / `0x0A` / `0x10A` / `0x0C` / `0x08` / `0x0B` …）。如果 2.4 的推论成立，端点只会给出 `subtype=0x00000001`（PCM），那么：

- `自动`模式会连 PCM，把 AC-3 位流当 PCM 播 → **是爆音/杂音，不是静音**；
- `位流优先`模式同样失败（它只挑压缩子类型，不会去嗅探 PCM 载荷）。

→ 需要一个"在 PCM 流里嗅探 IEC 61937"的入口。这个嗅探成本很低，因为 IEC 61937 是自同步的：sync `F8 72 4E 1F` + 2 字节 data-type，FFmpeg `spdif.h` 的定义可直接照抄：

| data-type | 含义 | Veyra 对应 decoder |
|---|---|---|
| `0x01` | AC-3 | `BitstreamKind::Ac3` |
| `0x15` | E-AC-3（含 DD+ Atmos 载体） | `BitstreamKind::Eac3` |
| `0x0B/0x0C/0x0D` | DTS type I/II/III | `BitstreamKind::Dts` |
| `0x11` | DTS-HD | `BitstreamKind::DtsHd` |
| `0x16` | TrueHD | `BitstreamKind::TrueHd` |

（现在的 `unwrapIec61937()` 只在缓冲区**开头**对 sync，不会去搜索第一个 sync；接入时需要加"找到第一个 sync 再对齐"这一步。这是实现细节，不是障碍。）

**缺 3：按 HDMI 源音频格式变化重新配置。**
AVerMedia 用 `StartChecking()` 干这件事。Veyra 有 `AudioInputRecovery`，但那是"数据不来了就重连"，不是"源从 PCM 切到 Dolby 就换解码路径"。真机上 PS5 在菜单/游戏之间切音频格式是常态，这一条不补，体验就是"进游戏才有声，回菜单就爆音"。

**已有的但从未在真卡上验证**：`BitstreamDecoder` 的真实采集卡协商与长时稳定性。`docs/WORKLOG.md` 写得很清楚："本机参考采集卡没有位流类型（device bitstream types=0），因此该分支不会被触发"，"真实采集卡的位流协商与长时稳定性：未验证"。**本轮不能把本地夹具通过当成实机通过。**

---

## 5. 四条路线

### 路线 A：进程内加载 AVerMedia 预编译组件

在 Veyra 进程里按绝对路径 `LoadLibraryExW` 加载用户的 `avt_device_opener.dll`，按它的 C++ 导出类调用 `VendorSdk` + `DeviceOpener`。

- 需要：`avt_device_opener.dll`（导出 `AVerMedia::VendorSdk` / `AVerMedia::DeviceOpener`）、`Qt6Core.dll`（它硬依赖，用于 `QLibrary/QDir/QString`）、以及同目录的 `RTICE_SDK_x64.dll` / `RTK_IO_x64.dll`。
- ABI 风险：导出的是 C++ 类，含 `std::string` / `std::wstring` / `std::function` 参数。必须与它同 ABI（MSVC，同 `_ITERATOR_DEBUG_LEVEL`，同 STL 版本）。Veyra 是 C++20/MSVC，**理论可行但要实测**；这是典型的"能跑就一直是它能跑"的脆接口。
- 额外依赖：`Qt6Core.dll` 是 LGPLv3 组件，动态加载合规、但要带许可证说明；且它今天是在 OBS 目录里被找到的。
- 工作量：小（一个 `VendorAudioSwitch` 模块 + 加载/降级逻辑）。

### 路线 B：只用 Realtek SDK，自写薄包装

跳过 `avt_device_opener.dll`，直接 `LoadLibraryExW` `RTICE_SDK_x64.dll`（它只依赖 `RTK_IO_x64.dll` + KERNEL32，**没有 Qt 依赖**），调用：

```
AT_Audio_Get_HdmiRX_AudioFormat | AT_Audio_CSR_Get_Send_Non_Pcm_Data_On_off | AT_Audio_Send_Non_Pcm_Data | AT_USB_Get_SerialNum
+ 设备定位：setDevice / setPort / initialize / closePort / uninitialize
```

- 好处：甩掉 Qt 依赖，依赖面最小。
- 代价：这些是**未公开的 C 导出，签名未知**（`avt_device_opener.dll` 里调用点可反汇编推参数，但没有文档）。需要逆向 + 黑盒验证。
- 工作量：中。风险：Realtek SDK 内部会枚举 Media Foundation 设备并走 USB 控制通道；版本差异可能导致失败或需要额外初始化序列。

### 路线 C：完全自实现设备切换（逆向 USB 协议）

不引入任何第三方二进制，自己复现"让卡送非 PCM 数据"的 USB 命令。

- 好处：零第三方依赖，最干净；符合 AGENTS.md"有开源就搬、没有就自己实现"的精神。
- 代价：`RTK_IO_x64.dll` 有 5.6MB，协议无公开文档，需要 USB 抓包 + 大量差分实验，而且**必须依赖真机**（本次连卡都没插）。这是一条独立项目级的活儿。
- 结论：**不作为首版路线**，除非 A/B 都被证伪。

### 路线 D：零代码改动（先决实验）

用 AVerMedia 官方软件（Streaming Center 的 Audio 5.1 设置，或那个 OBS 插件）把卡切过去，然后**让 Veyra 直接去抓**，看端点变成了什么。

- 这一步不需要 Veyra 写任何代码，但要给 Veyra 加一条日志/临时嗅探（属于"阶段 1"）。
- 它的价值：一次性回答 2.4 的假设 + 切换状态是否保持（插件退出时只 `closePort/uninitialize`，**没有**显式关掉 non-PCM；`Streaming Center` 是否常驻也没验证）。

---

## 6. 许可证与分发（这条比技术更硬）

| 组件 | 授权现状 | 对 Veyra 的含义 |
|---|---|---|
| 插件源码（OBS source / FFmpeg 解码线程） | 仓库 LICENSE = GPLv2 全文；`src/plugin-main.c` 头部是 "version 2 **or (at your option) any later version**" 的模板；抽查的 `AVerMediaAudioDShowInput.cpp`、`avt-win-audio-dshow-input.cpp`、`FfmpegAudioDecode.cpp` **没有任何许可证头** | GPL-2.0-**only** 与 GPLv3 不兼容，GPL-2.0-**or-later** 才兼容。边界不干净 → 搬运前必须澄清（发 issue 或联系 `RD5@avermedia.com`）。这不是形式主义，是能不能把代码贴进 GPLv3 项目的分界线 |
| `avt_device_opener.dll` / `avt_dshow_core.dll` | 仓库只给头 + 预编译 DLL/.lib，**无源码**；安装器许可页只给 GPLv2 全文 | 属于"再来一份二进制"。即使按 GPL 的边界去理解，也不该由 Veyra 重新分发；只能在用户本机已有安装的前提下按绝对路径加载（同 `runtime_local` 模式） |
| `RTICE_SDK_x64.dll` / `RTK_IO_x64.dll` | 只在安装器里，Realtek 出品，**包内没有任何对应许可证文件** | 再分发风险最高。按项目规则：只能"用户本地已有 + 进程内使用"，要进 Release 必须走 Runtime Pack + manifest + 用户明确授权 |
| `avmcallback.ax` / `msvcr110.dll` | 同上 | Veyra **不需要**它们：`avmcallback.ax` 是 AVerMedia 自己的 DirectShow 回调滤镜，Veyra 已经有自己的 sink |

补一条合规观察（不是我们的事，但影响判断）：**AVerMedia 在一个 GPLv2 仓库里分发无源码的预编译 DLL，本身就是他们的合规缺口。** 我们不应该把自己的实现建立在别人的缺口上——这跟 AGENTS.md 里"不得把 proprietary runtime 提交进版本控制"是同一件事。

---

## 7. 我的判断（含我可能错的地方）

**推荐：先做路线 D 的实验，用它的结果在 A 和 B 之间做选择；C 只在 A/B 都失败时才考虑。**

理由（第一性原理）：Veyra 缺的不是"5.1 能力"，是"让卡交出位流"的权限。这个权限今天唯一可用的实现是别人的二进制。所以真正要评估的是——**我们愿不愿意长期依赖一个没有源码、没有文档、许可证边界不干净的 DLL**。

对抗式审查，我自己最可能错的地方：

1. **2.4 的推论可能错**。如果切到非 PCM 后端点真的会换媒体类型（例如出现 `0x92`/`0x2000`），那缺 2 直接消失，Veyra 现有"位流优先"模式可能直接就能用，成本从"中"掉到"极低"。→ 所以阶段 0 必须真做，不能拿这篇文档当结论。
2. **RTICE SDK 可能不是独立可用的**。如果它需要 AVerMedia 的签名/授权/额外初始化（`rtk_initialize` 之外的东西），路线 B 直接死，只剩 A。
3. **切到 non-PCM 可能会破坏普通立体声**。如果卡在这一模式下不再输出 2.0 PCM，那"开 5.1"和"平时正常听声音"会互相排斥，必须做按源格式动态切换（对应缺 3）。这会碰 Veyra 目前很稳的采集音频路径，风险不是零。
4. **固件门槛**。如果用户的卡 < 1.0.7.7，后面全部免谈，且固件升级只能用户自己做。
5. **别用"我听到环绕声了"当验收**。AC-3 解错时常常是"有声但定位乱"。验收要用逐声道独立脉冲/静音测试（Veyra 已有 6 声道分离度测试夹具的思路：`veyra_multichannel_tests` 里"六个独立声道音调、其他声道泄漏 0"）。

---

## 8. 分阶段验证计划（本次不执行）

### 阶段 0：不改 Veyra，只做外部切换（判定点最重要）

准备：GC553G2 固件 ≥ 1.0.7.7；PS5 设为 **Dolby Digital**（不是 Linear PCM）；进游戏里。

1. 装官方 Streaming Center，按 FAQ 把 Audio 设为 5.1；或装那个 OBS 插件并激活一次 source。
2. 保持/退出该软件，分别记录：切换后 Veyra 连接采集卡时 `logs/veyra-app.log` 里 `[capture-audio] mediaType=N ...` 的枚举结果。
3. 判定：
   - 若出现 `0x92` / `0x2000` / `0x0A` / `0x10A` 等压缩子类型 → **路线 A/B 只需做切换**，下游零改动（最乐观）。
   - 若仍然只有 `0x00000001`（PCM），且 2ch/48k → 走阶段 1，必须在载荷里嗅探 IEC 61937。
   - 若两者都看不到、或卡直接不出声 → 说明切换没生效或需要在场软件常驻，重新评估。
4. 同时记录：拔插 USB 后是否需要重新切、重启后是否保持、切回 Linear PCM 是否自动恢复 2ch。

### 阶段 1：只做"读"，不做"写"（无风险）

在 Veyra 采集音频入口加**只读**诊断：对前 N 个缓冲做 IEC 61937 sync 搜索（`F8 72 4E 1F`），命中就按 data-type 记日志，不改任何选路。这一步能证明数据到底是位流还是 PCM，且不影响现有 PCM 路径。

### 阶段 2：接解码（仍可用官方工具切卡）

给采集音频加第四种模式/自动嗅探：命中 IEC 61937 → 交给 `BitstreamDecoder` → 进入现有 5.1 管线。这一步完全不依赖 AVerMedia 二进制，用他们的工具切卡即可验收。

### 阶段 3：把切换搬进 Veyra（路线 A 或 B，需要用户明确授权）

按 `runtime_local` 模式：绝对路径加载用户本机已安装的 `avt_device_opener.dll`（A）或 `RTICE_SDK_x64.dll`（B），进程内调用，不修改磁盘文件、不重签名、不进源码 Git；失败时只关闭 5.1 功能，基础采集/播放/导出不受影响。加上"按源音频格式变化重切"的监控（对应缺 3）。

### 阶段 4：验收

真卡 + 真实 PS5/Dolby 源，逐声道定位、暂停/seek、长时间连续性、PCM↔位流切换、拔插恢复；未做过的项如实写"未执行"。

---

## 9. 未验证项（不得声称）

- 未连接采集卡、未安装官方软件、未修改固件、未跑任何实机。**本文所有硬件相关结论均来自二进制/源码/官方 FAQ，不是本机实测。**
- 切换后 UAC 端点媒体类型是否变化：**未验证（本文最关键的未知）**。
- Veyra 能否在真卡上跑通位流分支：**未验证**（历史记录明确）。 
- 非 PCM 模式是否影响普通 2.0 采集、是否跨重启保持、是否需要常驻软件：**未验证**。
- `avt_device_opener.dll` 的 C++ 导出能否在 Veyra 进程内正常加载调用：**未验证**。
- RTICE SDK `AT_*` 函数签名：**未验证**（只知道名字）。
- 40 系/其他型号、Xbox、Switch、DTS 源：**未验证**。

---

## 附录 A：本次实际执行的命令与证据

```text
# 身份与哈希
Get-Item / Get-FileHash  AVerMediaMultichannelAudio-1.0.2-windows-x64-Installer.exe
innoextract --list / -d <tmp>        (Inno Setup 6.1.0，9 个文件 + tmp\LICENSE)
# PE 分析
python + pefile：全部 6 个二进制的导入/导出表
ASCII/UTF-16 字符串提取：avt_device_opener.dll / avt_dshow_core.dll / avmcallback.ax
# 上游
api.github.com/repos/AVerMedia-Technologies-Inc/obs-MultichannelAudio (+/git/trees?recursive=1)
raw.githubusercontent.com：README.md / LICENSE / buildspec.json / CMakeLists.txt / plugin-main.c /
  src/Win/avt-win-audio-dshow-input.cpp / src/Win/AVerMediaAudioDShowInput.cpp(+.h) /
  src/FfmpegAudioDecode.cpp(.hpp) / avt_*/include/*.h / cmake 打包脚本
# 本机
Get-PnpDevice（当前无 AVerMedia 设备在线，VID_07CA 无匹配）
rg logs\veyra-app.log：[capture-audio] 枚举结果为 0x00000001 PCM 2ch 44.1/32/22.05/11.025/8k + 48k/96k
# 下游能力核实
src/sink/BitstreamAudio.*、src/source/CaptureCardSource.cpp、apps/veyra/ui/CapturePanel.cpp
C:\veyra-deps\ffmpeg-ps5-slices-source\libavformat\spdif.h（IEC 61937 data-type 表）
```

本文档只新增本文件，未修改任何源码、CMake、配置或运行时文件；未 commit、未 push。

---

## 附录 B：GC553G2 用户实测日志分析（2026-09-16）

**文件**：`C:/Users/123/Desktop/5.1声道.log` ｜ 1,308,352 字节 ｜ 4,254 行
**SHA-256**：`226EBBBFE918187D57B8C5A16D1B971555D4230DBB279F6D9C2E15EE58383E74`
**性质**：这是 **Veyra 自己的日志**（不是 OBS 日志）。用户说明这是"目前软件状态下 GC553G2 用户的尝试"。我只读并分析，没有改代码、没有连线、没有复现。

### B1 这次实际发生了什么

视频侧全程正常：3840×2160@30、P010、BT.2020+PQ（HDR 进 HDR 出），`received=1548 dropped=0`。音频侧按时间顺序试了 10 次连接：

| 时间 | 接的音频 | 结果 |
|---|---|---|
| 13:03:13 | DirectShow 独立音频 index 0 | 2ch/48k/16bit，`device bitstream types=0`；有声音（峰值爬到 0.336） |
| 13:04:50 | 视频设备内置音频 (−2) | **失败**：`audio output pin not found hr=0x80040216`，随后重连循环 7 次（epoch 2→8，约 24 秒） |
| 13:05:15 | WASAPI (−3) | 2ch/48k/float32，有声音（峰值 0.162） |
| 13:12:13 / 13:12:35 / 13:14:11 | WASAPI ×3 | **全程静音**（13:12:35→13:14:09 连续 95 秒峰值恒为 0.00000） |
| 13:16:54 | WASAPI | 13:17:31 才出现声音（峰值 0.165） |
| 13:18:31 | DirectShow 独立音频 index 0，**生效模式=位流优先** | `device bitstream types=0` → 按设计回退 PCM；有声音（0.168） |
| 13:18:56 | 同上，**生效模式=强制 PCM** | `device bitstream types=0`；有声音（0.108） |
| 13:19:23 | DirectShow 独立音频 index 2，**生效模式=位流优先** | `device bitstream types=0` → 回退 PCM；有声音（0.177） |

`0x80040216` 已在 Windows SDK `vfwmsgs.h`（10.0.26100.0）核对：`VFW_E_NOT_FOUND`；这是 Veyra 自己 `findAudioOutputPin()` 在"没有音频输出引脚"时返回的码。

### B2 这份日志证明了什么

1. **这段时间里，GC553G2 对 Veyra 就是一个 2.0 声道的普通 USB 音频设备。** 4 次 DirectShow 连接（index 0 ×3、index 2 ×1）枚举出的都是同一组 15 个 PCM 媒体类型；**没有出现任何压缩（`0x92`/`0x2000`/`0x0A`/… ）类型，也没有出现任何 >2 声道类型**。
2. **「位流优先」模式真的被执行过**（13:18:31、13:19:23 两次连接的生效模式就是它），结果是设备一个位流类型都没提供。所以缺的不是"没试过位流模式"，而是**设备没给它可挑的东西**。
3. **声音是真实存在的**：DirectShow 各段峰值 0.108–0.336，不是静音，也不是持续削顶的爆音；音频链路本身（10ms 协商、重采样、20ms 储备、欠载=0）在这台机器上工作正常。
4. **"视频设备内置音频"对 GC553G2 根本走不通**：它的 DirectShow 视频滤镜没有音频输出引脚。
5. 与正文 4.2 的判断一致：**「缺 1：切卡」是真实阻塞点**，不是"Veyra 缺 5.1 能力"。

### B3 这份日志没有证明什么（关键）

1. **无法确认 `audioIndex=0` / `2` 是哪个物理设备。** Veyra 的 `binding=separate` 只写索引和 tag，**不写设备名**；index 0 与 index 2 的能力表完全相同（可能是同一张卡的两个端点，也可能是别的设备）。→ 这次实验连"采的是不是这张卡的音频"都没有证据闭环。
2. **无法确认 AVerMedia 侧切换当时是否生效。** 日志里没有任何 Streaming Center / OBS 插件存在的痕迹。如果切换没做，这次就是在默认 2.0 模式下测了一遍。
3. **无法区分"没切"和"切了但媒体类型不变"。** 按正文 2.4 的推断，切换可能不改变 DirectShow 媒体类型、只改变载荷。要区分只有一条路：在 Veyra 里对前若干缓冲做 IEC 61937 嗅探（正文第 8 节阶段 1）。今天没有这个能力，所以**这类日志天然无法回答 5.1 的问题**。
4. 无从判断 PS5 当时输出的是 Dolby 还是 Linear PCM（AVerMedia 官方要求必须 Dolby）。

### B4 顺带暴露的两个真实缺陷（与 5.1 无关，但该修）

1. **日志不含音频设备名**，出问题时无法定位用的是哪个端点。建议在 DirectShow / WASAPI 绑定处打印 FriendlyName 或端点 ID（截断）。
2. **"视频设备内置音频"失败后进入无意义的重连循环**：引脚不存在是**静态**事实，但 `capture-audio-reconnect` 把它当"PCM 停滞"处理，13:04:50→13:05:14 重试 7 次（epoch 2→8），每次都 `Stop`+重绑+同样失败，刷了 21 行日志。建议把 `VFW_E_NOT_FOUND` 归类为"该设备不支持"，停止重试并在 UI 上给出明确提示。

### B5 下一步（给这个用户的具体动作）

按顺序做，每步留日志：

1. 在采集面板里确认卡对应的音频设备名（抄下 `[DirectShow] <名字>`），并确认 GC553G2 固件 ≥ V1.0.7.7。
2. PS5 音频输出改成 **Dolby Digital**（不是 Linear PCM），并进入游戏（菜单通常不出 5.1）。
3. 开 Streaming Center，确认 "Multichannel detected"，把 Audio 设为 5.1；**保持它运行**，然后用 Veyra 连接采集卡的独立音频设备（**不要**选"视频设备内置音频"）。
4. 采集音频模式选「位流优先」，连一次。
5. 两种状态各抓一份日志：(a) Streaming Center 开着且已切 5.1；(b) 关掉 Streaming Center 之后（验证状态是否保持）。
6. 拿到日志后只看一处：`device bitstream types=` 是否非 0。若仍为 0 → 说明切换不改媒体类型 → 直接进正文第 8 节阶段 1（加嗅探），否则按阶段 2 接解码。

### B6 对正文结论的影响

- 正文第 0 节结论不变，且被这次实测加强：**当前软件状态下，Veyra 从 GC553G2 拿到的只有 2.0 PCM。**
- 正文 4.2「缺 1」= **已确认为阻塞点**；「缺 2」「缺 3」仍未验证。
- 新增前置条件：**必须先补"设备名日志 + 载荷嗅探"两个诊断**，否则后续实测依旧无法判定。
- 未验证项照旧：**真机 5.1 至今一次都没成功过**。
