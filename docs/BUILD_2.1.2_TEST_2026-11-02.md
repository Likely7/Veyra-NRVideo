# 2.1.2 本地 main 整合与测试构建
> ⚠️ 路径提示：文中 `E:/项目/Veyra/...` 是迁移前的记录（现为 `D:\Program\Veyra`），按日期理解；当前入口见 [DEVELOPMENT.md](DEVELOPMENT.md)。

用户授权：合并当前各修复（含 Claude）到 main，构建 2.1.2 供测试；不包含 GitHub 推送或发布。

## 整合范围

起点 main `8535791`。当前 14 个未提交文件已保全，提交 `9e836ab` 后 main 快进合入，无冲突。之前双重超分/超采样、英伟达低延迟、RX7000、采集帧率/音频/屏幕捕获、补帧返工及 VFG 自动调控等分支 tip 均已是 main 祖先，无需重复合并。最新 Claude 会话未在接手后继续产生新改动。

只有 Lecram 两条暂停分支及 AverMedia 诊断存档还含独立提交，不纳入可交付修复；旧脏工作树原位保护。完整分支/工作树状态及原文件 SHA 见 `reports/codex-build-2.1.2-20261102/before.json`；原补丁与文件 ZIP 在同任务 `archives`。合并前标签 `checkpoint/pre-main-2.1.2-20261102` 指向原 main。

新增：用户选择的自适应容量等待上限 3 ms、极简音量拖动/键盘/空白提交。等待按**实际垂直同步呈现路径**生效：自动＋VRR 也可能转入此路径；普通自动或允许撕裂不走该队列。输出≤80fps且使用平滑锚点时跟随抖动，XeSS提供方/串流/内容去重按原路径。此前“仅显式垂直同步”文字过窄，以实际路径为准。提交计数不等于扫描显示，原上屏估算不作为验收证明。

## 构建与打包

项目与显示版本 2.1.2，主 EXE 的 PE 文件/产品版本 2.1.2.0。复用既有 Release 构建，并发2、单次≤900秒，子测试≤300秒。SDK/模型不进源码 Git。

```powershell
python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-build-2.1.2-20261102 --root 'E:/项目/Veyra/worktrees/screen-recording-fix-20261022' --build-dir 'E:/项目/Veyra/build/claude-screen-recording-20261022' --reconfigure --label 2.1.2 --jobs 2 --option VEYRA_RDNA3_NETWORK_SOURCE_DIR=E:/项目/Veyra/deps/amd-rdna3-nr-20261029/prepared veyra_qml_ui veyra_amd_nr_worker veyra_live_timing_tests veyra_realtime_preview_tests veyra_presentation_worker_tests veyra_presentation_pacing_tests veyra_repair_contract_tests veyra_ui_i18n_tests veyra_effect_chain_tests veyra_nr_model_settings_tests veyra_preset_library_tests veyra_qml_quick_tests
python -X utf8 -B scripts/acceptance/package-2.1.2.py stage
python -X utf8 -B scripts/acceptance/package-2.1.2.py seal
python -X utf8 -B scripts/acceptance/package-2.1.2.py cold
```

包以已发布2.1.1的逐文件manifest为供体，不从使用过的应用目录复制。更新已构建EXE/worker/着色器、受控QML/翻译/说明。NVIDIA包保留既有运行组件；AMD包保留RX9000 lmxxf并加入此前已核验的本地RX7000子集（RedDuke 1.3.0，27项清单，原版310.9.16.0网络、固定ZLUDA shim、内核/缓存/许可证及自有host）。不带真实驱动/HIP SDK，不向主进程根目录放ZLUDA替身；发行白名单不自动扩展。

归档前检查每项默认运行文件哈希、PE版本/依赖、双GPU包边界、源代码隔离，ZIP逐项 SHA/大小/CRC，解压后的实际冷启动/本地播放使用独立配置和只含系统目录的PATH。RX7000/RX9000实卡推理不由本机RTX5070冷启动替代。

## 对应源码

本次应用源码 `Veyra-2.1.2-source.zip` 从 main 的已提交源码快照生成；`Veyra-2.1.2-rdna3-network-source.zip` 包含实际prepared网络源码及固定来源/修改manifest；SHA见DELIVERY。没有专有SDK、DLL、模型或私有配置。

未改变的依赖对应源码继续使用已核验固定资产（本地已有原件，不为测试重复复制大档）：

- `Veyra-2.0.6-dependency-source.zip`：`4eccde6343d66e0511b641aaacc12b999e424738a383fcce268d762abb3dceb9`
- `FFmpeg-9.0.1-slices256-dav1d-zlib-source.zip`：`49db22049f072bd5448faa664aa736074dccfe6e72b4a933f673d8ae954b0bb6`
- `FFmpeg-9.0.1-deinterlace-module-source.zip`：`f993629b6587bac09d26c3b365b59f009edd01e539c79436aae7775fc4448152`
- `lmxxf-044-corresponding-source.zip`：`f83267c40fee6b36b183169e9e192089994628aa7b524357c8a0af5d4225070b`

固定公开来源见 [2.1.1对应源码说明](BUILD_2.1.1.md)，实际文件在本机 `archives/dependency-migration-cleanup-20261023/dependency-corresponding-source` 及 `releases` 对应版本目录。保留 patched FFmpeg 和所有许可证，不把旧 vcpkg 元数据冒充实际修改源码。

## 交付记录

输出 `E:/项目/Veyra/test-packages/codex-build-2.1.2-20261102`，NVIDIA/AMD 两个完整ZIP与可直接启动目录，附应用/网络源码和SHA256。测试结果、失败及保护核验以 `E:/项目/Veyra/reports/codex-build-2.1.2-20261102/FINAL.json` 为准；本计划不是通过证明。正式公开版仍2.1.1，未推送/发布。


## 2026-11-02 实际完成结果

产品快照 `47b5e84`，main 已包含合入修复 `9e836ab`。上面的12个目标构建87.8秒通过；8组合同/设置通过，QtQuick132/0/1既有跳过，2718条翻译缺失及占位符问题0。VFG运行中自动调控切换、DLSS请求6X、XeSS2X三组实际提供方生成并干净退出，无D3D12错误；这是短测功能回归，不是吞吐/显示延迟保证。CPU与QtQuick重叠但GPU阶段没有其他Veyra测试，压缩与GPU重叠，不引用其时刻作性能结论。

NVIDIA ZIP 676470641字节、1634项payload；AMD ZIP716404207字节、2260项payload（含27项RX7000子集），主EXE SHA256 `7f9ee826282a5e86698853a833978475632b3f034e9bf76b37db2d56d7880723`，两包PE文件/产品版本2.1.2.0，ZIP逐文件大小/SHA/CRC通过。独立源码ZIP73378474字节，实际prepared网络源码29191字节，4个未变依赖对应源码档在原位逐SHA核验，不重复复制大档。

冷启动从已校验ZIP重新解压，最小PATH、独立配置、预热/恢复关闭：NVIDIA/AMD各约10.5秒退出0，本地播放分别提交214/222真实帧，无QML/D3D12错误；这仍是RTX5070默认效果关闭的冷启动，不冒充AMD7000/9000实卡推理。两个解压副本逐SHA确认与保留目录完全一致后，原生PowerShell删除，逻辑字节合计2623969881；未测净物理回收。最终用户包目录、ZIP、源码、SHA及证据保留。

阶段纠正：最初 `py_compile` 生成单个未跟踪pyc，导致stage的干净源码检查正确拒绝；核对绝对路径后只删除该自生成文件，再stage成功，源码ZIP无pyc/测试缓存。没有修改校验规则绕过检查。新27项模型/兼容组件来自此前已授权本地研发的固定子集，原默认运行组件原字节保持；没有新发布授权扩展。

输出根目录 `E:/项目/Veyra/test-packages/codex-build-2.1.2-20261102`；独立启动脚本 `启动2.1.2-NVIDIA测试.cmd` / `启动2.1.2-AMD测试.cmd`，使用 `tests/codex-build-2.1.2-20261102/interactive-*` 配置。原共享开发运行目录也已更新，旧真实配置及所有旧脏树保护核验见FINAL。后续提交只补文档，用户包源码快照和产品二进制仍对应47b5e84；未push/Release，公开正式版仍2.1.1。

## 2026-11-02 重新打包：加入自动剔除重复帧修复（Claude）

用户在去重修复实测后要求“把修复合并到main，然后重新打包2.1.2”。修复见[执行记录](DEDUP_PRECISE_2026-11-02.md)：整画面去重允许约百万分之五的超界分量，并删除图内64×36弃帧。版本仍为2.1.2（PE 2.1.2.0）。

- 新包输出到 `E:/项目/Veyra/test-packages/claude-build-2.1.2-dedup-20261102`，任务名 `claude-build-2.1.2-dedup-20261102`。
- `package-2.1.2.py` 增加 `--task` 参数，不传时仍为原任务名。
- 上面Codex的首个2.1.2包目录原样保留，包括用户运行它时生成的 `runtime_local/user-data-2.0.0`。
- 步骤与上面相同：`build-recording-handoff.py --task claude-build-2.1.2-dedup-20261102 ...`，随后执行 `package-2.1.2.py --task claude-build-2.1.2-dedup-20261102 stage|seal|cold`。
- 合同、QtQuick、GPU短测沿用Codex的同组检查。

实际结果（2026-11-02）：
- **提交**：main `30337c8`→`a90b4a9`（Codex未提交的监测记录，原样单独提交）→`358be8c`（去重修复），快进合入；产品源码快照为`358be8c`。
- **构建**：12个目标61.5秒，回执head `358be8c`、dirty=false。主EXE SHA256 `33d8e615666365ba…`，两包PE文件/产品版本均为2.1.2.0。
- **检查**：
  - 8组合同全部通过；QtQuick 132/0/1（1项为既有跳过）。
  - GPU短测3组（VFG运行中切换自动调控、DLSS 6X、XeSS 2X）均退出0、无D3D12错误。期间用户自己的Veyra测试实例在运行，这是功能短测，不作性能对比。
- **打包**：
  - stage 20秒，两包通过。
  - seal 147秒：NVIDIA ZIP 676472179字节/1634项；AMD ZIP 716405735字节/2260项（含27项RX7000子集）；逐项大小/SHA/CRC通过。
  - 源码ZIP 73389714字节，网络源码29191字节。
- **冷启动**：从ZIP重新解压，最小PATH、独立配置。NVIDIA/AMD各约10.5秒退出0，本地播放分别提交214/222帧，无QML/D3D12错误。这仍是RTX5070默认效果关闭的冷启动，不能代替AMD实卡推理。
- **清理**：两个解压副本逐SHA核对后删除，逻辑字节合计2623978645。
- **旧包保护**：Codex首个2.1.2包目录、其中用户运行时生成的7个配置文件、4个ZIP，均与打包前快照逐字节一致。
- **共享开发运行目录**：只替换了`veyra_presentation_pacing_tests.exe`。其中的`veyra_qml_ui.exe`被用户正在用的测试实例占用，仍是同一源码早一次的构建`fb1e3ad7…`。
- **交付**：`E:/项目/Veyra/test-packages/claude-build-2.1.2-dedup-20261102`，内含两包目录、ZIP、源码、SHA256、DELIVERY.json，以及启动脚本`启动2.1.2-NVIDIA测试.cmd`/`启动2.1.2-AMD测试.cmd`（独立配置在`tests/claude-build-2.1.2-dedup-20261102/interactive-*`）。
- 回执：`E:/项目/Veyra/reports/claude-build-2.1.2-dedup-20261102/FINAL.json`。未push/Release，公开正式版仍为2.1.1。
