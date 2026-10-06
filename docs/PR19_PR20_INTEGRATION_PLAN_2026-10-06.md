# PR #19 / #20 审查、适配及合并

用户已测试2.0.4-fix1并确认9000系NR可用，随后要求检查PR19/20并做好适配。该反馈只覆盖用户实际测试设备，不代表全部RX9000型号。本轮从已交付现场修复 `63ce3964d9bca0baca70b619b294f6a0ef25e9d9` 开始，保留OBS软件UI重绘、Native多层NR导出及AMD合成器修复。

工作树 `E:/项目/Veyra/worktrees/pr19-pr20-20261006`，分支 `codex/pr19-pr20-20261006`。archives/build/tests/logs/tmp/verify/test-packages均为E:/项目/Veyra下同名任务目录。不可变start SHA256 `f3d49a940e731ef7412d83666525cc37f08464f7a48ad226b9f9a286e947c771`；已保存24个原工作树的HEAD/status/修改SHA及原字节ZIP、四个原发布ZIP身份、source-before.bundle和两个PR的原diff/提交/评论/评审元数据。bundle verify成功。原桌面、用户配置、其它工作树及运行库字节保留。

## 固定审查对象

| PR | 固定head | 内容与初始状态 |
| --- | --- | --- |
| [19](https://github.com/Likely7/Veyra-NRVideo/pull/19) | b14dc5a43dc008a1a8f4fa521d5a5a0fd41cc95c | 静态HDR输出曲线、HDR10 metadata、七预设、强度和显示峰值；有冲突，不能直接并入 |
| [20](https://github.com/Likely7/Veyra-NRVideo/pull/20) | 979ee3f46e64430120acc14bf2d5a61acd41a729 | MF编码器宽字串UTF-8日志和关闭RemotePlay时PIN接点；GitHub可合并 |

正确远端nrvideo=Likely7/Veyra-NRVideo，开工远端/本地main均为f8045fb53a7d800b053f0631a9b44dc9f5f1aacb。PR描述及作者测试是待核实输入，不能代替本轮证据。现有PR13/14继续暂缓。

## 实施和通过条件

1. 先审查两原diff及当前配置版本、呈现颜色契约与metadata所有者，再按原始head合并，保留作者历史。先PR20后PR19；冲突按当前产品语义逐项解决。
2. HDR曲线只在最终预览输出的线性nits域生效。默认恒等、SDR旁路、导出不继承显示器调优；检查参数非有限值、强度插值、峰值、单调性和root constant尺寸，不能破坏其它8常量图形pass。
3. 更新配置版本并支持此前真实版本。HDR参数/强度/显示峰值保存与恢复保持一致，不能丢NR强度5、三风格调控、独立抗闪烁、效果顺序及列表光流/节奏设置。源切换/reset/resize/SDR↔HDR不得沿用前一来源的metadata；检查swapchain重建和显示器变化。
4. 标准生产构建及RemotePlay关闭构建，定向HDR单元/真实GPU shader/预设和会话回归；正常UI及多层导出、AMD identity共享图短测，顺序执行GPU场景。不改Windows HDR全局开关，不把软件读数/RTX夹具冒充物理HDR或HIP实卡验证。
5. 对最终源码/EXE/证据冻结SHA，按改动完成对抗性复核、记录失败及修复。只有实际通过的完整实现才合入main。合并前复查main和PR heads未被其他人更新；禁止force push。保留原始PR heads为main祖先，让GitHub识别合并，并核实远端树/PR状态。

本轮允许必要提交、源码bundle、main合并及普通源码推送；不创建Release或修改v2.0.4资产，不代发评论、评审或任何贡献者消息，不关机。测试≤300秒/进程、构建≤900秒，无压力或竞争程序。所有命令、日志及未验边界记录WORKLOG和下方实际进度。

## 实际进度

开工保全已完成，guard和计划已建立。产品审查、适配及本轮构建/测试尚未完成。
