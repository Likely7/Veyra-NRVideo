# 超分画质实验撤回与本地修复测试包（2026-10-10）

本轮从 `c664a675c7b7a5c6cb54ed79c07b8a72127f4dfb`，在 `codex/sr-visual-quality-20261010` 隔离工作。用户明确三个超分的实际画质提升均不明显，主要场景是1080p或更低→2K/4K；随后决定停止继续优化、去掉负优化，最后授权制作本地测试包。

结论：**没有找到可靠的普遍画质提升，本轮所有SR生产实验全部撤回**。最终 CMakeLists、EnhanceGraph、SR后端和生产shader与本轮基点一致；SrMotion/SrStaticDetect及额外纹理、检测pass不进入产品。只保留共同原图参考的验证工具、报告和此前已经完成的功能修复。不能将“像素变了”或静止重复帧误差下降当成一般视频画质提升。

## 测法与判断依据

本机RTX5070 12GB、驱动616.56。直接只读用户的 `E:/Ai/知识/小七姐/GTAVI_An_Extended_Look_4K_Native.mp4`，3840×2160/30fps，14,115,273,308字节，不复制媒体。选原生4K帧作为共同参考，Lanczos降至1080p后恢复到4K，对比普通双线性、VSR1、VSR4、DLSS、FSR。NR/FG关闭；同帧参考SHA一致。

第60秒做重复输入，第90/300/900/1200秒各32帧连续运动，保存第1/8/32帧。精确向前解码到目标PTS，颜色转换使用实际标签/明确BT.709，避免把前一个关键帧或BT.601误算为参考。测量coded BT.709亮度MSE/PSNR/MAE、8×8不重叠总体方差SSIM、梯度误差；这里的SSIM不是标准高斯窗口版。另存原像素裁切，不通过缩放截图制造细节。指标是相对受控参考的误差，不是主观画质或所有素材收益百分比。

最终基线：`logs/sr-visual-quality-20261010/tests-20261010-143134`、`tests-20261010-143421`，以下相对证据均在 `E:/项目/Veyra/`。早期142741试跑未精确前解码且默认矩阵不明确，已经纠正，不用于最终质量结论。用户要求停止后，没有继续720p→2K/4K测试。

| 实验 | 实际结果 | 决定 |
| --- | --- | --- |
| DLSS Balanced切Quality | 本次路径默认preset下输出逐像素相同 | 恢复原档位，不宣传提升 |
| FSR自动曝光 | 变化很小、正负混合，没有稳定收益 | 恢复原设置 |
| 离线RCAS 0.25/0.5/1 | 第900秒各后端的参考误差均增加 | 不接入产品 |
| 全局零运动 | 重复帧DLSS MSE 5.696424→4.405476，FSR 11.816919→7.952452；第900秒运动DLSS 2.014786→2.514555，FSR 2.577306→4.933844 | 运动恶化，拒绝 |
| 3×3局部静态校验，0.5/255容差 | 第1200秒运动DLSS MSE增加18.19% | 拒绝 |
| 3×3完全相等且匹配严格优于原运动 | 第1200秒运动DLSS MSE仍增加11.31% | 拒绝 |
| 整个光流输入完全重复才归零 | 本次四段运动72张输出PNG与基线字节一致，重复帧误差改善；每个适用SR帧增加两次GPU dispatch、R32标志及RG16F纹理 | 收益仅重复帧，仍有默认运行成本，按用户要求全部撤回 |

整帧候选在4K DLSS时额外RG16F约31.64MiB，1080p FSR时约7.91MiB，另有标志资源分配；没有测出对应毫秒代价，不捏造帧率/功耗收益。它不是本次普遍清晰度优化，最终包不包含它。实验patch、两个shader及SHA在 `archives/sr-visual-quality-20261010/withdrawal.json`；四段一致性在 `reports/sr-visual-quality-20261010/discarded-global-candidate.json`。局部和整帧候选的shader/NR/FG组合检查通过也不抵消画质失败。

## 接入核对与限制

VSR调用现有SDK参数与1–4质量档，没有发现已公开的另一个强度控制。DLSS/FSR使用线性输入；现有motion缩放合同、零jitter、常量相对depth与真实游戏引擎提供的采样信息不同。普通视频没有实际渲染jitter/depth，不能凭空填写来制造重建信息。NR残差合成保留全分辨率基底，不是简单把整幅画面强制变成1080p；最终缩小显示也可能削弱超分细节的可见度。未证明存在另一个能简单调参解决的共享颜色或采样错误。

核对的一手资料：[NVIDIA RTX Video SDK](https://developer.nvidia.com/rtx-video-sdk)、[DLSS公开定义](https://github.com/NVIDIA/DLSS/blob/main/include/nvsdk_ngx_defs.h)、[DLSS开发指南](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf)、[AMD FSR接入手册](https://gpuopen.com/manuals/fidelityfx_sdk/techniques/super-resolution-upscaler/)。没有拷贝新上游算法、下载/更换未知运行库，SDK/模型/二进制不入Git。

## 撤回后构建和回归

- `py -3.11 -B scripts/acceptance/sr-visual-build.py`：最终撤回后16个EXE链接成功，`logs/sr-visual-quality-20261010/build-20261010-152739/receipt.json`；当前源/头依赖闭包编译或按SHA重用，旧缓存只读，不冒称所有第三方clean重编。
- `...build.py cmake-check`：`build-20261010-153030/receipt.json`，DeckLink原生/no-SDK两种配置及驱动对象、当前集成测试对象编译通过。此配置检查关闭串流依赖；实际GUI链接仍使用完整PS5/Moonlight/Xbox/libass配置。
- `...test.py sr presenter slider menu components lifecycle video rollback inactive inactive-node chain contracts hdr`：`tests-20261010-152929/receipt.json`，13组全部通过。包含三个SR实际执行合同、SDR/HDR设置事务、13项数字输入键盘、8项菜单布局、22项组件、呈现生命周期、用户GTA6热切换及失败回退、列表/节点关闭效果编辑各70项、chain287项、repair259项、HDR颜色合同。
- 末次仅改变本地构建标签及Windows版本资源：`...build.py ui-only`，`build-20261010-153535/receipt.json`，界面/文件属性均为 `2.0.6-fixes-20261010`。引擎/管线测试对象没有改变。生产版本仍2.0.6，不推送新正式版。

早期测试编译缺fstream等问题修正后重新构建；错误日志保留。局部静态算法的负收益没有通过删掉断言“解决”。正常播放未新增GPU像素回读、逐pass CPU等待或无界队列。每个测试进程≤300秒；质量并发试验不作为性能数字，最终回归时用户播放器已退出。

## 本地测试包

包目录 `E:/项目/Veyra/test-packages/fixes-20261010/`，完整NVIDIA候选 `Veyra-2.0.6-fixes-20261010-NVIDIA-win64-portable.zip`，另有当前应用源码和FFmpeg/dav1d/zlib对应源码。基于已审母包的1614文件清单暂存，不从运行过的APP目录复制；EXE/QML/必要shader/FFmpeg按原子替换，原件SHA不变。运行组件维持批准身份，社区NR的HashMismatch/NotSigned仍明确标注；软件没有恢复哈希锁。源码与运行二进制物理分离。

测试包保留此前设置切换恢复、手动数字输入焦点、长文字布局、关闭效果编辑、抗闪烁参数保存、PGS/压缩字幕、播放列表/PR31、Blackmagic原生开关、HDR补帧兼容开关及既有VFG修复。BMD/HDR两个兼容开关默认关闭。原尺寸VSR执行和FSR首帧修复属于先前有效功能修复，仍保留；不是本轮画质收益。

打包命令 `...sr-visual-package.py stage / smoke / seal`（依次单独运行）。使用独立data-dir、仅系统PATH与包内DLL分别做GPU/软件QML冷启动和GTA6实际播放，日志、QML缓存、截图、配置均在E盘外部目录，不修改产品QML。最终状态以同目录 `package-smoke.json`、`package.json` 和 `reports/sr-visual-quality-20261010/package-current.json` 为准，包含EXE/源码/ZIP SHA、来源提交、逐文件SHA/CRC、运行组件记录和原件核查。

封装前两种界面冷启动/实际播放均exit0，生产包文件前后SHA一致，`package-smoke.json`保留每次实际realPresented计数和命令。初次打包检查错误匹配了不存在的frames字段而断言失败，实际日志已经realPresented=460；修正脚本为核对realPresented≥100后重验两种模式，未改产品或降低为只检查进程退出。最终GUI SHA-256 `870623fec3b265bf9f3ce0837449e90934ec47164d22eb8b484e33ee95dbfc4c`。

本机没有AMD/BMD实卡；未重测全部串流/导出编码器，不声称反馈者黑屏/overlay问题或NV显存增长已根治。没有main合并、push、GitHub Release或关机。独立start保护45既有工作树、495脏文件和main `0e7018adab9b211d76dd05da40cfbaeb9d77cf2c`；源码commit/checkpoint、增量bundle及最终守卫见 `reports/sr-visual-quality-20261010/final.json`。
