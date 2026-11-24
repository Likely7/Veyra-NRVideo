# PSN Account ID for PS5 Remote Play / PS5 远程游玩的 PSN Account ID

[English](#english) | [简体中文](#简体中文)

## English

Pairing Veyra with a PS5 needs the **Account ID** of a PSN user who has signed in on that console, plus the 8-digit code the PS5 shows under **Settings → System → Remote Play → Link Device**. The Account ID is a number, usually written in Base64 (12 characters, e.g. `FYHpffQQIhE=`). It is not a password, and you only need it once: it never changes.

### Get it with Sony's own sign-in, never a third-party site
Enter your password only on Sony's sign-in page (`my.account.sony.com`).

1. **chiaki-ng** is the open-source Remote Play client Veyra's PS5 streaming is built on. In its console registration dialog, sign in to PlayStation and copy the Account ID. [chiaki-ng repository](https://github.com/chiaki-ng/chiaki-ng)
2. **psn-account-id** is an independent, recently launched open-source project. Review its PowerShell or Python source before running it locally. The tool asks for the redirect URL after Sony sign-in and prints the ID in Base64. Keep the redirect URL private. [psn-account-id repository](https://github.com/metalito/psn-account-id)

Then paste the ID into Veyra's **PSN Account ID** field and enter the 8-digit code.

### Never paste your NPSSO cookie anywhere
Some Account ID lookup websites ask for your **NPSSO** cookie (from `ca.account.sony.com/api/v1/ssocookie`). That cookie **is your signed-in session**: whoever has it can act as you on PSN and read your purchases, email and real name. Veyra 2.1.6–2.2.0 linked to such a site (PSNTools, which requires the NPSSO to register). If you pasted your NPSSO there or anywhere else, open your PlayStation account settings, choose **Sign out of all devices**, change your password and turn on 2-step verification.

## 简体中文

Veyra 与 PS5 配对需要在该主机上登录过的 PSN 用户的 **Account ID**，以及 PS5 在 **设置 → 系统 → 远程游玩 → 关联设备** 中显示的 8 位配对码。Account ID 是一个数字，通常写成 Base64（12 个字符，例如 `FYHpffQQIhE=`）。它不是密码，只需获取一次：它永远不会改变。

### 只用索尼官方登录获取，不要用第三方网站
请仅在索尼登录页（`my.account.sony.com`）输入密码。

1. **chiaki-ng** 是 Veyra 的 PS5 串流所基于的开源客户端。在它的主机注册窗口登录 PlayStation，再复制自动填好的 Account ID。[chiaki-ng 项目页](https://github.com/chiaki-ng/chiaki-ng)
2. **psn-account-id** 是近期发布的第三方开源项目。可以先查看其 PowerShell 或 Python 源码，再在本机运行。它读取索尼登录后跳转的完整地址并输出 Base64 ID。请妥善保管跳转地址。[psn-account-id 项目页](https://github.com/metalito/psn-account-id)

然后把 ID 粘贴到 Veyra 的 **PSN Account ID** 栏，再输入 8 位配对码。

### 绝不要在任何地方粘贴 NPSSO cookie
有些 Account ID 查询网站会要求你提供 **NPSSO** cookie（来自 `ca.account.sony.com/api/v1/ssocookie`）。这个 cookie **就是你的登录会话**：拿到它的人可以以你的身份使用 PSN，并读取你的购买记录、邮箱和真实姓名。Veyra 2.1.6–2.2.0 曾链接到这类网站（PSNTools，注册时要求提供 NPSSO）。如果你曾在那里或其他地方粘贴过 NPSSO，请打开 PlayStation 账号设置，选择 **从所有设备注销**，修改密码并开启两步验证。
