# F6 独立只读复核 — 2026-09-07

Reviewer：新上下文 `final_readonly_review`。接管基线6f0ebaa，主体为当前用户授权的本机软件交付合同，非原始规格所有项/公开发布许可。

初审 preflight/phase5=0/0，但 VERDICT FAIL：默认实时档误缩小4K图片；取消时编码回调捕获对象先析构；第二次音轨源读取失败静默降为无声。Maker修复后再次构建。

最终 GATE_EXIT=0/0，WORKTREE_MUTATED_BY_REVIEWER=no，VERDICT=PASS，未发现新的P0/P1。当前EXE SHA256=70DD23C44337004FC734FBAE8BB6139E185C52370AE97CCE6C3432A3C6F30DE1。

独立复验 `logs/delivery/9be0614da5d642e394a35c71d80f6207/result.json`，40.31秒。

- 所有联合短测通过，含真实NR/NVOF、GBV、4K输入实时FG播放、暂停定位、WIC、H264/HEVC原生4K音视频导出和解码。
- Live1080开启时，4K静态图保存仍3840×2160。
- 取消前实际提交43次NVENC EncodePicture，随后排空释放，预期exit3，仅保留.partial，没有成功MP4。
- tracked diff与四个修复文件SHA256在Reviewer运行前后不变。

证据边界：实卡、长时稳定、完整device-loss压力未证明；音轨检查失败分支完成代码复核，未故障注入。短测与本机交付PASS不等于公开分发许可、原始完整规格全部验收或与竞品画质等价。
