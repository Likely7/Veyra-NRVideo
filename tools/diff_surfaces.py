"""生成「切换清单」：自动找出定制版与作者版之间所有不同的文件。

用法：
    python tools/diff_surfaces.py --author <作者包> --custom <构建目录> --package <程序包>

输出：应纳入切换脚本的文件清单，并可直接生成 .orig / .p5 备份。
这是升级流程的强制步骤：新增文件（作者加的新着色器、新 DLL、新目录）也会被发现。
"""
import argparse
import hashlib
import sys
from pathlib import Path

sys.stdout.reconfigure(encoding="utf-8", errors="replace")

# 参与比对的路径（相对包根），支持 glob
WATCH = [
    "veyra_qml_ui.exe",
    "shaders/**/*.dxil",
    "shaders/**/*.cso",
    "qml/**/*.qml",
    "qml/**/*.js",
    "runtime/config/**/*",
]


def sha(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for c in iter(lambda: f.read(1 << 20), b""):
            h.update(c)
    return h.hexdigest()


def collect(root: Path) -> dict[str, Path]:
    """收集 root 下所有受监视的文件 → {相对路径: 绝对路径}"""
    out = {}
    for pattern in WATCH:
        for p in root.glob(pattern):
            if p.is_file():
                out[str(p.relative_to(root)).replace("\\", "/")] = p
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--author", required=True, help="作者原版包目录")
    ap.add_argument("--custom", required=True, help="本仓库构建目录（含定制产物）")
    ap.add_argument("--package", required=True, help="实际运行的程序包目录")
    args = ap.parse_args()

    author = Path(args.author).resolve()
    custom = Path(args.custom).resolve()
    pkg = Path(args.package).resolve()

    a_files = collect(author)
    c_files = collect(custom)

    print("=" * 70)
    print("切换清单：定制版 vs 作者版")
    print("=" * 70)
    print(f"作者包  : {author}  ({len(a_files)} 个受监视文件)")
    print(f"构建目录: {custom}  ({len(c_files)} 个受监视文件)")
    print(f"程序包  : {pkg}")
    print()

    all_paths = sorted(set(a_files) | set(c_files))
    different = []
    only_author = []
    only_custom = []

    for rel in all_paths:
        in_a = rel in a_files
        in_c = rel in c_files
        if in_a and in_c:
            if sha(a_files[rel]) != sha(c_files[rel]):
                different.append(rel)
        elif in_a:
            only_author.append(rel)
        else:
            only_custom.append(rel)

    print(f"[A] 内容不同，必须纳入切换：{len(different)} 个")
    for r in different:
        print(f"     {r}")

    print(f"\n[B] 只在作者包里有：{len(only_author)} 个")
    for r in only_author[:20]:
        print(f"     {r}")
    if len(only_author) > 20:
        print(f"     ... 还有 {len(only_author) - 20} 个")

    print(f"\n[C] 只在定制构建里有：{len(only_custom)} 个")
    for r in only_custom[:20]:
        print(f"     {r}")
    if len(only_custom) > 20:
        print(f"     ... 还有 {len(only_custom) - 20} 个")

    print()
    print("=" * 70)
    print("结论")
    print("=" * 70)
    total = len(different) + len(only_author) + len(only_custom)
    if total == 0:
        print("  两版完全相同，不需要切换脚本。")
        return 0

    print(f"  需要切换的文件共 {len(different)} 个（内容不同的）。")
    print(f"  另有 {len(only_author)} 个只在作者版、{len(only_custom)} 个只在定制版。")
    print()
    print("  ⚠ 切换脚本必须覆盖 [A] 的全部文件。")
    print("  ⚠ [B] 和 [C] 通常不需要切换，但要确认它们不会造成版本混搭")
    print("     （例如作者新增的着色器若被定制版 exe 引用，就要一并处理）。")

    # 检查程序包里这些文件的 .orig / .p5 备份是否存在
    print()
    print("=" * 70)
    print("程序包里的备份状态")
    print("=" * 70)
    missing = []
    for r in different:
        base = pkg / r
        for suffix in (".orig", ".p5"):
            bak = base.with_name(base.name + suffix)
            if not bak.exists():
                missing.append(f"{r}{suffix}")
    if missing:
        print(f"  缺少 {len(missing)} 个备份（切换脚本会因此失败）：")
        for m in missing[:15]:
            print(f"     {m}")
    else:
        print("  所有差异文件都有 .orig / .p5 备份 ✓")

    return 0


if __name__ == "__main__":
    sys.exit(main())
