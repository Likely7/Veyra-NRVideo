# 本机录制和截图控制

在 **设置 → 播放 → 允许本机控制录制和截图** 中开启，再点 **复制连接信息**。将地址和 `Authorization: Bearer …` 请求头填入本机工具。每次启动时功能关闭，每次开启会生成新的凭据。关闭功能会立即作废凭据，并保留正在进行的录制供软件手动停止。

地址默认为 `http://127.0.0.1:18765/control/`。使用本机原生 HTTP 客户端，并在每个请求中带上复制的 `Authorization` 请求头。写操作使用空请求体，也可用 `Content-Type: application/json` 发送 `{}`。

| 方法 | 路径 | 动作 |
|---|---|---|
| `GET` | `/control/state` | 查看录制、截图和即时回放状态 |
| `POST` | `/control/screenshot` | 按现有截图设置保存当前画面 |
| `POST` | `/control/record/start` | 按现有录制设置开始录制 |
| `POST` | `/control/record/stop` | 停止录制并交给录制器收尾 |
| `POST` | `/control/replay/save` | 写入现有即时回放缓冲，需先在软件开启即时回放 |

`202 accepted` 表示软件接受动作，文件仍由录制器或截图处理异步写入。重复开始或停止会返回 `200 unchanged`。状态接口返回 `recording`、`recordingFailed`、`replayEnabled`、`replaySaving`、`screenshotBusy` 和 `screenshotSaved`，均为布尔值。`409` 表示当前忙或操作不可用。`429` 表示请求过快，读请求至少间隔 250 毫秒，写请求至少间隔 300 毫秒。

接口沿用了 Arucious 提议的本机录制控制入口。 [PR #356](https://github.com/Likely7/Veyra-NRVideo/pull/356)
