# 2c：最近单NR配置缓存实测与保留

前存档cc8db0f / checkpoint/perf-nr-2c-revised-before-20261004。候选仅常规文件、本机未补丁NVIDIA/Lecram单NR、无SR/FG/HDR/抗闪烁/稳定器/颜色，NR→效果全关→严格同配置；最多一份非活动Graph。持有完整Graph可以保持NR adapter/IAT/Core所有权一致，效果全关新图不给CoreCache，不执行第二次NGX初始化。其他增强图必须先释放缓存，绝不同时创建第二个adapter。默认无需开关，TEST_DISABLE_RECENT_GRAPH_CACHE提供同EXE回退。

key包含完整EnhancementSettings（仅revision归零）、节点顺序、源/work/NR尺寸、格式/位深、runtime路径及NR DLL大小/mtime；这不是运行库哈希加载锁，变化只让缓存失效。实际QueryVideoMemoryInfo构建前后差作为保守占用估计；未知/0不保留。WDDM余量必须>占用1.5倍，并限制自身usage<budget/3，比原案更保守。内存检查500ms包括暂停；压力/失败/源resize/rollback/退出都先释放缓存。命中先清暂停残差seed，第一次完整process(reset=true)重置NR/flow等全部历史。

原生50次开关，前20与此前已封存B2a-v2-nr三轮相同自然源前三帧/Realtime1080参数；之后每轮改自然源时间段验证历史恢复。保存每轮所有完整RGBA8 SHA及8张关键图，CPU创建计时不含readback，D3D12 debug及最终Core关闭/显存记录。压力注入增加独立测试limit，实际资源/累计MiB/HRESULT入日志，不改变默认产品设置。

## 三轮原生与实际界面结果

build-recent-cache-v1 因误用不存在的 ChainExecutionPlan.mode 编译失败；v2 使用实际 runtimeNodeOrder 字段限定列表模式，构建成功。B2c-native-v1 开/关各三轮，共300完整输出帧，所有SHA一致；前20轮与封存B2a-v2-nr三个对照共60项逐字节一致，A-A噪声为0。每轮24次命中；暖NR创建中位数再取三轮中位数：off355.661ms（354.409–356.887），on0.156600ms（0.152700–0.156750），减少99.956%。这是图重激活CPU时间含GPU完成，不包含Presenter重开，也不是显示空档。最终关闭自身显存off134.270MiB、on133.113MiB，各三轮稳定；debug错误/设备移除0。

B2c-ui-v1 首个界面组完成，但driver误读 settled 的 on 字段而退出，失败证据保留，未纳入统计。修正为实际 nr 字段后的 B2c-ui-v2：开/关各三轮，每组20次真实桥接请求。每个on组10次缓存命中，NR开启 createMs 三轮中位数off363.8505ms（362.014–366.706），on3.6020ms（3.4855–3.6580），减少99.010%。这里包含实际Presenter创建；桥接观察还含200ms轮询，不把它当物理显示停顿。

pressure 组实际每秒分配256MiB、上限3072MiB，达到WDDM预算阈值后日志 event=evict reason=budget-pressure-or-device，未命中缓存，随后NR重新创建并继续播放；所有12笔分配HRESULT成功。invalidate 组NR关闭后改到720内部尺寸，严格key变化释放缓存，再次关闭源 event=evict reason=session-close。两组正常结束，无泄漏参数块或产品ERROR。保留单NR缓存，SR/NR+SR实例复用须另测隐含历史与画面，当前不擅自扩大。

原始CSV/完整SHA/关键RGBA/JSON/环境在 E:/项目/Veyra/logs/perf-nr-20261004/B2c-native-v1-*、B2c-native-v1-comparison.json、B2c-ui-v1-*、B2c-ui-v2-*。真实UI EXE及NR DLL SHA均记录在receipt；源码/运行库隔离，所有测试进程有上限，不进行桌面输入。整产品R0导出回归仍待。
