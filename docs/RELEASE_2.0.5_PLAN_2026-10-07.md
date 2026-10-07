# 2.0.5 已完成修复整合与本地测试交付

用户指令：将目前已完成修复合入main，构建2.0.5测试；显存问题下个版本再深入排查。本轮无新的公开推送、Release、上传或关机授权。

main起点578d63c3a0143429b306e26eeb89137473b4d99f；候选工作树 `E:/项目/Veyra/worktrees/release-2.0.5-20261007`，分支 `codex/release-2.0.5-20261007`，起点71483d56f17d85784f6ff1cbb40f83b90747b0d4。已完成的产品修复来自ef2069f、4c917aa，后续仅调查/验收文档及只读采集脚本。

1. 保留PR19/20、OBS软件重绘、AMD NR合成、列表预设等main已有改动；核对全部分支，避免重复覆盖。冻结27个既有工作树及发布包的独立基线，main只允许按本轮实测源树推进。
2. CMake项目/显示/PE版本统一2.0.5；不修改本轮已经验收的产品算法、默认优先级、DLL、模型或驱动。NR缺失Shutdown及NV/AMD显存所有权猜测均不施工。
3. 继承固定Qt6.8.3、patched FFmpeg/libass/串流依赖和运行组件字节，单一生产EXE生成NVIDIA/AMD两个完整便携包。源码/SDK/模型继续隔离。
4. 构建生产及相关定向目标；串行执行契约/预设/统计组件、真实NR播放与FSR导出、AMD缺组件下的入口/选择、窗口及全屏显示同步等检查。AMD HIP/编码与用户显示器实测缺口明确报告。每个测试进程≤300秒，构建≤900秒；不制造压力或竞争负载。
5. 冻结实际源输入、EXE/测试收据，提交实测候选并本地--no-ff合入main；验证合并树与实测树一致。生成对应源码、完整ZIP及逐文件manifest，从最终ZIP独立解压、仅系统PATH和私有profile启动，核对PE版本及包内模块来源。
6. 以DELIVERY.json、main-merge.json和final-check.json交付；公开Release/已发布文件保持。保留必要构建、最终包、源码和验收证据，清理仅本轮重复副本且不得绕过自动审批拒绝。

全部新产物 `E:/项目/Veyra/{archives,build,tests,logs,tmp,test-packages,verify}/release-2.0.5-20261007`。先运行 `python -B scripts/acceptance/release-2.0.5-control.py`；独立start SHA256 `48b998d583676c9c39b14cc417c89d294639060f2e2fd4aa1986b366113625c8`，旧任务guard/baseline不修改。
