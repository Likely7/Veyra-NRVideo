# PR #80：HDR 支持（交付记录）

## 基本信息

| 项 | 值 |
|---|---|
| PR | [#80 HDR support: Dolby Vision (P5/P7/P8.x/P10), HDR10+, HDR Vivid](https://github.com/Likely7/Veyra-NRVideo/pull/80) |
| 状态 | OPEN（2026-10-29 创建） |
| 源分支 | `feature/hdr-support-20261029`（fork：`georgeg911/Veyra-NRVideo`） |
| 目标分支 | `origin/main`（`Likely7/Veyra-NRVideo`） |
| 提交数 | 248 |
| 规模 | +43,252 / −19,073（含下段说明的历史提交） |

## 提交构成（reviewer 必读）

248 个提交分两段：

| 段 | 数量 | 内容 |
|---|---|---|
| 前段 | 206 | **2.1.1 的合并**，其中包含杜比视界 P5/P7/L1 的**原始实现**。这些代码尚未进入 `main`，所以后一段依赖它们 |
| 后段 | 42 | 本轮工作：FEL 崩溃与配对修复、P8.4/P10 路由（含 P10 的 CCID 0 修复）、HDR10+、Vivid、跟随路径的三个 bug、五份文档 |

**只关心本轮的 reviewer 看 `20fae68f..HEAD` 这一段。**

## 为什么 diff 看起来很大

因为 base 是 `origin/main`，而那 206 个 2.1.1 提交不在 `main` 上，所以整个 2.1.1 的改动都算进了 PR。这不是本轮的工作量，但无法回避——后段的 42 个提交改的正是前段引入的代码，拆开就编译不过。

## 推送时的验证状态

| 项 | 结果 |
|---|---|
| 构建 | ✅ 5 个目标（app + 4 个测试二进制） |
| 单元测试 | ✅ 4/4（`veyra_repair_contract_tests` 306 checks，含 P7 重建与 ETSI CPU 参考的 parity） |
| 实跑 | ✅ 9 个样本 / 5 种格式，0 错误 |
| 工作树 | ✅ 干净 |

## 后续怎么更新这个 PR

**不要另开分支。** 在 `feature/hdr-support-20261029` 上继续提交，然后：

```bash
git push fork feature/hdr-support-20261029
```

PR #80 会自动更新。

**注意**：本地的 `merge-2.1.1-20261027` 和 `feature/hdr-support-20261029` 目前指向同一个提交，后续提交请落在 feature 分支上，避免两边再次分叉。

## 如果对方要求拆分

两条路：

1. **先让 2.1.1 进 `main`**，再提那 42 个（那时 base 变成新的 `main`，PR 只显示本轮改动）
2. 或者把 42 个 squash 成一个提交挂到 `main` 上——但这要求前 206 个已经在 `main` 里，否则同样编译不过

两条都依赖上游先接受 2.1.1，不是本仓库能单方面决定的。
