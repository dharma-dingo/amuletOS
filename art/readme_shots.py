#!/usr/bin/env python3
"""README screenshots (v0.3): rendered on the PC from the firmware's own art, fonts and drawing code.

  python art/readme_shots.py [BUILD_DIR]      (BUILD_DIR default build/v060: fonts of the release build)

Writes docs/screenshots/{menu,now_playing,playlists,blockz}.png at 2x with the screen's rounded corners.
  menu         the Music card exactly as packed in the firmware (art/pocket.py; stock adds its status icons)
  now_playing  game/device/npui.c run on the PC (art/np_preview.py) with a public-domain sample and an original
               placeholder cover
  playlists    the Playlists screen laid out as game/device/playlists.c paint() draws it, sample playlist names
  blockz       the Blockz renderer (game/render.c) from the bot replay (python game/build.py preview)
Sample content only: no real cover art or game footage.
"""
import subprocess, sys
from pathlib import Path
import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "art"))
import pocket            # noqa: E402
import np_preview as NP  # noqa: E402
from zeal import Canvas  # noqa: E402

OUT = ROOT / "docs/screenshots"


def finish(im296):
    """296 x 240 RGB -> rounded screen corners (np_preview) -> 2x nearest"""
    a = np.array(im296.convert("RGB")).astype(np.uint32)
    v = ((a[..., 0] >> 3) << 11 | (a[..., 1] >> 2) << 5 | (a[..., 2] >> 3)).astype("<u2")
    return NP.to_image(v.tobytes()).resize((592, 480), Image.NEAREST)


def cover():
    """original placeholder cover: night sky, moon (116 x 116, the stock cover size)"""
    im = Image.new("RGB", (116, 116))
    d = ImageDraw.Draw(im)
    for y in range(116):
        t = y / 115
        d.line([(0, y), (115, y)], fill=(int(36 + 70 * t), int(40 + 30 * t), int(92 + 60 * t)))
    d.ellipse((60, 18, 96, 54), fill=(250, 246, 226))
    d.ellipse((70, 14, 104, 48), fill=(56, 52, 112))
    for x, y in ((18, 22), (34, 48), (22, 70), (48, 30), (100, 72), (84, 92), (12, 96)):
        d.rectangle((x, y, x + 1, y + 1), fill=(240, 236, 250))
    d.rectangle((0, 98, 115, 115), fill=(30, 34, 70))
    return im


def now_playing(fdir):
    def background():
        im = pocket.now_playing().image(1).convert("RGB")
        for n, xy in ((34, (105, 10)), (31, (143, 6)), (45, (183, 7)), (287, (209, 11))):
            im.paste(Image.open(NP.ART / f"{n:03d}.png").convert("RGB"), xy)
        im.paste(cover(), (17, 46))
        a = np.array(im).astype(np.uint16)
        return ((a[..., 0] >> 3) << 11 | (a[..., 1] >> 2) << 5 | (a[..., 2] >> 3)).astype("<u2")
    NP.background = background
    NP.SAMPLES = [("Clair de Lune", "Claude Debussy", "Suite bergamasque", 102, 301, 3, 4)]
    tmp = ROOT / "build/readme_np"
    sys.argv = ["np_preview.py", str(fdir), str(tmp)]
    NP.main()
    return Image.open(tmp / "np_layout_a.png")          # already 2x with corners


def playlists():
    C_WHITE, C_HEAD, C_TRACK, C_TINT, C_GRAPE = "#ffffff", "#f2f3f6", "#dfe1e8", "#e9e3fb", "#7353e0"
    cv = Canvas(296, 240)
    cv.rect(0, 0, 296, 33, C_WHITE); cv.rect(0, 33, 296, 2, pocket.INK)
    cv.rect(0, 35, 296, 24, C_HEAD); cv.rect(0, 59, 296, 2, pocket.INK)
    cv.rect(0, 221, 296, 2, pocket.INK); cv.rect(0, 223, 296, 17, pocket.SCREEN)
    pocket.text(cv, "Playlists", 12, 39, pocket.INK)
    names = ["Morning Run", "Focus", "Road Trip", "Chiptune Favorites", "Jazz Nights", "Rainy Day"]
    glyph = (0xDF, 0xDF, 0x00, 0xDF, 0xDF, 0x00, 0xDC, 0xDC)
    for r in range(7):
        y, on = 61 + r * 23, r == 2
        cv.rect(0, y, 296, 22, C_TINT if on else C_WHITE)
        if on: cv.rect(0, y, 4, 22, C_GRAPE)
        cv.rect(0, y + 22, 296, 1, C_WHITE if r == 6 else C_TRACK)
        if r >= len(names): continue
        for gr in range(8):
            for gc in range(8):
                if glyph[gr] & (0x80 >> gc): cv.rect(12 + 2 * gc, y + 3 + 2 * gr, 2, 2, C_GRAPE)
        pocket.text(cv, names[r], 40, y + 3, pocket.INK)
    return finish(cv.image(1))


def main():
    fdir = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "build/v060"
    pocket.FONT_DIR = fdir
    pocket._f1 = None
    OUT.mkdir(parents=True, exist_ok=True)
    finish(Image.open(ROOT / "build/art_pocket/014.png")).save(OUT / "menu.png")
    playlists().save(OUT / "playlists.png")
    subprocess.run([sys.executable, str(ROOT / "game/build.py"), "preview"], check=True, stdout=subprocess.DEVNULL)
    finish(Image.open(ROOT / "build/game_preview/mid.png").resize((296, 240), Image.NEAREST)).save(OUT / "blockz.png")
    now_playing(fdir).save(OUT / "now_playing.png")
    print("wrote", ", ".join(sorted(p.name for p in OUT.glob("*.png"))), "to", OUT)


if __name__ == "__main__":
    main()
