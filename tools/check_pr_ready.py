"""提 PR 前的自动检查：防止本地专有内容污染上游。

用法：
    python check_pr_ready.py <分支名>

检查项：
    1. 分支是否从 origin/main 开出（而不是从 custom）
    2. 是否夹带本地专有文件
    3. 改动规模是否合理
    4. 是否删除了上游已有的功能
    5. 提交信息是否合格
"""
import re
import subprocess
import sys

# Windows consoles default to GBK; force UTF-8 so the report never dies on a symbol.
try:
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")
except Exception:
    pass

REPO = r"D:\项目\Veyra-NV\Veyra-NRVideo"

# 只属于本地、绝不该进上游的文件
LOCAL_ONLY = {
    "CUSTOM_LEDGER.md",
    "docs/VeyraNV_LOCAL_BUILD_NOTES.md",
    "docs/DOVI_WORK_HANDOFF.md",
    "docs/DOVI_P5_VERIFICATION.md",
    "docs/DOLBY_BL_EL_COMPOSITING.md",
    "tools/veyra-switch/make_switch.py",
    "tools/dovi_p5_probe/main.cpp",
    "tools/dovi_p5_probe/compare_raw.py",
}

# 本地专有的设置字段/开关名（改动里出现要提醒）
LOCAL_MARKERS = [
    "doviP7Compose",
    "doviFollowAverage",
    "doviFollow",
]


def run(*args, check=False):
    p = subprocess.run(["git", *args], cwd=REPO, capture_output=True, text=True, errors="replace")
    if check and p.returncode != 0:
        print("  git failed:", p.stderr.strip()[:200])
        sys.exit(1)
    return p.stdout


def base_of(branch):
    """找出这个分支的实际分叉点。"""
    # 与 custom 的合并基 vs 与 origin/main 的合并基
    mb_custom = run("merge-base", "custom", branch).strip()
    mb_main = run("merge-base", "origin/main", branch).strip()
    return mb_custom, mb_main


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    branch = sys.argv[1]
    ok = True

    print(f"检查分支: {branch}\n")

    # 1. 分叉点
    mb_custom, mb_main = base_of(branch)
    main_head = run("rev-parse", "origin/main").strip()
    if mb_custom == mb_main:
        print("[1] [OK] 从上游干净开出（不含 custom 的定制）")
    else:
        print("[1] [FAIL] 分支基于 custom，会夹带本地定制！")
        print(f"     merge-base(custom)={mb_custom[:8]}  merge-base(origin/main)={mb_main[:8]}")
        ok = False

    # 2. 本地专有文件
    files = [l for l in run("diff", "--name-only", f"origin/main..{branch}").splitlines() if l]
    bad = [f for f in files if f in LOCAL_ONLY]
    if bad:
        print(f"[2] [FAIL] 夹带本地专有文件: {', '.join(bad)}")
        ok = False
    else:
        print(f"[2] [OK] 无本地专有文件（共 {len(files)} 个文件）")

    # 3. 规模
    stat = run("diff", "--shortstat", f"origin/main..{branch}").strip()
    print(f"[3] [INFO]  {stat}")
    m = re.search(r"(\d+) insertions", stat)
    ins = int(m.group(1)) if m else 0
    if ins > 800:
        print(f"     [WARN]  改动超过 800 行，作者审查负担大。考虑拆分。")

    # 4. 是否删除上游功能
    deleted = [l for l in run("diff", "--name-status", f"origin/main..{branch}").splitlines()
               if l.startswith("D\t")]
    if deleted:
        print(f"[4] [WARN]  删除了上游文件: {', '.join(l.split(chr(9))[1] for l in deleted)}")
        print("     删除上游功能需要作者明确同意，否则不要提。")

    # 5. 本地专有名称
    diff_text = run("diff", f"origin/main..{branch}")
    hits = [k for k in LOCAL_MARKERS if k in diff_text]
    if hits:
        print(f"[5] [WARN]  改动里出现本地专有开关: {', '.join(hits)}")
        print("     确认这些不是只在你本地才有的字段。")

    # 6. 提交信息
    subjects = [l for l in run("log", "--format=%s", f"origin/main..{branch}").splitlines() if l]
    weak = [s for s in subjects if len(s) < 20 or s.lower() in ("fix", "update", "wip", "改了一下")]
    if weak:
        print(f"[6] [WARN]  提交信息过简: {weak}")
    else:
        print(f"[6] [OK] {len(subjects)} 个提交，信息合格")

    print()
    if ok:
        print("结论：可以提 PR")
    else:
        print("结论：不要提，先修掉上面的问题")
        sys.exit(1)


if __name__ == "__main__":
    main()
