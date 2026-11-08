# SchalaOS handoff

State of the project at the end of 2026-11-07. Read this first, then `README.md` (status
checklist, release plan, decisions), `docs/HARDWARE.md` (every technical finding),
`docs/BLOCKZ.md` (v0.2 game spec) and the Notion product spec.

---

## 1. What it is, where things live

**SchalaOS** is custom firmware for the **Hidizs AP30 Music Boy**: Rockchip RKNano (two
Cortex-M3 cores), CS43198 DAC, 296×240 RGB565 screen with rounded bezel corners, Game Boy-style
controls (d-pad, A, B, Start, Select, a volume wheel, power). Theme: the Kingdom of Zeal colours
(sky blue, cloud white, lavender, gold) in a Game Boy Color era style. Owner: Brett.

| Thing | Where |
|---|---|
| Repo (local) | `C:\Users\Admin\claude\schala-os` (own git repo, branch `main`) |
| Repo (remote) | github.com/dharma-dingo/schala-os (private). Repo-local identity `dharma-dingo` with a noreply email. |
| Product spec | Notion → Retro Tech → "SchalaOS (AP30 Custom Firmware): Product Spec" (page 411894f1713a81338dcbdb2ae963ced5) |
| Design mockups | claude.ai artifact "AP30 Screen Mockups" (REib6hwrurHvLUW9jQSEuJ). The boot splash and icon art were ported from it. |
| Stock firmware | `firmware/HIFIYF18.IMG` (V1.7.8). PC only, git-ignored, never commit or share. |
| Device backup | `backup/` (476 MB flash dump, verified twice, SHA-256 in `backup/SHA256SUMS`). PC only. |
| Zadig | `tools/zadig/` (WinUSB driver for loader mode). PC only. |
| Toolchain | Arm GNU Toolchain 13.3.Rel1 at `C:\Users\Admin\tools\arm-gnu-toolchain\...\bin` (`-mcpu=cortex-m3 -mthumb`) |
| Python | 3.12 with capstone, pyusb + libusb_package, numpy, Pillow. ffmpeg is installed. |

---

## 2. How to build, flash and recover

**Normal install (no special mode):** copy `HIFIYF18.IMG` to the top level of the AP30's
internal drive (the ~216 MB one, usually D:), eject, unplug, restart. The device installs it on
the next boot (the screen can stay black for a minute or two) and deletes the file. **The build
stamp must be newer than the installed one.**

- Stamp format: `YY YY DD MM BB 00 00 00`, BCD, year little-endian, then day, month, build.
  Today's builds use `26 20 07 11 BB`. Next free build number: **27** (`2620071127000000`).
- **Low battery makes the updater silently eat the file without flashing.** Charge first.
- Pressing **B on the USB-transfer screen drops USB** (goes back to the main menu).

**Loader mode (recovery / raw flash access):** unplug, hold power ~10 s, hold a d-pad corner + A
+ B *before* plugging in, keep holding ~3 s, let go. Screen stays black; device appears as USB
`2207:262D`. If Windows shows "Device Descriptor Request Failed", power off fully and replug.
Always send `python tools/rkread.py td` before the first read in a session.

- Read: `tools/rkread.py rl LBA COUNT OUT` (read-only tool, max 32 sectors per transfer).
- Write: `tools/rkwrite.py wl LBA FILE --allow-any` (every write is read back).
- Slots: fw1 at LBA 0, fw2 at LBA 0xF000. A slot on flash is the IMG **without** its CRC
  trailer, so a change confined to one 512-byte sector can be written directly into both slots
  (done twice on 2026-11-07: H0 → H0a, then back to stock). Full recovery: write a good image at
  LBA 0.

**Build tools:**

| Command | What it does |
|---|---|
| `tools/ap30fw.py info/verify/unpack/pack` | image container; byte-identical round trip; `pack --stamp HEX16` |
| `tools/build_hook.py NAME STAMP16` | assembles `hooks/*.S`, stores it in the code.bin gap, points **module slot 2** at it, redirects a BL, packs + verifies |
| `tools/imgres.py list/export/pack` | UI images (raw big-endian RGB565), byte-identical round trip |
| `tools/build_reskin.py BASE ART STAMP16 --out DIR` | overlays `NNN.png` replacements onto BASE's images and packs |
| `tools/inquiry.py D [--loop N --changes]` | live SCSI INQUIRY to the drive (reads D1 diagnostics, no admin) |
| `tools/fwmap.py`, `xref.py`, `fwdis.py` | function/string index, caller trees, annotated disassembly |

**Rebuilding v0.1 from scratch:**

```
python tools/ap30fw.py unpack firmware/HIFIYF18.IMG build/stock
python tools/build_hook.py d1 2620071113000000            # build/d1 (stock + D1 hook)
python tools/imgres.py export build/stock/images.bin build/img_stock
python art/boot_splash.py build/art_v01
python art/screens.py build/art_v01
python art/menu_panels.py build/art_v01
python art/recolor.py build/img_stock build/art_v01 build/art_v01_auto
# merge: auto first, then hand-made on top
mkdir build/art_v01_all; cp build/art_v01_auto/*.png build/art_v01_all/; cp build/art_v01/*.png build/art_v01_all/
python tools/build_helpers.py build/d1 build/d1h game/device/np_helpers.c game/device/np_stub.S game/device/libscan.c
python tools/build_overlay.py build/d1h build/d1h_o game/device/npui.c --slot 28 --run 0x03083F0C --max 0x758 --at 0x5B400 --json npui.json
python tools/patch_code.py build/d1h_o build/d1h_np np_v4,np_hide_format,menu_games_to_blockz,lib_skip
python tools/build_module.py build/d1h_np build/d1h_np_bz game/device/blockz.c --at 0x58800
python tools/build_reskin.py build/d1h_np_bz build/art_v01_all 2620071128000000 --out build/v028 --font tall
```

---

## 3. What happened, in order

### M0: tooling and backup
Container decoded (header, six sections, backup header, Rockchip CRC32 poly 0x04C10DB7). Stamp
decoded. Loader mode found and a full flash backup taken. Official V1.7.8 installed (the unit had
been on an August build).

### Gate 1: modified code runs (passed)
- 1a: USB vendor string changed in three places in code.bin → enumerated as "SCHALAOS USB MP3".
- 1b: one instruction in module 12 (USB storage) changed → internal disk answered as the SD card.
- No signature or code checksum exists on the update path.

### M1: hardware map (most of it)
- **Code layout:** code.bin loads at 0x03050000; 113 modules, each `code load/exec/size, data
  load/exec/size, ZI exec/size`; "load" addresses are file positions (minus 0x03050000).
- **Module loading:** `ModuleOverlay(n, type)` at 0x030683DE copies a module from **flash** to
  its run address (bit 0 code, bit 1 data, bit 2 zero ZI). The image is never loaded into RAM as a
  whole. **RAM ends at 0x030A0000 (640 KB).** Free: 0x0309F800–0x0309FFF0 (2 KB; 0x0309FFF0 holds
  reset magic).
- **H0/H0a failed:** code placed in the 16 KB zero gap in the file (code.bin 0x57B8C) was never
  loaded; its assumed RAM address was past the end of RAM. USB enumeration hung. Undone by
  single-sector loader-mode writes.
- **H1 passed (build 12):** empty **module slot 2** is loaded by stock in 21 places, including
  USB start right after module 12. Pointing slot 2 at bytes in the file gap with run address
  0x0309F800 makes stock load our code itself. The internal disk enumerated as "SCHALAOS ZEAL
  HOOK". **This is the proven way to run our own code.**
- **D1 (build 13):** diagnostics in the disk's INQUIRY name, read live with `inquiry.py`.
  - CPU clock **200 MHz** (USB mode); system clock variable at `*(0x030065C0 + 0x18)`.
  - Battery ADC ch0 ≈ 0x372 (≈ 4.3 V on USB).
  - All 8 face buttons share one ADC ladder (ch1). Measured: Right 0, Up ~119, Left ~240,
    Down 364, Select ~490, Start ~670, B (exits USB, inferred 700–849), A ~854, idle 1023.
  - **Two face buttons pressed together read as one** (series ladder, lowest value wins:
    Right > Up > Left > Down > Select > Start > B > A). Hardware limit. Only the wheel (GPIO
    quadrature encoder) and one GPIO key (GPIO2 pin 5, unidentified button) can combine with a
    face button.
- **Screen push timing deferred to M2:** stock's real flush path is indirect; the obvious blit
  routines have no callers. DMA controller, SysTick (unused on core A, usable as a cycle counter)
  and the fill/text routines are documented in HARDWARE.md.
- **Play delay:** static analysis found ~270 ms of fixed DAC power-up waits of the ~3 s; the rest
  is untraced. Deferred to ≈ v0.3 (owner has no smartphone for slow-motion video; firmware
  timestamp hooks are the plan when we get there).

### Re-scope (owner)
"Get v0.1 stylistically correct and playing music with some block." Deep latency work moves to
≈ v0.3. Release plan in README.

### v0.1: Zeal reskin of stock (built, on device)
- The UI images section (ROCK26IMAGERES) is 478 raw big-endian RGB565 images with a simple
  table; `imgres.py` exports/packs them losslessly.
- Static analysis of the image-draw functions (module 0, ~0x030646CE–0x03064D88) found the images
  the UI really uses; most of the 452 unique images are Rockchip SDK leftovers.
- Art pipeline in `art/` (all original, drawn on a 148×120 grid, scaled 2×): `zeal.py` (palette,
  5×7 font, canvas), `boot_splash.py` (000 boot, 002 goodbye), `icons.py` (24×24 relic icons with
  auto outline), `menu_panels.py` (main menu as art-book panels), `screens.py` (volume 336–436,
  list 159, highlight bars 184/185, plain 089, Now Playing 026, default art 277, USB 003, charging
  004–007, DAC 017, updating 437), `recolor.py` (automatic Zeal recolour of every other image;
  pure black/white kept as possible colour keys).
- Rule used everywhere: stock draws **dark text** on top at run time, so text areas stay light
  (cream panels); colour lives in frames, margins and patterns.
- Owner feedback applied: "v0.1" was clipped by the rounded corner (moved in); the icon set chosen
  was **B, Zeal relics**; the menu became **art-book panels** (one big card per item with the
  neighbours peeking and a 2×3 position map) instead of the 6-tile grid; **subtitles removed**
  ("AI fluff"), titles only.
- **Menu image mapping (measured):** stock shows 016 for top-left (Music), 014 top-middle (Games),
  018 top-right (Library), 022 bottom-left (Folders), 020 bottom-middle (BT), 024 bottom-right
  (Setup). The first v0.1 build had these swapped; fixed in build 16.
- Not reskinned in v0.1: stock's games (they draw with fill colours in code, no images) and text
  colours (stock's dark text is readable on our panels).

### Builds made on 2026-11-07

| Build | Stamp | Content | Result |
|---|---|---|---|
| 09 | 26 20 07 11 09 | Gate 1a (vendor string) | passed |
| 10 | …10 | Gate 1b (INQUIRY swap) | passed |
| 11 | …11 | H0 / H0a (file gap) | USB hang; undone in loader mode |
| 12 | …12 | H1 (slot 2 hook) | passed |
| 13 | …13 | D1 diagnostics | passed, results above |
| 14 | …14 | D1 + boot/goodbye art | passed ("v0.1" clipped) |
| 15 | …15 | D1 + full v0.1 reskin | art-book "beautiful", menu order wrong |
| 16 | …16 | build 15 + menu mapping fix + no subtitles | passed (owner: menu panels and text OK) |
| 17 | …17 | build 16 + font style D (`--font bold1x`): ASCII glyphs in both stock fonts replaced | installed; font replaced everywhere but **too small to read** (owner): needs a bigger font |
| 18 | …18 | build 16 art + tall font (`--font tall`, `art/font_tall.py`) + Now Playing patches (`tools/patch_code.py np_hide_album_genre,np_title_not_filename`) | installed: font size right (owner); Now Playing showed artist + **file name** (0x03048450 is the file name), album/genre hidden; genre icon, white strips past the panel and format line still to fix |
| 19 | …19 | build 18 + Blockz test module T1 in module 82 (`tools/build_module.py build/d1np build/d1np_t1 game/device/test_module.c`) | superseded by 20 (never on device) |
| 20 | …20 | build 18 fixes: title tag on the scrolling line with file-name fallback (`np_title_tag`, hooks/np_title.S), format line hidden (`np_hide_format`), Now Playing art: only the artist icon, panels run off the edge so the line strips (069, 028) blend in; + Blockz T1 | installed: owner wants album back, artist overflows the box; Blockz not reachable (game list untouched) |
| 21 | …21 | slot-2 resident helpers after D1 (`tools/build_helpers.py`, `game/device/np_helpers.c`): artist wraps onto 2 lines, album on line 3, title with file-name fallback (`np_v2`); format hidden; **Games tile opens Blockz directly** (`menu_games_to_blockz`), panel relabelled BLOCKZ; Blockz T1 moved to code.bin 0x58800, quits to the main menu | passed: BLOCKZ tile and test screen work (owner); title right (could be bolder); artist wrap good; wants artist higher and album wrapped too |
| 22 | …22 | Now Playing: artist at y 49 and album at y 91, both wrapping over two lines (`np_v3`, helpers 500 B); **Blockz T2: the real game** (`game/device/blockz.c`: core + renderer via FillRect, SysTick 60 fps pacing, ADC directions, GetKey presses, wheel = hold) | Blockz runs and "everything else looks perfect" (owner); bug: random backtracks / sudden drops / shifted hard drops on about every other piece = raw ladder ADC caught mid-sweep |
| 23 | …23 | Blockz: ladder reading accepted only after 16 ms stable (debounce); read-only INFO page (pause, then Left) showing stock's play state for v0.21 | better but same issues (owner); worst right after a hard drop: Up's release sweeps through Left/Down, and rubber pads can make sweeps slower than 16 ms |
| 24 | …24 | Blockz: direction-aware ladder debounce (release sweeps up the ladder: 80 ms; press 24 ms, Down 40 ms; to idle 16 ms), host test `game/test/test_ladder.c` | superseded by 25 (never on device) |
| 25 | …25 | **v0.21 attempt**: `game/device/bgmusic.c` (if nothing plays: find /_blockgame/ on SD via stock FindFirst/FindNext, random track via the play record + player thread, shuffle-loop by watching the record, stop on exit; MUSIC panel status, INFO lines BG/BX) + build 24's debounce | movement "great" (owner; d-pad hardware itself is poor). Music did not start in Blockz on the first try; no library scan had run at power-on, it started only when Music was opened afterwards. MUSIC panel: **EMPTY** (folder found by name, stock folder count 0). Stock's own Files browser also shows /_blockgame/ empty (folder recreated 16:20 by the owner's sync; likely an exFAT layout stock's folder code mishandles), while Music lists and plays the file |
| 26 | …26 | v0.21: when stock's folder count is 0, take the folder's tracks from the library database (records +0 folder cluster, +4 file number) | built and verified on PC (`build/v026`); not on device |
| 27 | …27 | **v0.3 part 1.** Library scan skip (`game/device/libscan.c` in slot 2, patch `lib_skip`): module 5 builds only when the exFAT SD card's fingerprint (serial, cluster count, root directory hash without access times, allocation bitmap hash) differs from the one saved after the last build; fingerprint kept in config block 0 at +0x300; Folders "Update Library?" Yes always rebuilds. Blockz: Select = hold, wheel = volume (VOL readout), exit via Start > Quit. Includes build 26 | host test `test_libscan` (18 cases) passes; image diff vs 26 = only the expected sites; not on device |
| 28 | …28 | **v0.3 Now Playing, layout A** (owner): `game/device/npui.c` as a code-only overlay in module slot 28 (run 0x03083F0C, 1860 of 1880 B), loaded by `np_load` (slot 2) from module 27's init; patch `np_v4` (replaces np_v3): artist 2 lines + album 3 lines in the right panel, double-size title scrolling when too long (file name without extension when untagged), times m:ss / -m:ss, counter n/total, own progress bar; art 026 without the right-panel icons. Includes build 27 | PC preview of the real hook code (`art/np_preview.py`) good; image diff vs 27 = only the expected sites; not on device |

### v0.2 planning (Blockz)
- Owner wants stock's block game **replaced** by our own: Apotris-inspired feel, Tetris DX
  (Game Boy Color) look, original name/code/art. **Sprint 40 lines first.**
- Research: Apotris' source is no longer public; its wiki gives modes, DAS/ARR presets, 1–5
  previews, skins. The SRS guideline (tetris.wiki) gives exact kick tables, 7-bag, hold, 0.5 s
  lock delay with 15 move resets. All captured in `docs/BLOCKZ.md`.
- Owner's picks: **1 next preview; no line-clear or entry delay ("fluid and fast"); Up = hard
  drop; A = rotate clockwise, B = counter-clockwise; wheel up = hold; Select quits to menu (as in
  stock games); Start = pause**. Handling defaults: DAS Fast (9f), ARR Fast (1f), soft drop
  Instant.
- Stock game interface mapped (HARDWARE.md, "UI window / game module interface") — see §6.

---

## 4. Current state

- **Device:** build 16 (v0.1 reskin + D1 diagnostics) was copied to the internal drive; the owner
  restarts to install. Build 15 was confirmed working before that. Next stamp: build 17.
- **Repo:** pushed through `5536755`; later commits (handoff, v0.2 game core) are local only (ask
  before pushing).
- **v0.2 game core (2026-11-07, later session):** §6.3 step 3 done on the PC: `game/core.c`,
  `game/render.c`, tests (87 checks), bot preview and a playable browser build. See
  `docs/BLOCKZ.md` "Status". Next: owner plays `build/game_preview/play.html`
  (`python game/build.py web`), then §6.3 step 1 (minimal device test module) and step 2.
- **Notion:** spec updated with Gate 1, M1 findings (corrected memory model, buttons, 200 MHz),
  release plan (v0.1 reskin, v0.2 own UI and Blockz, v0.3 speed), Blockz requirement.
- **Memory (assistant):** project, owner preferences and feedback files are up to date.

---

## 5. Decisions and owner preferences

- Hybrid approach: keep stock's audio engine (decoders on core B, DAC tuning), replace the UI,
  navigation, library and play-start path over time.
- Release plan: **v0.1** Zeal reskin of stock · **v0.2** own UI (ES-DE-style carousel, Start
  opens Settings, compact font, metadata browsing) + Blockz (Games tile becomes
  **"Blockz"** and launches it directly; other stock games dropped) · **v0.3** speed (play
  start, boot to music, screen timing) · M5 Game Boy emulation (note: direction + A/B is
  impossible on the face buttons; would need the wheel/GPIO key).
- Look: Zeal Sky default, lavender accents, diamond pattern, arrow cursor, bevelled blocks, 2×
  pixel scale, keep content ~12 px from screen corners.
- **Plain labels only; no flavour text or themed copy** (theme lives in art and palette).
- Themes: support theme development, ship a handful. Palette-driven now → theme packs at patch
  time → runtime switching with our own UI.
- **Public release concept:** ship a **web patcher**, never a firmware image. The user downloads
  official V1.7.8 from Hidizs; the patcher verifies its SHA-256, applies our changes locally and
  writes their own image with a newer stamp; install is the normal copy + restart. CLI for power
  users, GitHub Actions builds both, "back to stock" option. Before public: rename away from
  Chrono Trigger names, strip diagnostic hooks, licence + disclaimer, scan git history for
  firmware, testers on other units.
- Working style with Brett: **lead every reply with his next physical action** when there is one;
  ask before flashing and before each push; he has no smartphone; wants design input early, with
  choices as buttons; terse is fine.

---

## 6. Guidance for building out

### 6.1 Finish v0.1
1. Confirm build 16 on the device: each menu panel matches what opens; check Now Playing, a list,
   volume, USB screen for text overlapping frames (stock text positions were inferred).
2. Fix anything that overlaps by adjusting `art/screens.py` panels; rebuild with the next stamp.
3. Optional polish: the "Chinese prompt after updates" (settings likely reset by each update;
   find where settings live in the hidden FAT32 volume at flash 0x0E208000); patch text colours if
   any stock beige text remains.
4. For a clean v0.1 without diagnostics, build from `build/stock` instead of `build/d1`.

### 6.2 v0.2 Blockz: architecture
- **Replace module 82's table entry** (code.bin offset 8 + 32×82). Stock's menu loads module 82
  through ModuleOverlay when the game is chosen, so our module runs in its place and stock's music
  keeps playing.
- Our module must follow the window contract stock uses for every UI screen:
  - code runs at 0x030815C4 (the overlay window);
  - **data runs at 0x03023B48 and must begin with the window descriptor**
    `{0, 0, tick, service, paint, init, deinit}`; ZI follows the data;
  - **space:** keep code below 0x0308BF84 (music module 33 is resident there while music plays),
    i.e. ~43 KB; module 82 currently uses 4 KB code, 1 KB data, 0.8 KB ZI.
- Stock behaviour to mirror (from module 82): init calls 0x030664A2 then 0x03053822; deinit calls
  0x0306645A then 0x03065BC4; service reads `GetKey()` (0x03062FA6: event type in the top byte,
  key code in the low bits, codes as in §3) and returns 0 to keep running; **on Select it calls
  0x030680D4(0xA, &7) and returns −1, which closes the window back to the menu**; paint redraws
  according to message flags (post 0x03067ED4, test-and-clear 0x03067EF0); drawing uses the
  current colour at 0x03022DEC with fill 0x03064688(x0, y0, x1, y1) and text 0x03063E38(x, y,
  UTF-16 string). Entry points are collected in `game/stock.h`.
- **Where the module's bytes go in the file:** the 16 KB gap at code.bin 0x57B8C (slot 2's D1 hook
  uses the first 140 bytes; drop D1 or start after it). If the game outgrows 16 KB, extend
  code.bin and adjust the table (the packer already handles resized sections; verify on device).
- **Build tooling to add:** a linker script placing `.text` at 0x030815C4 and `.desc` + `.data`
  at 0x03023B48 (descriptor first) with `.bss` after; a `tools/build_module.py` that compiles
  (`arm-none-eabi-gcc -mcpu=cortex-m3 -mthumb -Os -ffreestanding -nostdlib`), extracts text and
  data, writes them into the gap, rewrites module 82's eight table words, then packs with
  `build_reskin.py`-style art (base `build/v01`) and verifies.

### 6.3 v0.2 Blockz: sequence
1. **Minimal device test module** following the contract above: draws a screen, reacts to keys,
   quits on Select. Expected failure mode if wrong: only launching the game crashes or hangs; the
   menu, music and copy-updates keep working. If the device hangs on launch, restart; if it
   boot-loops (not expected), use loader-mode recovery. Use D1's INQUIRY name to expose debug
   values if needed (e.g. last key event, tick count).
2. **Confirm the event types** from `GetKey()` (press, repeat, release, long press) by logging
   them; DAS/ARR need press and release, or a held-state query. If stock only reports presses and
   repeats, read the ADC ladder value directly (table at 0x03005A8C, ch1) once per frame and
   decode with midpoint thresholds between the measured values.
3. **Game core in portable C** (`game/core.c`): field (10×40), pieces and SRS states, kick tables
   from the spec, 7-bag, hold, ghost, gravity 1/60 G, lock delay 30 frames with 15 move resets,
   DAS/ARR/soft drop, Sprint timer, 40-line goal, top-out. Drive it with one input bitmask per
   frame so it is deterministic. Add unit tests (kicks, T-spin-free Sprint, bag fairness, lock
   resets) and a PC harness that renders frames to PNG or an HTML replay for review before
   flashing.
4. **Device shell:** tick and service call the core once per frame; paint draws only changed
   cells (stock's fill writes pixel by pixel, too slow for full redraws); find the real screen
   flush path (deferred item) if 60 fps needs it.
5. **Wheel = hold:** read the encoder counters (GPIO2 pins 0/9; step counts at 0x030226E4/E5,
   total at 0x030226E8) while the game runs, and stop the wheel changing volume during play.
6. **Look:** 11 px cells, well 110×220 centred, NEXT (1 preview) and HOLD panels, timer, lines
   left, PPS; bevelled guideline-coloured blocks tuned to the palette; ghost outline; plain labels.
   Reuse `art/zeal.py` colours; render a mock frame on PC for owner review first.
7. **Main menu:** relabel the Games panel "Blockz" and make the tile launch module 82 directly
   (patch the main menu's Games action, module 21); drop the other game modules later to free
   flash.

### 6.4 After that
- v0.2 Now Playing (owner, 2026-11-07): v0.1 left stock's run-time text (font and positions) on
  Now Playing, so it doesn't match the mockup. Replace module 27 with our own window drawn like the
  mockup ("AP30 Screen Mockups", Now Playing): status bar, art panel, artist and album in
  the compact 6 px font (owner, revised: text = artist + song title only, much bigger; album art stands in for the
  album; play time; no genre, file name, disc or format), double-size scrolling track title, progress bar with lavender playhead,
  elapsed/remaining. Needs stock's playback state (tags, position, length, cover) and its transport
  calls; trace them from module 27. Reuses Blockz's module build tooling and renderer
  approach (own RGB565 drawing, dirty rects). Fallback: patch ASCII glyphs in stock's font section.
- v0.2 carousel (owner, 2026-11-07): one level, items Now Playing, Music, Blockz, Library;
  Start opens Settings; Bluetooth and Folders go under Settings later. Stock's Music and Library
  screens are opened directly until ours replace them (trace their launch calls in module 21).
- v0.2 own UI: an ES-DE-style carousel (true one-direction movement, Start opens Settings),
  compact 6 px font, scrolling titles, metadata browsing. Same window contract, replacing the main
  menu (module 21) and then the browser modules.
- Themes: move palettes into data files; theme packs at patch time; runtime switching later.
- Public release: the web patcher (see §5).
- v0.3 speed: play-start timestamps via hooks, keep the DAC powered while awake, keep the music
  path resident, boot straight to music.

### 6.5 Testing rhythm
Batch changes and review previews on the PC (render from the built image in RGB565 so colours are
exact); flash once per batch. Keep each build's stamp increasing; keep `build/` outputs to roll
back by copy-update. Never commit firmware, backups or the Zadig installer.
