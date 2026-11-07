#!/usr/bin/env python3
"""Boot splash (stock image 000) and goodbye screen (002) for SchalaOS v0.1.

Ported from the mockup's drawBoot(): sky-kingdom starfield, four floating islands
(one with a mountain and waterfall), lavender cloud bank, the gold-set pendant gem,
and "SchalaOS". Single still frame. Output: 296x240 PNGs.

  python art/boot_splash.py OUTDIR
"""
import math, os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from zeal import Canvas, THEMES, W, H, mix, tw

GEM = ["......ggg......", ".....gHHhg.....", "....gHHhhhg....", "...gHhhhhhdg...", "..gHhhhhhhddg..",
       ".gHhhhhhhhdddg.", "gHhhhhhhhhddddg", "ghhhhhhhhdddddg", ".ghhhhhhhddddg.", "..ghhhhhhdddg..",
       "...ghhhhhddg...", "....ghhhhdg....", ".....ghhdg.....", "......ggg......"]

ISLANDS = [dict(x=128, y=34, w=13, h=7, p=1.7), dict(x=21, y=24, w=19, h=10, p=0, spire=1, fall=1),
           dict(x=40, y=80, w=11, h=6, p=3.1), dict(x=113, y=76, w=23, h=11, p=4.4, fall=1)]

def jsround(v):  # JavaScript Math.round
    return math.floor(v + 0.5)

def island(cv, t, i, f):
    haze = 0.6 if i["w"] < 15 else 0.4
    rock, rock_lit = mix(t["border"], t["lav"], haze), mix(t["border"], t["lav"], haze + 0.2)
    grass = mix("#4fa860", t["lav"], haze * 0.35)
    grass_lit = mix("#9ee08a", t["lav"], haze * 0.25)
    vine = mix("#3c8a52", t["border"], 0.25)
    cy = i["y"] + jsround(1.2 * math.sin(f / 22 + i["p"]))
    x0 = i["x"] - (i["w"] >> 1)
    jag = lambda c, r: 1 if (c * 7 + r * 13 + i["w"]) % 5 == 0 else 0
    for r in range(i["h"]):
        half = max(0, jsround((i["w"] / 2) * math.pow(1 - r / i["h"], 0.85))) - jag(r, 1)
        for c in range(-half, half):
            cv.rect(i["x"] + c, cy + 2 + r, 1, 1, rock_lit if c < -half / 3 else rock)
    cv.rect(x0, cy, i["w"], 3, grass); cv.rect(x0 + 1, cy - 1, i["w"] - 2, 1, grass)
    cv.rect(x0 + 2, cy - 1, max(2, i["w"] >> 2), 1, grass_lit)
    cv.rect(x0 + (i["w"] >> 1) + 1, cy - 1, max(1, i["w"] >> 3), 1, grass_lit)
    for c in range(1, i["w"] - 1, 4):
        cv.rect(x0 + c, cy - 2, 1, 1, grass_lit)
    for c in range(2, i["w"] - 2, 3):
        cv.rect(x0 + c, cy + 3, 1, 1 + ((c * 5 + i["w"]) % 3), vine)
    if i.get("spire"):
        mx, mh = i["x"] - 3, 8
        mtn, mtn_lit, slope = mix(t["border"], t["lav"], 0.5), mix(t["border"], t["lav"], 0.72), mix("#4fa860", t["lav"], 0.2)
        for r in range(mh):
            half = jsround(r * 0.6) + (1 if r > 0 else 0)
            y = cy - mh + r
            for c in range(-half, half + 1):
                cv.rect(mx + c, y, 1, 1, slope if r > mh - 3 else (mtn_lit if c < 0 else mtn))
            if 2 < r < mh - 2:
                cv.rect(mx + half, y, 1, 1, mix(mtn, "#000000", 0.25))
        cv.rect(mx, cy - mh, 1, 1, mtn_lit); cv.rect(mx - 1, cy - mh + 1, 1, 1, mtn_lit)
    if i.get("fall"):
        wx = x0 + i["w"] - 3
        for k in range(14):
            if (k + (f >> 1)) % 4 == 0: continue
            cv.rect(wx, cy + 2 + k, 1, 1, mix(t["panel"], t["border"], k / 16))

def scene(t, f=0):
    cv = Canvas()
    cv.rect(0, 0, W, H, t["border"])
    for i in range(40):
        x, y = (i * 53 + 7) % W, (i * 29 + 11) % 70
        cv.rect(x, y, 1, 1, t["panel"] if (i + (f >> 2)) % 9 == 0 else t["dim"])
    for i in ISLANDS:
        island(cv, t, i, f)
    for x in range(0, W, 2):
        h = 14 + jsround(4 * math.sin((x - f * 0.5) / 13)); cv.rect(x, H - h - 20, 2, h + 20, t["lav"])
    for x in range(0, W, 2):
        h = 8 + jsround(4 * math.sin((x + f) / 9) + 3 * math.sin(x / 5)); cv.rect(x, H - h - 14, 2, h + 14, t["bg2"])
    for x in range(0, W, 2):
        h = 4 + jsround(3 * math.sin((x - f) / 7)); cv.rect(x, H - h - 6, 2, h + 6, t["bg1"])
    gx, gy, glint = 67, 18, (f % 40) < 4
    gc = {"g": t["accent"], "H": "#ffffff" if glint else mix(t["bg1"], "#ffffff", 0.6), "h": t["bg1"],
          "d": mix(t["bg2"], t["border"], 0.45)}
    for r, row in enumerate(GEM):
        for c, k in enumerate(row):
            if k != ".": cv.rect(gx + c, gy + r, 1, 1, gc[k])
    title = "SchalaOS"
    cv.text(title, (W - tw(title, 2) + 1) // 2, 44, t["panel"], 2)
    return cv

def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "build/art"
    os.makedirs(out, exist_ok=True)
    t = THEMES["zeal"]
    boot = scene(t)
    boot.text("v0.1", W - tw("v0.1") - 3, H - 9, t["border"])
    boot.image().save(os.path.join(out, "000.png"))
    bye = scene(t)
    msg = "Goodbye"
    bye.text(msg, (W - tw(msg) + 1) // 2, 64, t["accent"])
    bye.image().save(os.path.join(out, "002.png"))
    print("wrote", os.path.join(out, "000.png"), os.path.join(out, "002.png"))

if __name__ == "__main__":
    main()
