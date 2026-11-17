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

## 排查

- **提示「Veyra 是否在运行？」**：Veyra 没启动，或端口被占用。
  看 Veyra 日志里有没有 `[playback-server] listening on 127.0.0.1:18765`。
- **B 站返回错误码**：多半是视频本身（`62002` = 不存在/已删）。
- **端口冲突**：18765 被别的程序占用时，Veyra 日志会写
  `listen 127.0.0.1:18765 failed`，此时插件也无法工作。
