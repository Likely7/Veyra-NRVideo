# 30 系（RTX 30）补帧决策（D 工作流，2026-09-16）

> 2026-09-16 复查更新：用户复核了 `sdli1995/dlssg_for_sm86` 的许可证显示，要求重新核查。
> 核查结果（`git ls-files` + 仓库目录实测，不是 GitHub 侧栏）：该仓库**没有任何 `.c/.cpp/.h`
> 源码文件，也没有 LICENSE 文件**；`README` 自述 GPLv3、GitHub 由此推断出 GPL-3.0 标签，
> 但仓库本体只有 18 个 DLL（`version.dll`、`dxgi.dll` 等命名）+ ini + 文档。它是**伪装成
> 系统 DLL 的代理加载器**：导出表只有 `GetFileVersionInfoW` 一类转发函数加一个
> `DlssgProxy_Name` 标记，运行时把 DLSSG 调用重定向到 FSR3 补帧。没有源码就没有可搬运对象，
> 随包分发二进制则等于分发无许可证文本的第三方 DLL——两条都不成立。

## 结论

**不移植 `sdli1995/dlssg_for_sm86`，也不随包分发它的 DLL。** 理由不是"做不到"，是两条硬约束：

1. 该仓库**没有源码、没有 LICENSE 文件**（README 自述 GPLv3 但没有随附许可证文本）。
   项目规则对无许可证材料的原话是"不得复制代码"；二进制同样不作为可搬运对象。
2. 它的形态是**替换式代理**，本质是把 DLSSG 调用转成 FSR3 帧生成。也就是说：它提供的能力
   我们自己**已经有了一个更干净的等价物**——见下。

## dashdogy/RTX40MFG-Unlock 的 30 系实验路径（2026-09-16 评估）

dashdogy v1.3.3 源码中含有 30 系实验后端（`ampere_backend.cpp`、`ampere_gpu.cpp`、
`ampere_cuda_program.cpp`、`ampere_native_cache.cpp` 等，MIT）。但 `BUILD.md` 明确写明：
它需要**单独准备的 "validated SM86 kernel cache"**（`native-cache/`、`native-cache-3109/`、
`all-provider-layouts/` 三套 fatbin 缓存 + manifest），并原话标注 *"Kernel caches are
separately prepared build inputs and are not included in this source tree. A source checkout
alone cannot reproduce the DLL without them."* README 同时标注 30 系支持 *"very early and
experimental"*、可能完全不工作。这些 kernel cache 是构建输入的二进制 blob，不在公开源码里，
我们既不伪造也不向第三方索取未公开材料——所以**维持搁置**：30 系不接原生 DLSS MFG 路径。

同类里许可证明确的是 `Nukem9/dlssg-to-fsr3`（GPL-3.0）。如果将来要做"代理式"方案，
应当以它为上游（GPL-3.0 与 Veyra 兼容），而不是用无源码的 sm86 包。

## 2026-09-16 二次评估：sm_86 指令集可行性实测（不依赖 30 系硬件）

用户追问 30 系后补做了实验，结论先行：**30 系跑 DLSS-G 的障碍不是硬件能力，是供应商
没有为 sm_86 发布任何目标**。证据（可复现，脚本 `scripts/diagnostics/sm86-ptx-feasibility.py`）：

1. 结构事实：审计版 `nvngx_dlssg.dll` 310.7 含 69 个 fatbin；sm_89 PTX 69 个
   （解压后共 1,250,185 字节）+ sm_120 PTX 31 个 + sm_89 cubin 31 个。
   **sm_86 目标：0 个**（既无 PTX 也无 cubin）。
2. 兼容性实验：把全部 69 个 sm_89 PTX 的 `.target sm_89` 改写为 `.target sm_86`，
   逐个交给 NVIDIA 驱动 JIT（本机 RTX 5070 / 驱动 32.0.16.1656，JIT 在强制 sm_86
   特性集下编译）：**69/69 通过，0 失败**。说明这批 kernel 没有使用任何 Ada 专有
   指令（如 FP8 转换），指令集层面 sm_86 完全可承载。
3. dashdogy 的 30 系方案拆解：他的 `ampere_backend`（MIT，源码全公开）就是
   "自产 SM86 程序 + 在进程内替换 provider 的 kernel 加载"。他的 `BUILD.md` 里
   不发布的是**编译并验证过的 SM86 kernel 数据**（native-cache/*.fatbin + manifest）；
   而 `ampere_native_cache` 本身支持 ePtx 模式（PTX 直载）——本实验证明我们能自产
   这批数据，不需要索取他的未公开材料。
4. 边界（必须如实）：JIT 通过只证明指令集合法性，**不证明 sm_86 硬件上的数值行为、
   性能与稳定性**；那需要 30 系实机。dashdogy 自己标注该路径 "very early and
   experimental. It may not work in some games or configurations"。

### 30 系的三条路（供决策）

| 路线 | 内容 | 状态 |
| --- | --- | --- |
| A 原生 DLSS-G | 按 dashdogy ampere 机制在 310.7 上重新实现的 `AmpereMfgUnlock`（69 fatbin → sm_86 重建、200 指针槽 + 44 lea + 2 gate 发布、CUDA 预验证、全量回滚） | **2026-09-16 用户授权后已实现**（`4154733`/`3dd5add`），本机机制验证通过；**待 30 系实机验证**（`veyra.exe --fg-multiplier 6 --smoke-seconds 15 <视频>`） |
| B FSR 补帧 | Veyra 自带 AMD FidelityFX 帧生成（E 工作流，已实现）。AMD 官方 FSR3 FG 支持 RTX 30 系及以上 | 保留为备选；待 30 系实机验证 |
| C 代理式 | `Nukem9/dlssg-to-fsr3`（GPL-3.0）代理 | 不推荐：它会替换磁盘 DLL 语义，与"进程内修改"路线冲突 |

说明：dashdogy 的三个 gate pattern（metadata / create-validation / sl-availability）针对 310.9
fixture，在 310.7 上 0 匹配，因此 gate 采用 40 系同款两处架构比较（0x1b0→0x170）。若 30 系实机
发现 provider 仍有 310.7 特有的额外拒绝点，将以实机日志为证据迭代定位。

## 30 系用户现在的 2X 路径

Veyra 已经落地了 AMD FSR 帧生成（E 工作流，本机 RTX 5070 实测 2X 可用）：
它是 D3D12 上厂商无关的 FidelityFX 效果，**不依赖 Blackwell**，因此 30 系用户
在软件里直接选「AMD FSR 帧生成 · 2X」即可获得补帧，不需要任何 NVIDIA DLL 代理，
也不需要绕过签名。

证据（本机）：`docs/FSR_FRAMEGEN_INTEGRATION_2026-09-16.md`（1080p 226/222、4K 559/555、
同帧对照平均绝对差 0.31/255、生成帧确实位于时间中点）。

## 未验证 / 待办

- **没有在 RTX 30 实机上验证过 FSR 帧生成**（本机只有 5070）。提供方在 5070 上正常枚举与运行，
  但"30 系也能跑"目前是**推断**，不是证据；必须在一张 3060/3070 上跑
  `veyra.exe --fg-fsr --smoke-seconds 10 <视频>` 才能对外宣称。
- 若 30 系实机发现 FSR FG 不可用，再评估以 `Nukem9/dlssg-to-fsr3`（GPL-3.0）为上游做代理式方案；
  那需要额外的发布范围授权（它会替换游戏/系统目录里的 `nvngx_dlssg.dll` 语义，
  与 Veyra 现在"进程内不改磁盘"的路线不同）。
- RTX 20 系（SM75）不在本次范围。
