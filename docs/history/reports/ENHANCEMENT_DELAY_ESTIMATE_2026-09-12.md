
## 2026-09-12 增强额外延迟估计

用户确认主面板需要相对无增强播放的新增延迟，允许预估。新增 EnhancementDelayEstimate.h；Engine在实际原帧Present后记录一秒窗口样本。文件使用媒体时钟正向lateness（预处理驻留不计，启动/seek重新锚定不属于稳态）；直播使用已解码时间到Present的帧龄，减去颜色、输出合成及Present基础开销估计。PS5取真实decodedHost，采集无该时间戳时取callback，可能包含基础转换/排队而偏高。没有同源同时无增强A/B标定，不承诺精确因果差值；无增强定义0、基线缺失返回未测，负值夹0。XeSS内部排队/屏幕扫描不可测。故不应称端到端实测。

LiveStatusPanel主数改“增强额外延迟 · 估计”，原驻留均值/P95移至详情。本轮不改变音频、增强、调度策略。单独记录估计样本，不能用不同统计群体的P95相减。

构建 out/remoteplay/build-extra-delay.cmd 通过，logs/extra-delay-final-build.log。repair_contract_tests 88 checks 0 failures（含预读取不计延迟、基线扣除、未知/无效样本检查），logs/extra-delay-contract.log。UI首次脚本过早检查WM_CREATE子控件失败，保留logs/extra-delay-ui.log；加同步WM_NULL等待创建处理完毕后重跑通过，logs/extra-delay-ui-retry.log，overview实际截图已查看。最终仅修改底栏文案后重新构建通过。

用户GTAVI_An_Extended_Look_4K_Native.mp4实测12秒 --native --nr --fg-multiplier 2 --no-sr --smoke-seconds 12，exit0、291原帧/289生成、failed=false、末段约60呈现/秒、absLatenessP95=0.92ms，旧驻留P95=67.022ms；证据logs/extra-delay-4k.log。未执行实卡/PS5新对照测量。软件路径仍 out/remoteplay/product-repair/veyra.exe，桌面PS5测试版指向此处。未发布、未push、无二进制入Git。
