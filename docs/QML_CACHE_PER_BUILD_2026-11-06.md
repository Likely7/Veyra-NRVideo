# 每个构建使用自己的界面编译缓存（2026-11-06，Claude）

- **分支**：`claude/qmlcache-20261106`，工作树 `E:/项目/Veyra/worktrees/claude-qmlcache-20261106`。
- **基线**：main `edd901e`（2.1.3 产品 `7a2d15d` 加文档）。
- **状态**：未提交，未合并，未 push/Release。
- **用户要求**：先排查“2.1.3 选不了 RTX 视频超分三种模型”，确认原因后要求“实现修复”。

## 原因（排查见 `E:/项目/Veyra/reports/claude-sr-qmlcache-20261106/FINDINGS.md`）

- **Qt 的缓存规则**：Qt 把编译好的界面放在每个用户共用的 `%LOCALAPPDATA%\Veyra\Veyra\cache\qmlcache`。缓存文件名是界面文件完整路径的 SHA-1；只要路径和修改时间不变就直接沿用，不比较内容。
- **两版发布 ZIP 的时间戳相同**：几乎所有文件都是 1980-01-01。
- **结果**：
  - 2.1.3 解压到 2.1.2 用过的同一路径时，沿用的是 2.1.2 编译的 ProPage；
  - 旧处理 `Number(id)` 把 `rtx`、`rtx-hb`、`rtx-st` 变成 0，于是跳回 DLSS。
  - 2.1.2 到 2.1.3 共改了 9 个界面文件，原地升级的用户可能整套界面都还是旧的。

## 改动

**`include/veyra/ui/QmlDiskCache.h` 和 `src/ui/QmlDiskCache.cpp`（新增）**：`chooseQmlDiskCache` 决定这次构建用哪个缓存目录。

- **计算方式**：对程序自带的 Veyra 界面模块（`<程序>/qml/Veyra`，约 101 个文件、1.9 MB）的“相对路径 + 大小 + 内容”算 SHA-256，取前 16 位，作为缓存目录名 `qmlcache-<摘要>`。
- **不计入 Qt 自带模块**：Qt 升级时，Qt 自己会检查版本和编译哈希。
- **保留明确设置**：已设置 `QML_DISK_CACHE_PATH` 时保持不动；`QML_DISABLE_DISK_CACHE` 为非 0 时不做任何设置。
- **无法计算时**：找不到模块或文件读不出，则保持 Qt 原样，并记录原因。

**`apps/veyra-qml/main.cpp`**：在创建 QML 引擎之前调用上面的函数，把 `QML_DISK_CACHE_PATH` 设为 `<用户缓存目录>/qmlcache-<摘要>`。

- 日志新增一行 `[qml-cache] folder=… digest=… files=… bytes=… ms=…`。
- Qt 用 `fromLocal8Bit` 读这个变量。如果系统代码页无法表示这个路径，就改为关闭界面编译缓存（宁可慢一点，也不退回共用目录）。
- `VEYRA_TEST_QML_CACHE_ROOT` 仅供测试，用来把缓存根目录换成私有目录。
- 程序用退出码 42 自动重启时，子进程继承同一个设置；同一构建对应的目录本来就相同，不受影响。

**`CMakeLists.txt`**：`veyra_qml_ui` 加入新源文件，并新增 `veyra_qml_cache_tests`。

**没有做的**：

- 不自动删除旧的 `qmlcache` 或其他构建的缓存目录：一个构建约 2–3 MB，新程序也不会再读旧目录。
- 不改打包时间戳：发布包的可复现性和摘要核对保持不变。

## 验证

**构建**：`scripts/acceptance/build-recording-handoff.py --task claude-qmlcache-20261106 --label 2.1.3-qmlcache-test --jobs 2 veyra_qml_ui veyra_qml_cache_tests`，新建构建目录 `build/claude-qmlcache-20261106`，301.0 秒通过，新文件没有编译警告。

**单元测试 `veyra_qml_cache_tests`**：通过。覆盖这些情况：

- 同样的文件得到同一目录，与安装位置和列出顺序无关；
- 同名、同大小、同 1980 时间戳但内容不同时换新目录；
- 改名或新增文件时换新目录；
- 明确设置的 `QML_DISK_CACHE_PATH` 保留；
- `QML_DISABLE_DISK_CACHE=1` 保持关闭，`=0` 不算关闭；
- 没有模块或没有缓存根目录时，保持 Qt 默认。

**真实程序**：

- **方法**：脚本 `E:/项目/Veyra/tmp/claude-sr-qmlcache-20261106/qmlcache_fix_run.py`。
  - 在固定目录先解压 2.1.2 并运行，填充旧的共用 `qmlcache`；
  - 再把 2.1.3 解压到同一位置覆盖，两次都保留 ZIP 里的 1980 时间；
  - 私有缓存根目录，私有配置和日志；
  - 探针模拟依次选择 FSR、高码率、FSR、标准、FSR、流媒体、FSR、DLSS。
- **结果**：

| 运行 | 缓存目录 | ProPage 编译缓存 | 选框宽度 | 三种 RTX 提交 |
|---|---|---|---|---|
| 未修复的 2.1.3 程序 | 旧共用 `qmlcache` | 沿用 2.1.2 的 | 150 | 0 / 0 / 0（跳回 DLSS） |
| 修复版，升级后首次启动 | `qmlcache-69f354eeebfa5bcb`（101 个文件，摘要 15.7 ms） | 重新编译；旧目录未动 | 210 | 17 / 2 / 21 |
| 修复版，第二次启动 | 同上（12.3 ms） | 复用 | 210 | 17 / 2 / 21 |
| 只改 ProPage 一个数字（大小与 1980 时间不变） | `qmlcache-2d41dc8775b7b895`（13.5 ms） | 重新编译 | 150（改动立即生效） | — |
| 改回发布原样 | 回到 `qmlcache-69f354eeebfa5bcb`（12.4 ms） | 复用 | 210 | — |
| 修复版，外部已设置 `QML_DISK_CACHE_PATH` | 保持外部设置（日志 `kept`） | — | — | — |

- 测试结束后，ProPage 已逐字节恢复为 2.1.3 发布版的内容。

## 没有验证的部分

- **未在现场验证**：没有在用户的 RTX 3080 上验证。已经中招的用户，需要换成带这个修复的新版本程序后才会自动恢复；仍用 2.1.3 的用户，要按排查报告删除 `qmlcache` 目录。
- **代码页回退未实测**：“代码页无法表示路径时关闭缓存”这一分支只有代码审读，没有在非中文代码页的系统上实测。
- **启动耗时**：本机摘要约 12–16 ms；机械硬盘冷启动时可能更长，没有测。
- **其余测试**：没有重跑全套 QtQuick 和其他合同测试。这次没有改动 QML，也没有改动除启动缓存设置以外的 C++。
- **测试副本**：`E:/项目/Veyra/tests/claude-sr-qmlcache-20261106/install` 约 1.1 GB，可以重新生成，留待提交后清理；里面的 EXE 已换成本次测试构建。

## 产物

- **排查报告**：`E:/项目/Veyra/reports/claude-sr-qmlcache-20261106/`。
- **本次回执**：`E:/项目/Veyra/reports/claude-qmlcache-20261106/FINAL.json`。
- **日志**：`E:/项目/Veyra/logs/claude-qmlcache-20261106`（构建与单元测试）和 `E:/项目/Veyra/logs/claude-sr-qmlcache-20261106/fx-*.log`（真实程序）。
