# RTX 30 系 VFG 兼容接入

用户2026-11-02明确授权移植RTX30-Broadcast-FrameGen。隔离分支 `codex/vfg-ampere-20261102`，基线4895549，包含上一轮全屏进度条修复，不合main、不push/发布。本机RTX5070，不能宣称RTX30实卡已验证。

上游 Xchen991/RTX30-Broadcast-FrameGen，固定07536ab32a508b7daa714a4a9bda5b56f031bbc3，MIT。移植src/dllmain.cpp中VFG模块CUDA查询、fatbin/ELF架构重定向机制；不接Broadcast UI、version.dll代理、固定IAT偏移、1ms轮询或系统级驱动修改。对应来源/修改及原MIT全文写入产品源码与THIRD_PARTY_NOTICES。

沿用现有VFG模块/模型：nvVFXVideoFrameGeneration.dll，207321712字节，SHA256 270CF4FFF9329908F9770D306CB51B910EE04323AF5520CD02A98A2E22ABA795。新兼容只作用于30系SM86；40/50原路径不安装补丁。界面/能力检测允许30系并明确实验状态，真正初始化仍以匹配适配器、组件识别和SDK结果为准。未知组件不应用已知布局的兼容改写，不把用户替换的DLL作为普通播放器启动锁。

实现边界：限定VFG模块的导入表查询与修改，原驱动入口保持；只伪装选中SM86设备在VFG调用内的minor属性；CUDA内核架构标记在私有内存副本修改，原模块/磁盘字节不改；有界结构解析、生命周期租约、先销毁效果后恢复导入表再释放库；安装/回退失败拒绝对应实验增强并记录原因，不影响普通播放。无每帧轮询、无额外GPU像素回读/同步、无新模型或额外运行库。

验证：结构/截断/畸形/未知GPU与设备隔离合同、真实已审计模块的导入扫描与安装/恢复（本机控制测试，不冒充Ampere执行）、40/50不打补丁及原VFG输出GPU回归、相关UI/能力/设置和翻译；每进程≤300秒，构建并发2/≤900秒。依赖在E:/项目/Veyra/deps/vfg-ampere-20261102，临时/日志/报告/测试按同名任务目录。原用户进程/配置、DLL、驱动及SDK保持。

最终命令、结果、包体积差异、文件身份和未验边界追加在本文件，机器回执reports/vfg-ampere-20261102/FINAL.json。


## 实现与验证记录

已实现：新VfgAmpereCompat负责真实PE按名称发现两个导入、SM86设备/当前CUDA上下文隔离、私有fatbin副本以及多实例引用租约。原CUDA入口和原kernel内容不改；API加载后私有输入副本可释放的生命周期按NVIDIA CUDA Driver API cuLinkComplete约定（先cuModuleLoadData再cuLinkDestroy）核对，见https://docs.nvidia.com/cuda/archive/13.0.2/cuda-driver-api/group__CUDA__MODULE.html。普通40/50初始化明确记录native-no-patches；20系及SM80数据中心卡未开放。界面/节点/能力检测/四语说明已同步，30系标实验。当前支持已审计的原VFG 207321712字节组件；未知版本仅拒绝该实验补丁，不限制40/50原生运行时替换。

构建：沿用scripts/acceptance/build-recording-handoff.py，task codex-vfg-ampere-20261102、build-dir E:/项目/Veyra/build/claude-screen-recording-20261022、label 2.1.2-test、jobs2；首次reconfigure后89.0s，后续5.4s，补充页保护读回检查后最终4.2s，均退出0。最终回执logs/codex-vfg-ampere-20261102/build-20261102-222307/receipt.json。目标veyra_qml_ui、veyra_vfg_ampere_tests、veyra_vfg_gpu_tests、veyra_effect_availability_tests、veyra_vfg_settings_tests、veyra_ui_i18n_tests、veyra_qml_quick_tests、veyra_repair_contract_tests。构建期间一次设备上下文隔离补充由后续顺序增量重建确认纳入最终程序。

新兼容合同180项通过；真实已审计模块的62个fatbin私有副本、导入发现、3轮双租约安装/解绑、跨设备拒绝、原指针与原页保护逐项恢复共342项通过；这里没有在RTX5070执行Ampere改写后的内核，不冒充30系推理。能力75项、VFG设置683项、repair300项、i18n0失败；QtQuick140通过、0失败、1既有条件跳过。真实程序独立配置中选择更新后的VFG菜单，2X输出统计60FPS、源时间4.3s并正常退出；无QML错误，原生路径没有安装兼容补丁。

原生GPU回归覆盖8/10位、各质量与2X–8X、独特插值位置、混合模型、切镜、异步状态与销毁，2425项0失败。最终重建后的同套回归结果由FINAL.json记录，不以提交FPS冒充物理显示或端到端延迟。测试命令和逐项结果保存在reports/vfg-ampere-20261102/run.py、ui.py与对应JSON，原始日志在logs/vfg-ampere-20261102。首次repair启动因测试子进程PATH缺少FFmpeg DLL退出0xC0000135，添加既有运行目录到该子进程PATH后通过；失败日志保留repair-missing-path.*，未改系统环境或驱动。

30系本机无实卡：尚不能证明模型在Ampere上实际执行、画质或实时性能；需要持卡运行验证。RTX5070原生回归不能代替该证据。暂不扩大到未知SDK版本/其他架构，不自动安装驱动或下载模型。

## 本地交付

提供2.1.2 NVIDIA便携包的小型覆盖补丁，不重新复制整包。包括新主程序、两份全屏进度条修复QML、翻译目录、第三方notice/原MIT与对应源码差异。运行库与模型全部沿用原包；原用户配置保持。正式2.1.2主EXE 21200896字节，准确新大小和增量见FINAL.json（首轮实测增加12800字节，最终以回执为准）。ZIP是包含整个新EXE的更新包，下载大小不等于功能增量。

补丁目录test-packages/codex-vfg-ampere-20261102。退出软件，解压到2.1.2 NVIDIA版EXE目录覆盖；在补帧模型选择NVIDIA VFG，30系自动走实验适配。默认效果/已有选择不强改。AMDGPU包缺VFG组件，不应只套此补丁就宣称具备VFG。含上一轮4895549全屏进度条修复。源码差异基于公开v2.1.2源码，随补丁提供；不含专有SDK/模型。无push、main合并或GitHub新发布授权。
