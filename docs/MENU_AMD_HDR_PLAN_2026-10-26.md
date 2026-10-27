# 菜单文字抖动与AMD HDR+NR修复

用户要求先暂停2.1.1封包，修复弹出菜单/下拉选框文字抖动，以及AMD用户HDR与NR不能同时开启。起点2de14fd，独立分支codex/menu-amd-hdr-fix-20261026；原2.1.1分支与未封包stage保留，标记PAUSED与superseded，不交付旧stage。

- VSelect复用VMenu；用户否决淡入淡出替换方案；撤回该方案及静止文字坐标的错误验收条件，恢复原450ms弹簧缩放/平移。用户指定以2.0.7对照：官方ZIP的VMenu/VSelect/Theme与原分支一致。首次打开前嵌套布局未结算，文字局部y从14变42的回归已复现；修复仅在原动画开始前设定content宽度并ensurePolished，移除打开后延迟重定位。字体hinting无效、软件layer裁剪等探索均撤回，不改全窗绘制或引擎节奏。保留点击选择、勾选、滚动、动态模型与边界定位，覆盖GPU/软件UI及不同DPI。
- 当前AMD NR在HDR working模式直接return false。不能只解除限制；复用HdrColor.hlsli中的203nit固定代理和HDR残差还原，模型仍只接受受限SDR线性FP16代理，原HDR亮度/宽色域基底保持。仅AMD+原生HDR工作路径分配资源；SDR/NVIDIA不改变原路径。共用已有层、残差/保护/时序与reset。
- 增加真实D3D12图/着色器的恒等和非恒等代理测试（fake AMD provider，不冒充HIP推理），覆盖多层/缩放/时序/强度/reset、HDR10/scRGB、高光/负色域/黑位及增强确实生效。保留AMD vendor与ABI检查。
- 必要引擎、图、着色器、每层资源、QML、翻译、构建和定向验收接点获当前用户修复授权。无AMD实卡，不称RX9000推理/画质已验；等待用户补充原生HDR或SDR转HDR症状时先推进独立工作。
- 单Agent、全E盘、复用构建-j2<=900秒/单测<=300秒，不停止用户进程、不动采集卡/真实配置/运行库模型驱动。不封包/main合并/push/发布，Lecram暂停。

证据目录E:/项目/Veyra/{reports,logs,tests,tmp}/menu-amd-hdr-fix-20261026。完成后在WORKLOG及本任务报告写明实际执行结果、失败与验收边界。
