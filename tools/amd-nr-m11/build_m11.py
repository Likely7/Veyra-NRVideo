import os
import re
import shlex
import shutil
import subprocess
import sys
from pathlib import Path

BUILD_DIR = Path(__file__).resolve().parent
REPO_ROOT = BUILD_DIR.parent.parent
VEYRA_INC = REPO_ROOT / "include"

SRC_ROOT = Path(os.environ.get("DLSSNR_AMD_SRC", str(REPO_ROOT / "third_party_local" / "dlssnr-amd")))
CORE_DIR = SRC_ROOT / "windows" / "src" / "core"
PE_DIR = SRC_ROOT / "windows" / "src" / "pe"
WIN_SRC_DIR = SRC_ROOT / "windows" / "src"
VULKAN_INC = Path(os.environ.get("VULKAN_HEADERS_INC", str(SRC_ROOT / "windows" / "third_party" / "vulkan")))

LLVM_BIN = Path(os.environ.get("LLVM_BIN", shutil.which("clang++") and str(Path(shutil.which("clang++")).parent) or r"C:\Program Files\AMD\ROCm\7.2\bin"))
CXX = str(LLVM_BIN / "clang++.exe")
CC = str(LLVM_BIN / "clang.exe")
LLVM_OBJDUMP = str(LLVM_BIN / "llvm-objdump.exe")

OBJ_DIR = BUILD_DIR / "obj"
RUNTIME_DIR = BUILD_DIR / "out"
LOG_PATH = OBJ_DIR / "build.log"


def parse_rdna4_defines() -> list[str]:
    text = (SRC_ROOT / "windows" / "build" / "arch" / "rdna4.sh").read_text(encoding="utf-8")
    m = re.search(r"NR_PRODUCT_DEFINES=\((.*?)\)", text, re.S)
    if not m:
        raise RuntimeError("Could not parse NR_PRODUCT_DEFINES from rdna4.sh")
    return shlex.split(m.group(1), comments=True)


def run_logged(cmd: list[str], log_fp, check: bool = True) -> subprocess.CompletedProcess:
    line = subprocess.list2cmdline(cmd)
    log_fp.write(line + "\n")
    log_fp.flush()
    r = subprocess.run(cmd, cwd=str(BUILD_DIR), stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    if r.stdout:
        log_fp.write(r.stdout)
        log_fp.flush()
    if check and r.returncode != 0:
        print(f"COMMAND FAILED ({r.returncode}): {line}", file=sys.stderr)
        print(r.stdout, file=sys.stderr)
        sys.exit(r.returncode)
    return r


def generate_vk_stubs(objs: list[Path], libs: list[str], log_fp) -> Path:
    probe_exe = OBJ_DIR / "_link_probe.exe"
    r = run_logged([CXX, "-fuse-ld=lld", "-Wl,/errorlimit:0", *[str(o) for o in objs], *libs, "-o", str(probe_exe)], log_fp, check=False)
    undefs = set()
    for line in (r.stdout or "").splitlines():
        m = re.search(r"undefined symbol:\s*(\S+)", line)
        if not m:
            m = re.search(r"(?:unresolved external symbol|无法解析的外部符号)\s+([^\s，,]+)", line)
        if m:
            undefs.add(m.group(1).strip().strip('"').strip("'"))

    non_vk = sorted(s for s in undefs if not (s.startswith("vk") or s.startswith("nr_vk_")))
    if non_vk:
        print("NON-VK UNDEFINED SYMBOLS:", non_vk, file=sys.stderr)
        sys.exit(2)

    vk_syms = sorted(s for s in undefs if s.startswith("vk") or s.startswith("nr_vk_"))
    stubs_c = OBJ_DIR / "vk_stubs.c"
    lines = ["#include <stdint.h>", "// Zero-Vulkan stubs for Native D3D12 Direct ELF Passthrough."]
    for sym in vk_syms:
        lines.append(f"void* {sym} = 0;" if sym.startswith("nr_vk_p_") else f"int64_t {sym}(void) {{ return -3; }}")
    stubs_c.write_text("\n".join(lines) + "\n", encoding="utf-8")
    stubs_obj = OBJ_DIR / "vk_stubs.obj"
    run_logged([CC, "-O2", "-c", str(stubs_c), "-o", str(stubs_obj)], log_fp)
    return stubs_obj


def main():
    if not CORE_DIR.is_dir():
        raise RuntimeError(f"Set DLSSNR_AMD_SRC to an extracted mochizuki0323/DLSSNR-AMD checkout (missing {CORE_DIR})")
    OBJ_DIR.mkdir(parents=True, exist_ok=True)
    RUNTIME_DIR.mkdir(parents=True, exist_ok=True)
    product_defs = parse_rdna4_defines()

    common = [
        "-std=c++17", "-O2", "-DNDEBUG", "-Wno-c++11-narrowing",
        "-DNR_INT4=1", "-DNR_BUILD_STAMP=\"0.0.9m11-veyra\"",
        "-D_CRT_SECURE_NO_WARNINGS", "-DWIN32_LEAN_AND_MEAN", "-DNOMINMAX",
        "-DVK_USE_PLATFORM_WIN32_KHR",
        f"-I{VULKAN_INC}", f"-I{WIN_SRC_DIR}", f"-I{CORE_DIR}",
        f"-I{PE_DIR}", f"-I{BUILD_DIR}", f"-I{VEYRA_INC}", f"-I{BUILD_DIR / 'compat_inc'}",
    ]

    with LOG_PATH.open("w", encoding="utf-8") as log_fp:
        core_objs = []
        for src, obj, extra in [
            (CORE_DIR / "nr_runtime.cpp", OBJ_DIR / "nr_runtime.obj", product_defs),
            (CORE_DIR / "nr_native_plan.cpp", OBJ_DIR / "nr_native_plan.obj", []),
            (CORE_DIR / "nr_renodx.cpp", OBJ_DIR / "nr_renodx.obj", []),
            (PE_DIR / "nr_d3d12_passthrough_net.cpp", OBJ_DIR / "nr_d3d12_passthrough_net.obj", []),
            (BUILD_DIR / "d3d12_host_shim.cpp", OBJ_DIR / "d3d12_host_shim.obj", []),
        ]:
            run_logged([CXX, *common, *extra, "-c", str(src), "-o", str(obj)], log_fp)
            core_objs.append(obj)

        worker_obj = OBJ_DIR / "veyra_amd_nr_worker_m11.obj"
        run_logged([CXX, *common, "-c", str(BUILD_DIR / "veyra_amd_nr_worker_m11.cpp"), "-o", str(worker_obj)], log_fp)

        libs = ["-ld3d12", "-ldxgi", "-ldxguid", "-ladvapi32", "-luser32", "-lshell32", "-lole32", "-lcfgmgr32"]
        vk_stubs_obj = generate_vk_stubs(core_objs + [worker_obj], libs, log_fp)

        worker_exe = RUNTIME_DIR / "veyra_amd_nr_worker.exe"
        run_logged([CXX, "-fuse-ld=lld", str(worker_obj), *[str(o) for o in core_objs], str(vk_stubs_obj), *libs, "-o", str(worker_exe)], log_fp)

        imp = run_logged([LLVM_OBJDUMP, "-p", str(worker_exe)], log_fp)
        dlls = sorted({m.group(1) for m in re.finditer(r"DLL Name:\s*(\S+)", imp.stdout, re.I)})
        print(f"{worker_exe.name} imports: {dlls}")
        if any("vulkan" in d.lower() or "hip" in d.lower() for d in dlls):
            raise RuntimeError(f"Unexpected Vulkan/HIP DLL import: {dlls}")


if __name__ == "__main__":
    main()
