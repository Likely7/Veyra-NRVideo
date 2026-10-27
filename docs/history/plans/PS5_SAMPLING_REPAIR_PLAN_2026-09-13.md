# PS5 色度与显示采样修复

用户授权实施并在完成后关机；随后明确撤回音频缓冲选项，现有音频链路不改。基线320f860，存档checkpoint/ps5-sampling-2026-09-13，分支codex/ps5-sampling-repair。

范围：PS5可选精细采样（默认），保留兼容采样用于用户A/B。输入按实际chroma_location重建4:2:0色度；缺失时记录并采用left回退。输出仅在放大时使用带局部限幅的Catmull-Rom重采样，1:1保留像素，缩小时保留原路径。本次不改变NR/SR/FG执行顺序、音频、调度、PS5带宽反馈或增加来历不明的协议字段。

采样选项保存于现有RemotePlay本地settings.ini，重连生效；不改变已有配对、codec、码率、HDR选择。文件、采集、导出默认不启用此次PS5精细采样。

按用户要求不执行画面识别、PS5连接或运行时验收测试；完成shader与C++构建、差异检查并明确报告未执行项。构建通过不等于画质验收。更新施工记录和WORKLOG，创建本地Git存档，不推送发布。完成后发出Windows关机请求，不使用强制关闭应用参数。

## 实施结果

- `ColorDescription` 新增色度位置及显式PS5重建开关；`resolveFrameColor` 解析FFmpeg的left/center/top-left/top/bottom-left/bottom。RemotePlay按当前帧metadata解析，缺失时保持Unknown并在shader采用left回退；首帧日志记录fine、chromaLocation、assumedLeft。缺失值不冒充实际元数据。
- `YuvToLinearRgb.hlsl` 仅在重建开启时按位置做四邻域色度插值，边缘限制在可见尺寸内，不读取解码填充区。亮度仍逐像素读取。NV12/P010及软件planar上传共享该实现；兼容模式沿用2×2色度复制。
- `PresentBlit.hlsl` 增加Catmull-Rom放大采样和局部2×2范围限幅，限制负瓣引入的光晕，保留实际HDR邻域值。1:1像素中心直接读取；缩小仍用原双线性，不冒充高质量抗混叠缩小。参考画面与正常画面使用同一采样选项，sRGB编码单独用flag控制。
- `EnhanceGraphDesc` / `VideoPresenter` 将PS5设置接入现有呈现pass，没有新队列、新中间纹理或正常播放GPU回读；既有GPU呈现计时会包含新增采样工作。开关在初始化后随配置重建保留，PS5恢复重连保留同一connect desc。
- PS5面板新增“采样（重连生效）”：默认“精细采样 · 色度重建与双三次缩放”；可切“兼容采样 · 原有方式”。选择点击连接后存入 `%LOCALAPPDATA%/Veyra/remoteplay/settings.ini` 的 `FineSampling`。旧配对、codec、码率、HDR、解码模式不自动改变。面板下方内容下移44逻辑像素，增加窗口高度。新增悬停说明。
- **音频缓冲选项撤回，未新增。** 未修改音频同步、控制器、串流调度或带宽反馈。未声称本地采样等价Portal官方高质量模式，未伪造固定100Mbps、无损或更高源分辨率。

## 构建与未验收项

两次 `cmd /c out\remoteplay\build-clock-product.cmd` 均exit0，等价MSVC环境执行 `cmake --build out/remoteplay/product-repair --target veyra -j8`。

- 首次日志 `logs/ps5-sampling-product-build.log`：PresentBlit PS/VS、YuvToLinearRgb DXIL及C++主程序完成构建。
- 最终日志 `logs/ps5-sampling-final-build.log`：补充色度fallback与帮助文字后主程序链接成功。
- 保留既有FFmpeg C4244 / WX覆盖警告；本轮无编译失败。`git diff --check` 通过。
- 可执行文件：`out/remoteplay/product-repair/veyra.exe`，沿用桌面“Veyra PS5 测试版”入口。本次仅本机开发版，不是新的Github Release。
- 用户明确要求不测试：本轮未运行自动回归、未执行NR/SR/FG Create/Evaluate、未连接PS5、未检查游戏画面、未执行HDR或GUI视觉验收。此前320f860的旧采样测试不能当作本次新算法已通过。新增采样GPU成本尚未实测，不能保证一定改善观感或保持原延迟。

## 明天验收

1. 打开桌面PS5测试版，保持同一codec、码率和显示尺寸，先关闭增强；选择精细采样并连接。
2. 切换兼容采样并重连，肉眼对比人物、彩色细线及放大显示；不要把两个模式不同codec/倍率造成的差异算在采样上。
3. 如精细模式不合适，直接保持兼容采样，不需覆盖DLL。完整代码回退点为 `checkpoint/ps5-sampling-2026-09-13`，本轮没有不可逆配对迁移。

下一项唯一任务是用户验收精细/兼容采样。源头量化与Portal高质量协议仍是未解决的研究项，不能因本次本地修复宣称严重模糊已全部解决。
