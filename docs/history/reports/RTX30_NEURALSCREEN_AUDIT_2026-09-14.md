# RTX 30 / NeuralScreen 1.8.2 接入调研

## 状态与结论

用户提供 `C:/Users/123/Desktop/neuralscreen-v1.8.2-full/`，授权研究。仅静态检查文件及固定提交源码；没有启动用户包、LoadLibrary 运行库、安装 hook、覆盖 Veyra DLL 或修改产品代码。没有 RTX 30 实机 Create/Evaluate、输出与性能验收。本轮不发布。

该方案仍调用 D3D12 NGX Feature 18，可以复用 Veyra 的 NR 图接口，但不能把“增加选择项”等同于支持完成。需要单独运行目录、调用方架构兼容层、可恢复生命周期和持卡用户验证。RTX 20 不列入已支持范围。

## 文件身份（实测）

| 字段 | 值 |
| --- | --- |
| 文件 | `native/nvngx_dlssnr.dll` |
| 大小 | 165840496 bytes |
| 文件/产品版本 | 310,8,0,0 |
| SHA256 | `DCC0DC2414AEDEC4A8E084647070383BE068554042587180C20C784D4772D36F` |
| Authenticode | `HashMismatch`；含 NVIDIA 证书信息不等于签名有效 |
| 包内记录 | 与 VERSION.txt 的 SHA256 一致 |

不同于 Veyra 原版 E16BCF15… 和 RTX40/50 社区版 984BEE0F…，不得作为原有白名单文件静默替换或打包。用户给包研究不等于授权新 Release。未复制任何第三方 DLL/SDK/模型入源码。

dumpbin /exports 静态确认现有适配器需要的五项入口均存在：`NVSDK_NGX_D3D12_Init_Ext`、`CreateFeature`、`EvaluateFeature`、`ReleaseFeature`、`Shutdown1`。这只证明入口存在，不证明参数、初始化及图像输出成功。

## 兼容实现

包里未附 native worker C++ 源码；从 VERSION.txt 标记的 GitHub 提交 `8098ccf261bedc16e4b5fe7887c51a07eb41720e` 只读检查 `native/dlss5-feed-host64.cpp`，未下载 SDK 头文件或其他运行时。

- `SetupArchSpoof` 在加载 NR 之前，通过 NVAPI 查询/缓存 GPU 架构，并把进程中的 `NvAPI_GPU_GetArchInfo` 入口改为自己的处理函数。
- 处理函数针对缓存索引 0 的 Turing/Ampere/Ada 改写架构返回值到 `0x1B0`。通过 `NS_ARCH_SPOOF=0` 可关闭启动安装；不是运行时恢复接口。
- 该兼容层只影响本进程内的函数内存，没有写磁盘驱动文件；但可影响同一进程其他 NVAPI 调用者。
- 包另有 `nvngx.dll_ns-forwarder.dll` 解决调用模块身份；Veyra 已有 scoped caller-name shim，不能盲目再叠一套或复制它的 Python/worker 播放流程。
- 声称包含 sm_75/86/89/120 kernels 来自包清单与上游说明，本轮没有独立解析 fatbin，不能作为硬件运行证明。

## 不应照搬的实现

1. **适配器选择**：上游按 NVAPI 索引 0 改写，未知 GPU handle 也回落索引 0。不能假设该卡就是 Veyra 按 DXGI 选择的处理卡；需要精确匹配。
2. **生命周期**：固定源码未见对应解除函数/原始入口字节恢复；单 worker 退出释放地址空间的做法不适合 Veyra 同进程切换运行版本。必须处理在途调用、失败回滚、其他 NR/SR/FG 消费者影响与恢复。
3. **入口改写**：上游直接写 12-byte 跳转，并忽略恢复内存保护的返回值；不能照搬成长期驻留、可反复切换的兼容层。
4. **路径**：上游 `LoadLibraryW("nvapi64.dll")` 不应原样照搬；继续遵守 Veyra 系统路径/受限加载规则。
5. **能力统计**：Create 成功不能表示 NR 实际工作，必须成功 Evaluate、真实输出变化及有效 GPU 时间，失败应回退并明确显示。

## 文档矛盾和性能边界

包 README/VERSION 标记 RTX30/40/50 可用、RTX20 不支持；TECHNICAL 仍说 RTX30 未验证，源码注释又泛称覆盖所有代。不能把任何一段直接当完整验收。

独立用户曾报告 RTX3060 Ti 在 NeuralScreen v1.3 有效果，作者 v1.5.1 记录恢复 30 系兼容；这不是 Veyra 或这个下载包已在 RTX30 测试的证据。Boost 的 45.7→72.6fps 来自作者 RTX5070 Ti 的降低网络分辨率测试，不能用作 3060/3070 性能承诺。没有可用的 RTX30 原生1080p/4K纯 NR耗时表。

## 若后续授权实施

先建可回退分支；保留原版与现有社区版，新增独立的“RTX30兼容·实验”候选及运行目录。先验证本机 Feature18 ABI/颜色/失效回退（本机通过不算RTX30通过），再以持卡用户测试决定支持范围。

兼容层需封装在 NR 适配器边界，精确识别 GPU、可关闭、可恢复，不修改原 DLL、不假装系统/驱动升级。不改变默认运行版本，不承诺旧卡 DLSS FG 同时解锁；NR 和 FG 的硬件能力分别判断。参数/预设/导出与已有 EnhanceGraph 共用，禁止另建播放器循环。

调研时包 LICENSE 对原创程序给出 MIT；NVIDIA DLL 明确不在该许可内。若复用原创代码，保留作者版权与 MIT 文本并记录固定来源。新运行文件的发布身份及范围另行记录。

## 证据与复核方式

- PowerShell `Get-FileHash -Algorithm SHA256`、`Get-AuthenticodeSignature`、`Get-Item ... VersionInfo`。
- VS 2022 MSVC 14.44.35207 `dumpbin.exe /exports`，只读静态分析。
- GitHub API 固定 ref 读取源文件；不执行第三方构建或安装脚本。
- 本文与工作记录 `git diff --check`；未构建，未运行 NVIDIA/RTX30 Create/Evaluate。

来源：[固定源码](https://github.com/perseval-BLR/DLSS5-NeuralScreen/blob/8098ccf261bedc16e4b5fe7887c51a07eb41720e/native/dlss5-feed-host64.cpp)、[1.5.1兼容修复](https://github.com/perseval-BLR/DLSS5-NeuralScreen/releases/tag/v1.5.1)、[3060Ti反馈](https://github.com/perseval-BLR/DLSS5-NeuralScreen/issues/5)。
