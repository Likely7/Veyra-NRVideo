# 粉丝字幕与鼠标穿透补丁适配报告

已适配至 main b571a44 之后的独立分支 `codex/fan-subtitle-passthrough-20261016`。最终产品构建提交 `5f6a8a94ca860d3fa4fa9dba78f70e72534a57d2`，显示版本 `2.0.8-fan-adapt`。本轮没有合并 main、推送、发布或制作用户包；上一轮 NR 同步修复分支保持独立。

## 字幕

采纳粉丝发现的共享字幕缓存 64MiB 截断问题，将按需压缩缓存的默认上限提高到 **4GiB**。这是内存预算上限，不是启动时分配 4GiB，也不是预占显存。环境变量 `VEYRA_SUBTITLE_CACHE_MB` 支持 1–65536MiB，严格拒绝尾随垃圾、负值、溢出等输入并回到默认值。乘法从首个操作数起使用 64 位，避免 4GiB 在 32 位无符号算术中变成 0；并非必须写成单个纯字面量。

保留矩形尺寸、条目数量、坏数据、文本文件大小及取消保护。预算与拒绝次数写日志，重复的位图拒绝日志限制为每轨前三次，避免长片持续刷屏。

实际回归用独立编写的五轨压缩 PGS Matroska 样本：每轨 40 条，总计 200 条，实际位图运行数据 **78,643,200 字节（75MiB）**。同一解码器在旧 64MiB 预算下五轨后段均截断；默认新预算下五轨完整，119 秒处的后段字幕均可查找并展开为正确像素。另通过 PGS、SUP、VobSub、DVB、文本格式、字幕层实际栅格与异步取消/加载回归。没有拿这个样本冒充粉丝原始影片或任意影片均已验收。

## 游戏模式、鼠标位置与热键

没有直接套用粉丝的全局左上角视频布局。左上对齐只有在尺寸也一致时才解决坐标问题，而且会改变普通电影播放。现在仅在屏幕捕获游戏模式中，把画面宿主贴合被捕获窗口的真实可见边界，或被捕获显示器区域；按 Qt 实际缩放换算回逻辑坐标，再按物理像素取整。普通视频、专业模式播放条和原全屏布局保持原有规则。

窗口化游戏保持原位置与原尺寸，余下区域为黑色；要铺满屏幕，请把游戏设为无边框全屏。使用系统真实输入穿透，不新增向游戏注入/重发鼠标的处理。窗口移动/缩放的几何缓存沿用原 250ms 心跳，只在变化时通知界面；这个跟随周期不是额外的视频缓存延迟。

采用低层键盘钩子方向，补齐以下行为：

- 独立消息线程接收键盘事件，钩子只跟踪必要状态并投递动作，不在回调里运行 Qt 面板、窗口样式或昂贵操作。
- 区分左右 Ctrl/Alt/Shift，精确匹配组合，长按只触发一次，认领的抬起与按下配对，其他输入放行。
- 面板键只在游戏模式对应前台上下文接管；退出键在模式内可用。离开时卸载，旧会话/旧绑定的排队动作失效，监听无法启动则拒绝穿透。
- 设置 → 快捷键新增“游戏模式：调整画面”“游戏模式：退出鼠标穿透”。默认 **Home / Ctrl+Alt+Home**，支持自定义、冲突拒绝、保存、重启沿用和全部还原。捕获对话框与面板关闭提示同步显示当前键位。
- 不让游戏模式里的 QML Home 与全局钩子重复触发；保留普通视频全屏原有 Home 面板功能。Win 修饰键不会被录制界面悄悄丢掉再保存成另一个组合。
- 回归中发现 Qt 全屏切换尚未完成时就切成不接受焦点，导致重新进入时 `requestActivate()` 报错。改为全屏进入完成后排队执行一次游戏模式接管，退出仍立即恢复；最终日志该错误消失。

没有提高 33ms/250ms 刷新频率，没有修改 NR/SR/FG、源时间线、呈现调度或着色器，也没有新增逐帧 CPU 等待。新增线程和初始化/窗口转换有正常管理开销，未做性能百分比或端到端延迟收益宣传。

## 实际验证与证据

本机 RTX 5070、Windows HDR 桌面，私有配置；没有更改真实用户设置、驱动、SDK 或专有运行库。首次完整构建 12 个目标通过（119.1 秒）；修正后增量构建与最终 739 个输入/12 个 EXE 身份复核通过。每测试进程小于 300 秒、每构建小于 900 秒。

| 检查 | 结果 |
| --- | --- |
| 13 个相关回归进程 | 全部退出 0；包含实际键盘钩子、字幕缓存、格式/栅格/位图/异步加载、UI 合同、效果链、设置事务及 Qt Quick |
| Qt Quick | 76 passed，0 failed，0 skipped，0 blacklisted |
| 原生键盘 | 真实 SendInput 经过 WH_KEYBOARD_LL：重复按下只触发一次、上下文放行、左右修饰键、退出重入；接收窗口忙 1.5 秒后仍收到动作 |
| 实际播放器列表模式 | 实际捕获独立测试窗口，真实点击落在目标客户区 (100,100)，F9 呼出/关闭/重开面板，Ctrl+Alt+F10 退出与重入通过 |
| OBS 软件界面、125% 缩放 | 独立进程目标注册 Raw Input、禁用传统键盘消息；上述操作仍通过，无 QML/GPU ERROR |
| 原生窗口位置 | 初始视频与目标可见边界同为 (307,240)–(1193,803)，移动缩放后同为 (457,290)–(1443,903)，物理像素边界一致 |
| 偏好 | 自定义两键、9 类有效/无效检查、重启读取、恢复默认后再次重启、未知 sentinel 保存通过；完整结果见回执 |
| 翻译 | 2402 条源字符串，英/日/繁中缺译及占位符错误均为 0；英文快捷键页截图复核 |

最后的实际命令：

```text
python -X utf8 -B -I E:/项目/Veyra/tmp/fan-subtitle-passthrough-20261016/build.py --label 2.0.8-fan-adapt
python -X utf8 -B -I E:/项目/Veyra/tmp/fan-subtitle-passthrough-20261016/restage.py
python -X utf8 -B -I E:/项目/Veyra/tmp/fan-subtitle-passthrough-20261016/checks.py
python -X utf8 -B -I E:/项目/Veyra/tmp/fan-subtitle-passthrough-20261016/preferences.py
python -X utf8 -B -I E:/项目/Veyra/tmp/fan-subtitle-passthrough-20261016/ui.py
python -X utf8 -B -I scripts/i18n/extract.py --check
python -X utf8 -B -I E:/项目/Veyra/tmp/fan-subtitle-passthrough-20261016/final-audit.py pre
python -X utf8 -B -I E:/项目/Veyra/tmp/fan-subtitle-passthrough-20261016/final-audit.py post
```

实际回执在 `E:/项目/Veyra/reports/fan-subtitle-passthrough-20261016/{build,checks,preferences,ui}.json`。最终构建日志 `build-20261016-223446`，测试 `checks-223730`，偏好 `preferences-224017`，播放器 `ui-224204`，均在本任务 logs 目录。QML 抓图只验证界面，原生视频窗口不在 QML 抓图内，不能拿黑色抓图区域判断视频输出。

失败证据未删除：首次同进程 Raw Input 夹具的五项断言失败，多次调整唤醒方式和发键线程仍无改善；这个配置把原始输入注册到监听/接收测试进程自身，与捕获外部游戏的架构不同。最终改用独立进程 Raw Input 目标与真实播放器做主场景验证并通过。**同进程差异的底层原因没有进一步确定，没有宣称被修复。** 临时输入跟踪已从产品删除，产品游戏模式不采用该夹具的键盘 Raw Input 注册。中间 `ui-223056` 的 Qt 焦点 ERROR 已按上文修复，最终 `ui-224204` 不含 ERROR。原输入线程/消息唤醒猜测没有当成已证根因。

真实独占游戏、反作弊、提升权限的游戏、跨屏混合 DPI 和真实硬件键盘组合未实测；独占全屏仍需改无边框。原始输入游戏可能仍观察到物理按键，不保证钩子能封锁全部游戏输入，应选避开游戏自身动作的快捷键。[Microsoft 钩子线程与超时说明](https://learn.microsoft.com/en-us/windows/win32/winmsg/lowlevelkeyboardproc)、[Raw Input 注册边界](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-registerrawinputdevices)。

## 来源、保护与收尾

原补丁 16,129 字节，SHA256 `f0c74f69c37c82f3efeb4e7d89d972352fad12258328dc01d0e09237e7612b1c`；说明 SHA256 `a06ee46d6df240b2380ee08283b23088577c0ac28f4d313e236c0166aca53c16`。未署名作者不臆造；来源与适配区别写入 THIRD_PARTY_NOTICES。

修改文件为字幕实现/新预算头；GamePassthrough、新 GameHotkeys、QmlPlayerBridge 头/源和 QML 宿主；Main/Settings/DialogHost/Pro/Node 五个页面；catalog、CMake、两项回归与 PGS 生成脚本，以及授权、计划、报告、WORKLOG。没有修改其他增强/解码/呈现实现。

开工已存档 81 个旧工作树、93 个原分支、509 份未提交文件及附件，标签 `checkpoint/pre-fan-subtitle-passthrough-20261016`；修改前 bundle 74,957,056 字节且 verify 通过。收尾复核旧状态、附件/归档 SHA、739 个构建输入、12 个 EXE、1544 个共享运行文件；QA 注入恢复原始字节。增量源码 bundle 与最终回执位于本任务 archives/reports，结论以 `final.json` 为准。只有本轮独立分支推进，main 与此前 NR 修复保持。

源码与证据提交保存后按当前用户要求安排正常关机：后台延迟后执行 `shutdown /s /t 0`，不使用 `/f`，避免非零 `/t` 隐含强制关闭。实际安排回执为本任务 `reports/shutdown.json`；安排不等于已观察到断电。没有远端发布、代发消息或新用户包。
