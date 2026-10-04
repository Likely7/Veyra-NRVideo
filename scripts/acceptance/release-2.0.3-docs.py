"""Versioned docs and UI translations, including the user's shared-FSR exception."""
import json
from pathlib import Path
ROOT = Path(__file__).resolve().parents[2]
def edit(name, transform):
    p = ROOT / name; old = p.read_text(encoding='utf8'); new = transform(old)
    assert old != new, name
    p.write_text(new, encoding='utf8')

def english(t):
    t = t.replace('2.0.2', '2.0.3')
    t = t.replace('## New in 2.0.3\n', '''## New in 2.0.3

- **GPU packages:** choose NVIDIA or AMD. AMD NR assets and NVIDIA-only NR/NGX/VFG/CUDA components are separated. Both keep the existing shared FidelityFX 2.3.0 components and cross-vendor FSR3.1/XeSS; FSR4 stays gray on NVIDIA.
- **Visible compatibility:** four NR versions, SR backends, FG backends, RTX HDR and optical flow retain unsupported entries with a disabled reason in List/Node mode. Restoring old configurations disables unavailable stages and preserves their parameters.
- **VFG and field fixes:** all 2–8X multipliers and Low/Medium/High; Xbox audio startup and bounded recovery; bitrate draft/validation/frozen exports; opaque RTSS-compatible UI; removed false NVIDIA App crash detection; fixed the minimal window's right-edge gap. Nine unrelated VFG NPP libraries are omitted. RX9000 inference and Xbox hardware long sessions remain unverified; HDR/Dolby PRs are deferred.

''')
    t = t.replace('Download **Veyra-2.0.3-win64-portable.zip**, extract into a new writable folder and run **veyra_qml_ui.exe**. This is the only release asset you need to run the application.', 'Download **Veyra-2.0.3-NVIDIA-win64-portable.7z** or **Veyra-2.0.3-AMD-win64-portable.7z** for Veyra\'s active GPU. Extract with 7-Zip into a new writable folder and run **veyra_qml_ui.exe**. You need one GPU package to run the application; source assets are only for rebuilding.')
    t = t.replace('The NR version selector offers RTX 50 · NVIDIA original, RTX 50 · Lecram and RTX 20–50 · SF-v2 in both List and Node mode. It switches the entire NR chain, retaining the existing default.', 'The NR version selector offers RTX 50 · NVIDIA original, RTX 50 · Lecram, RTX 20–50 · SF-v2 and RX9000 · lmxxf (experimental) in both List and Node mode. Unsupported versions remain visible but gray; switching supported versions updates the entire NR chain. AMD NR requires driver HIP 7 and is limited to its 1080p pixel budget; HDR/native 1440p or 4K NR export is unavailable.')
    t = t.replace('three NR runtime choices (NVIDIA original for RTX 50, Lecram and SF-v2)', 'four NR runtime choices (NVIDIA original for RTX 50, Lecram, SF-v2 and AMD lmxxf)')
    return t
edit('README.md', english)

def chinese(t):
    t = t.replace('2.0.2', '2.0.3')
    t = t.replace('## 2.0.3 新增与修复\n', '''## 2.0.3 新增与修复

- **显卡分包**：NVIDIA 包不带 AMD NR；AMD 包不带 NVIDIA NR/NGX/VFG/CUDA。按用户追加决定，两包都保留原有 FidelityFX 2.3.0 共用组件，FSR3.1/XeSS 可用，NVIDIA 的 FSR4 入口置灰。
- **功能可见但禁用**：NR 四版本、超分、补帧、RTX HDR、光流与节点添加统一判断硬件/组件，并显示禁用原因。恢复旧配置关闭不可用效果，保留参数。
- **VFG 与现场修复**：VFG 全部 2–8X、低/中/高；Xbox 音频启动与有界恢复；导出码率草稿/验证/worker 冻结；小飞机兼容背景不透明；移除 NVIDIA App 插件误判；修复极简右侧像素黑边。删除九个 VFG 不使用的 NPP 库。RX9000 推理和 Xbox 真机长稳仍待验，HDR/杜比 PR 暂缓。

''')
    t = t.replace('下载 **Veyra-2.0.3-win64-portable.zip**，完整解压到可写新目录，运行 **veyra_qml_ui.exe**。运行只需这一个下载包。', '按 Veyra 实际使用的显卡下载 **Veyra-2.0.3-NVIDIA-win64-portable.7z** 或 **Veyra-2.0.3-AMD-win64-portable.7z**，用 7-Zip 完整解压到可写新目录，运行 **veyra_qml_ui.exe**。运行只需一个显卡包，源码包仅供重编译。')
    t = t.replace('列表与节点的 **NR 版本** 均可选 RTX 50 · NVIDIA 原版、RTX 50 · Lecram、RTX 20–50 · SF-v2，切换时全 NR 链同步，原有默认选择保留。', '列表与节点的 **NR 版本** 均可选四项：RTX 50 · NVIDIA 原版、RTX 50 · Lecram、RTX 20–50 · SF-v2、RX9000 · lmxxf（实验）。不支持的版本保留置灰，支持的版本切换时全 NR 链同步。AMD NR 需要驱动 HIP 7，内部最高 1080p 像素预算，HDR/原生 1440p/4K NR 导出不支持。')
    return t
edit('README_CN.md', chinese)

def build(t):
    a = t.index('\n- Official FidelityFX SDK 1.1.4'); b = t.index('\n- MIT VFG sample reference', a)
    t = t[:a] + '\n- Both GPU packages retain the exact existing FidelityFX SDK 2.3.0 loader, upscaler and framegeneration components, at the user\'s explicit request to keep FSR3.1/4 together. Corresponding 2.3.0 sources are in the unchanged dependency archive. NVIDIA only exposes FSR3.1; FSR4 remains disabled there. No legacy SDK replacement or ABI compatibility change is shipped.\n' + t[b:]
    return t.replace(', `--fsr31`', '').replace('、官方 FSR 3.1 源码', '（FSR 2.3.0 对应源码沿用）')
edit('docs/BUILD_2.0.3.md', build)

def components(t):
    t = t.replace('Official SDK 1.1.4 signed 3.1-only DLL', 'Existing SDK 2.3.0 compatibility provider')
    t = t.replace('Gray; no FSR4 ML DLL/model payload', 'Gray; shared FidelityFX components retained by explicit user choice')
    a = t.index('NVIDIA FSR 3.1 uses'); b = t.index('\n\nAMD NR source', a)
    t = t[:a] + 'Both packages retain the existing signed SDK 2.3.0 loader, FSR4.1.1 SR provider and FSR4.0.1 FG provider, including FSR3.1 compatibility. The user explicitly requested keeping FSR3.1/4 together instead of splitting the runtime. NVIDIA exposes FSR3.1 and keeps FSR4 ML disabled; AMD exposes the ML paths only for qualifying RX9000 hardware. Each DLL keeps its existing approved identity. No old SDK replacement, DLL patch or new ABI compatibility branch is shipped.' + t[b:]
    return t.replace('NVIDIA 包不带 AMD NR 与 FSR4 ML；', 'NVIDIA 包不带 AMD NR；两包保留原有 FSR3.1/4 共用组件，NVIDIA 的 FSR4 ML 仍禁用；')
edit('docs/RUNTIME_COMPONENTS_2.0.3.md', components)

def notes(t):
    t = t.replace('不携带 AMD NR 或 FSR4 ML。FSR3.1 换为官方独立原件，约 6.4 MiB。', '不携带 AMD NR。按用户追加决定保留原有 FSR3.1/4 共用组件，NVIDIA 的 FSR4 选项仍置灰。')
    return t.replace('AMD NR and FSR4 ML assets are excluded. The standalone official FSR3.1 runtime is about 6.4 MiB.', 'AMD NR assets are excluded. The existing shared FidelityFX 2.3.0 runtime is retained by explicit user choice; FSR4 ML remains disabled on NVIDIA.')
edit('docs/RELEASE_NOTES_2.0.3.md', notes)
edit('docs/RELEASE_2.0.3_PLAN_2026-10-04.md', lambda t: t.replace('NVIDIA 不带 AMD NR/HIP 或 FSR4 ML，AMD 不带 DLSS/NGX/NVOF/VFG/CUDA。', 'NVIDIA 不带 AMD NR/HIP，AMD 不带 DLSS/NGX/NVOF/VFG/CUDA。用户追加决定保留现有 FSR3.1/4 共用组件，两包均使用已核验 SDK 2.3.0 三 DLL；NVIDIA 的 FSR4 ML 仍可见置灰。旧 SDK 独立 FSR3.1 研究与兼容试改已撤下，不进入最终产品。'))

def notices(t):
    t = t.replace('Only the fourteen native VFG dependencies are used;', 'Only the five VFG-required native dependencies are used; nine unrelated NPP libraries are omitted;')
    t = t.replace('The local candidate carries its NVIDIA software/AI/model terms and third-party\nnotices with a separate `vfg-runtime-manifest.json`; public redistribution has\nnot been authorized or established by this integration.', 'The 2.0.3 NVIDIA package carries its NVIDIA software/AI/model terms and third-party\nnotices with a separate `vfg-runtime-manifest.json`. The user explicitly authorized\nthis experimental Release distribution; this is not vendor certification or a general\nredistribution grant, and the integration is not an independent legal audit.')
    t = t.replace('AMD NR is not available in the current release.', 'The 2.0.3 AMD package includes the lmxxf runtime and user-supplied 0.39 assets; actual RX9000 inference remains unverified.')
    return t.replace('local candidate does not authorize a public release.', '2.0.3 AMD package follows the user\'s explicit experimental distribution authorization; open-source code does not relicense these models.')
edit('THIRD_PARTY_NOTICES.md', notices)

entries = [
 ('当前包缺少此功能的运行组件','目前套件缺少此功能的執行元件','This package lacks the runtime components for this feature','このパッケージに必要なランタイムがありません'),
 ('需要 NVIDIA RTX 50 / Blackwell','需要 NVIDIA RTX 50 / Blackwell','Requires NVIDIA RTX 50 / Blackwell','NVIDIA RTX 50 / Blackwell が必要です'),
 ('需要 NVIDIA RTX 显卡','需要 NVIDIA RTX 顯示卡','Requires an NVIDIA RTX GPU','NVIDIA RTX GPU が必要です'),
 ('需要 AMD RX 9000','需要 AMD RX 9000','Requires AMD RX 9000','AMD RX 9000 が必要です'),
 ('需要 AMD HIP 7 驱动运行组件','需要 AMD HIP 7 驅動執行元件','Requires the AMD HIP 7 driver runtime','AMD HIP 7 ドライバーランタイムが必要です'),
 ('需要支持 DirectX 12 的硬件显卡','需要支援 DirectX 12 的硬體顯示卡','Requires a hardware GPU with DirectX 12 support','DirectX 12 対応のハードウェア GPU が必要です'),
 ('需要 NVIDIA RTX 40 / 50（Ada / Blackwell）','需要 NVIDIA RTX 40 / 50（Ada / Blackwell）','Requires NVIDIA RTX 40 / 50 (Ada / Blackwell)','NVIDIA RTX 40 / 50（Ada / Blackwell）が必要です'),
 ('需要 NVIDIA RTX 显卡及 NVOF 驱动','需要 NVIDIA RTX 顯示卡及 NVOF 驅動','Requires an NVIDIA RTX GPU and the NVOF driver','NVIDIA RTX GPU と NVOF ドライバーが必要です'),
 ('FSR 3.1 / 4 · 自动兼容','FSR 3.1 / 4 · 自動相容','FSR 3.1 / 4 · Automatic compatibility','FSR 3.1 / 4 · 自動互換'),
 ('超分算法','超解析演算法','Upscaling algorithm','アップスケーリング方式'),
 ('AMD NR 最高 1080p 像素预算','AMD NR 最高 1080p 像素預算','AMD NR has a maximum 1080p pixel budget','AMD NR の上限は 1080p の画素数です'),
 ('当前硬件或包不支持所选效果，请查看组件页','目前硬體或套件不支援所選效果，請查看元件頁','The hardware or package does not support this effect; check Components','選択した効果は現在の GPU またはパッケージに対応していません。コンポーネントを確認してください'),
 ('已关闭当前硬件或包不支持的效果，参数仍保留：','已關閉目前硬體或套件不支援的效果，參數仍保留：','Unavailable effects have been disabled; their parameters are preserved: ','非対応の効果を無効にしました。設定値は保持されています：')]
p = ROOT / 'i18n/catalog.json'; catalog = json.loads(p.read_text(encoding='utf8'))
known = {e['zh'] for e in catalog['entries']}
for zh, tw, en, ja in entries:
    if zh not in known:
        catalog['entries'].append(dict(zh=zh, src='src/ui/QmlPlayerBridge.cpp; qml/Veyra', **{'zh-TW':tw,'en':en,'ja':ja}))
catalog['entries'].sort(key=lambda e:e['zh'])
p.write_text(json.dumps(catalog, ensure_ascii=False, indent=1) + '\n', encoding='utf8')
print('2.0.3 docs and translations updated; FSR exception retained')
