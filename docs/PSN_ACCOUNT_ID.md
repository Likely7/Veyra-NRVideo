# PSN Account ID for PS5 Remote Play / PS5 远程游玩的 PSN Account ID

[English](#english) | [简体中文](#简体中文)

## English

Pairing Veyra with a PS5 needs the **Account ID** of a PSN user who has signed in on that console, plus the 8-digit code the PS5 shows under **Settings → System → Remote Play → Link Device**. The Account ID is a number, usually written in Base64 (12 characters, e.g. `FYHpffQQIhE=`). It is not a password, and you only need it once: it never changes.

### Get it with Sony's own sign-in, never a third-party site
Pick one. Both sign you in on Sony's page (`my.account.sony.com`), so your password and session never pass through anyone else.

1. **psn-account-id** (open source, standard library only, English/Español): https://github.com/metalito/psn-account-id
   - Windows: download `psn-account-id-windows-amd64.exe` from its Releases page and double-click it. A PowerShell script and a Python script are also available if you prefer to read the code first.
   - Sign in on Sony's page, paste the address you land on, and copy the **Base64** line it prints.
2. **chiaki-ng** (the open-source Remote Play client that Veyra's PS5 streaming is built on): https://github.com/chiaki-ng/chiaki-ng
   - Its console registration dialog has a built-in PlayStation sign-in that fills in the Account ID. Copy it from there.

Then paste the ID into Veyra's **PSN Account ID** field and enter the 8-digit code.

### Never paste your NPSSO cookie anywhere
Some Account ID lookup websites ask for your **NPSSO** cookie (from `ca.account.sony.com/api/v1/ssocookie`). That cookie **is your signed-in session**: whoever has it can act as you on PSN and read your purchases, email and real name. Veyra 2.1.6–2.2.0 linked to such a site (PSNTools, which requires the NPSSO to register). If you pasted your NPSSO there or anywhere else, open your PlayStation account settings, choose **Sign out of all devices**, change your password and turn on 2-step verification.

## 简体中文

Veyra 与 PS5 配对需要在该主机上登录过的 PSN 用户的 **Account ID**，以及 PS5 在 **设置 → 系统 → 远程游玩 → 关联设备** 中显示的 8 位配对码。Account ID 是一个数字，通常写成 Base64（12 个字符，例如 `FYHpffQQIhE=`）。它不是密码，只需获取一次：它永远不会改变。

### 只用索尼官方登录获取，不要用第三方网站
任选其一。两种方式都在索尼自己的页面（`my.account.sony.com`）登录，密码和登录会话不会经过任何第三方。

1. **psn-account-id**（开源，仅用标准库）：https://github.com/metalito/psn-account-id
   - Windows：在其 Releases 页面下载 `psn-account-id-windows-amd64.exe` 并双击运行。如果想先阅读代码，也有 PowerShell 和 Python 脚本版本。
   - 在索尼页面登录，把跳转后的完整地址粘贴回工具，复制输出的 **Base64** 那一行。
2. **chiaki-ng**（Veyra 的 PS5 串流所基于的开源远程游玩客户端）：https://github.com/chiaki-ng/chiaki-ng
   - 它的主机注册对话框内置 PlayStation 登录，会自动填好 Account ID，从那里复制即可。

然后把 ID 粘贴到 Veyra 的 **PSN Account ID** 栏，再输入 8 位配对码。

### 绝不要在任何地方粘贴 NPSSO cookie
有些 Account ID 查询网站会要求你提供 **NPSSO** cookie（来自 `ca.account.sony.com/api/v1/ssocookie`）。这个 cookie **就是你的登录会话**：拿到它的人可以以你的身份使用 PSN，并读取你的购买记录、邮箱和真实姓名。Veyra 2.1.6–2.2.0 曾链接到这类网站（PSNTools，注册时要求提供 NPSSO）。如果你曾在那里或其他地方粘贴过 NPSSO，请打开 PlayStation 账号设置，选择 **从所有设备注销**，修改密码并开启两步验证。
