# AMD RX 9000 NR 接入记录（2026-10-03）

## 来源与资源

独立运行时来自 [lmxxf 官方仓库](https://github.com/lmxxf/dlss5-on-amd-9070xt-porting)，
固定 `78f548749e74824327b8458c57be31a1df78376a`。源码 MIT；权重不因此获得 MIT 许可。
只复制公开 C ABI header 和 MIT 原文到 Git。独立 DLL 用该提交的原始 codec/HIP
实现编译；MSVC 适配仅为 NOMINMAX、noinline 注解和 user32 链接，见
`scripts/amd-nr/build-lmxxf-runtime.py` 与 `third_party/lmxxf/VEYRA_ORIGIN.md`。

用户提供 `C:/Users/123/Downloads/Magpie-DLSS5-AMD-0.39.zip`，340434944 bytes，
SHA256 `9ea84c665d270cd45e24184729b8272c152485df462a1528539ed778d41849f5`。
740 个文件项，739 个有包内 SHA256 记录的文件全部匹配。
其中模型权重 186 个（122 f16 + 64 f32），索引映射 2 个 i32，
HIP 内核 62 个（gfx1200、gfx1201 各 31）。不加载包内 Magpie.exe、dxgi.dll
或 dlss5-amd.addon64；不在磁盘修改 NVIDIA/AMD 驱动或运行库。

原解压：`E:/项目/Veyra/deps/magpie-amd-039-20261003`；
宿主资源：`E:/项目/Veyra/deps/veyra-amd-nr-039-20261003`；
独立 DLL：`E:/项目/Veyra/build/lmxxf-runtime-20261003/LmxxfNrRuntime.dll`。
包审计：`E:/项目/Veyra/logs/field-upgrade-20261003/amd-039-archive.json`。
运行时逐文件审计在宿主资源的 `amd-nr-local-manifest.json`。

## 共享图接入与兼容

- NR runtime ID 4 为 lmxxf；旧 NVIDIA 0/2/3 编号不变。
- AMD 卡默认选择 lmxxf NR、AMD 光流和已有 FSR SR；效果仍默认关闭。
  保存的 NVIDIA 光流选择在 AMD 图中解析到 AMD 光流，不访问 NVOF。
- 每个共享 `NrInstance` 拥有一个独立 runtime context；输入为同尺寸稳定的
  linear RGBA16F。运行时负责 encode/network/decode，Veyra 接着执行共享残差、
  保护、抗闪烁与输出，不复制播放器或另写网络。
- 绝对路径 `runtime/amd-nr/LmxxfNrRuntime.dll`，资产在同级 `assets`；
  LoadLibraryExW 限制 DLL directory/System32 搜索。C ABI 1 的表大小
  144/136、FrameInfo 104/80 可协商；缺函数表、创建/准备/执行失败均记录。
- producer 录制/提交 → 同一 direct queue 的 HIP enqueue → consumer 录制/提交
  → Retire。Retire 在输出 reader 提交后执行，不能提前释放或复用输出。
  图退出先排空队列、释放 runtime，再释放借用输入与 queue。
- host 不添加逐帧 CPU fence 等待。上游首次准备、资源更换和 teardown 可等待；
  实际 HIP 时序/耗时尚未在本机测量，不能将 identity-copy 测试当成 HIP 性能。
- 环境 `DLSS5_CODEC_SRGB=1` 拒绝（Veyra 是线性输入）；运行时的 ResetHistory
  当前无网络历史重置实现，因此有 `DLSS5_VIT_ADAPTIVE!=0` 环境时拒绝。
  不偷偷改变全局环境。默认无网络跨帧复用，Veyra 自身 history 正常 reset。
- 上游 0.40 runtime 明确支持旧内核目录，缺可选内核则关闭该优化；本地采用
  用户 0.39 的内核选择和 skip/style 配置，FAST_NUMERIC=0。codec shaders 使用
  与 C++ 同一固定提交，HIP/权重原样，生成相对路径 HIP/SHA256SUMS。

## 已知功能边界

公开 ABI 接受高度 ≤1080、总像素 ≤1920×1080、宽度 ≤2560（可容纳部分超宽）。
AMD NR 的 UI 只提供 480/720/900/1080 内部档；列表与节点共用该编辑器。
预览可降采样后再回到目标输出尺寸。当前图片/视频导出按产品原生 NR 合同处理，
超出预算明确拒绝；**没有原生 1440p/4K AMD NR 导出**。不改成隐形降采样来冒称原生。
HDR 输入也明确拒绝，避免把未经验证的 codec 结果当作 HDR 增强。
tone/structure/skin/autoMask/uiCorrection 不在此 C ABI 支持范围，AMD UI 隐藏这些
NVIDIA 专用控件；模型强度通过 transfer_strength，style 使用运行时配置。

## 本机证据与复现

1. `build-lmxxf-runtime.py --source E:/项目/Veyra/deps/lmxxf-nr-20261003 --build E:/项目/Veyra/build/lmxxf-runtime-20261003 --tmp E:/项目/Veyra/tmp/field-upgrade-20261003 --log E:/项目/Veyra/logs/field-upgrade-20261003/lmxxf-recipe.log`：MSVC 编译成功。
2. `prepare-local-runtime.py --package E:/项目/Veyra/deps/magpie-amd-039-20261003 --source E:/项目/Veyra/deps/lmxxf-nr-20261003 --dll E:/项目/Veyra/build/lmxxf-runtime-20261003/LmxxfNrRuntime.dll --out E:/项目/Veyra/deps/veyra-amd-nr-039-20261003`：739 hashes、62 modules、真实 DLL 的 API/capabilities/module layout 通过。没有执行网络。
3. `veyra_lmxxf_nr_tests`：62 checks，0 failures；独立 identity GPU-copy
   provider 验证新/旧 ABI、同队列调用顺序、reader 后 retire、12 帧 allocator
   复用、缺库/坏函数表/错误环境/失败 enqueue 拒绝。此测试 DLL 不进入候选。
4. RTX 5070 的真实 NR+SR 4K NVENC 导出通过，NVIDIA 路线保持可用。

本机无 RX 9000，System32 也没有 amdhip64_7.dll。没有证明 AMD 真正推理、颜色、
画质、性能、多实例显存、长期稳定或 RX9060 通过。下一步实卡按 720/900/1080、
源切换/seek/暂停恢复/resize、多层 NR、与 FSR SR/FG 组合分别验证；初始化日志、
真实输出与无设备移除必须同时取得。当前是有完整资产的本地实验接入。
