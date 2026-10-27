# 直播兼容实验模式

## 范围

用户要求提供可选开关进行捕获对照，并先提供专业模式UI调整方案。本次默认恢复FLIP_DISCARD，勾选“直播兼容 · 实验”使用FLIP_SEQUENTIAL。没有进程检测，不依赖OBS先后启动。该实验不是已经证明解决捕获问题的修复；前序对“子窗口FLIP_DISCARD就是根因”的定论缺乏实际捕获对照，撤回该定论。

入口位于专业参数面板顶部、还原默认按钮右侧，四个页面均可见，不随参数滚动。生效模式在实时处理状态中显示。UI整体重排仅写方案，见 `PROFESSIONAL_UI_LAYOUT_PLAN_2026-09-11.md`。

## 行为与边界

- `EnhancementSettings.captureCompatible` 默认false；v10预设保存开关，旧v1-v9默认false。还原默认关闭直播兼容。
- `PresentSink::Desc` 接收开关，以GetDesc1确认创建的交换链类型符合请求；失败返回错误，设置事务负责回退。保持RGBA8、三缓冲、原有vsync/tearing和GPU呈现路径，没有逐帧CPU像素回读。
- `VideoPresenter`向呈现端传递设置，播放/采集/普通图片在原有事务边界排空、重建和回退，包含暂停预览与恢复路径。切换存在短暂停顿，目前沿用完整图重建，不能称为仅一行参数无成本切换。
- 总增强关闭时，显示设置仍独立请求生效，不打开NR/SR/FG。大图单独重建预览呈现器，纯显示变化不重新处理全部NR分块。离线视频导出不使用该选项改变输出。
- XeSS使用代理交换链；以实际GetDesc1为准，不兼容则通过既有错误/回退机制处理。本轮不承诺第三方捕获能获取所有XeSS生成帧。
- 没有自动检测外部软件是否抓到画面，状态只表示交换链选项已应用。两种模式均不能保证传统BitBlt捕获GPU画面；后续对照应区分WGC窗口捕获、游戏捕获和显示器捕获。
- 性能和画质变化尚无外部捕获对照证据。不能宣称固定增加一帧、必然黑屏或已经改善GPU占用。

## 验证记录

本轮开关与设置接入完成，标准入口 `Veyra.cmd` 对应 `out/build/x64-release/veyra.exe` 已更新。EXE SHA256 `041A14751E182936B21A8878AE7C3508C96EF9009FA9C1D05171121315A82F74`。

实际执行：

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Root . -Preset x64-release
out/build/x64-release/veyra_repair_preset_tests.exe logs/broadcast-preset-test.v1
out/build/x64-release/veyra_repair_contract_tests.exe
python scripts/acceptance/ui-broadcast-mode.py out/build/x64-release/veyra.exe
python scripts/acceptance/ui-broadcast-mode.py out/build/x64-release/veyra.exe --xess
powershell.exe -NoProfile -ExecutionPolicy Bypass -File scripts/gates/delivery.ps1 -Root . -BuildDirectory out/build/x64-release
```

构建exit0，日志 `logs/broadcast-mode-build.log`；CPU合同81项、v4-v10预设42组迁移和完整设置往返通过，v1升级亦通过。真实RTX5070运行，NR及DLSSG Create `0x1` / SEH0。UI脚本以实际按钮触发标准→兼容→标准，再关闭总增强后往返；覆盖暂停、全屏进出、1280x900和1040x540固定位置及滚动不移动开关，截图非黑。DLSS结果 `logs/broadcast-mode/1789101764400727400/result.json`，XeSS结果 `logs/broadcast-mode/1789101876592375800/result.json` 均PASS，对应app.log记录请求和实际swapEffect及Applied revision。截图为软件桌面显示，未冒充外部窗口捕获。

delivery23项PASS，42.881秒，`logs/delivery/e6eb77b2bcc5468781d458f7e5371e77/result.json`、`logs/broadcast-delivery.log`。本次没有执行实体采集卡、社区NR与兼容模式组合、超大图片切换专项、外部录制画质/性能对照或新的错误注入回退测试；回退接入既有事务。下一步唯一验收为用户在同一捕获源上切换开关对照，记录外部软件捕获方式。不要把测试通过解释为原捕获故障已解决。

修改文件：设置结构和v10预设/单元测试；`PresentSink.h/.cpp`、`VideoPresenter.h/.cpp`、`EngineController.cpp`、`EngineControllerImage.cpp`；`SettingsWindow.cpp`、`AppShell.cpp`、`LiveStatusPanel.h`；新增UI回归脚本、本方案和专业布局方案、WORKLOG。没有引入逐帧进程枚举或额外颜色转换。

首次UI脚本等候采集专用frame-rate日志，在文件回放中超时，程序实际播放正常；改为等候文件player-timing记录。第二次总增强关闭后立即点击触发已有的主开关事务互斥，脚本增加等待UI完成事务；不绕过产品互斥。

本次未新增运行DLL、未推送或发布。原NR双运行时工作区修改继续保留。
