# Player startup, playback rate and optional fullscreen protection

Current user authorises: put automatic fullscreen VRAM fallback behind a saved Settings switch, off by default; optionally resume the last source at application startup (movie position or remembered capture configuration); choose minimal/professional startup page; add real 1x/1.5x/2x/3x movie playback controls to both pages. This explicitly authorises the necessary audio/engine/QML/build integration beyond the historic UI-only freeze. Existing Xbox repair stays included; field VRAM root cause remains unconfirmed and public 2.0.1 publication remains pending. README stays unchanged.

Branch codex/player-startup-speed-20261003 starts at 15a0e39 in E:/项目/Veyra/worktrees/player-startup-speed-20261003. Baseline hashes are immutable in archives/player-startup-speed-20261003; task outputs, tests and process TEMP/TMP use E:/项目/Veyra/<purpose>/player-startup-speed-20261003. Checkpoint checkpoint/pre-player-startup-speed-20261003. No desktop/main changes.

- Protection only acts when explicitly enabled. Disabling also releases the current UI restriction; the switch does not claim to fix retained allocations.
- Startup reuse is opt-in, one attempt, and explicit command-line media takes priority. File/capture restoration uses the existing source and position code; unavailable inputs report failure. Existing startup-page preference is retained and grouped with automatic resume.
- Rate is a file-preview transport setting, default 1x. Capture/streams, image processing and export are not sped up. Media PTS stay in original coordinates; the device audio clock maps output samples to those coordinates. Speed changes use the existing seek/reset boundary. SoundTouch 2.4.1, fixed upstream commit 0047e0b1ecfceb041348579119bf79b73a322a3a, supplies pitch-preserving tempo; no proprietary runtime changes. The copied LGPL-2.1 sources remain unmodified, with attribution and licence, statically built from the corresponding Veyra source archive.
- Verify generated-tone pitch/duration, real WASAPI media-clock rates, switching/seek/pause, 1x regressions, startup file position and capture configuration, QML controls and default-off/opt-in VRAM behavior. Tests bounded to 300 seconds. Build and package candidate with identity audit; no fake real-device acceptance.

Status: implementation and local regressions complete; final build/package audit in progress. Evidence and exact commands are in WORKLOG and task artifacts. Field Xbox/VRR and 5060 Ti results remain unverified; no publication or integration into Claude/main worktrees.

Additional current user authorisation: investigate and fix repeated Xbox disconnects
from veyra-qml(13).log, and frame generation with console VRR passed through a
VRR-capable capture card. The user clarified this is the PS5/Switch capture input,
not the PC monitor's VRR setting. Claude works in other branches; all work here
stays in this isolated branch, with no changes to his worktrees or shared dependencies.
The scope guard is extended for the Xbox session/protocol and capture timing only;
the external baseline remains unchanged. Log evidence: SCTP errno=108 immediately
precedes the unhandled C++ exception at 14:56:05.080Z. libdatachannel 0.24.5 itself
closes after Disconnected, so an application grace timer would not recover it.
Check missing client frame feedback against the already pinned Greenlight source,
contain sends during shutdown, and test real local peers. Capture tests must vary
both PTS and arrival intervals; keep actual backward clocks, stalls, mailbox
boundaries, and compressed decode discontinuities effective. User-device VRR and
Xbox acceptance cannot be inferred from synthetic or local-peer tests.
