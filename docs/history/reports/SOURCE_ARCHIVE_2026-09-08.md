# 2026-09-08 源码存档

目标仓库：[Likely7/Veyra-DLSS-Video-Player](https://github.com/Likely7/Veyra-DLSS-Video-Player)。用户明确授权本次源码上传。此记录说明存档范围，不代表成品安装包或专有组件获准分发。

## 两份历史的关系

- 本地开发基线为 `c557d5f6237164610f323634594026ac7b7b42b1`，完整开发历史保留在本机 `agent/veyra-v1-loop`。
- 本次更新 README、保留远端既有 GPL v3 LICENSE、增加此文档和工作日志后，建立新的本地 checkpoint。
- 公开源码存档使用相同文件树，父提交仅为远端已有的 `8556fc7e5c03797f557a955c0172ceacc2a0b0b5`；本地分支名为 `codex/github-source-archive`，目标为远端 `main`。
- 本地开发分支与公开存档分支提交身份不同。源码树一致，但不会通过 merge 把旧本地历史连接到公开分支；无需重写或删除本地历史。

这样处理是因为本地 94 次历史提交中，存在一个已移除的测试视频和本地 depth manifest。当前 248 个文件不含这些路径；忽略规则只能防止新文件入库，不能使已提交的旧文件从历史消失。

## 包含与排除

包含当前产品源码、shader、测试 / 构建脚本、开发文档、23 份 Lucide SVG 及完整通知、GitHub 原有 LICENSE。README 说明实际功能、构建依赖、测试证据与尚未完成项。

不上传 NVIDIA DLL / SDK、RenoDX add-on、FFmpeg 二进制、测试媒体、截图、运行日志、个人配置、构建产物或完整本地旧历史。不创建安装包、GitHub Release 或自动上传 artifact 的工作流。

既有开发文档保留其历史内容，包括已被后续修正的状态、当时的本地路径和旧提交编号；当前状态以 README 与最新交付报告为准。本地 `logs/` 引用不是公开附件，旧本地提交编号也不保证能在公开仓库解析。

## 实际检查与证据位置

- `git status --short`、`git log`、`git rev-list --objects HEAD` 和 `git cat-file`：初始工作区干净；检查 661 个历史 blob、文件类型与受限路径。
- 对历史 blob 扫描常见 token、私钥头和凭据 URL 等模式，仅输出路径 / 规则，不输出凭据内容；本次未发现匹配。结果：`logs/github-archive/preflight-audit.json`。
- `gh repo view`、`git ls-remote` 和只读 `fetch`：目标为用户有 ADMIN 权限的公开仓库，原始 `main` 只有 README 与 GPL v3 LICENSE。
- 根目录及 staged NR SHA256、RenoDX add-on SHA256 与固定身份一致；不修改这些文件。
- 提交前检查 README 相对链接、代码围栏、`git diff --check`、图标 SHA256 与远端许可证原始字节。上传前检查公开提交的可达历史和文件树；上传后核对远端 HEAD、文件树与 README blob。
- 原始审计与最终提交映射保留于忽略目录 `logs/github-archive/`；这些记录本身不上传。

本次为文档与源码存档工作，未重新执行构建、RTX / NGX、采集卡或性能测试。最近真实 GPU Create / Evaluate 证据见 [UI v4 交付报告](UI_CONTROLS_V4_DELIVERY_2026-09-08.md)。整体仍为 Phase 7 / needs_review。

## 后续同步

继续本地开发可保留原分支。下次上传前应再次检查远端变化和当前源码树，从公开分支已有历史追加经过检查的更改；不要直接将完整本地开发分支合并到公开 `main`，也不要使用 `git push --all`、`--mirror`、`--tags` 或 force push。

此仓库沿用初始化时已有的 GPL v3 LICENSE。历史文档中针对闭源复用的讨论属于当时的约束和记录，不改变远端已有许可证，也不为 NVIDIA 或其他第三方组件新增分发权。
