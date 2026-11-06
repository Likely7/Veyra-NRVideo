# 2.1.3 默认档位与低延迟提示重构建

用户要求：超分默认中档；英伟达低延迟警告改红色，仅“开＋增强”显示；删除上一轮构建产物，重建2.1.3。

## 改动

- `kDefaultVideoSrQuality=2`，列表/节点、基础/第二次超分统一中档。从DLSS/FSR切入RTX版本没有既有档位时使用该默认（标准2、高码率17、流媒体21），已有档位和保存设置不迁移覆盖。
- NvidiaLowLatencyEditor共用组件按选择显示警告，Boost用Theme.err红字，Off/On无警告。非NVIDIA的硬件不可用说明保持普通说明色。
- 不改变低延迟驱动调用、模型或运行库；版本仍2.1.3。本轮不push/Release。

## 清理与路径

上一轮`codex-build-2.1.3-20261106`的包、ZIP、验收解压副本和测试二进制按受控清单逐文件SHA核对后删除；未列入清单的个人配置/日志和修改文件保留。共享构建缓存、依赖、旧正式版和源代码工作树不删。
清理脚本/实际清单在`reports/codex-rebuild-2.1.3-20261106/cleanup-old.ps1`与CLEANUP-PLAN/CLEANUP.json。不使用跨Shell删除或递归到连接目标。

复用`build/claude-screen-recording-20261022`，新task为`codex-rebuild-2.1.3-20261106`；test-packages/tests/logs/tmp/verify/reports全部按新task隔离，旧interactive配置保留在原位。

## 验证

构建主程序、AMD worker及相关QML/设置/预设/合同目标；更新原UI合同和低延迟交互用例，验证新默认与显式保存值的恢复。新包逐文件SHA/CRC和独立解压最小PATH启动/播放。实际命令、失败与通过、交付身份以WORKLOG和新task FINAL.json为准，本计划不作为通过证明。
