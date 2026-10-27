# 内容节奏、同步与正常限速状态修正

基线 e8300d9，分支 codex/cadence-status-20261011，保留刚验收的新超分接入；本轮只修状态与说明，以及实测发现的设置应用确认遗漏。没有修改采集、去重、补帧、NR/SR、呈现算法，没有提高界面刷新频率或 GPU 优先级。

## 原因和修正

旧状态把源传输帧率乘补帧倍率当成目标。60 Hz 传输中的30张独立画面去重后只需处理30张，屏幕同步、有效输出上限、慢放、对比暂停补帧也会改变目标，原比较会误报。

- 共享 DashboardHistory 用已有 captureRateSkipped 的有界一秒窗口扣除主动跳过的帧，60→30只计算一次；不能仅因为勾选Auto或存在内容帧率提示就降低目标。静止画面的正常去重也计入，目标下限1fps，停住不能伪装正常。
- 输出目标依次考虑内容帧率、播放速度、实际补帧倍率、对比暂停、已生效限帧、实际视频窗口所在屏幕的同步上限。XeSS采用SDK提交口径，被provider取消的请求上限不能当作已生效。屏幕刷新率只读Qt缓存，不猜未知值。
- 保留原95%阈值和8次连续采样确认；输入不足、时间线异常、真正低于有效目标或无正常限速理由的补帧降档继续告警。不用实测输出直接充当目标。
- 性能球预算按进入增强的内容帧间隔计算。去重和慢放会改变这个间隔，输出限帧和垂直同步本身不会虚增处理预算。
- 列表状态条上方、节点统计区显示已开启选项、当前内容节奏或未生效原因；状态详情显示有效目标。长文本换行并提供完整悬停提示。列表原固定172像素统计区会随文本适当增高，避免说明把状态挤出窗口；四种语言共补16条翻译。

实际GUI还复现：同一回调关闭DLSS 4X并设0.5X，速度修改触发seek，重建后的缓存帧在discardBefore处跳过。局部transaction标记丢失，实际已经15fps，applied仍为4X，界面永久“调整中”。EngineController增加pendingSettingsAck跨循环保留确认，只在新设置首帧成功process后发布实际applied；不提前确认，不加逐帧锁、等待或额外重建。真实GUI同一序列及永久集成用例都通过。

## 本轮验证

全部路径前缀为 E:/项目/Veyra/。

| 检查 | 结果及证据 |
| --- | --- |
| 增量构建 | tmp/cadence-status-20261011/build.py；最终 logs/cadence-status-20261011/build-20261011-215110/receipt.json，15.0秒，7个源依赖对象及资源/MOC、10个EXE。复用已验构建缓存、DeckLink和patched FFmpeg，不能冒称全量clean构建。新apps/veyra/ui include优先于旧缓存路径。 |
| 状态策略 | harness/regress.py ui-contracts；logs/cadence-status-20261011/cadence-status/tests-20261011-213440，27条新策略断言和原UI契约通过；覆盖去重/静止/半率/同步/限帧/provider/倍速/对比及真正欠速、未知刷新率、策略切换失效。 |
| 真实生产GUI | harness/ui-smoke.py；logs/cadence-status-20261011/ui-20261011-215150，48.2秒，9种状态、6次布局检查通过；GTA 1080p30素材，真实提交30、限帧15、DLSS 4X+VSync提交120、对比30、慢放15、组合限帧12。简中/繁中/英文/日文，列表与节点、1120和1440宽度通过，无Binding loop/TypeError/D3D12 error。截图为Qt界面，不包含独立原生视频子窗口，不能用它判断视频是否黑屏。 |
| 设置重建与跳转 | harness/regress.py video；logs/cadence-status-20261011/cadence-status/tests-20261011-215330/video，原4K GTA输入、1080p NR，43.0秒，8轮NR/FG/SR切换及新增全效果关闭+0.5X+seek确认通过。 |
| 失败回滚 | 同批 rollback，17.8秒，原回滚诊断与恢复出帧检查通过。 |
| 翻译/静态 | scripts/i18n/extract.py --check，placeholder problems=0；原有29条未翻译保留，没有删除旧词条。git diff --check通过。 |
| 原件保护 | archives/cadence-status-20261011/protect.py verify：58个原工作树、504个脏文件及source-before.bundle不变；每次staging/回归前后核验donor原包1626文件。最终再次核验见logs/cadence-status-20261011/final.json。 |

失败证据完整保留：213650首次GUI发现统计卡片越界并修正；213826为重启夹具在打开文件前等待applying的错误；213957/214052为夹具未等补帧冷初始化/250ms状态发布完成；214312/214630真实复现同回调FG关闭+速度seek后应用确认丢失，修复后215150通过。没有将这些失败批次记成通过。

最终GUI SHA256：be00985661932e578f305e454589999c1dc8117f45bad787f10c1eee0a0ac442；显示版本2.0.6-cadence-status-test-20261011。独立运行目录 tests/cadence-status-20261011/app，测试夹具已从生产Main.qml移除并恢复源码字节。用户测试profile复制先前私有测试配置，GUI自动验证使用另一个gui-profile；不改真实用户配置。

## 验证边界

本机RTX5070与320Hz显示器；本次真实VSync 4X提交120fps未触及320Hz上限，60Hz上限的降目标与低于上限告警由契约矩阵验证。没有现场连接30-in-60采集卡，也未实际运行其它显卡/provider组合；去重计数与这些策略的交互由共享判定用例验证，仍需用户实卡复核。提交FPS和SDK计数不是物理扫描帧率。本轮没有性能A/B或画质评测，不能宣称提速；保留原统计采样和有限动画，没有新增生产Timer。

源码、构建、测试、日志、临时缓存与增量归档分别留在worktrees/build/tests/logs/tmp/archives的cadence-status-20261011子目录。必要证据保留，失败GUI的临时CUDA/Qt缓存清理记录在final.json。未合并main、push、打包、发布或关机。
