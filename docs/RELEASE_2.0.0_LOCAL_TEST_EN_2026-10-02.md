# Veyra 2.0.0 — local stability test package

Full build of main `aafdeb061e45ae8cdaebfaf68e16c3d589771c05`, with the current QML interface, languages, export queue, audio/subtitle track support, PC streaming and experimental Xbox streaming. This local package uses version 2.0.0. It has not been published and does not contain the cancelled overlay compatibility changes.

Extract into a new writable folder and run **veyra_qml_ui.exe**. Keep your previous version intact. Qt, approved enhancement runtimes and applicable notices are included; developer tools are not required.

Start with effects disabled, then test your usual NR, super-resolution and frame-generation combinations. Check playback, pause/resume, seeking, fullscreen, source changes and clean shutdown. Also test export cancellation/retry and MP4/MKV audio/subtitle tracks. Watch for increasing VRAM usage, audio drift, freezes and crashes during longer sessions.

RTSS/GamePP injection remains incompatible: device loss and a separate D3D11/XeSS crash were observed. Disable their injection into Veyra for this test. Historical NR/TDR and VRAM growth reports remain unresolved. Short tests do not establish long-term or cross-hardware stability; physical capture devices and streaming hosts need their own validation.

Preserve the incident time, settings, GPU/driver version, `logs/veyra-qml.log` and any `veyra-crash-*.dmp`. These may contain local paths; nothing is uploaded automatically. Account credentials and personal settings are excluded from this package.

Experimental/community components do not imply official vendor certification. Runtime manifests audit the distributed files and do not lock user replacements. The component page uses static manifest metadata, not authoritative live module status. The separate Veyra source snapshot is not yet a complete dependency source distribution for public release.

See the separate `STABILITY_REPORT.md` for measured results and untested cases.

## Updated OBS compatibility and application icon

Settings → General & appearance includes OBS Game Capture compatibility (off by default). Changes are saved immediately. Yes in the restart prompt closes the player cleanly and relaunches it; No keeps the current session and applies the setting next time. Restart stops playback/streaming and is deferred while an export is running.

Software UI rendering avoids competing UI DXGI swapchains and simplifies some shadows/blur. Native D3D12 video and NR/SR/FG are unchanged. Game Capture targets video; use Windows 10 (1903+) Window Capture for the complete interface. The user confirmed the preceding candidate; 720 local resize operations passed across four enhancement configurations. This is not long-term or cross-hardware certification.

Includes the rounded gradient-black EXE icon and Qt window/taskbar icon. Built from the stated main plus local isolated fixes; not an unchanged main snapshot. No public release.
