#!/usr/bin/env python3
"""Apply named byte patches to an unpacked code.bin, checking the original bytes first.

  patch_code.py SRC_DIR OUT_DIR PATCH[,PATCH...]

Copies SRC_DIR (an unpacked image) to OUT_DIR and patches OUT_DIR/code.bin.
Each patch entry: (module, run address, original bytes hex, new bytes hex).
"""
import os, shutil, struct, sys

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
    # Scrolling bottom line: one mode draws the long file name buffer (0x030248FC), the other the
    # title buffer (0x03048450, filled from the library record +0x400). Point both at the title.
    "np_title_not_filename": [
        (27, 0x03083550, "fc480203", "50840403"),   # literal pool word 0x030248FC -> 0x03048450
    ],
}

def file_off(code, mod, addr):
    cl, ce, cs = struct.unpack_from("<3I", code, 8 + 32 * mod)
    assert ce <= addr < ce + cs, f"{addr:#x} outside module {mod}"
    return cl - BASE + (addr - ce)

def apply(code: bytearray, names):
    for name in names:
        for mod, addr, old, new in PATCHES[name]:
            o = file_off(code, mod, addr)
            have = code[o:o + len(old) // 2].hex()
            if have == new:
                print(f"  {name} {addr:#x}: already applied"); continue
            assert have == old, f"{name} {addr:#x}: expected {old}, found {have}"
            code[o:o + len(new) // 2] = bytes.fromhex(new)
            print(f"  {name} {addr:#x}: {old} -> {new}")

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
