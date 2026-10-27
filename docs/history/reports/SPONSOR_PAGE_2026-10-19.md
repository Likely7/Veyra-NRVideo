# 首页文案与“赞助作者”页（2026-10-19，Claude）

用户要求：首页“今天看点什么？”改为“Veyra NR Video”，下面的小字改为“本软件开源免费 如果对你有帮助你可以→请作者喝杯咖啡”，
点小字跳到设置里新增的赞助页；赞助页先放作者账号（B站、抖音、X、GitHub、YouTube，用图标和“前往主页”按钮，不显示链接），
下面放赞助码（绿底微信、蓝底支付宝）和 Ko-fi 咖啡按钮（与 GitHub README 上的一样）。“我们现在有点入不敷出，得多拉点赞助。”

分支 `claude/sponsor-20261019`（main `1dc8c18`），工作树 `E:/项目/Veyra/worktrees/sponsor-20261019`。没有合并 main、推送或发布。

## 改动

| 位置 | 内容 |
|---|---|
| `qml/Veyra/HomePage.qml` | 标题“Veyra NR Video”；小字“本软件开源免费，如果对你有帮助，你可以 → ☕ 请作者喝杯咖啡”，后半句强调色，整行可点（悬停下划线），发出 `requestSponsor` |
| `qml/Veyra/Main.qml` | `showSponsor()`：设置页切到 `sponsor` 分区再进入设置 |
| `qml/Veyra/SettingsPage.qml` | 左栏最后新增“赞助作者”（粉色心形图标，便于发现）；“关于”里原“交流群与赞助”按钮从未接通（只提示“尚未接入”），拆成“赞助作者（Ko-fi、微信与支付宝）→ 前往”（跳本页）和“交流群 → 查看”（打开项目 README，群二维码在那里） |
| `qml/Veyra/SponsorPanel.qml`（新） | 标题；强调色渐变说明卡；“关注作者”五个平台卡（品牌图标、名称、“前往主页”，整卡可点、悬停上浮）；“请作者喝杯咖啡”（金额随意，每一份支持都是很大的鼓励）先放 Ko-fi 官方样式“Buy me a coffee”按钮，下面“中国大陆用户 · 可以用微信或支付宝扫码”放两张码卡（220 px 宽，窄窗口自动换行）；“赞助完全自愿，不影响任何功能”脚注 |
| `src/ui/QmlPlayerBridge.cpp/.h` | `openSponsorLink(id)`：链接只写在 C++ 里（界面只传 id，不显示网址）；`openProjectPage()` 改为打开 `github.com/Likely7/Veyra-NRVideo#readme`；打不开浏览器时提示。测试开关 `VEYRA_TEST_SPONSOR_NO_BROWSER`（默认不生效）只记日志不开浏览器 |
| `qml/Veyra/sponsor/` | 品牌图标 SVG（`tools/qt_probe/make-sponsor-icons.py` 生成）与两张收款码 PNG |
| `IconData.js` / `icons.js` | 新增 Lucide `coffee`、`external` |
| `i18n/catalog.json` | 新增 20 条，繁中/英/日全部补齐；删除不再使用的旧文案 |

链接（2026-10-19 用户纠正 B 站 ID）：B站 `https://space.bilibili.com/19500532?spm_id_from=333.1007.0.0`；抖音 `v.douyin.com/9s06Bf2Inqc/`（用户粘贴的分享文案里只取链接，桌面浏览器跳到
“Likely7玩Ai的抖音”主页）；X `x.com/likely2002`；GitHub `github.com/Likely7`；YouTube `youtube.com/@Likely7Ai`；Ko-fi `ko-fi.com/likely7`。
2026-10-19 原验收记录为逐个请求均 HTTP 200；HTTP 状态不证明主页 ID 正确，本次按用户提供的正确地址更正。

## 第二轮（用户试看后）

用户：“应该是咖啡在前面，然后中国大陆用户可以使用两个二维码。”Ko-fi 移到“请作者喝杯咖啡”下第一位（去掉原来的“海外用户可以通过 Ko-fi 支持”），
两张码卡归到“中国大陆用户 · 可以用微信或支付宝扫码”小标题下；“关于”提示改为“Ko-fi、微信与支付宝”。组件测试和真实程序验收都加了顺序检查
（Ko-fi 在“中国大陆用户”之上，标题在码卡之上）。

## 素材来源

- 品牌图形：Simple Icons 16.34.0（npm，CC0-1.0，2026-10-04 发布），路径数据原样使用，只加底色块和配色；抖音沿用与 TikTok 相同的音符标志，
  按抖音配色叠青/红偏移；Ko-fi 杯子按其官方按钮拆层上色（白杯、深色描边、红心、白边）。原始文件在
  `E:/项目/Veyra/deps/claude-sponsor-20261019/simple-icons-16.34.0`。CC0 只覆盖图形本身，标志仍是各自所有者的商标，这里只用于链接作者自己的主页。
- 咖啡、外链图标：lucide-static 1.51.0（ISC），与项目已有 Lucide 图标同一许可。
- Ko-fi 按钮颜色取自 README 使用的官方按钮图 `kofi5.png`（底色 #72A4F2、心形 #FF5A16、描边 #202020），在界面里用矢量重画，不打包该图片。
- 收款码：用户本次提供的两张原图。统一为 572 px 宽并把圆角做进像素（软件渲染没有着色器遮罩）。脚本
  `E:/项目/Veyra/tmp/claude-sponsor-20261019/tools/qr_images.py` 逐张核对：处理后的图片、以及缩到 216 px 宽时，解出的收款内容与原图完全相同（只比较、不打印内容）。

## 验证

- 构建：`veyra_qml_ui`、`veyra_qml_quick_tests`（`E:/项目/Veyra/build/claude-sponsor-20261019`，118 秒）。
- Qt Quick 组件测试 88/88 通过，其中新增 `tst_sponsor_panel.qml` 4 项：六个入口各发出正确 id、10 张图片（含 SVG）全部加载且码图显示宽 220 px、Ko-fi 在中国大陆码卡之前、窄宽度下卡片换行不越界。
- 真实程序验收 `scripts/acceptance/sponsor-gui.py`（第二轮最终 `E:/项目/Veyra/logs/claude-sponsor-20261019/gui-20261019-111537`）：简中/英文 1280×900、日文/繁中 900×800、英文 900×800 150%，
  以及软件渲染界面（OBS 游戏捕获兼容，与 RTSS 等监控软件兼容模式同一路径）简中 1280×900，共 6 组全部通过。
  每组用投递到窗口的真实鼠标点击（不移动用户光标）：首页小字 → 设置“赞助作者”；五个平台卡和 Ko-fi 依次点击，日志按顺序记下六个正确网址；
  “关于 → 前往”回到本页。每张卡内文字不越界；从程序实际显示的码卡截图解码，微信、支付宝收款内容与原图一致。
- 首页和赞助页截图在上述日志目录各组的 `home.png`、`settings.png`、`panel.png`。
- 第二轮第一次全量运行时，软件渲染那一组失败过一次：点完 GitHub 后，测试脚本 0.6 秒后再找 YouTube 卡片时返回空（同一张卡片在上一步刚找到过；
  程序日志与通过时逐行相同，没有页面切换或报错）。之后单独连跑 7 次、全量 1 次均通过，GPU 界面从未出现。原因没有查明；脚本已在失败时输出
  当前页面、分区与面板是否在场景中，再出现时可以定位。

## 没有验证

- 自动测试用测试开关代替真正打开浏览器；真实点击打开默认浏览器需要用户在测试版里点一下确认。
- 用手机实际扫描屏幕上的码（已用 OpenCV 从程序截图解码验证）。
