# 2a：NGX 核心重建基线与复用实验

## 优化前实测（A）

起点产品源码 `8cdc612120cbf23ba116a33c3cb0a53e2043f718`；实际产品 EnhanceGraph，RTX5070 / 616.56 / Lecram NR SHA256 F95FEB54137EA11979F9B4EC4F00AFD84B5C98A5624D3388FBF6A87714A39FCC。固定 M1 的前三帧，每轮 20 次重建，每组独立三轮。CPU create/destroy 包含必要 GPU 等待，像素读回在计时段外；没有实际 Present，不能当成界面最大呈现间隔。

| 组 | 创建耗时三轮中位数 ms | 各轮中位数范围 ms |
|---|---:|---:|
| NR 开（开关交替中仅取开） | 1569.935 | 1561.335–1577.420 |
| NR 关（仅取关） | 39.020 | 38.791–39.377 |
| SR 开关（NR 始终开） | 1411.975 | 1395.785–1561.080 |
| NR 层数 1/2/3 | 1462.380 | 1459.055–1490.710 |
| NR 尺寸原生/720/480 | 1351.520 | 1350.655–1352.670 |

四组共 240 次重建 / 720 次源帧处理：12/12 native exit0，D3D12 debug error0，设备正常，实际 NR/SR Evaluate 数与配置一致。各组每轮保存20张完整 RGBA；r1/r2/r3 同配置对应图像逐字节完全相同（80/80图像，重复两次比较均0差异）。这是后续候选输出的噪声底线，不能用“接近”代替相同。

原始证据：`E:/项目/Veyra/logs/perf-nr-20261004/A-rebuild-v1-summary.json`，各 `A-rebuild-v1-{nr,sr,layers,sizes}-r{1,2,3}/` 中的 rebuild.csv、engine.log、console.log、result.json 和 frame-0..19.rgba。native EXE SHA256 `2d71772f8d59bf063f2b48504ce6e1f4430566f5e815547d7b0bf42a56305e27`；`nr-build.py A build-A-rebuild-v1 veyra_nr_rebuild_benchmark`、`nr-rebuild.py A A-rebuild-v1` 均 exit0。逐次显存数据保留在 CSV，不混用 DXGI 本进程使用量与 nvidia-smi 全卡量。

## 候选范围

先只在同设备、同 runtime、同 NR 模块与相同补帧配置下保留 NGX 核心；功能及参数仍释放。设备移除、初始化失败、运行库/补帧配置改变或未释放参数均清空核心。Ada/Ampere 补丁会话先保持完整原流程，待实卡验证后再扩大。候选必须支持同一二进制 `VEYRA_TEST_DISABLE_NGX_CORE_REUSE=1` 对照，参数销毁与最终核心关闭顺序必须可核对。

候选将核心所有者放在 EngineController 的设备会话中，Graph 使用共享借用；缓存仅一份，借用者未释放时拒绝下一次 prepare，旧图全部参数释放且设备正常才 retain。普通 Graph 消费者默认不启用。NR 适配器仍每次重建加载/释放，是否允许在保留核心时重新 Init_Ext 必须实测。benchmark 通过显式缓存注入测试，B-off 同一 EXE 环境开关；最终关闭发生在设备销毁之前。此时尚未测得收益；上述时间全部是优化前数据。
