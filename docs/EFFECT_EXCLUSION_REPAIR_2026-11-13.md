# 全效果剔除修复（2026-11-13）

起点为本地main 2271bc9。用户反馈画了两个区域、开关开启却无作用，并明确要求完成后关机；无push/Release授权。

## 原因与改动

- manual.log记录选区accepted=true，但16:17–16:18反复报EffectExclusion.dxil missing。Agent的私有stage继承旧包shaders而未同步本次build，漏了该文件，PresentBlit_ps也仍是旧版。这是交付遗漏，不是用户选区错误。已原子替换两项，全部88个build着色器逐SHA一致。
- 补齐文件后，GPU回归另抓到ANR生成帧未恢复原图，区域平均差57.55/255。ANR Vulkan桥接在命令列表提交时完成生成，同列表的遮罩合成提前执行、随后被覆盖。仅“ANR＋全效果剔除”先提交生成，再用同队列下一列表合成；不加CPU fence wait或未来源帧窗口，其余后端/关闭遮罩路径不变。
- 新增scripts/acceptance/sync-built-shaders.py，默认只检查，显式--apply才原子替换、避免写穿硬链接；目标app运行时拒绝变更，已实测该保护。开发指南要求手工stage也必须使用，正式package-qml-release.py原本就从build/shaders复制。

## 本次验证

源/build仍位于E:/项目/Veyra的worktrees/main-claude-anr-20261113、build/codex-main-claude-anr-20261113；本轮logs/reports/tests/tmp/archives任务名codex-effect-exclusion-repair-20261113。

```powershell
py -3.11 -X utf8 -B -I scripts/acceptance/build-recording-handoff.py --task codex-effect-exclusion-repair-20261113 --root E:/项目/Veyra/worktrees/main-claude-anr-20261113 --build-dir E:/项目/Veyra/build/codex-main-claude-anr-20261113 --label 2.1.5-main-integration-test --jobs 2 veyra_repair_fg_tests veyra_qml_ui veyra_repair_contract_tests veyra_preset_library_tests
py -3.11 -X utf8 -B -I scripts/acceptance/sync-built-shaders.py --build-dir E:/项目/Veyra/build/codex-main-claude-anr-20261113 --app-dir E:/项目/Veyra/tests/codex-fg-quality-labels-20261113/app --apply --report E:/项目/Veyra/reports/codex-effect-exclusion-repair-20261113/SHADERS-APPLY.json
py -3.11 -X utf8 -B -I E:/项目/Veyra/reports/codex-effect-exclusion-repair-20261113/gpu.py
py -3.11 -X utf8 -B -I E:/项目/Veyra/reports/codex-effect-exclusion-repair-20261113/gpu.py --extra
py -3.11 -X utf8 -B -I E:/项目/Veyra/reports/codex-effect-exclusion-repair-20261113/ui.py
py -3.11 -X utf8 -B -I E:/项目/Veyra/reports/codex-effect-exclusion-repair-20261113/verify-export.py
```

- 最终4目标构建28.4s；完善夹具后单目标4.3s通过。合同308/0、预设629通过；翻译2797条缺失/占位0。
- 11组GPU、66真实/80生成帧：矩形/椭圆/羽化/尺寸适配/SDR/PQ HDR/VFG3X/DLSS2X/ANR2X和6X/NR＋SR共存通过，D3D12错误0。独立无效果图为原图真值，真实B恢复原始B、生成帧恢复原始A；内部采样平均差≤0.424/255，区域外采样差0，椭圆框角落保持增强。羽化边界用独立线性光混合判据，增强与原图确有差异。
- 真实UI绘制矩形/椭圆、39px羽化、启停、切ANR、seek及导出通过，最终0ERROR。GUI真实ANR2X worker60源→120输出；FFmpeg独立解码120帧，PTS单调、源插值最大差0.333us；生成帧遮罩内与原始A的编码后采样均差0.80–1.48/255，区域外仍有31.67/255增强差异。
- 最终程序实启/正常退出0，0ERROR；活动app资产变更保护拒绝通过。19个ANR原件与104个QML模块文件核验，运行组件/用户配置/默认值/金红标注保持。

失败回执SHADERS-BEFORE、GPU-first、GPU-before-anr-fix保留。最初HDR夹具仅开HDR输出却给SDR输入，被产品合同正确拒绝；改为明确P010/PQ后通过。UI首轮漏--export-out进入目录选择并出现createDIB错误，仅终止本任务实例；保留UI-first.json及logs/ui，补参数/新配置重试0ERROR，未将失败轮次当通过，也未声称根治独立createDIB问题。

XeSS本轮仅图内真实帧/遮罩资源，未复验SDK生成帧UI叠加节奏；未测其他显卡实卡或屏幕光学延迟，不据此给出新画质评级。

## 交付与清理

原入口tests/codex-main-claude-anr-20261113/启动main整合测试.cmd不变，仍指向tests/codex-fg-quality-labels-20261113/app并保留原manual-profile。新EXE SHA256为00b39a4f89ad4d12a55abbdbab558a589ceb012927c7c52e958e73155b8d171f；旧3b5d20a3单EXE归档archives本任务，没有整工程备份。

临时QA app核对绝对路径、无reparse、无活动进程后退役1779文件；逻辑1,087,848,888字节不等于物理回收，E盘可用当时实测增加46,112,768字节。保留当前app/build、测试源与必要图片/输出视频/失败回执/配置。证据为reports同任务CLEANUP.json、DEPLOY.json、EXPORT-INDEPENDENT.json、FINAL.json。源码仅本地main，未push/发布；记录完成后按本轮用户要求正常关机，不强制结束其他应用。
