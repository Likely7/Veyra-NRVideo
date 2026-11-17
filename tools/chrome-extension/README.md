# 在 Veyra 播放（Chrome 插件）

把浏览器当前页面交给 Veyra 播放。对 B 站来说，插件只传**页面地址**，
由 Veyra 自己解析 CDN 流（带账号 cookie，会员画质可用）——插件不碰
`<video>` 元素，因为 B 站用的是 MediaSource，`blob:` 地址在浏览器外无效。

## 前置

- Veyra 2.1.9 或更新（含本地 HTTP 端点）
- Veyra 正在运行（它在 `127.0.0.1:18765` 监听；只绑本机）

## 安装（一次性）

1. 打开 Chrome / Edge 的扩展页：
   - Chrome：`chrome://extensions`
   - Edge：`edge://extensions`
2. 打开右上角「开发者模式」
3. 点「加载已解压的扩展程序」
4. 选择本目录（`chrome-extension`）
5. 插件栏出现「在 Veyra 播放」

## 使用

1. 在浏览器里打开 B 站（或任意网页视频页）
2. 点插件图标 → 「用 Veyra 播放当前页面」
3. Veyra 立刻开始解析并播放，插件弹窗自动关闭

## 可选：让插件自动启动 Veyra

插件默认只在 Veyra **已经在运行**时工作（走 `127.0.0.1:18765` 本地 HTTP）。
如果希望 Veyra 没开时点插件也能直接拉起它，需要注册一次 `veyra://` 协议：

1. 把 `install-protocol.cmd` 复制到 Veyra 程序目录（和 `veyra_qml_ui.exe` 同级）；
2. 双击运行一次（写 `HKCU\Software\Classes\veyra`，不需要管理员权限）；
3. 之后浏览器遇到 `veyra://` 链接会直接启动 Veyra 并传入网址与画质。

不注册也能用：只要先打开 Veyra，插件仍能正常播放（弹幕、字幕、画质都不受影响）。

## 画质

- **当前网页画质**（默认）：页面里注入的脚本记录 B 站播放器最近一次 `playurl?qn=`，点按钮时按它发送；
- **最高可用**：交给 Veyra 选最高档；
- 也可手动指定 8K / 杜比视界 / HDR 真彩 / 4K / 1080P / 720P / 480P / 360P。

## 登录与会员画质

插件会通过 `chrome.cookies` 读取 `bilibili.com` 的登录 cookie（`SESSDATA` 等）随请求发给
Veyra，所以**不需要在 Veyra 里再登录一次**；Chrome 首次会提示"读取 cookie"，需要允许。

## 排查

- **提示「Veyra 是否在运行？」**：Veyra 没启动，或端口被占用。
  看 Veyra 日志里有没有 `[playback-server] listening on 127.0.0.1:18765`。
- **B 站返回错误码**：多半是视频本身（`62002` = 不存在/已删）。
- **端口冲突**：18765 被别的程序占用时，Veyra 日志会写
  `listen 127.0.0.1:18765 failed`，此时插件也无法工作。
