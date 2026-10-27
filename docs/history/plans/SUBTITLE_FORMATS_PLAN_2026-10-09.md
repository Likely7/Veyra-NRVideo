# 字幕格式与内嵌 PGS 不可用排查

用户要求修复 PGS/SUP 不显示并检查其它常见字幕格式；随后确认内嵌 PGS 在菜单中均显示“不可用”，提供五条 HDMV_PGS_SUBTITLE 轨道的截图。尚无该视频文件或现场日志，不把短样本通过冒充此文件根治。

收尾用户明确“不用打测试包”，当前结果仅为代码修复和验收报告，不继续打包、不交付测试包。指令到达前已生成的包留作内部中间证据，不作为交付。

基线 main 0e7018ad，隔离分支 codex/subtitle-formats-20261009。开工独立 archives/subtitle-formats-20261009/start.json、source-before.bundle 保护39既有工作树/495脏文件；main不变。本轮只改共享字幕解析/解码/字幕层、必要bridge/字幕菜单/翻译和定向验收，不改NR/补帧/视频解码/音频/导出算法。当前用户授权覆盖这些字幕接点的历史冻结，不运行或重写历史UI迁移guard。

1. 先验证随包 FFmpeg 的实际字幕解码器与既有位图样本。已只读确认随包 pgssub、dvdsub、dvbsub、xsub 和多种文本 decoder 存在，TTML不存在；不能归因为缺PGS解码器。记录各层失败的真实返回值和轨道状态。
2. 修正经样本或源码验证的PGS加载/时间线/清屏/发布问题；区分正在读取、无内容、格式不支持、初始化失败与损坏数据，不在扫描期间笼统称不可用。
3. 外置SUP/PGS和VobSub等可复用现有FFmpeg解码器，不把二进制当UTF文本解析。文本格式优先复用已随包decoder/现有ASS/SRT/VTT解析，拓展常见SubViewer、MicroDVD、SAMI等。只列出已实测支持，不用扩展名白名单代替端到端支持。
4. 定向回归内嵌与外置、主副轨/选择、延时/seek/清屏/缩放、Unicode路径、坏文件和取消/旧异步结果；验证实际字幕层像素与QML轨道状态，不只看解码计数。
5. 构建当前main全部必要覆盖对象，不混旧ABI；原SDK/运行库/模型/已验母包字节保持。每测试≤300秒、构建≤900秒，产物E:/项目/Veyra/{reports,build,tests,logs,tmp,test-packages,archives}/subtitle-formats-20261009，仅一个共享测试app。完工记录失败/限制、提交隔离分支；没有新merge/push/Release/版本更新/关机授权，不派Agent、不造压、不代发消息。

## 实证补充：压缩PGS

公开FFmpeg样本 `https://samples.ffmpeg.org/sub/PGS/supsample.mkv` 在产品原DLL上轨道存在但0条cue，输出 `Unsupported encoding type`。同文件在FFmpeg CLI上解封装为2个PGS显示包；原DLL配置为 `--disable-zlib`，Matroska源码明确拒绝zlib压缩轨道。另一公开PGS样本不压缩，可正常解码。这是已经复现的封装能力缺口，不是PGS decoder缺失；不能推断所有反馈片源均为此原因。

本轮另行构建同一n9.0.1 patched源码，仅切换 `--enable-zlib`，链接已有libass依赖中的静态zlib1.3.2。保留slices256、dav1d、LGPL配置及全部旧格式能力；输入原件不覆盖，FFmpeg/zlib对应源码及许可证留在E盘、哈希/配置随候选交付。构建脚本/打包接点纳入范围，不增加专有运行库。回归原样本、未压缩对照与既有视频解码。
