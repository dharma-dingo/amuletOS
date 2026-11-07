#!/usr/bin/env python3
"""Build a stock image with an injected hook in the resident zero gap.

  build_hook.py H0 STAMP16 [--src build/stock] [--out build/h0]

Assembles hooks/<name>.S at GAP_RUN, writes it into code.bin at GAP_FILE,
redirects the hooked BL to it, then packs and verifies HIFIYF18.IMG.
"""
import argparse, os, shutil, struct, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TC = r"C:\Users\Admin\tools\arm-gnu-toolchain\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin"
BASE = 0x03050000
GAP_FILE, GAP_SIZE = 0x57B8C, 0x4000
GAP_RUN = 0x030A7B8C

# hook name -> (symbol, BL site run address, BL site code.bin offset, expected original target)
HOOKS = {
    "h0": ("h0_inquiry.S", "h0_send", 0x03083978, 0x81750, 0x03081AA4),
    "h0a": ("h0a_pass.S", "h0a_send", 0x03083978, 0x81750, 0x03081AA4),
}

def bl_decode(hw, pc):
    h, l = struct.unpack("<HH", hw)
    if (h & 0xF800) != 0xF000 or (l & 0xD000) != 0xD000:
        return None
    s = (h >> 10) & 1
    j1, j2 = (l >> 13) & 1, (l >> 11) & 1
    i1, i2 = 1 - (j1 ^ s), 1 - (j2 ^ s)
    off = (s << 24) | (i1 << 23) | (i2 << 22) | ((h & 0x3FF) << 12) | ((l & 0x7FF) << 1)
    if s: off -= 1 << 25
    return pc + 4 + off

def bl_encode(pc, target):
    off = target - (pc + 4)
    assert -(1 << 24) <= off < (1 << 24) and off % 2 == 0, "BL out of range"
    off &= (1 << 25) - 1
    s = (off >> 24) & 1
    i1, i2 = (off >> 23) & 1, (off >> 22) & 1
    j1, j2 = (1 - i1) ^ s, (1 - i2) ^ s
    h = 0xF000 | (s << 10) | ((off >> 12) & 0x3FF)
    l = 0xD000 | (j1 << 13) | (j2 << 11) | ((off >> 1) & 0x7FF)
    return struct.pack("<HH", h, l)

def tool(name, *args):
    subprocess.run([os.path.join(TC, f"arm-none-eabi-{name}.exe"), *args], check=True)

def main():
    p = argparse.ArgumentParser()
    p.add_argument("hook"); p.add_argument("stamp")
    p.add_argument("--src", default=os.path.join(ROOT, "build", "stock"))
    p.add_argument("--out")
    a = p.parse_args()
    src, sym, site_run, site_file, orig = HOOKS[a.hook]
    out = a.out or os.path.join(ROOT, "build", a.hook)
    obj = os.path.join(out, "obj"); os.makedirs(obj, exist_ok=True)

    # assemble + link at the gap
    o, elf, binf = (os.path.join(obj, a.hook + e) for e in (".o", ".elf", ".bin"))
    tool("as", "-mcpu=cortex-m3", "-mthumb", "-o", o, os.path.join(ROOT, "hooks", src))
    tool("ld", "-Ttext", hex(GAP_RUN), "-e", sym, "-o", elf, o)
    tool("objcopy", "-O", "binary", "-j", ".text*", elf, binf)
    blob = open(binf, "rb").read()
    syms = subprocess.run([os.path.join(TC, "arm-none-eabi-nm.exe"), elf], capture_output=True, text=True, check=True).stdout
    entry = next(int(l.split()[0], 16) for l in syms.splitlines() if l.split()[-1] == sym)
    assert len(blob) <= GAP_SIZE, f"hook is {len(blob)} bytes, gap is {GAP_SIZE}"
    assert GAP_RUN <= entry & ~1 < GAP_RUN + len(blob), "entry symbol outside the hook blob"

    # copy stock sections, patch code.bin
    for f in os.listdir(a.src):
        if os.path.isfile(os.path.join(a.src, f)):
            shutil.copy(os.path.join(a.src, f), out)
    cp = os.path.join(out, "code.bin")
    code = bytearray(open(cp, "rb").read())
    assert not any(code[GAP_FILE:GAP_FILE + GAP_SIZE]), "gap is not empty in source code.bin"
    got = bl_decode(code[site_file:site_file + 4], site_run)
    assert got == orig, f"site holds BL to {got and hex(got)}, expected {orig:#x}"
    code[GAP_FILE:GAP_FILE + len(blob)] = blob
    target = entry & ~1
    code[site_file:site_file + 4] = bl_encode(site_run, target)
    assert bl_decode(code[site_file:site_file + 4], site_run) == target
    open(cp, "wb").write(code)
    print(f"{a.hook}: {len(blob)} bytes at {GAP_RUN:#x} (code.bin {GAP_FILE:#x}), "
          f"BL @{site_run:#x} -> {target:#x} (was {orig:#x})")

    img = os.path.join(out, "HIFIYF18.IMG")
    py = sys.executable
    subprocess.run([py, os.path.join(ROOT, "tools", "ap30fw.py"), "pack", out, img, "--stamp", a.stamp], check=True)
    subprocess.run([py, os.path.join(ROOT, "tools", "ap30fw.py"), "verify", img], check=True)

if __name__ == "__main__":
    main()
