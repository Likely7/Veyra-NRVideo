# AVerMedia 5.1 采集开关：实现与验证

## 2026-09-18 收尾决定

用户决定圆刚方案到此结束，不再作为待办继续开发。已验证的产品修复已随 1.4.1 合入 main。隔离区剩余诊断工具以 `8819ca8` 单独存档，标签 `checkpoint/avermedia-closed-20260918`；未合入产品，也未新增构建或实卡验证结论。以下保留历史证据，任务结束不等于所有圆刚型号的 5.1 均已验收。

日期：2026-09-17
分支：`codex/avermedia-51-switch-20260917`（独立 worktree `out/worktrees/avermedia-51`，基于 `6119a7d`）
上游调研：[`AVERMEDIA_5P1_CAPTURE_FEASIBILITY_2026-09-17.md`](AVERMEDIA_5P1_CAPTURE_FEASIBILITY_2026-09-17.md)

目标：让 Veyra 也能从 GC553G2 / GC553PRO / GC575 拿到 5.1——也就是把 AVerMedia 的 OBS 插件
真正做的那一步自己做掉，而不是再写一份可行性分析。

---

## 1. 做了什么

### 1.1 新模块：`include/veyra/source/AverMediaAudioSwitch.h` + `src/source/AverMediaAudioSwitch.cpp`

按绝对路径加载用户**已经装好**的厂商组件，调用与 OBS 插件完全相同的入口点：

```
Qt6Core.dll (OBS bin\64bit)  ->  预加载，避免依赖靠 PATH 猜
avt_device_opener.dll        ->  LoadLibraryExW + 13 个修饰名导出
  AVerMedia::VendorSdk(dataDir) -> initialize()
  AVerMedia::DeviceOpener       -> SetVendorSdk() -> SwitchDeviceThenDetectAudioFormat({name,path})
                                -> IsAudioFormatNonPcm() -> StartChecking()
  VendorSdk::getAudioFormat()   -> 唯一可信的成功判据（原因见 1.3）
```

- **不分发任何第三方二进制**：组件只从用户自己的安装目录加载；找不到就静默降级，基础播放/采集/导出不受影响。
- 发现顺序：环境变量 `VEYRA_AVERMEDIA_COMPONENT`（诊断覆盖）→ `HKLM\SOFTWARE\AVerMedia\OBS_Plugins\AVerMediaMultichannelAudio\Path` → `%ProgramFiles%\obs-studio`。
  三种来源都要求同一个目录布局（`obs-plugins\64bit\avt_device_opener.dll` + `data\obs-plugins\AVerMediaMultichannelAudio\RTICE_SDK_x64.dll` + `RTK_IO_x64.dll`）。
- 外来代码可能抛 C++ 异常也可能触发 SEH：调用被拆成"只含 POD 的 `__try/__except` 调用器 + 外层 `catch(...)`"，任何失败都变成一条日志，不允许把采集进程带走。
- `stop()` 幂等；DLL 故意**不** `FreeLibrary`（其监控线程与 Qt 运行时在进程末尾卸载不安全）。

### 1.2 接进采集源：`src/source/CaptureCardSource.cpp`

- 音频图**建立之前**先武装开关（媒体类型枚举必须看到切换后的状态）：`configure()` → `applyVendorAudioSwitch(name,path)` → 再 `connectDirectShowAudio()`。
  只对 DirectShow 独立音频设备、且设备路径含 `vid_07ca` 时才尝试；WASAPI 端点与"视频设备内置音频"不动。
- `binding=separate ...` 日志新增 `device="<友好名>"`：这是上一次复盘里最缺的信息（当时无法确认 index 0/2 到底是哪个端点）。
- 嵌入式音频引脚不存在（`VFW_E_NOT_FOUND`）被判为**静态不可用**：不再每 5 秒重试（上次现场日志里空转了 7 次、20 秒、21 行噪音）。
- `close()` 关闭开关。

### 1.3 为什么用 `getAudioFormat` 当成功判据

`SwitchDeviceThenDetectAudioFormat` 返回 `void` 且内部吞掉错误，只有它自己会打印失败信息。
而 `vendor initialize/setDevice/setPort/AT_Audio_Send_Non_Pcm_Data` 的返回值都在内部被丢弃，
唯一能确认"真的跟卡说上话"的接口是 `AT_Audio_Get_HdmiRX_AudioFormat`：

```cpp
if (!formatOk) { state=Failed; detail="card not reached: getAudioFormat ret=..."; return false; }
```

芯片返回 **20 (0x14)** 表示 HDMI 源正在发非 PCM（Dolby）；其他值表示 PCM。
注意这是**源的当前格式**，不是"有没有武装成功"——用户在菜单里时它一样是 PCM。

### 1.4 只读载体探针：`sink::Iec61937Probe`

**为什么必须要它**：`chipFormat=20` 只说明"源在发 Dolby"，它无法区分下面两种完全不同的现状——

- 切换**没生效**：卡照旧把 Dolby 解成 2.0 PCM 送出来（今天用户听到的正常声音）；
- 切换**生效了**：卡把位流原样塞进一个**仍然自称 PCM** 的媒体类型（厂商插件正是靠"每次回调问一次
  `IsAudioFormatNonPcm()`"来判断的，说明媒体类型本身不带这个信息）。

这两种情况下 `device bitstream types` 都可能是 0，用耳朵也分不出来（后者是噪音，前者是正常游戏声）。
唯一可靠的区别是**字节本身**：IEC 61937 的突发头是自同步的。

所以新增一个小探针，挂在现有 PCM 回调上，**只读、不改路**：

- 扫 `F8 72 4E 1F` → 读 data-type 与 payload 长度 → 要求**至少两个同类突发**才判定；
- 上限 1 MiB，判定即停；跨回调只保留 8 字节尾巴（处理头被切断的情况）；
- 判定一次就写一行日志，之后不再参与任何逻辑。

顺带把 data-type 映射做成可测的纯函数 `classifyIec61937DataType()`（0x01 AC-3 / 0x15 E-AC-3 /
0x0B–0x0D DTS / 0x11 DTS-HD / 0x16 TrueHD），下一步真正接线时直接复用。

**这一步刻意不做的事**：不把位流改道去解码。那条音频路径刚经过 20 ms 储备、10 ms 协商、
欠载/漂移修复，没有真卡的情况下盲改，等于把现场问题换成新问题。先把事实拿到手。

---

## 2. 实测证据（全部来自本机，可复核）

### 2.1 构建

```
out\worktrees\avermedia-51\out\build-avermedia-51.cmd        exit 0
（CMake presets x64-release + Ninja，DLSS SDK / FFmpeg / clip-tools 指向 C:\veyra-deps 与主检出的 third_party_local；
  RemotePlay=OFF 以缩短隔离构建时间——与发布构建的这一处差异必须单独复核）
日志：out\worktrees\avermedia-51\out\build-7.log（213 个目标）
```

### 2.2 导出名表与真实二进制的比对

```
python + pefile 对比 avt_device_opener.dll(1.0.2, SHA256 100A3D2E…82F1) 的导出表：
required exports present: 13 / 13
```

即 1.1 里那张修饰名表没有一个是猜的。

### 2.3 单元测试（无硬件、无 OBS）

`tests/unit/AverMediaAudioSwitchTests.cpp` → `veyra_avermedia_switch_tests.exe`

裸机（本机没有装 OBS 插件）：
```
ok   vid_07ca path is recognised
ok   uppercase VID_07CA is recognised (case-insensitive)
ok   a non-AVerMedia device is rejected
ok   an empty path is rejected
ok   a WASAPI endpoint id is rejected
note component not installed on this machine (expected on a bare test host)
ok   locateComponentRoot returns empty when absent
ok   apply() reports failure when the component is absent
ok   status marks the component as absent / DLLs not loaded / 有原因可记
ok   status state is Unavailable
ok   stop() leaves the helper inactive
ok   destructor after stop() is safe
PASS (0 failures)
```

### 2.4 用**真实组件**跑通加载链（本次最有价值的一条）

把安装包解出的真实文件铺成同一套目录结构（`%TEMP%\averma-fake-component`），并放入一台现成的
`Qt6Core.dll`（6.8.3.0，来自本机其它软件，仅用于本地验证，不进任何交付物），然后
`set VEYRA_AVERMEDIA_COMPONENT=...` 再跑同一个测试：

```
DeviceOpener::SwitchDeviceThenDetectAudioFormat, closePort ret: 0
SetupDiGetDeviceInterfaceDetail : 1 / Not found the device   × N   （本机没有插卡）
serialNumber:
uvcName: USB3 Digital Audio                                        （我们传入的结构化参数被正确读走）
Fail to open COM1                                                  （厂商自己的兜底路径）
ok   located component provides avt_device_opener.dll
note apply() with component present returned 0: card not reached: getAudioFormat ret=5
PASS (0 failures)
```

这条同时证明了四件事：

1. Qt 预加载 + `LoadLibraryExW` 路径策略有效（同一个 DLL 在只加 Qt 之前会以 126 失败，见下）；
2. 13 个导出在**运行时**全部解析成功；
3. 手写的 `DeviceOpenerParam{std::wstring name; std::wstring path;}` ABI 镜像被真实组件正确读取（它打印出了我们传的 `uvcName`）；
4. 没有卡时是**干净失败**：进程不崩、不挂，`getAudioFormat ret=5` 就是"组件在、卡不在"的签名，而且我们的返回值如实报 0（false）。

对照实验（未加 Qt 时）：
```
LoadLibraryExW(avt_device_opener.dll) -> 失败，err=126 (ERROR_MOD_NOT_FOUND)
LoadLibraryExW(RTICE_SDK_x64.dll)     -> 成功（RTK_IO_x64.dll 在同目录，靠 DLL_LOAD_DIR 解析）
```

### 2.5 项目交付短测

```
powershell -File scripts/gates/delivery.ps1 -Root <worktree> -BuildDirectory <worktree>\out\build
DELIVERY SHORT GATE PASS   (exit 0)
logs\delivery\084be258e4834f7d958efa997a0b40c1\result.json
```

### 2.6 载体探针单测

`tests/unit/Iec61937ProbeTests.cpp` → `veyra_iec61937_probe_tests.exe`（21 项全过）：

```
ok   data type 0x01/0x15/0x0B/0x0C/0x0D/0x11/0x16 映射正确
ok   an unknown data type is rejected / data type 0 is rejected
[capture-audio-carrier] IEC 61937 detected on the PCM-labelled carrier: dataType=0x15 (E-AC-3/DD+) bursts=2
ok   two E-AC-3 bursts conclude the probe / are reported as IEC 61937
ok   a partial header alone does not conclude the probe
ok   bursts split across two feeds are still detected          ← 头被切断的情况
ok   unknown data types are never accepted as a bitstream
ok   the probe reaches a verdict within its scan budget
ok   a single burst followed by PCM is rejected                ← 反例：偶然命中 sync 不算证据
ok   64 KB of PCM does not conclude the probe
PASS (0 failures)
```

探针改动后重跑交付短测仍为 PASS：`logs\delivery\427e950eeb6f4db5b0b2273d9be87139\result.json`。

---

## 3. 用户侧怎么验收（这是唯一还没走完的一步）

1. 机器上装好 AVerMedia Multichannel Audio 组件（他已经在 OBS 里用上了，说明组件与固件都满足）。
2. PS5 输出设 **Dolby Digital**，进游戏（菜单一般不出 5.1）。
3. 用 Veyra 连接采集卡的**独立音频设备**（不要选"视频设备内置音频"，那张卡没有内置音频引脚），
   采集音频模式选「自动」或「位流优先」。
4. 看日志里这行 `[capture-audio-vendor]`：
   - `non-PCM switch armed ... chipFormat=20 nonPcmNow=1` → 切换生效，源正在发 Dolby；
   - `chipFormat=<其它>` → 组件在跑但源不是 Dolby；
   - `card not reached: getAudioFormat ret=5` → 没插卡/被别的软件占着；
   - `not installed` → 组件没找到（可用环境变量指路）。
5. 看 `[capture-audio-bitstream] device bitstream types=?`：
   - **非 0** → 卡切换后真的换了媒体类型，现有"位流优先"链路直接吃下，5.1 立刻可用，收工；
   - **仍然是 0** → 看下一行。
6. 看 `[capture-audio-carrier]`：
   - `IEC 61937 detected ... dataType=0x15 (E-AC-3/DD+)` → 切换生效，载荷确实是位流，
     而媒体类型没变。此时声音会是"位流当 PCM 播"的噪音；下一步就是把这条已验证的字节流
     接到现有 `BitstreamDecoder`（代码位置、判定条件都已经明确，改动小且可测）。
   - `no IEC 61937 burst in the first N bytes ... treating it as linear PCM` → 切换没有改变载荷，
     听到的仍然是正常的 2.0，说明卡在这个固件/设置下没被切换过去。

这三行日志就是这次唯一无法在本机回答的问题的答案——本机没有卡，也没有 5.1 源。
无论落在哪一支，都不需要再靠"听起来对不对"来判断。

---

## 4. 未验证 / 已知限制（不得含糊）

- **真卡未验收**：本机没有 GC553G2，`SwitchDeviceThenDetectAudioFormat` 走到"找不到设备"就结束了；
  真正把卡切到 non-PCM、以及切换后端点媒体类型是否变化，都还没有观测。
- **载体探针只给结论，不改路**：它不会把位流送去解码。在"切换生效但媒体类型不变"这一支，
  用户会听到噪音——这是已知且刻意保留的状态，目的是先用一行日志把事实钉死。
- **只验证到组件加载与调用**：`initialize` 后的真实设备交互、`StartChecking` 线程在长时运行下的行为未验证。
- **退出不还原模式**：`stop()` 只 `StopChecking` + `closePort/uninitialize`，不调 `setNonPcmOnOff(0)`
  ——这与厂商插件行为一致（它也不还原）。副作用是：之后若用**没有**装插件的软件采集同一端点，
  可能听到位流当 PCM 的噪音。要不要在关闭时还原，等真机结果再定。
- **没有 UI 开关**：目前是"检测到 AVerMedia 设备 + 组件存在就自动武装"。若将来需要按需关闭，加一个设置项即可。
- **厂商自己的日志走 stdout**：`avt_device_opener.dll` 在没有安装 log handler 时直接打印
  （`Not found the device`、`Fail to open COM1` 等）。没有接 `SetLogHandler`，因为它的参数是
  `std::function`，跨 DLL 边界构造的 ABI 风险高于收益。这些行会出现在控制台，不进 Veyra 日志文件。
- **隔离构建 RemotePlay=OFF**：本分支的验证构建关闭了 RemotePlay；提交前需要一次与发布一致的完整构建。
- **不是所有 AVerMedia 卡都适用**：本模块按 `vid_07ca` 放行，真正支不支持 5.1 由卡与固件决定；
  官方只列 GC553G2 / GC553PRO / GC575（GC553G2 需固件 ≥ V1.0.7.7），且源必须是 Dolby 位流，不支持 5.1 LPCM。

---

## 5. 改动文件

| 文件 | 说明 |
|---|---|
| `include/veyra/source/AverMediaAudioSwitch.h` | 新模块接口 |
| `src/source/AverMediaAudioSwitch.cpp` | 发现 / 加载 / 调用 / 降级 / 停机 |
| `src/source/CaptureCardSource.cpp` | 建图前武装开关；记录音频设备名；嵌入式引脚静态失败不再重试 |
| `include/veyra/source/CaptureCardSource.h` | 新增 `applyVendorAudioSwitch` 声明 |
| `tests/unit/AverMediaAudioSwitchTests.cpp` | 无硬件单元测试（14 项） |
| `CMakeLists.txt` | 新源文件 + 测试目标 |

未提交任何 SDK / DLL / 模型 / 用户配置；`third_party_local`、`runtime_local`、`loop\local` 在隔离 worktree 里
是**目录联接**（只读复用主检出的本地依赖），不参与版本控制。

---

## 附录 C：用户实测日志复盘（2026-09-17）与修复

日志：`veyra-app(8).log`（1,472,697 字节，4104 行，SHA256 待补），隔离测试包第一次真机运行。

### C1 结论：这次**没有测到功能**，是我的识别逻辑把它挡在门外

日志里 6 次 DirectShow 连接全部是同一行：

```
[capture-audio-vendor] skipped: not an AVerMedia capture device (device="")
```

`device=""` 是空的——同时那行 `binding=separate ... device="0040..."` 打出来的是一串十六进制。
解码后是：

```
@device:cm:{33D9A762-90C8-11D0-BD43-00A0C911CE86}\wave:{C25BCE96-8C5E-4894-8ED1-092A0D29FDA5}
```

即 `pathTag(selection.audioPath)`：**这张卡音频设备的 DirectShow moniker 根本没有 `DevicePath` 属性**，
Veyra 保存下来的是类管理器显示名，里面没有 `vid_07ca`。而开关的准入条件恰恰是"路径含 vid_07ca"，
于是 6 次全部直接跳过——**开关一次都没发出去**，`device bitstream types=0` 和
`no IEC 61937 burst` 都只是"没切"的自然结果。

顺带两处自曝的毛病：
1. 那行 `binding=separate` 的格式串只有 3 个占位符却传了 4 个参数，`device=` 打的是路径而不是友好名
   （我上一轮想加的"设备名进日志"实际没生效）；`audioPathTag=0` 也是历史遗留的错位参数。
2. 因为准入失败，日志里连"组件装没装"都没记录。

### C2 这次实测仍然提供了三条有价值的事实

1. **音频设备确实是那张卡**：枚举出 15 个 PCM 媒体类型（含 8 位 / 11.025k / 8k）+ 48k/96k，
   与本机 GC553G2 的指纹完全一致（索引 0 和索引 2 各一个，指纹相同）。
2. **声音是真的**：多个会话峰值 0.17–0.33（11:07 那次到 0.81），说明游戏音频正常送达，
   不是静音、也不是位流当 PCM 的噪音。
3. **用户把三种接法都试了**：DirectShow 独立设备、WASAPI（-3）、以及视频设备内置音频；
   DirectShow 与 WASAPI 都能出声音，行为与预期一致。

### C3 修复

1. **不再依赖 DirectShow 路径识别设备**：新增 `AverMediaAudioSwitch::findUsbFunctions()`，
   用 SetupAPI 按 `KSCATEGORY_AUDIO` / `KSCATEGORY_CAPTURE` 枚举设备接口，挑出 `VID_07CA` 的
   USB 功能，把**真实接口路径**（`\\?\usb#vid_07ca&pid_2553&mi_02#...#{guid}`）喂给厂商组件——
   它本来就只从路径里抠 `vid_`/`&pid_` 两个 token，拿不出这两个 token 就什么都做不了。
   接口类枚举失败时退化为实例 ID（小写化、反斜杠转 `#`），仍然带得住这两个 token。
   所有候选（实例 ID / 友好名 / 接口路径）都进日志。
2. **修好设备名日志**：格式串与参数对齐，`binding=separate` 现在打印真正的 DirectShow 友好名
   （stable 路径与序号两种形式都解析）。
3. **组件状态永远可见**：即使设备识别失败，日志也会写出设备树里找到了什么。

### C4 本机验证

```
veyra_avermedia_switch_tests：PASS（0 失败）
  note vid_07ca functions on this machine: 0        ← 本机没插卡，符合预期
  note vid_ devices on this machine: 2
  note of those: interfacePath=2 friendlyName=2     ← 接口类枚举确实拿到真实路径
  ok   every path handed to the vendor carries the vendor token
  ok   the composite audio function is preferred over the camera functions
delivery 短测：PASS（logs\delivery\ac2283ab2a9d4f9e90738120ad79aada\result.json）
```

### C5 环境坑（记录一次，避免重复踩）

隔离 worktree 最初把 `runtime_local` 做成了指向主检出的**目录联接**。`applicationRoot()` 会从 exe
往上找到第一个含 `runtime_local` 的目录，于是两个会话**共用同一份 UI 设置/预设**：门禁里的
`--smoke-color` 因此出现一次假失败（共享状态漂移），改成"只硬链接运行库、不共享状态文件"后复跑
PASS。隔离工作区可以共享二进制依赖，**不能共享状态**。

---

## 附录 D：第二次真机日志（5.1.log）与修复

### D1 这次进步很大：卡被认出来了

```
[capture-audio-vendor] device tree: instance="USB\VID_07CA&PID_2553&MI_02\9&249B832B&0&0002" friendly="Live Gamer Ultra 2.1-Audio"
[capture-audio-vendor] device tree: instance="USB\VID_07CA&PID_2553&MI_00\9&249B832B&0&0000" friendly="Live Gamer Ultra 2.1-Video"
[capture-audio-vendor] using device-tree audio function instance="USB\VID_07CA&PID_2553&MI_02\..." for selected device="HDMI/Line In (Live Gamer Ultra 2.1-Audio)"
[capture-audio-vendor] non-PCM switch unavailable ... componentFound=1 dllsLoaded=1 detail="card not reached: getAudioFormat ret=5"
```

设备树兜底生效（r2 的修复方向正确），组件也加载了，但 SDK 没能跟卡说上话。

### D2 反汇编给出的两条线索

1. **名字**：`DeviceOpenerParam::getDeviceSwitchParam` 里有 `-Audio` / `-Video` 两个常量——它把
   名字里的 `-Audio` 换成 `-Video` 去找 UVC 设备。我们当时传的是 DirectShow 端点名
   `HDMI/Line In (Live Gamer Ultra 2.1-Audio)`，换完变成
   `HDMI/Line In (Live Gamer Ultra 2.1-Video)`，而真实视频功能叫 `Live Gamer Ultra 2.1-Video`
   —— **对不上**。现在改用设备树里的 `Live Gamer Ultra 2.1-Audio`。
2. **路径**：同类函数找的是 `\\?\` 前缀与 `#{` 分隔符；我们传的真实接口路径
   `\\?\usb#vid_07ca&pid_2553&mi_02#...#{...}\global` 同时具备这两者，**方向是对的**。

### D3 本轮修复

1. **接上厂商自己的日志**（`DeviceOpener::SetLogHandler`，带 SEH/异常兜底）：下次失败日志里会出现
   `vendor: DeviceOpener::OpenDevice, SDK setDevice ret: N` 这类行，直接指出是哪一步被拒。
   本机用真实组件验证过：这些行确实进了 Veyra 日志（`vendor: DeviceOpener::OpenDevice, devicePath: ...`
   `vendor: DeviceOpener::OpenDevice, SDK setDevice ret: 4`）。
2. **补上 `initialize()` 之后的 1 秒等待**：厂商插件在初始化后固定 `Sleep(1000)` 才动手，
   拆除时也是 `closePort → Sleep(1000) → uninitialize`。我方原先初始化完立刻切卡——这是最可疑的
   失败原因，已按厂商节奏对齐。
3. **同一次连接内多试一个功能**：音频功能被拒时，自动改用卡的另一个 USB 功能再试一次（不用多跑一趟）。
4. 名字统一取设备树的 `...-Audio`，并在日志里同时保留 DirectShow 名字以便对照。

### D4 本机验证

```
veyra_avermedia_switch_tests：PASS（真实组件在场时能读到 vendor: 行，无卡时 getAudioFormat ret=5）
veyra_iec61937_probe_tests：PASS
delivery 短测：PASS（logs\delivery\7028a66ae7b24f6581993ac60de9dafc\result.json）
```

仍未验证：真卡上的实际切换结果。r3 包把能自证的都自证了，剩下这一步只能由带卡的机器回答。
