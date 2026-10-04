# 2.0.3 显卡分包、现场修复合并与发布方案

用户本轮明确授权：分别打 AMD/NVIDIA 包，排除对该显卡无效的运行内容；软件功能保留并置灰；规则写入 AGENTS.md；将此前完成的几项修复合并 main 并发布 Likely7/Veyra-NRVideo 2.0.3。HDR/Dolby Vision PR #13/#14 明确暂缓，既有静态研究文档不是施工授权。此前关机请求已经执行，本次不重复关机。

## 基线与存档

- 开工产品/文档基线 cb9b89e4259d1c430f40f976997f6155886bff46，包含 Xbox、导出码率、RTSS、不再误判 NVIDIA App 插件、AMD NR、VFG 全倍率/质量、极简像素缺口及依赖瘦身。
- 分支 codex/release-2.0.3-20261004；沿用 E:/项目/Veyra/worktrees/field-upgrade-20261003。桌面旧工作树仍有用户改动，禁止切分支或覆盖。
- 开工 tag checkpoint/pre-release-2.0.3-20261004；完整 bundle E:/项目/Veyra/archives/release-2.0.3-20261004/source-before.bundle，verify 成功，SHA256 06A8A2EBCD2F8580E7583342696986EAADA80C3B244400213C3E23461BFB0E93；桌面 working patch/status 和本地 main status 同目录保存。
- 本地 main 66cd3e5；远端 main 712daf3 只调整 README 语言链接位置，已 fetch 并在发布分支保留该提交，不能覆盖其改动。后续 push 使用常规快进，不 force。
- 产物统一 E:/项目/Veyra/{build,tests,logs,tmp,releases,verify,archives}/release-2.0.3-20261004；每测试子进程最多 300 秒，每构建最多 900 秒，TEMP/TMP 只设子进程。

## 修改与验证

1. 审计现有 runtime 锁、AMD/VFG manifest、PE imports/delay imports 和动态加载路径，建立 NVIDIA/AMD/公共依赖清单。两份包使用同一生产构建，包含公共文件/Qt/完整基础功能；拒绝另一厂商专用内容。NVIDIA 不带 AMD NR/HIP，AMD 不带 DLSS/NGX/NVOF/VFG/CUDA。用户追加决定保留现有 FSR3.1/4 共用组件，两包均使用已核验 SDK 2.3.0 三 DLL；NVIDIA 的 FSR4 ML 仍可见置灰。旧 SDK 独立 FSR3.1 研究与兼容试改已撤下，不进入最终产品。FSR3.1/XeSS 等跨厂商支持按实际契约保留，不因品牌名删除。
2. 修复列表/节点/NR/SR/FG/Video HDR/光流选择的隐藏行为，使缺失库或不支持硬件的功能可见且置灰，说明真实原因。复用统一能力判断，保留 settings/preset/session；直接接口也拒绝不支持的请求，不将 UI 灰色当后端保护证明。
3. 建立可复用分包脚本/每包清单与运行身份记录，并将长期规则写入 AGENTS.md。更新 2.0.3 版本、双语 README、Release notes、组件说明、实际构建/对应源码指南；继续携带 fixed runtime identities、patched FFmpeg 及许可。AMD/VFG 的新运行内容只作 Release 资产，绝不进 Git。
4. 编译当前生产目标及相关能力/设置/Qt/Xbox/音频回归。实际 NVIDIA 包测试 NR+SR+VFG 2–8X/三质量/worker 冻结、旧码率交互、极简边缘和软件 UI 不透明。AMD 包在本机验证无 NVIDIA 文件的干净启动与能力禁用、宿主 ABI/模型内核身份；没有 RX9000，不宣称 AMD 推理通过。
5. 提交后在干净 main 工作树合并当前发布分支，检查源码差异和 no-HDR-PR 范围；用最终 main 构建或验证产品源树与实跑构建一致，生成两份最终包、项目源码及最新依赖对应源码，压缩/解压校验、可启动、版本和 manifest 全部核对。
6. push main 和 v2.0.3 到 nrvideo，创建 Release 并上传已核验资产与 SHA256。Release 保留微信赞助/交流群两图各 width=220，验证远端正文/图片可访问、所有资产大小/digest。记录准确测试和未测边界；不把自动重连宣传成网络根治，不把 8X High 宣传成实时 240fps，不借本机 RTX 测试冒称 RX9000/RTX40 或用户 616.92 已验。

## 当前状态

开工存档、新分支、能力 UI、显卡分包脚本、构建与本机定向回归已完成；FSR 原共用组件按追加决定保留。实际证据与首次失败修正见 RELEASE_2.0.3_ACCEPTANCE_2026-10-04.md。当前进入最终 main 合并、无损归档及 GitHub 上传，最终状态以 WORKLOG 发布回执为准，不能用方案推断尚未执行的上传已通过。
