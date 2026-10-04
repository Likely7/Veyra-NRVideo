# 2c：最近单NR配置缓存候选

前存档cc8db0f / checkpoint/perf-nr-2c-revised-before-20261004。候选仅常规文件、本机未补丁NVIDIA/Lecram单NR、无SR/FG/HDR/抗闪烁/稳定器/颜色，NR→效果全关→严格同配置；最多一份非活动Graph。持有完整Graph可以保持NR adapter/IAT/Core所有权一致，效果全关新图不给CoreCache，不执行第二次NGX初始化。其他增强图必须先释放缓存，绝不同时创建第二个adapter。默认无需开关，TEST_DISABLE_RECENT_GRAPH_CACHE提供同EXE回退。

key包含完整EnhancementSettings（仅revision归零）、节点顺序、源/work/NR尺寸、格式/位深、runtime路径及NR DLL大小/mtime；这不是运行库哈希加载锁，变化只让缓存失效。实际QueryVideoMemoryInfo构建前后差作为保守占用估计；未知/0不保留。WDDM余量必须>占用1.5倍，并限制自身usage<budget/3，比原案更保守。内存检查500ms包括暂停；压力/失败/源resize/rollback/退出都先释放缓存。命中先清暂停残差seed，第一次完整process(reset=true)重置NR/flow等全部历史。

原生50次开关，前20与此前已封存B2a-v2-nr三轮相同自然源前三帧/Realtime1080参数；之后每轮改自然源时间段验证历史恢复。保存每轮所有完整RGBA8 SHA及8张关键图，CPU创建计时不含readback，D3D12 debug及最终Core关闭/显存记录。真实Qt与压力测试随后执行。压力注入增加独立测试limit，实际资源/累计MiB/HRESULT入日志，不改变默认产品设置。此文件当前是候选说明，未构建/未验收，不能称保留。
