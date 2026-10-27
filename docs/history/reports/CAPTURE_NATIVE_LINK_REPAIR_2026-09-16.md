# 原生采集链路修复报告（N1/N3/N4，N2 尝试后回退）

分支 `codex/capture-decode-latency-20260916`（从合并后的 main `2406f81` 开出）。本报告覆盖方案
`docs/CAPTURE_DECODE_LATENCY_PLAN_2026-09-16.md` §8 原生链路 N1–N4 的施工与真机数据。

## 1. 交付项

| 项 | 状态 | 说明 |
| --- | --- | --- |
| N4 视频 pin 缓冲协商 | 已交付 | 三档设置（自动/最小/驱动默认）+ 连前建议 + 连后 requested/actual 日志；预设 v17、面板与 `--capture-buffer=` |
| N3 格式排序与延迟标注 | 已交付 | NV12/P010 → YUY2 → RGB24/32 → UYVY/YVYU/RGB555/565 → 压缩；每行延迟档位；高成本格式一次性提示（capture-preferences v2） |
| N1 逐像素转换上 GPU | 已交付 | UYVY/YVYU/BGR24/RGB555/RGB565 保留原始 packing，回调只做按行/整块拷贝；`PackedCaptureToLinear` 在 GPU 解包；方向契约完整保留；`--capture-cpu-unpack` 可回退 |
| N2 两次拷贝合并 | **尝试后回退** | 功能已攻破但实测净亏（见 §4），按用户指示整体移除，软件内不留该路径 |

## 2. 证据

- 构建 `out\build\veyra-build-x64-release.cmd` exit 0；修复合同 191 项 0 失败；预设 66 组迁移 + v17 往返通过；`veyra_capture_color_tests` failures=0；`veyra_hdr_color_tests` 15 种格式 × full/limited GPU 颜色用例全过（新格式 error=0）。
- 4K 回调拷贝微基准（新工具 `veyra_capture_copy_bench`，3840×2160、30 次/帧，ms/帧）：RGB24 **1.120**（旧 5.23）、RGB565 **0.828**（旧 10.87）、RGB555 0.791（旧 11.06）、UYVY **0.640**（旧 2.47）、YVYU 0.595、YUY2 0.611、NV12 0.199。N1 目标（RGB24≤1.5 / RGB565/UYVY≤1.2）全部达标。
- 真机 A/B（**严格同协议**：优化前提交 = `checkpoint/pre-capture-decode-latency-20260916`（main `2406f81`），与优化后同一台机、同一张卡、同一命令、各 120 秒、无增强；基线由该提交重新构建后实测，不再是不同窗口的历史数据）：

| 配置 | 指标 | 优化前（2 分钟） | 优化后（2 分钟） | 变化 |
| --- | --- | --- | --- | --- |
| 1080p60 YUY2 | callback→Present P95 | **2.568 / 2.545 ms**（两采样） | **2.557 / 2.562 ms** | 持平（±0.2%） |
| 1080p60 YUY2 | processCpu P95 | 0.380 / 0.406 ms | 0.422 / 0.430 ms | +0.03~0.05 ms（图侧，见下注） |
| 1080p60 YUY2 | 丢帧 / frame | 0 / 7178、0 / 7177 | 0 / 7178、0 / 7179 | 无 |
| 4K18 YUY2 | callback→Present P95 | **3.875 ms**（2150 帧、0 丢帧） | **3.586 ms**（2151 帧、0 丢帧） | **−7.5%** |
| 4K18 YUY2 | processCpu P95 | 1.126 ms | **1.028 ms** | **−8.7%** |
| 4K18 YUY2 | readAgeMs 区间 | 0.3–1.6 ms | 0.2–1.5 ms | 一致 |

结论：**4K 原生链路达到方案验收线**（≥两项改善 ≥5%：P95 −7.5%、processCpu −8.7%；丢帧/readAgeMs 无回退）。收益机制与微基准一致：N1 的整平面单次 memcpy 让回调拷贝从旧行拷贝（基线微基准 4K YUY2 ≈0.89 ms）降到 0.611 ms，节省量（≈0.28 ms/帧）与端到端 P95 的改善（0.29 ms）吻合。
**1080p 是持平**（P95 两轮都 ±0.01 ms 内；图侧 processCpu 有 +0.03~0.05 ms 的小幅移动，两次采样均可复现但幅度只占整链路 P95 的 ~1.6%，截至本报告未定位具体机制——图中 YUY2 入口代码与优化前一致，怀疑与回调改用大块 memcpy 后的缓存/带宽模式有关，如实记录，不外推）。本机没有能触发 N1 逐像素转换的采集格式，1080p 结论仅对 YUY2 有效。

## 3. 本卡边界（不得外推）

- N4：这张卡的驱动**接受调用但忽略建议**，三档都固定给 10 个缓冲（`[capture-buffer] mode=… requested=2 actual buffers=10 (driver ignored; negotiation not applied)`）。收益需一张真正响应协商的卡验证，本卡记 0 收益。
- N1：本卡只提供 YUY2/MJPEG，没有 UYVY/BGR24/RGB555/RGB565，逐像素格式的收益只有微基准 + 合成 GPU 颜色用例证据，**未在真卡验证**。4K 的回调拷贝收益来自 N1 的整平面单次 memcpy 快路径，对 YUY2 同样生效。
- N3：排序/标注/提示为 UI 与单元测试证据，未做 UI 自动化点击测试。

## 4. N2 回退记录（为什么不留）

实现：回调直接写进图的上传缓冲（省掉"回调→信箱帧 + 图 memcpy→upload buffer"中的第二次整帧拷贝）。功能上最终攻破（真机 1080p60/4K18 直接提交 100%、0 丢帧、1 次历史重置），但**性能净亏**：

| 配置 | 直接写入 | 信箱（现状） | 差值 |
| --- | --- | --- | --- |
| 1080p60 YUY2 | P95 3.536 ms / processCpu 1.494 ms | P95 2.629 ms / processCpu 0.429 ms | **+0.91 ms** |
| 4K18 YUY2 | P95 4.102 ms / processCpu 1.885 ms | P95 3.265 ms / processCpu 1.013 ms | **+0.84 ms** |

原因：合并后的写入目标是 D3D12 upload heap（write-combined 内存），CPU 写 WC 的成本 + "跨线程写完、GPU 读取前的 flush/可见性等待"超过省掉的那次 RAM→上传缓冲 memcpy。
处置：`git revert 1b4ca81 14c965e 406bdf1`（净 −194 行），软件内不保留直接写入路径与 `--capture-direct-ingress` 开关；调试过程与三条坑（fence 索引、CLI 字段丢失、视图帧 `buf[0]=null`）保留在 `docs/WORKLOG.md` 供后人参考。

## 5. 下一步

- 压缩解码链路（方案 §4 ①②③④⑦）：压缩样本直连 → H.264/HEVC/AV1 D3D12VA → MJPEG 并行软解；这是 MJPEG/H.264 用户延迟问题的正解。
- N4 需一张响应缓冲协商的卡；N1 逐像素格式需一张能输出 UYVY/BGR24/RGB565 的卡做真机验收。
