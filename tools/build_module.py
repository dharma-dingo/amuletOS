#!/usr/bin/env python3
"""Compile a C window module and install it in place of a stock module (default 82, block game).

  build_module.py SRC_DIR OUT_DIR SOURCE.c [SOURCE.c ...] [--module 82] [--at 0x57C20]

SRC_DIR is an unpacked image (e.g. build/d1np). OUT_DIR gets a copy with code.bin changed:
the module's .text and .data are written into the zero gap at code.bin --at (the gap is
0x57B8C..0x5BB8C; slot 2's D1 hook uses the first 0x8C bytes), and the module's 8 table words
are pointed at them. Link layout: game/device/module.ld (code at 0x030815C4, data at 0x03023B48
starting with the window descriptor, ZI after). Then pack with build_reskin.py as usual.
"""
import argparse, os, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARM = r"C:\Users\Admin\tools\arm-gnu-toolchain\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin"
BASE = 0x03050000
GAP = (0x57B8C + 0x8C, 0x5BB8C)          # usable gap (after the D1 hook)
CODE_EXEC, CODE_END = 0x030815C4, 0x0308BF84
DATA_EXEC, DATA_MAX = 0x03023B48, 0x708   # module 82's data + ZI footprint

def tool(name): return os.path.join(ARM, f"arm-none-eabi-{name}.exe")

def run(*a, **kw):
    r = subprocess.run([str(x) for x in a], capture_output=True, text=True, **kw)
    if r.returncode:
        sys.exit(f"{' '.join(map(str, a))}\n{r.stdout}{r.stderr}")
    return r.stdout

def main():
    p = argparse.ArgumentParser()
    p.add_argument("src"); p.add_argument("out"); p.add_argument("sources", nargs="+")
    p.add_argument("--module", type=int, default=82)
    p.add_argument("--at", type=lambda s: int(s, 0), default=0x57C20)
    a = p.parse_args()

    work = os.path.join(a.out, "module_build"); os.makedirs(work, exist_ok=True)
    elf = os.path.join(work, "module.elf")
    run(tool("gcc"), "-mcpu=cortex-m3", "-mthumb", "-Os", "-ffreestanding", "-nostdlib", "-nostartfiles",
        "-fno-tree-loop-distribute-patterns", "-ffunction-sections", "-fdata-sections", "-Wall", "-Wextra",
        "-Wno-unused-parameter", "-Wl,--gc-sections", "-T", os.path.join(ROOT, "game", "device", "module.ld"),
        "-Wl,-Map=" + os.path.join(work, "module.map"), "-Wl,--no-warn-rwx-segments", "-o", elf, *a.sources)
    run(tool("objcopy"), "-O", "binary", "-j", ".text", elf, os.path.join(work, "text.bin"))
    run(tool("objcopy"), "-O", "binary", "-j", ".data", elf, os.path.join(work, "data.bin"))
    text = open(os.path.join(work, "text.bin"), "rb").read()
    data = open(os.path.join(work, "data.bin"), "rb").read()
    secs = {}
    for line in run(tool("size"), "-A", "-x", elf).splitlines():
        f = line.split()
        if len(f) == 3 and f[0].startswith("."): secs[f[0]] = (int(f[1], 16), int(f[2], 16))
    syms = {f[2]: int(f[0], 16) for f in (l.split() for l in run(tool("nm"), elf).splitlines()) if len(f) == 3}
    bss_size, bss_addr = secs.get(".bss", (0, DATA_EXEC + len(data)))
    assert secs[".text"][1] == CODE_EXEC, "text must start at the overlay window"
    assert secs[".data"][1] == DATA_EXEC and syms.get("blockz_desc") == DATA_EXEC, "descriptor must start the data"
    assert CODE_EXEC + len(text) <= CODE_END, "code collides with the music module"
    assert len(data) + bss_size <= DATA_MAX, f"data+ZI {len(data) + bss_size:#x} > {DATA_MAX:#x}"
    desc = struct.unpack_from("<7I", data, 0)
    assert desc[0] == desc[1] == 0 and all(CODE_EXEC <= f & ~1 < CODE_EXEC + len(text) and f & 1 for f in desc[2:]), \
        f"bad descriptor {[hex(x) for x in desc]}"

    os.makedirs(a.out, exist_ok=True)
    for f in os.listdir(a.src):
        if os.path.isfile(os.path.join(a.src, f)) and f != "HIFIYF18.IMG":
            shutil.copy(os.path.join(a.src, f), a.out)
    code = bytearray(open(os.path.join(a.out, "code.bin"), "rb").read())
    t_off = a.at
    d_off = (t_off + len(text) + 3) & ~3
    end = d_off + len(data)
    assert GAP[0] <= t_off and end <= GAP[1], f"module {t_off:#x}..{end:#x} outside the gap"
    assert not any(code[t_off:end]), "gap area is not empty"
    code[t_off:t_off + len(text)] = text
    code[d_off:end] = data
    ent = 8 + 32 * a.module
    old = struct.unpack_from("<8I", code, ent)
    new = (BASE + t_off, CODE_EXEC, len(text), BASE + d_off, DATA_EXEC, len(data), bss_addr, bss_size)
    struct.pack_into("<8I", code, ent, *new)
    open(os.path.join(a.out, "code.bin"), "wb").write(code)
    print(f"module {a.module}: text {len(text)} B @ code.bin {t_off:#x}, data {len(data)} B @ {d_off:#x}, "
          f"ZI {bss_size} B @ {bss_addr:#x}")
    print("  table was", " ".join(f"{x:#x}" for x in old))
    print("  table now", " ".join(f"{x:#x}" for x in new))
    print("  descriptor", " ".join(f"{x:#x}" for x in desc))

if __name__ == "__main__":
    main()
