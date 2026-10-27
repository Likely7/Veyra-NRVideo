# NVIDIA 显存现场诊断包

用户要求给发生显存持续增长的用户一个可直接运行的诊断包。根因未确认；本轮不尝试 Shutdown、缓存清理、驱动设置或算法修复，不合并 main、不推送或发布。

从 main dbfceabee804cd81bac4a4781f0b7b4cf272902e 在独立 codex/nv-vram-field-20261008 施工；产品基底为已测 71723e0。产物统一 E:/项目/Veyra/{reports,tests,logs,tmp,test-packages}/nv-vram-field-20261008。36 个原工作树、495 份未提交文件以 reports 下不可变 pre-document-state.json 保护，不修改旧 guard。

交付一份诊断 EXE 与启动脚本，放入现有 2.0.6 NVIDIA 包使用，不覆盖原 EXE、运行库或用户配置。私有 profile 只复制普通设置白名单，不复制串流凭据；用户可在诊断进程中按需登录。默认连续记录同 PID，关闭诊断播放器后自动封装证据。记录键用于注明全屏、NR、停止播放阶段；不自动切换设置或重建图。显存增长明显时停止播放并等待约 30 秒再退出，不人为耗尽显存。

证据包括：每 2 秒的进程 LOCAL/NON_LOCAL DXGI 使用量/预算/原始 HRESULT、LUID、设备代次与完成 fence；图及 NR 层创建/关闭边界；仅本轮覆盖调用点的自有纹理/上传缓冲区最终 COM 销毁计数与分配大小估计；外部每 5 秒 WDDM 分实例 Dedicated/Shared/Committed、进程内存/句柄、实际已加载模块路径与 SHA256。跟踪 token 使用 SetPrivateDataInterface，不持有纹理/设备，不 hook SDK/vtable，不做像素回读或 GPU 等待。未覆盖 decoder/Qt/SDK 内部分配，计数不得冒充全进程实际驻留或直接相减判驱动泄漏。

构建通过现有缓存对象加诊断覆盖对象重链接，原构建、候选包全部 SHA 保持。只用一个共享运行目录、独立 profile/log/tmp；每个本机测试不超过 300 秒、串行正常播放，不造压。验收必须包括真实 RTX NR/NVOF 播放、关闭 NR及停止后同 PID 采样、资源 token 可回收、Windows PowerShell 5.1 启动及 ZIP 隐私白名单、计数不可用的明确记录、包 CRC/哈希/源码对应关系。实际结果写报告和 WORKLOG；受影响 GPU 长稳仍待现场。
