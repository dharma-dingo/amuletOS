#!/usr/bin/env python3
"""Main menu as art-book panels (stock images 014..024, one per selected item).

Each screen is one big card for the selected item: its relic icon at 2x on a framed
page, the title in double-size type, a flavour line, the neighbours in the same row
peeking at the edges, and a 2x3 map of where you are (stock still moves in a 2x3
grid: left/right along a row, up/down between rows). Status strip stays light for
stock's time/battery text. 148x120 grid, scaled 2x.

  python art/menu_panels.py OUTDIR
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zeal import Canvas, THEMES, W, H, mix, tw
from icons import SETS, N
from main_menu import panel, STATES

BASE = THEMES["zeal"]
ITEMS = [  # title, flavour, background pair
    ("Music",   "Songs of the sky",  ("#acd2f4", "#8fbaec")),
    ("Games",   "Blocks of Zeal",    ("#eef6f8", "#d4e8ee")),
    ("Library", "The royal archive", ("#d8cdf6", "#c2b2ee")),
    ("Folders", "Treasure vaults",   ("#f4e2b8", "#e8cf98")),
    ("BT",      "Crystal link",      ("#c6b2f4", "#ae96ea")),
    ("Setup",   "Palace workshop",   ("#c2e8de", "#a4d8cc")),
]

def diamond(cv, a, b, y0):
    for y in range(y0, H):
        for x in range(W):
            tx, ty = x & 7, y & 7
            m = abs(tx - 3.5) + abs(ty - 3.5)
            on = m <= 1.5 or (3 <= m <= 3.5 and ((x >> 3) + (y >> 3)) & 1)
            cv.rect(x, y, 1, 1, b if on else a)

def icon_at(cv, sprite, x, y, ink, scale=1, fade=None):
    c, m = sprite.outlined(ink)
    for yy in range(N):
        for xx in range(N):
            if m[yy, xx]:
                col = tuple(int(v) for v in c[yy, xx])
                if fade: col = mix(col, fade, 0.55)
                cv.rect(x + xx * scale, y + yy * scale, scale, scale, col)

def screen(sel, t=BASE):
    title, flavour, (b1, b2) = ITEMS[sel]
    icons = [f() for f in SETS["relic"]]
    cv = Canvas()
    cv.rect(0, 0, W, 16, t["panel"]); cv.rect(0, 16, W, 1, t["border"])
    diamond(cv, b1, b2, 17)
    # neighbours in the same row, faded, with arrows
    row = sel // 3
    left, right = row * 3 + (sel - 1) % 3, row * 3 + (sel + 1) % 3
    icon_at(cv, icons[left], 6, 35, t["border"], 1, fade=b1)
    icon_at(cv, icons[right], W - 6 - N, 35, t["border"], 1, fade=b1)
    cv.glyph([2, 6, 14, 30, 14, 6, 2], 33, 44, t["lavInk"])        # <
    cv.glyph([16, 24, 28, 30, 28, 24, 16], W - 38, 44, t["lavInk"])  # >
    # the page
    px, py, pw, ph = 44, 19, 60, 56
    panel(cv, px, py, pw, ph, t, True)
    icon_at(cv, icons[sel], px + (pw - 2 * N) // 2, py + 4, t["text"], 2)
    # title and flavour on a ribbon
    cv.rect(10, 77, W - 20, 28, t["border"]); cv.rect(11, 78, W - 22, 26, t["panel"])
    cv.text(title, (W - tw(title, 2) + 1) // 2, 80, t["text"], 2)
    cv.text(flavour, (W - tw(flavour) + 1) // 2, 96, t["dim"])
    # 2x3 position map
    mx, my = (W - (3 * 6 - 2)) // 2, 107
    for k in range(6):
        x, y = mx + (k % 3) * 6, my + (k // 3) * 5
        on = k == sel
        cv.rect(x + 1, y, 2, 4, t["accent"] if on else t["border"])
        cv.rect(x, y + 1, 4, 2, t["accent"] if on else t["border"])
    return cv

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/art"
    os.makedirs(out, exist_ok=True)
    for sel, idx in enumerate(STATES):
        screen(sel).image().save(os.path.join(out, f"{idx:03d}.png"))
    print("wrote art-book menu panels to", out)

if __name__ == "__main__":
    main()
