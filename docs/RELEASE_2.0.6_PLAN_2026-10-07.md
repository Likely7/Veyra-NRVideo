# Veyra 2.0.6 发布执行计划

用户于本轮明确授权构建、合入 main、推送并正式发布 GitHub 2.0.6，替换满员的 4 群为用户提供的 5 群二维码，发布后提供群公告。无代发群消息或关机授权。当前指令覆盖历史 UI-only/禁止合并发布限制，仅限本次已完成修复及版本发布。

## 固定输入与范围

- 发布工作树：E:/项目/Veyra/worktrees/release-2.0.6-20261007；分支 codex/release-2.0.6-20261007。
- 起点 bf6e6517a62abc94ea8de043b8db938be0a8638d；main/远端 nrvideo 起点 af5bfc3a66afce3047868229a3070a91c0fc9c22。
- 产品包含已完成 VFG 优化 ddf71b1、Blackmagic/HDYC 兼容 4294341、NR-first→FSR/blit 输出传递 bf6e651；本轮不继续修改引擎。CMake 只改数值/显示版本到 2.0.6。
- 运行库、模型、patched FFmpeg 与许可证沿用正式 2.0.5 两厂商包，逐文件校验；不加入测试 NR provider，不升级二进制组件。
- 新群码原图 SHA256 5cb236ce664cd523681a6a4fa83180d2ada6d9fe7209a70921a0d95d93fd62bd，原字节复制 docs/images/2.0.6/community-group.png；图片标注 5 群、2026-10-14 前有效。旧微信赞助、Discord、Ko-fi 保留，双二维码 width220。

## 执行与验收

1. 独立 start.json 固定开工源码、33 个其他工作树、495 个已有修改、3678 个旧运行组件/证据文件；source-before.bundle 及旧 Release API 回执保存在 archives/release-2.0.6-20261007。release-2.0.6-control.py 每步复核，不重写旧 baseline。
2. accepted 2.0.5 dependency cache 配置全新 E:/项目/Veyra/build/release-2.0.6-20261007，构建产品和相关测试。所有 tmp/log/tests/release/verify 均在 E:/项目/Veyra/ 对应任务目录，子进程私有 TEMP/TMP/profile；不覆盖用户配置。
3. 串行执行 AMD NR→FSR graph、ABI、效果链、HDYC/格式/GPU 颜色、VFG GPU/settings/短视频导出及 GTA Normal 预览回归。每次测试不超过 300 秒，构建不超过 900 秒，不同时跑 GPU 工作。
4. README 仅替换当前版本/最新更新/下载链接/群码，详细双语 Release Notes 记录收益条件、原始统计定义、未验收硬件和已知问题。新鲜回归与既有匹配性能对照分开。
5. 干净源码提交后 --no-ff 合入 main，树必须一致。输出 NVIDIA/AMD 完整便携 ZIP、应用源码 ZIP、依赖源码 ZIP、SHA256SUMS 共 5 资产。逐文件 manifest/SHA、PE 依赖闭包/厂商隔离、ZIP CRC、最终 ZIP 独立解压冷启通过。
6. 只向 nrvideo 普通 atomic 推送 main 与 annotated v2.0.6；先草稿上传 5 资产，核对服务器 SHA256 digest/大小/正文/标签，再发布为 latest 正式版。保留旧 2.0.5 正文/资产不变；核对公开下载、群码原字节、赞助图片/按钮可达。
7. 实际发布完成后仅在 main 追加状态/工作记录/发布回执，不改标签/产品/资产；最终 guard 及远端核对后交付不超过 1000 字群公告。

## 不能推断的结果

RTX5070 上的真实 FSR3.1 + 测试 NR provider 像素回归只验证图的输出传递，不能冒充 AMD HIP/FSR4.1/Xbox 实卡验收。没有 Blackmagic 卡，不宣称真实 HDMI 成功。VFG FPS 为软件呈现提交统计，不是物理刷新率或屏幕延迟。高倍率 GPU 工作仍可能超预算。NVIDIA 显存持续增长未定位/修复，不能写成已解决。
