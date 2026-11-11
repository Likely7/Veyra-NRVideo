# RX7000 NR启动失败：核显/独显LUID映射

用户补充cache.zip后续接原排查。分支codex/rdna3-nvapi-luid-20261103，基线ecfbb0f（继承本地NVENC导出修复、全屏及VFG实验）。不合main、不push/Release；不修改真实配置、AMD驱动、NVIDIA网络DLL、原ZLUDA/CUDA翻译器或已发布资产。

## 证据与根因

原主日志三个会话52次CreateFeature均失败。补充包中52份worker日志逐字节相同：识别AMD Radeon RX7900 XTX，HIP看见gfx1036和gfx1100，缓存覆盖gfx11-generic/gfx1100，创建前可用显存24351/24560MB。NGX最新日志明确记录目标LUID 0x10100、m_gpuArch=0xe0，随后`Unsupported GPU architecture 0x0, minimum required 0x1b0`；兼容gate已登记内核。

固定来源RedDukeDev/ZLUDA b135e233bb7a6ff7f8690e49701f87f4944adcee的nvapi/src/lib.rs中，cuda_device_luid硬编码cuDeviceGet(...,0)，只枚举一个假NVAPI设备。Veyra的network已按选定D3D12 LUID创建CUDA上下文，但NVAPI仍把第一块（可能为核显）的LUID报给NGX，导致独显LUID匹配失败并落到GK100/0xe0。这是适配中遗漏的多GPU合同；不是用降低内部尺寸或重建缓存可以修正的身份映射。

额外核对network::gpu_architectures按HIP ordinal顺序逐一输出，并非按名称排序；本例gfx1036先于gfx1100。固定ZLUDA的device::get直接调用hipDeviceGet，get_luid读取该设备HIP属性的LUID；这把实际日志顺序与源码硬编码第0设备的缺口关联起来，而非只凭“有核显”猜测。

使用正式包同源的nvapi64.dll转发层与原zluda_nvapi.dll，在独立子进程载入CPU测试nvcuda.dll：模拟第0核显LUID0x12004、第1独显0x10100且当前上下文为独显，原库实测错误返回0x12004。即使模拟多GPU无上下文或LUID查询失败也错误返回成功。原库13组合同8组失败，保留shipped-before/RESULTS.json；不冒充AMD推理。

## 最小修复

只修MIT OR Apache-2.0开源NVAPI实现并重新编译，不patch任何原二进制：优先cuCtxGetDevice获取当前上下文的设备，再查询其LUID；无当前上下文时只允许唯一设备回退，多设备/驱动错误明确失败，LUID查询失败向NVAPI调用方返回错误。保持NR原来的Blackwell兼容身份、参数、运行库和翻译缓存不变。

原nvapi64.dll是转发层，真实身份函数来自zluda_nvapi.dll；本地覆盖只替换后者，worker/主EXE/模型无需改变。补丁和完整许可证放third_party/redduke，固定源码、构建脚本、合同测试与SHA清单随本地更新提供。

开发依赖E:/项目/Veyra/deps/rdna3-nvapi-luid-20261103；构建E:/项目/Veyra/build/rdna3-nvapi-luid-20261103，CARGO_HOME与TEMP均在E盘，cargo并发2、每次上限880秒；测试子进程120秒。原始日志只读，从压缩包读取小日志，不复制约100MB数据库。

## 验证与交付

上游未改源码首次构建48.5秒，修改后2.3秒；用源码仓库的构建脚本再从未修改源码重放patch并重建2.33秒通过。Rust1.96/MSVC、Cargo.lock保持原始版本，并发2；每次cargo进程上限880秒。DLL为215040字节，最终SHA256 `a7adb0431ed69f7245312634f08990704d839aae46f075d80f28fe85b409caeb`。同一源码重新链接的PE时间戳会改变文件SHA，因此最终合同使用最终重建DLL再次跑过，不沿用初版结果。

构建：`python -X utf8 -B scripts/amd-nr/build-rdna3-nvapi.py --source E:/项目/Veyra/deps/rdna3-nvapi-luid-20261103/ZLUDA --output-root E:/项目/Veyra/build/rdna3-nvapi-luid-20261103`。构建脚本验证固定HEAD、准确源文件差异及Cargo.lock，设置任务CARGO_HOME/target/TEMP，不安装到应用。

测试：MSVC分别把tests/integration/Rdna3NvapiFakeCuda.cpp编成nvcuda.dll和带VEYRA_RDNA3_NR_CALLER宏的dlssnr-contract.dll（命令回执reports/rdna3-nvapi-luid-20261103/build-fixture.py与tests同名目录fixtures/compile.log）。调用`scripts/acceptance/verify-rdna3-nvapi.py --directory <新证据目录> --nvapi <受测zluda_nvapi.dll> --forwarder <原nvapi64.dll> --cuda-fixture <夹具nvcuda.dll> --nr-caller <夹具dlssnr-contract.dll>`，每子进程上限120秒。测试夹具不能打入任何产品运行目录。

原正式2.1.2 AMD包的两份NVAPI文件与本轮对照件SHA完全一致：转发层2678a451…（349696字节），真实NVAPI b86425dd…（226304字节）。不是以不同版本上游代替真实发行件复现。

最终新DLL通过13组合同：核显首位/独显当前、首适配器当前、单设备无上下文兼容、多设备歧义/未知上下文/无设备/LUID失败/非法设备拒绝、双线程64次上下文隔离、NR调用方原Blackwell身份、空参数和可省略LUID缓冲。旧DLL13组中8失败，新DLL13通过。回执tests/rdna3-nvapi-luid-20261103/{shipped-before,final-fixed}/RESULTS.json。这里加载的是原转发层和实际新/旧NVAPI DLL，但CUDA是明确的CPU夹具，没有伪装成RX7000推理。

本地补丁test-packages/codex-rdna3-nvapi-luid-20261103/Veyra-2.1.2-RX7000-NR-multiGPU-fix.zip：只替换runtime/amd-nr-rdna3/zluda/zluda_nvapi.dll，另带许可证、来源、源码差异、构建/验收脚本及清单。原工作app、原依赖组件和已发布ZIP不替换，主程序/模型/缓存不变；用户退出软件后覆盖2.1.2 AMD目录。未来重新组RX7000包须应用这份独立组件更新并重新生成manifest，不能从旧供体把b86425dd…带回来。大小、摘要、清理和证据总回执reports/rdna3-nvapi-luid-20261103/FINAL.json。

本机无RX7000实卡，用户仍需单次验证实际模型创建/推理；如果后续出现新的错误继续依据新日志排查。本次没有重新跑无关QML、NVENC或NVIDIA推理测试，不以CPU合同宣称NR已在该用户机器跑通。
