# 分支与工作树归档
> ⚠️ 路径提示：文中 `E:/项目/Veyra/...` 是迁移前的记录（现为 `D:\Program\Veyra`），按日期理解；当前入口见 [DEVELOPMENT.md](DEVELOPMENT.md)。

122个旧分支的完整tip保存在下表及已verify的bundle中，89个干净工作树已经退休。远端分支和发行标签没有删除。

恢复原件：E:/项目/Veyra/archives/project-maintenance-execution-20261027/source-before.bundle。完整索引、脏树保全与产物恢复映射在E:/项目/Veyra/reports/project-maintenance-execution-20261027及同任务archives。

## 恢复方法

先验证bundle，然后把原ref取回；工作树另用git worktree add创建。示例（按表中具名分支替换）：

```powershell
git bundle verify E:/项目/Veyra/archives/project-maintenance-execution-20261027/source-before.bundle
git fetch E:/项目/Veyra/archives/project-maintenance-execution-20261027/source-before.bundle refs/heads/codex/release-2.1.1-20261027:refs/heads/codex/release-2.1.1-20261027
```

6个脏树保留原位，index/worktree patch和未跟踪文件在dirty-files.zip中逐项SHA校验；不要直接把备份压到现有源树。退休构建/测试包的脚本、配置、日志和必要EXE在retired-artifact-evidence.zip及restore-map.json里按SHA寻址。已删除的完整运行副本按正式供体/固定依赖重建，不把删除说成完整打包存档。

## 保留项

main为产品基线；本轮施工分支验证后本地整合。6个旧脏树、Lecram两条暂停线、C盘带忽略文件的旧诊断/owner基线和既有Codex托管的脱离分支工作树保留。共用Git库仍在桌面.git。

## 已退休分支

| 分支 | 完整tip | 原先是否为main祖先 |
|---|---|---|
| claude/aa-53-20261017 | 6571e5d37765a40f403adbc9400bf51928a7d2f6 | True |
| claude/amd-sr-off-20261022 | 1b94bae8e162266f7702455c96e906ec296a41e1 | False |
| claude/audio-reader-20261011 | 87ca1e506ecd1cd56a1d4b067ce11f28fcda1848 | True |
| claude/beauty-20261017 | 390882eb7f5d40828425a5ffb010e43042bb0b96 | True |
| claude/cadence-exact-20261012 | 8d11cadbe0af1a5b13420db24fa735bce9a2d7fc | True |
| claude/capture-review-20261026 | b2eb77dfe7c25bcc595d82970f7d95ba977171f9 | True |
| claude/dedup-cadence-20261011 | 67574f265f1d041795a9363cbdf98ad4fe2d3273 | True |
| claude/effects-infra-20261017 | f01bf44544719c620b53aa0ee081eddbb125ab13 | True |
| claude/elgato-hdr-20261011 | 147e76ccd5f9a97d10d14a33a16071257daffa6c | True |
| claude/fg-gpu-home-20261014 | 6a97dc1ce8e967022e3a36b474adf38b487b00cf | True |
| claude/field-20261015 | 18fa6f3abfab21f1e15aa8eb7be8edc730e84a96 | True |
| claude/field-fixes-20261005 | 6e9d8273c1d3ae5182d07cfec8c370b68bb4da23 | True |
| claude/freeze-fix-20261010 | d0694d71d7317581ffbab2bb1e9153dae58c14ad | True |
| claude/latency-min-20261013 | e550f966117c8eabdbe3b3b0794b73b2f85710a8 | True |
| claude/network-buffer-48-20261017 | 5d9e57f8baf53eb8a69193ed91c09d70b9944ec7 | True |
| claude/pipeline-20261010 | be834fdc9a174de8f9098f27efc1c15c7d8f93b0 | True |
| claude/release-2.0.8-20261015 | f074e408b9cc3d8a9ca7bd30498c4114ffa72188 | True |
| claude/release-2.1.0-20261019 | e3453c2aa8f75f5674211c35401cf43ff1316f1d | True |
| claude/rtss-compat-20261003 | 460aea962e770b1620869fa6c88fabb65409fae2 | False |
| claude/screen-recording-20261022 | aa2773280ed1a78990062c9bea73eeae2088dd70 | True |
| claude/software-ui-default-20261026 | ea148bf2c8d66aa16f5681087be5d195552b858c | True |
| claude/sponsor-20261019 | dcd95b6401771ebdff75c30ed73a55b87e1abb4b | True |
| claude/ui-smear-20261011 | 5ba186a91e868c28b4b5ec46c9166aa321d90998 | True |
| claude/vfg-20261010 | d3803ab74de1422dc6366d034e5c9aea9d61b81c | True |
| claude/vfg-mixed-20261011 | 1d4c33f5fe2415d13bb4bfd7a48a6b1657a81c81 | True |
| claude/vrr-event-query-20261025 | 309bd58cde86c48ba7d9c650df6e74ca947d2351 | True |
| claude/xess-flow-tune-20261012 | e46bbdf08569e740c80f636e3c910a3d0c51e553 | True |
| codex/amd-nr-044-20261018 | f6c4fd9259bd36c774b3f553f3760a181c90789c | True |
| codex/amd-nr-fsr-handoff-20261007 | bf6e6517a62abc94ea8de043b8db938be0a8638d | True |
| codex/article-2.1.0-20261019 | e7eea6c2bc2cc09f74c72b73d08082153aa8b780 | False |
| codex/beauty-node-plan-20261016 | b3d95e3e5650c114701ff2bface07c7d8c30973e | False |
| codex/beta-latency-ab-20260918 | 24bc11a3b4956ac1d402bc64bb2d1628552db65d | False |
| codex/blackmagic-capture-20261007 | 42943410d5e306eb18880f615b774cb200395c1a | True |
| codex/blackmagic-followup-20261009 | 886b461bb0ae5db2b5bf36588c9e1b112685f2c3 | True |
| codex/blackmagic-signal-research-20261009 | 1d7cd55c7790cd3705caa88f99dd4926addeb2f4 | True |
| codex/build-2.1.1-20261026 | 2de14fdacadae51ef0445757b484c442371d1733 | True |
| codex/build-2.1.1-resume-20261026 | 00d7d2db2f282ecada1c0afb6b086b0ce14f2877 | True |
| codex/cadence-status-20261011 | 4557ab64e4fb6e53769d3b9360b88d479f41d716 | True |
| codex/capture-audio-main-20261025 | 68def10a9c5cdc3af626a0b784029275ebda323f | True |
| codex/capture-review-handoff-20261026 | 26aa9dd786dc2afaadf578e12fa76e5f8c493059 | True |
| codex/capture-xbox-field-20261005 | 81b755bf012fe7e0af55fb42e7ea1c3e8ed31ed3 | True |
| codex/claude-handoff-20261022 | 4eee190617615bbd55fea2fc84d171ec14639df1 | True |
| codex/community-qr7-20261020 | 4ae285b0cc846c9ecbfc1e4e8751898e7cfb1ff7 | True |
| codex/decklink-native-20261009 | af2edf4052857c7db99d7c33f92827f02ab34319 | True |
| codex/dlss45-sr-handoff-20261011 | e8300d90c2a3cddaef3e92ae8a989105647ba16c | True |
| codex/doc-cleanup-vram-amd-20261020 | 674536857305afad029738068b91500bc984ee2b | False |
| codex/export-nr-scale-20261025 | 207555bcacf0c590f569e098c439d366148e1420 | True |
| codex/export-page-20261002 | e892c6a1998415b3623147041e772db474e1fc0b | True |
| codex/fan-subtitle-passthrough-20261016 | ab567d3c129db86c0fdad7a47dd682c01425b650 | True |
| codex/fg-capture-recovery-20261012 | 34f479e6f95229cd45fd81780eb874d8472e1f30 | True |
| codex/fg-flash-fix-20261009 | f6a8fa270773e9a2af03632750cbd3f18de7b462 | True |
| codex/fg-scheduling-repair-20261024 | 038beacd51b1274da8ff76263abc6f724c0a1942 | True |
| codex/fg-source-flash-20261008 | 3fd3cde2b04fe4117eb544ba24f809ebc97fc185 | True |
| codex/field-2.0.1-20261002 | 15a0e39a34e27e278a4b3aba56cef6260b905575 | True |
| codex/field-issues-20260930 | d383baaf8e56270b88c16488a0f5d201d81d9c05 | True |
| codex/field-upgrade-20261003 | a93af429930fd83563e0e4f1ae4e3c6a24623b25 | True |
| codex/fullscreen-input-20261018 | 3a6ac1d4fa8d8bb06aed82679319247a88f8debe | True |
| codex/github-source-archive | 2d2a3a6c5c43b3ce19f0555edd44d979564d13d2 | False |
| codex/inactive-effects-20261010 | c664a675c7b7a5c6cb54ed79c07b8a72127f4dfb | True |
| codex/issue67-nvapi-shutdown-20261021 | 619921298136fe162310596a28fa55461e327546 | True |
| codex/issues36-37-bmd-20261013 | 345f87fefffa82a5d3c779a4cd958df9a726b701 | True |
| codex/issues41-45-20261016 | ab0a266df31416e4435538c72028920406d13870 | True |
| codex/latency-chain-audit-20261013 | f91c7715d71530478364303c41600f05b36bf28f | True |
| codex/list-preset-flow-20261006 | 0a4e761931ccf87edec07d5a56f05ce6ce67a702 | True |
| codex/menu-amd-hdr-fix-20261026 | 41488c207070165cc9b80dfda6d8ecddea1ff51e | True |
| codex/merge-claude-fixes-20261011 | 9c14ee28a9175aa326fb0aa09c15bfa5808e0f0e | True |
| codex/merge-current-fixes-20261011 | adcf509e8a752d8a60645c894262cd5dd43f33ad | True |
| codex/minimal-edge-hdr-review-20261004 | fc2ca7163e986f97e53dcab51ffe1ac77bb0615a | True |
| codex/nr-manual-scale-20261024 | 97959cc840698243f76d3005a7906e4eb3a134aa | True |
| codex/nr-sf-310916-20261017 | bfce70be7b61845dda1d7b3fe1a7cdb8fd5b228c | True |
| codex/nr-strength-protection-20261005 | 33d6685a3a75d3d4e8ac5d32996b163ef1f488dc | True |
| codex/nr2060-sync-20261016 | 4a2c8e8a0fc8fe8546d6e39474bd8799757f6d3c | True |
| codex/nv-vram-deep-20261007 | 71483d56f17d85784f6ff1cbb40f83b90747b0d4 | True |
| codex/nv-vram-field-20261008 | a210ec18a4affc930f23f8b6ae48c2c85b7b4d11 | True |
| codex/obs-export-amdnr-20261006 | 63ce3964d9bca0baca70b619b294f6a0ef25e9d9 | True |
| codex/perf-nr-20261004 | 2dbf26cfe5d5dfe4491d68415064a8a0588b459b | True |
| codex/picture-rotation-20261023 | 108c010123aee036ced42ec7bc5cb794a8208cde | True |
| codex/playback-smoothness-20261004 | 8cdc612120cbf23ba116a33c3cb0a53e2043f718 | True |
| codex/player-startup-speed-20261003 | bfafbea844c7cc65ededd11303ca23f93e3d176c | True |
| codex/pr19-original-20261006 | b14dc5a43dc008a1a8f4fa521d5a5a0fd41cc95c | True |
| codex/pr19-pr20-20261006 | 578d63c3a0143429b306e26eeb89137473b4d99f | True |
| codex/pr20-original-20261006 | 979ee3f46e64430120acc14bf2d5a61acd41a729 | True |
| codex/pr31-playlist-20261009 | 973fc9b816602ed6f11a40e88a54fc8913a2a87c | True |
| codex/pr34-capture-settings-20261012 | 49eb6bdd04db6effe37ada36c846d603f4b85282 | True |
| codex/pr44-hdr-display-20261016 | b571a44b821c7981b107593e4c4b72bad521813d | True |
| codex/pr57-capture-window-20261017 | 26c0d61c395ad983634c40b917a552cdea4afbf1 | True |
| codex/pr63-avermedia-audio-20261018 | 786efeb2c99d2455c5101e9e10fc832e7aa364b7 | True |
| codex/pr64-avermedia-20261019 | 1dc8c18869c25e64558982e425cf0f5d8cc14020 | True |
| codex/pro-video-geometry-20261012 | d8832162ee0ab23a568db3a06332bbc9754ce1c2 | True |
| codex/publish-2.0.4-20261006 | f8045fb53a7d800b053f0631a9b44dc9f5f1aacb | True |
| codex/publish-2.0.5-20261007 | e44e828119710b8f031cc968e8ddb1982aecc873 | True |
| codex/release-2.0.0-20261002 | ddf5a2dae3305ef5b5da24b0461b87a4150df369 | True |
| codex/release-2.0.2-20261003 | 0dafc57e7ad54880b1748e7046970ddc825e7e46 | True |
| codex/release-2.0.3-20261004 | 354b1c6e39ba0f62b2e6aac4c161f682dea9c2ff | True |
| codex/release-2.0.4-20261005 | d2e589a5965b614844dd0f7159b979563e11b1ca | True |
| codex/release-2.0.5-20261007 | 4deb4793763c4c6197876e84d25140943b284856 | True |
| codex/release-2.0.6-20261007 | e1f6952a6ab11483fc6ff67177d8d390a8faea2f | True |
| codex/release-2.0.7-20261013 | ae7762b24f52f181100ef5026d85b92630659b8d | True |
| codex/release-2.0.8-20261015 | 0cfa8a8c8770bd394ed9df923c8d86724590e560 | True |
| codex/release-2.0.9-20261017 | d68d6be9c37127350698a1d4d47fc01b36b29475 | True |
| codex/release-2.1.0-20261019 | 3b295e89b5151c69a121ffc9e198c505bb9ac7ee | True |
| codex/release-2.1.1-20261027 | 2fecacfedd05266ca98ad6efa46e4ef4d0be0365 | True |
| codex/rtss-restart-loop-20261004 | fb8e5001a7ba8424aeddda002d9ca19a79e68f2f | True |
| codex/runtime-size-20261004 | cb9b89e4259d1c430f40f976997f6155886bff46 | True |
| codex/screen-recording-20261020 | aa2773280ed1a78990062c9bea73eeae2088dd70 | True |
| codex/settings-sr-ui-20261010 | 30c6f6095fc099963908945fcb1f11a3c84de5ff | True |
| codex/smooth-motion-experiment | 27c17ebb37669dee503d4a5f2c5fee7aa8f24e27 | False |
| codex/sr-visual-quality-20261010 | 48d8f3464d64ef5e7ee672bbca3526d1bccfca7e | True |
| codex/stability-export-priority-20261006 | aeb544cb63718787b9488be283ae54fc6ec3cc13 | True |
| codex/subtitle-formats-20261009 | 27cf4b4d89bd4ccb2c40b4cb63a67969cf839ed2 | True |
| codex/test-2.0.7-20261012 | 5b868e7f16b60b190f042de9b4c2ccf5ed5ebb23 | True |
| codex/test-2.0.7-refresh-20261012 | a94a1cec10f371dcc485e0b61d2d3edcfe3952ec | True |
| codex/test-2.0.8-handoff-20261015 | 3107e0076a0a8ad3cfdee767a0ea36152ed402a4 | True |
| codex/ui-overlay-release-20261002 | aafdeb061e45ae8cdaebfaf68e16c3d589771c05 | True |
| codex/ui-smear-handoff-20261011 | 29e33dfd5a8aa88cba9685009a4488d8e96ffc97 | True |
| codex/update-check-20261013 | 4d944dec4097075573c9f0120da91a6777ace0d9 | True |
| codex/vfg-diagnosis-20261007 | aec800e7a9729efd474a66ae53be4f4e3bf0c2a0 | True |
| codex/vfg-integration-20261003 | a927f92c6a35522a6081b61b00fc37a35bd5d988 | True |
| codex/vfg-next-20261007 | 71723e0249a17764696796fd204a6eb69d6da276 | True |
| codex/vfg-optimization-20261007 | cc22e88c3107900003c6b9e0cb58f187e1c9914c | True |
| codex/vfg-research-20261003 | 1802f43565e07f3c3040d3fe745ca2e939aab00f | True |
| codex/xess-handoff-20261012 | f0fafdfde9833f02d313830e399e65a52aeaf820 | True |
