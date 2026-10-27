# NR先行低延迟预览与悬停说明

用户2026-09-12授权。开工存档：`checkpoint/pre-nr-before-sr-2026-09-12`。当前实现完成并通过下列本机检查，未发布。

## 行为

专业增强页的NR处理档位下新增“低延迟模式 · 实验”，默认关闭。只有同时实际启用NR和超分放大时改变顺序：

- 默认：源颜色→SR→NR→残差/保护→FG→显示。
- 开启：源颜色→NR→源尺寸残差/保护→SR→FG→显示。

光流仍从源画面估算，并按NR与最终输出尺寸分别适配；不是从处理后的画面重复估算一遍。NR原生档使用该阶段源尺寸，实时档仍受1080内部处理上限约束。输出仍为选择的2K/4K/8K上限；不是先插值放大再谎称超分。RGBA16F线性残差送DLSS SR；RTX Video SR使用现有显式编码/解码转换。共用SR调用、NR/FG及呈现循环，正常播放没有像素回读。

切换会排空旧任务并重建依赖尺寸/描述符/历史，短暂停顿正常。仅预览更换顺序；图片和视频导出维持默认完整路线。对照底图在NR先行路径为源图缩放，不宣称是独立无NR超分对照。没有超分放大或关闭NR时，低延迟选择不改变实际路线。

画质/延迟收益不是保证。尤其原本已用实时1080 NR档时，收益可能小；低尺寸NR的错误可能被SR放大。拖影/边缘瑕疵与真正显示撕裂不是同一概念。没有声称达到游戏原生集成画质。

预设格式11追加lowLatency字段；旧1–10版本默认关闭，默认设置false；保存的用户预设可记住选择。

## 悬停说明

专业设置的按钮、下拉、数值框及滑条通过SettingHelp.h提供具体说明，覆盖NR、SR、光流、补帧、音频、保护区、模型参数、预设、导出等。主界面常用增强/播放设置及采集、PS5连接面板也补充说明。深色小浮窗约550ms触发、15秒消失；滑条纳入提示注册。语气轻松，但实验与未证实参数、SDK限制及取值范围明确说明，不把“开最大”冒充最好。

## 代码与证据

- EnhancementSettings/ResolutionPlan/EnhanceGraphDesc增加可选顺序；EngineController负责初始、设置事务、串流尺寸变化时同步计划。
- EnhanceGraph复用runSr逻辑，NR输入与残差按源尺寸绑定，输出合成改取后SR结果；默认路线保留。
- SettingsWindow、AppShell、CapturePanel、RemotePlayPanel及SettingHelp.h提供开关、提示和注册。
- PresetStore v11与迁移回归；RepairContractTests增加预览/导出尺寸边界；LivePresentationTests增加 `--nr-first` 真GPU验证。

构建：`out/remoteplay/build-extra-delay.cmd`，最终记录 `logs/nr-first-final2-build.log`。90项contract通过，`logs/nr-first-contract.log`。预设完整字段roundtrip及42组旧后端迁移通过，`logs/nr-first-presets-final.log`；早先测试仍断言写入版本10而失败，已更新预期11并保留 `logs/nr-first-presets.log`。

实际RTX5070测试：`veyra_live_presentation_tests.exe logs/ps5-p1-1080p30-long.mp4 logs/nr-first-integration --nr-first`，9项exit0：NR先行RTX Video SR+FG、实际NR源尺寸、4K PNG、换DLSS SR、seek、关闭低延迟恢复4K原生NR、停止。`logs/nr-first-integration/engine.log` 明确记录pipeline-order和实际Evaluate；RTX Video SR阶段样本P95约2.03ms、NR约5.88ms，不作为端到端延迟结论。实际3840×2160输出PNG已查看，非黑图，仅诊断保存允许回读。

统一回归 `scripts/gates/delivery.ps1 -Root $PWD -BuildDirectory out/remoteplay/product-repair -PlayerExe out/remoteplay/product-repair/veyra.exe` 48.25秒PASS，`logs/delivery/2cb7655434ef4cf6a5d71dfad282a8e4/result.json`：默认NR/NVOF/GBV/颜色/播放/图片/视频音轨导出通过。之后只补充串流动态尺寸时的路线更新及状态文案，最终重建和UI检查通过；没有用旧gate哈希冒充最终EXE哈希。

UI检查 `out/remoteplay/test-low-help.ps1` 通过，`logs/nr-first-help-ui.log`：低延迟控件存在且默认关闭，tooltip窗口存在，原模式/全屏/PS5面板与精细面板切换通过。未对每条提示逐条截图；未做真实PS5、采集卡、新路线8K或长期画质对照。本轮下一步为用户实际低延迟开关A/B验收，不发布、不上传SDK/DLL。

## 悬停注册修复（2026-09-12）
用户实测发现无说明。旧测试只确认窗口存在，未验证注册或实际弹出；当前环境完整 TOOLINFOW 大小被拒绝，工具数为0。改用兼容 V2 大小，嵌套控件直接父窗口及静态文字，失败记录日志。最终实际鼠标悬停低延迟控件，工具数103、浮窗可见，中文截图已检查。构建 logs/hover-final-build.log；悬停 logs/hover-visible-final.log 与 hover-visible.png；详见 WORKLOG 同日修复记录。未逐一截图所有设置。
