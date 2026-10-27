# Veyra 2.1.1 本地交付验收（2026-10-26）

已完成NVIDIA/AMD两个完整Release配置便携ZIP，版本/PE均为2.1.1/2.1.1.0，未合main、未push、未公开发布。菜单保留2.0.7弹出动画，仅提前首次布局；带入AMD原生HDR经SDR代理NR再还原的修复。本轮不修改产品逻辑。

## 实际执行

- python -X utf8 -B scripts/acceptance/build-recording-handoff.py --task codex-build-2.1.1-resume-20261026 --root 当前E盘树 --build-dir E:/项目/Veyra/build/claude-screen-recording-20261022 --reconfigure --label 2.1.1 --jobs 2，应用及11定向目标，26.1秒通过；完整targets/哈希在logs同任务/build-20261026-224756/receipt.json。
- python -X utf8 -B E:/项目/Veyra/tmp/codex-build-2.1.1-resume-20261026/run-checks.py：12组全部exit0，QML117通过/0失败/1跳过，翻译2674条/缺译及占位问题0。包括效果链、异步显示查询、UI、Repair、呈现、预览、配置及预设回归。
- python -X utf8 -B scripts/acceptance/package-2.1.1.py stage|seal|cold --task codex-build-2.1.1-resume-20261026，三阶段依次成功。受控2.1.0供体逐文件核验，NVIDIA50/AMD654个runtime行保持，签名/版本相同，PE版本/依赖闭包/模型边界通过；实际新编译HDR AMD两着色器入包。ZIP全文件SHA及CRC成功，应用源码2292个文件复核；未变依赖源码随目录提供。
- 从ZIP解压、隔离PATH/私有配置，NVIDIA D3D12/软件界面分别实际呈现284/290帧，AMD包D3D12/软件界面289/289帧，四轮exit0/无QML运行错误。AMD包在本机NVIDIA显卡只验基础播放与打包依赖，不是HIP推理验收。

## 证据与边界

当前主EXE SHA与菜单/HDR验收时完全一致，de07548373b8bd308d9af8865b06bb9551aa507761225ea35e574142b356db6a；图源码与VMenu哈希也相同。沿用原真实NVIDIA HDR+NR/AMD fake provider图测试的明确边界，不重复声称新做了实卡采集；RX9000 HIP原生HDR仍待实卡。T911/3080增强后停流首因未定、未修；4K约53fps未修、原CV710/3090仍待复测、长期显存/真实VRR仍未扩大验收。没有首次停流新插桩。

603个旧refs与100个其它工作树HEAD不变，main仍68def10。对应源码快照89116ba，产品构建回执2c0c82f，两者产品代码无差异。本报告是封包后的验收记录，未倒填进已经核验的ZIP/源码快照。

自动审批拒绝“准备清理旧stage和验收解压副本脚本”的操作，仅给出blocked by policy；没有实际生成脚本/执行删除，也未绕过重试。旧stage、新stage及verify解压副本保留，cleanup-blocked.json记录。本机原误建build的既有清理拦截状态也不变。

交付目录：E:\项目\Veyra\test-packages\codex-build-2.1.1-resume-20261026

最终证据：E:\项目\Veyra\reports\codex-build-2.1.1-resume-20261026\FINAL.json

NVIDIA：Veyra-2.1.1-NVIDIA-win64-portable.zip，679560249 bytes，SHA256 e3d78f91b4c2669108f2c22c1d491d17f3bfdc73b2dbda8f9eba823d5f32b451

AMD：Veyra-2.1.1-AMD-win64-portable.zip，466204342 bytes，SHA256 5153e97be26c03062b8596462fbede41b700f354799df02875bad3bf5d895f15

