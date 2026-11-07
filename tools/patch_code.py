#!/usr/bin/env python3
"""Apply named byte patches to an unpacked code.bin, checking the original bytes first.

  patch_code.py SRC_DIR OUT_DIR PATCH[,PATCH...]

Copies SRC_DIR (an unpacked image) to OUT_DIR and patches OUT_DIR/code.bin.
Each patch entry: (module, run address, original bytes hex or None, new bytes hex).
"""
import os, shutil, struct, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ARM = r"C:\Users\Admin\tools\arm-gnu-toolchain\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin"
BASE = 0x03050000
NOP2 = "00bf00bf"          # two Thumb NOPs, replaces one 4-byte BL

PATCHES = {
    # Stock Now Playing (module 27). Info box: line 1 artist (0x15e), line 2 album (0x25c),
    # line 3 genre (0x35a), each drawn by text 0x03063F76 with a fallback 0x030644A0 when empty.
    # Owner: show only artist and song title. Skip the album and genre draws (areas stay cleared).
    "np_hide_album_genre": [
        (27, 0x03082A78, "e1f77dfa", NOP2),   # line 2 album text
        (27, 0x03082AF2, "e1f7d5fc", NOP2),   # line 2 empty fallback
        (27, 0x03082AC6, "e1f756fa", NOP2),   # line 3 genre text
        (27, 0x03082B04, "e1f7ccfc", NOP2),   # line 3 empty fallback
    ],
    # Build 18 tried this; 0x03048450 turned out to be the file name, not the title. Superseded by np_title_tag.
    "np_title_not_filename": [
        (27, 0x03083550, "fc480203", "50840403"),   # literal pool word 0x030248FC -> 0x03048450
    ],
    # Format line ("16bit 44.1KHz 320Kbps") at (145, 60): skip its background strip and its text.
    "np_hide_format": [
        (27, 0x0308296E, "e2f7d7f9", NOP2),   # image 69 strip at (145, 60)
        (27, 0x03082996, "e1f7eefa", NOP2),   # format text
    ],
}

# ---- patches built from assembly ----
HOOK_AT, HOOK_END = 0x03082A30, 0x03082ACA   # module 27 dead code once np_hide_album_genre is applied

def thumb_bl(src, dst):
    off = dst - (src + 4)
    assert -(1 << 24) <= off < (1 << 24) and not off & 1
    s = (off >> 24) & 1; i1 = (off >> 23) & 1; i2 = (off >> 22) & 1
    imm10 = (off >> 12) & 0x3FF; imm11 = (off >> 1) & 0x7FF
    j1 = (~i1 ^ s) & 1; j2 = (~i2 ^ s) & 1
    return struct.pack("<HH", 0xF000 | (s << 10) | imm10, 0xD000 | (j1 << 13) | (j2 << 11) | imm11)

def thumb_b16(src, dst):
    off = dst - (src + 4)
    assert -2048 <= off < 2048 and not off & 1
    return struct.pack("<H", 0xE000 | ((off >> 1) & 0x7FF))

def assemble(src, addr):
    """assemble hooks/<src>.S linked at addr; returns (bytes, symbols)."""
    with tempfile.TemporaryDirectory() as t:
        o, e, b = (os.path.join(t, n) for n in ("h.o", "h.elf", "h.bin"))
        for cmd in ([os.path.join(ARM, "arm-none-eabi-as.exe"), "-mcpu=cortex-m3", "-mthumb", "-o", o,
                     os.path.join(ROOT, "hooks", src + ".S")],
                    [os.path.join(ARM, "arm-none-eabi-ld.exe"), "-Ttext=" + hex(addr), "-o", e, o],
                    [os.path.join(ARM, "arm-none-eabi-objcopy.exe"), "-O", "binary", "-j", ".text", e, b]):
            subprocess.run(cmd, check=True)
        nm = subprocess.run([os.path.join(ARM, "arm-none-eabi-nm.exe"), e], capture_output=True, text=True).stdout
        syms = {f[2]: int(f[0], 16) for f in (l.split() for l in nm.splitlines()) if len(f) == 3}
        return open(b, "rb").read(), syms

def np_title_tag():
    """Scrolling line shows the title tag, else the file name (hooks/np_title.S). Needs np_hide_album_genre."""
    code, sym = assemble("np_title", HOOK_AT)
    assert HOOK_AT + len(code) <= HOOK_END, f"hook is {len(code)} B, room for {HOOK_END - HOOK_AT}"
    nop = bytes.fromhex("00bf")
    return [
        (27, 0x03082A2E, "0020", thumb_b16(0x03082A2E, 0x03082ACA).hex()),   # after line 1: straight to the epilogue
        (27, HOOK_AT, None, code.hex()),                                       # helpers in the now-dead code
        (27, 0x0308349A, "2e48e0f759fc", (thumb_bl(0x0308349A, sym["np_fill_strlen"]) + nop).hex()),
        (27, 0x03083466, "02a9a8f800400223384a0846e0f780fd", (thumb_bl(0x03083466, sym["np_fill_draw"]) + nop * 6).hex()),
        (27, 0x03083554, "50840403", "e0850403"),                              # mode-0 literal 0x03048450 -> title
    ]

GENERATED = {"np_title_tag": np_title_tag}

def file_off(code, mod, addr):
    cl, ce, cs = struct.unpack_from("<3I", code, 8 + 32 * mod)
    assert ce <= addr < ce + cs, f"{addr:#x} outside module {mod}"
    return cl - BASE + (addr - ce)

def apply(code: bytearray, names):
    for name in names:
        entries = GENERATED[name]() if name in GENERATED else PATCHES[name]
        for mod, addr, old, new in entries:
            o = file_off(code, mod, addr)
            n = len(new) // 2
            if code[o:o + n].hex() == new:
                print(f"  {name} {addr:#x}: already applied"); continue
            if old is not None:
                have = code[o:o + len(old) // 2].hex()
                assert have == old, f"{name} {addr:#x}: expected {old}, found {have}"
            code[o:o + n] = bytes.fromhex(new)
            print(f"  {name} {addr:#x}: {old or '(dead code)'} -> {new[:40]}{'...' if len(new) > 40 else ''}")

def main():
    src, out, names = sys.argv[1], sys.argv[2], sys.argv[3].split(",")
    os.makedirs(out, exist_ok=True)
    for f in os.listdir(src):
        if os.path.isfile(os.path.join(src, f)) and f != "HIFIYF18.IMG":
            shutil.copy(os.path.join(src, f), out)
    p = os.path.join(out, "code.bin")
    code = bytearray(open(p, "rb").read())
    apply(code, names)
    open(p, "wb").write(code)

if __name__ == "__main__":
    main()
