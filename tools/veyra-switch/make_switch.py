#!/usr/bin/env python3
"""从「作者原件 vs 本仓库定制构建产物」的实际差异，生成双版本切换脚本。

为什么需要它
------------
切换脚本要替换的文件集合，**每个上游版本都可能不同**：本仓库可能改了新的着色器、
新增了界面文件，或上游改动导致某个原本逐字节一致的着色器不再一致。照抄上一版脚本会漏文件，
而漏掉的后果是"两个版本混着跑"（例如切到作者版后，某个着色器仍是我们编译的）。

所以：先逐文件比对，再由比对结果生成脚本。每次升级都要跑一遍。

比对范围（递归，含子目录）
--------------------------
    veyra_qml_ui.exe        程序目录\\veyra_qml_ui.exe    vs 构建目录\\veyra_qml_ui.exe
    shaders\\**\\*.dxil     程序目录\\shaders\\**          vs 构建目录\\shaders\\**
                            （含 shaders\\dis\\ 子目录，合计 44 个：顶层 25 + dis 19）
    qml\\Veyra\\**          程序目录\\qml\\Veyra\\**       vs 源码仓库\\qml\\Veyra\\**

字节相同的文件**不进入**切换集合（没有切换的必要）。不同的：非界面文件逐个 `copy`
（备份为 `*.orig` / `*.p5`）；界面按**整目录** `robocopy /MIR` 同步（备份为
`qml.orig\\Veyra` / `qml.p5\\Veyra`），因为界面文件之间会互相引用，逐个替换没有意义。

用法
----
    # 只看差异、列出将进入切换集合的文件（不改任何东西）
    python make_switch.py --package <程序目录> --build <构建目录> --repo <源码目录>

    # 备份 + 生成两份 .cmd + 生效设为定制版
    python make_switch.py --package ... --build ... --repo ... --apply

前提：`--package` 当前应当是**作者原版**（刚被新版本覆盖过）。若已是定制版，比对无意义，
工具会检测并提醒。
"""
from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import sys

EOL = '\r\n'


def sha(path: str) -> str | None:
    try:
        with open(path, 'rb') as f:
            return hashlib.sha256(f.read()).hexdigest()
    except OSError:
        return None


def size(path: str) -> int:
    try:
        return os.path.getsize(path)
    except OSError:
        return 0


def walk_rel(root: str, exts: tuple[str, ...] | None = None) -> dict[str, str]:
    """递归收集相对路径 -> 绝对路径。"""
    out: dict[str, str] = {}
    for dp, _dn, fn in os.walk(root):
        for f in fn:
            if exts and not f.lower().endswith(exts):
                continue
            p = os.path.join(dp, f)
            out[os.path.relpath(p, root)] = p
    return out


def collect_diff(package: str, build: str, repo: str):
    """返回 (非界面差异, 界面差异, 提示)。差异项为 (相对路径, 作者原件, 定制产物)。"""
    other: list[tuple[str, str, str]] = []
    qml: list[tuple[str, str, str]] = []
    notes: list[str] = []

    # --- 主程序 ---
    a = os.path.join(package, 'veyra_qml_ui.exe')
    b = os.path.join(build, 'veyra_qml_ui.exe')
    if not os.path.isfile(b):
        notes.append('★ 构建目录里没有 veyra_qml_ui.exe，请先编译')
    elif not os.path.isfile(a):
        notes.append('★ 程序目录里没有 veyra_qml_ui.exe')
    elif sha(a) == sha(b):
        notes.append('★ 主程序与构建产物逐字节相同 —— 请确认当前程序目录是作者原版、且构建正确')
    else:
        other.append(('veyra_qml_ui.exe', a, b))

    # --- 着色器（递归，含 dis\ 子目录） ---
    fa = walk_rel(os.path.join(package, 'shaders'), ('.dxil',))
    fb = walk_rel(os.path.join(build, 'shaders'), ('.dxil',))
    same = 0
    for rel in sorted(set(fa) | set(fb)):
        if rel not in fb:
            notes.append('★ 包里有、构建没有的着色器: shaders\\%s' % rel)
            continue
        if rel not in fa:
            notes.append('★ 构建有、包里没有的着色器: shaders\\%s' % rel)
            continue
        if sha(fa[rel]) != sha(fb[rel]):
            other.append((os.path.join('shaders', rel), fa[rel], fb[rel]))
        else:
            same += 1
    n_top = sum(1 for r in set(fa) & set(fb) if os.sep not in r)
    notes.append('着色器比对 %d 个（顶层 %d + dis 子目录 %d）：一致 %d，不同 %d'
                 % (len(set(fa) & set(fb)), n_top, len(set(fa) & set(fb)) - n_top,
                    same, len(other) - (1 if any(r[0] == 'veyra_qml_ui.exe' for r in other) else 0)))

    # --- 界面（整目录同步；只把真正不同的列出来） ---
    qa = os.path.join(package, 'qml', 'Veyra')
    qb = os.path.join(repo, 'qml', 'Veyra')
    if not os.path.isdir(qb):
        notes.append('★ 源码仓库里找不到 qml\\Veyra')
    else:
        ga, gb = walk_rel(qa), walk_rel(qb)
        for rel in sorted(set(ga) | set(gb)):
            av, bv = ga.get(rel), gb.get(rel)
            if bv is None:
                notes.append('界面: 定制版没有、切换时会从作者版删除: %s' % rel)
                qml.append((rel, av or '', ''))
            elif av is None:
                notes.append('界面: 定制版新增: %s' % rel)
                qml.append((rel, '', bv))
            elif sha(av) != sha(bv):
                qml.append((rel, av, bv))
        notes.append('界面比对 %d 个文件：不同 %d 个（切换时整目录同步，不逐个替换）'
                     % (len(set(ga) | set(gb)), len(qml)))
    return other, qml, notes


def write_gbk_crlf(path: str, text: str) -> None:
    with open(path, 'wb') as f:
        f.write(text.replace('\n', EOL).encode('gbk'))


def write_utf8(path: str, text: str, bom: bool = False) -> None:
    enc = 'utf-8-sig' if bom else 'utf-8'
    with open(path, 'wb') as f:
        f.write(text.replace('\n', EOL).encode(enc))


def build_cmd(is_p5: bool, other: list[tuple[str, str, str]], qml_diff) -> str:
    suffix = '.p5' if is_p5 else '.orig'
    src_dir = 'qml.p5' if is_p5 else 'qml.orig'
    title = ('P5 / HDR 定制版（本仓库构建：Dolby Vision Profile 5 正确转换、HDR 亮度曲线 /'
             '七档预设 / 强度 / 显示器峰值 / HDR10 元数据）' if is_p5 else
             '作者完整版（作者发行版原样，含 Remote Play / Moonlight / Xbox / FSR / XeSS）')
    files = [rel for rel, _a, _b in other]

    L: list[str] = ['@echo off', 'chcp 936 >nul', 'setlocal', 'cd /d "%~dp0"', '',
                    'rem ============================================================',
                    'rem  ' + title,
                    'rem ============================================================', '',
                    'rem --- 运行中预检：程序在跑时任何替换都可能只成功一半，所以先拦住 ---',
                    'tasklist /fi "imagename eq veyra_qml_ui.exe" 2>nul | find /i "veyra_qml_ui.exe" >nul',
                    'if not errorlevel 1 (',
                    '  echo [错误] Veyra 正在运行。请先完全退出程序再运行本脚本，',
                    '  echo        否则 exe、着色器、界面可能只替换一部分，造成版本不一致。',
                    '  pause',
                    '  exit /b 1',
                    ')', '',
                    'rem --- 存在性检查（缺一个都不动，避免切到一半） ---']
    for rel in files:
        L += ['if not exist "%s%s" (' % (rel, suffix),
              '  echo [错误] 找不到 %s%s，无法切换。' % (rel, suffix),
              '  pause',
              '  exit /b 1',
              ')']
    L += ['if not exist "%s\\Veyra" (' % src_dir,
          '  echo [错误] 找不到 %s\\Veyra 文件夹，无法切换。' % src_dir,
          '  pause',
          '  exit /b 1',
          ')', '', 'rem --- 替换 ---']
    for rel in files:
        L.append('copy /y "%s%s" "%s" >nul 2>nul' % (rel, suffix, rel))
    L += ['if errorlevel 1 (',
          '  echo [错误] 文件替换失败，请先完全退出 Veyra 再运行本脚本。',
          '  pause',
          '  exit /b 1',
          ')',
          'rem 界面整目录同步（定制版与作者版有 %d 个文件不同）' % len(qml_diff),
          'robocopy "%s\\Veyra" "qml\\Veyra" /MIR /NFL /NDL /NJH /NJS /NP >nul' % src_dir,
          'if errorlevel 8 (',
          '  echo [错误] 界面文件同步失败，请先完全退出 Veyra 再运行本脚本。',
          '  pause',
          '  exit /b 1',
          ')', '']
    L.append('echo 已切换到 ' + ('P5 / HDR 定制版。' if is_p5 else '作者完整版。'))
    if is_p5:
        L += ['echo   * Dolby Vision Profile 5 正确转换（不再偏绿）',
              'echo   * HDR 亮度曲线 + 七档预设 + 强度 + 显示器峰值 + HDR10 元数据',
              'echo   * 首页清空、构建适配等本仓库定制']
    else:
        L += ['echo   注意：此版本不含 Dolby Vision Profile 5 转换，播放 P5 片源会偏绿；',
              'echo         也没有 HDR 亮度曲线等定制。日常看片建议用「启动-P5修复版.cmd」。']
    L += ['echo 正在启动 Veyra ...',
          # 两个版本必须各用自己的数据目录：本仓库给 chain session / 预设库扩展了字段并占用了
          # 新的版本号，上游的解码器读不了我们写的文件，会判定"模式会话损坏，已拒绝覆盖原文件"。
          # 数据目录是全部用户状态的唯一根（会话、界面偏好、预设库），所以隔离它即可。
          'start "" "veyra_qml_ui.exe" --data-dir "%%~dp0runtime_local\\user-data-2.0.0-%s" %%*'
          % ('p5' if is_p5 else 'author'),
          'exit /b 0', '']
    return '\n'.join(L)


def build_switch_doc(other, qml_diff, pkg_name: str) -> str:
    lines = [
        'Veyra 双版本切换说明（%s + 本仓库定制）' % pkg_name,
        '=' * 50,
        '',
        '两份 .cmd 用来在同一套程序上切换两个版本。**本文件由 tools/veyra-switch/make_switch.py',
        '按「作者原件 vs 定制构建产物」的实际差异生成**，因此下面列出的就是全部会被替换的文件；',
        '没有列出的文件两个版本完全相同，不需要切换（若某次升级后着色器差异变多，脚本会自动多出对应行）。',
        '',
        '会被替换的文件：',
        '',
    ]
    for rel, a, b in other:
        lines.append('    %-34s 作者 %9d B / 定制 %9d B' % (rel, size(a), size(b)))
    lines.append('    %-34s %d 个文件不同（整目录同步）' % ('qml\\Veyra\\', len(qml_diff)))
    lines += [
        '',
        '备份命名：',
        '',
        '    *.orig          作者原版（veyra_qml_ui.exe.orig、各 .dxil.orig）',
        '    *.p5            本仓库定制版（veyra_qml_ui.exe.p5、各 .dxil.p5）',
        '    qml.orig\\Veyra   作者原版界面',
        '    qml.p5\\Veyra     本仓库定制界面',
        '',
        '',
        '1) 启动-P5修复版.cmd      ← 日常看片用这个',
        '   ------------------------------------------------',
        '   本仓库基于作者发行包源码构建的定制版，包含：',
        '     * Dolby Vision Profile 5 按 IPT-PQ-C2 正确转换（不再偏绿）',
        '     * HDR 亮度曲线 + 七档一键预设 + 强度旋钮 + 显示器峰值',
        '     * HDR10 静态元数据（母版峰值 / MaxCLL / MaxFALL）',
        '     * 首页清空、构建适配等本仓库的其它定制',
        '',
        '   本版按「本地已有 SDK」构建：含 NVIDIA NR(DLSSNR)、DLSS SR、DLSS FG、NVOF、VFG；',
        '   不含 Remote Play / Moonlight / Xbox / FSR / XeSS（本地没有它们的依赖源码）。',
        '   界面里 PS5 / Xbox 相关对话框会因为对应功能未编译而显示属性缺失提示，属既有现象。',
        '',
        '   VFG（视频插帧）说明：编译期开关，需要 CUDA driver 开发头',
        '   （CMake 变量 VEYRA_CUDA_DRIVER_INCLUDE_DIR，指向含 cuda.h 的目录）。',
        '   实测建议：VFG 倍数用 2X–4X（6X 会触发大量时间线重置）；诊断插帧必须用带音轨素材。',
        '',
        '',
        '2) 启动-作者完整版.cmd    ← 需要串流等功能时用',
        '   ------------------------------------------------',
        '   作者发行版原样，功能齐全（含 PS5 Remote Play / Moonlight / Xbox 串流 / FSR / XeSS）。',
        '   不含本仓库任何定制：没有 P5 转换（播 P5 会偏绿）、没有 HDR 亮度曲线等。',
        '',
        '',
        '3) 两者关系（重要：两个版本各用自己的数据目录）',
        '   ------------------------------------------------',
        '   同一套程序目录、同一套运行时（runtime\\ 与 runtime_local\\ 共用，切换不动它们）；',
        '   两个 exe 基于作者同一份源码基线，区别在编译选项与本仓库的定制。',
        '',
        '   设置与会话**分开存放**，两个脚本各自带 --data-dir 指定：',
        '',
        '     本仓库定制版  runtime_local\\user-data-2.0.0-p5',
        '     作者完整版    runtime_local\\user-data-2.0.0-author',
        '',
        '   为什么必须分开：本仓库给链会话（模式会话）和预设库**扩展了字段、占用了新的版本号**，',
        '   上游的解码器读不了我们写的文件。若两者共用一个目录，切到作者版时会读到我们写的会话，',
        '   字段错位导致解析失败，作者版会判定文件损坏并拒绝保存，界面弹出',
        '   「当前修改尚未保存：模式会话损坏，已拒绝覆盖原文件」。分开存放后互不干扰。',
        '   代价是两个版本的设置各自独立（格式不同，本来也无法互通）。',
        '   请始终用这两个 .cmd 启动 —— 直接双击 veyra_qml_ui.exe 会用回共用的默认目录。',
        '',
        '',
        '4) 出问题时',
        '   ------------------------------------------------',
        '   * 脚本会先检查程序是否在运行；若在运行会直接提示并退出，不会动任何文件。',
        '     这是为了避免只替换一半（exe / 着色器 / 界面不匹配）。请完全退出 Veyra 后再运行。',
        '   * 若仍提示「无法写入」，用任务管理器结束 veyra_qml_ui.exe 再试。',
        '   * 想知道当前是哪个版本：看 veyra_qml_ui.exe 的大小（定制版约 5.6 MB，作者版更大）。',
        '   * 想手动还原作者原版：把各 *.orig 复制回原名，并用 qml.orig\\Veyra 覆盖 qml\\Veyra。',
        '',
        '',
        '构建信息见同目录「本机构建说明.txt」；完整定制与踩坑记录见源码仓库 CUSTOM_LEDGER.md。',
    ]
    return '\n'.join(lines) + '\n'


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument('--package', required=True)
    ap.add_argument('--build', required=True)
    ap.add_argument('--repo', required=True)
    ap.add_argument('--apply', action='store_true')
    ap.add_argument('--pkg-label', default='', help='写进说明文件的版本标签，如 "NVIDIA 2.0.4"')
    args = ap.parse_args()

    for label, p in (('程序目录', args.package), ('构建目录', args.build), ('源码目录', args.repo)):
        if not os.path.isdir(p):
            print('找不到%s: %s' % (label, p))
            return 2

    live = os.path.join(args.package, 'veyra_qml_ui.exe')
    built = os.path.join(args.build, 'veyra_qml_ui.exe')
    if os.path.isfile(live) and os.path.isfile(built) and sha(live) == sha(built):
        print('提醒：程序目录当前的 exe 已与构建产物相同（看起来已是定制版）。')
        print('      若这不是你预期的状态，请先用 *.orig 还原成作者原版再运行本工具。')
        print()

    other, qml_diff, notes = collect_diff(args.package, args.build, args.repo)
    print('=' * 78)
    print('差异比对结果')
    print('=' * 78)
    for n in notes:
        print('  ' + n)
    print()
    print('  将进入切换集合的文件：')
    for rel, a, b in other:
        print('     %-38s 作者 %9d B / 定制 %9d B' % (rel, size(a), size(b)))
    print('     %-38s %d 个文件不同（整目录同步）' % ('qml\\Veyra\\', len(qml_diff)))
    for rel, a, b in qml_diff:
        print('        %-32s 作者 %7d / 定制 %7d' % (rel, size(a), size(b)))

    if not args.apply:
        print()
        print('（未加 --apply，未改动任何文件）')
        return 0

    print()
    print('=' * 78)
    print('执行备份与脚本生成')
    print('=' * 78)
    for rel, a, b in other:
        if not (a and os.path.isfile(a) and os.path.isfile(b)):
            print('  ★ 跳过（缺文件）: %s' % rel)
            continue
        shutil.copy2(a, os.path.join(args.package, rel + '.orig'))
        shutil.copy2(b, os.path.join(args.package, rel + '.p5'))
        print('  %-38s -> .orig / .p5' % rel)

    qo = os.path.join(args.package, 'qml.orig', 'Veyra')
    qp = os.path.join(args.package, 'qml.p5', 'Veyra')
    for target, src in ((qo, os.path.join(args.package, 'qml', 'Veyra')),
                        (qp, os.path.join(args.repo, 'qml', 'Veyra'))):
        if os.path.isdir(target):
            shutil.rmtree(target)
        shutil.copytree(src, target)
        print('  %-38s -> %d 个文件' % (os.path.relpath(target, args.package), len(os.listdir(target))))

    write_gbk_crlf(os.path.join(args.package, '启动-P5修复版.cmd'), build_cmd(True, other, qml_diff))
    write_gbk_crlf(os.path.join(args.package, '启动-作者完整版.cmd'), build_cmd(False, other, qml_diff))
    write_utf8(os.path.join(args.package, '版本切换说明.txt'),
               build_switch_doc(other, qml_diff, args.pkg_label or '作者发行版'), bom=True)
    print('  已生成 启动-P5修复版.cmd / 启动-作者完整版.cmd / 版本切换说明.txt')

    for rel, _a, b in other:
        if os.path.isfile(b):
            shutil.copy2(b, os.path.join(args.package, rel))
    if os.path.isdir(qp):
        shutil.rmtree(os.path.join(args.package, 'qml', 'Veyra'))
        shutil.copytree(qp, os.path.join(args.package, 'qml', 'Veyra'))
    print('  生效文件已设为定制版')
    return 0


if __name__ == '__main__':
    sys.exit(main())
