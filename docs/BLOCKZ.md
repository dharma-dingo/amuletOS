# Blockz (v0.2) spec

Blockz: our own falling-block game, replacing stock's block game (module 82). Gameplay feel inspired by
Apotris, look inspired by Game Boy Color era menus (Tetris DX), in the SchalaOS palette.
Original name, code and art; no third-party code (Apotris source is not public, so we design
from its published mechanics and the public SRS guideline). First release: **Sprint 40 lines only.**

Values marked **(owner?)** are proposals waiting for Brett's pick.

## Sprint mode

- Goal: clear 40 lines as fast as possible. (20 / 100 later, as in Apotris.)
- Start: READY, GO countdown (about 1 s total), timer starts on GO.
- HUD: timer `m:ss.cc`, lines left (40 counting down), pieces placed, PPS (pieces per second).
- Finish screen: final time, pieces, PPS, personal best (saved to internal storage later; v0.2 first cut can keep it in RAM).
- Top-out ends the run (shown as failed, no time).
- Gravity: constant 1/60 cell per frame (Apotris Marathon level 1 = 0.01667 G). No levels in Sprint.

## Rules (guideline SRS)

- Playfield 10 wide, 20 visible rows plus a 20-row hidden buffer (store 40 rows).
- Spawn: all pieces horizontal, I and O centred, 3-wide pieces rounded left; J, L, T flat side
  down, I, S, Z in their upper horizontal state; spawn in rows 21/22 (1-indexed, just above the
  visible field) and fall immediately.
- Piece states and rotation centres: standard SRS (I and O rotate about a grid point, others
  about a mino). Kick tables as on tetris.wiki "Super_Rotation_System" (+x right, +y up):

  J, L, S, T, Z

  |      | 1 | 2 | 3 | 4 | 5 |
  |---|---|---|---|---|---|
  | 0→R | (0,0) | (-1,0) | (-1,+1) | (0,-2) | (-1,-2) |
  | R→0 | (0,0) | (+1,0) | (+1,-1) | (0,+2) | (+1,+2) |
  | R→2 | (0,0) | (+1,0) | (+1,-1) | (0,+2) | (+1,+2) |
  | 2→R | (0,0) | (-1,0) | (-1,+1) | (0,-2) | (-1,-2) |
  | 2→L | (0,0) | (+1,0) | (+1,+1) | (0,-2) | (+1,-2) |
  | L→2 | (0,0) | (-1,0) | (-1,-1) | (0,+2) | (-1,+2) |
  | L→0 | (0,0) | (-1,0) | (-1,-1) | (0,+2) | (-1,+2) |
  | 0→L | (0,0) | (+1,0) | (+1,+1) | (0,-2) | (+1,-2) |

  I

  |      | 1 | 2 | 3 | 4 | 5 |
  |---|---|---|---|---|---|
  | 0→R | (0,0) | (-2,0) | (+1,0) | (-2,-1) | (+1,+2) |
  | R→0 | (0,0) | (+2,0) | (-1,0) | (+2,+1) | (-1,-2) |
  | R→2 | (0,0) | (-1,0) | (+2,0) | (-1,+2) | (+2,-1) |
  | 2→R | (0,0) | (+1,0) | (-2,0) | (+1,-2) | (-2,+1) |
  | 2→L | (0,0) | (+2,0) | (-1,0) | (+2,+1) | (-1,-2) |
  | L→2 | (0,0) | (-2,0) | (+1,0) | (-2,-1) | (+1,+2) |
  | L→0 | (0,0) | (+1,0) | (-2,0) | (+1,-2) | (-2,+1) |
  | 0→L | (0,0) | (-1,0) | (+2,0) | (-1,+2) | (+2,-1) |

  O does not kick. No 180° rotation in the first cut.
- Randomiser: 7-bag.
- Hold: one slot, once per piece until it locks; held piece returns in spawn state at the top.
- Next previews: **1** (owner).
- Ghost piece: on.
- Lock delay: 30 frames (0.5 s), move reset limited to 15 moves/rotations, reset on falling a row.
- Line clear delay and entry delay: **none** (owner: Sprint is fluid and fast).
- Top-out: spawn overlaps blocks, or a piece locks fully above the visible field.

## Handling (frames at 60 fps; Apotris-style presets)

| Setting | Presets | Proposed default |
|---|---|---|
| DAS | Slow 16f · Mid 11f · Fast 9f · Very fast 8f | Fast 9f |
| ARR | Slow 3f · Mid 2f · Fast 1f · Very fast 0.5f · Instant 0f | Fast 1f |
| Soft drop | same as ARR, added on top of gravity | Instant |

Directional delay (DAS resets on direction change): on. Initial hold/rotate (IHS/IRS): later.

## Controls (AP30 hardware: one face button at a time; the wheel and the GPIO key combine)

| Input | Action |
|---|---|
| Left / Right | move (DAS/ARR) |
| Down | soft drop |
| Up | hard drop (with drop protection: ignore a second hard drop within a few frames) |
| A / B | rotate clockwise / counter-clockwise |
| Wheel up | hold (owner) |
| Wheel down | unassigned for now |
| Select | quit to menu (same as stock games) |
| Start | pause menu (Resume, Retry, Quit) |

## Look

- 296×240 screen, rounded corners: nothing important within ~12 px of a corner.
- Well 10×20 cells of 11 px (110×220) centred; left panel: HOLD box, timer, lines left; right
  panel: NEXT (1 preview). Framed panels and diamond background like the menus.
- Bevelled blocks (light top-left edge, dark bottom-right), guideline piece colours tuned to the
  palette (I cyan, O yellow, T purple, S green, Z red, J blue, L orange). Ghost = outlined cells.
- Plain text labels only (HOLD, NEXT, TIME, LINES, PPS). No themed copy.

## Status (2026-11-07)

Portable core and renderer done and tested on the PC; not on the device yet.

| File | What |
|---|---|
| `game/core.c/.h` | rules, freestanding C (no libc), one input bitmask per frame. ARM `-Os`: 2.5 KB code, no data |
| `game/render.c/.h` | draws into a 296x240 RGB565 buffer, only what changed (cell cache, text cache, dirty rect). ARM `-Os`: 3.5 KB |
| `game/test/test_core.c` | 87 checks: SRS states, kicks (wall, floor, I), 7-bag, spawn, lock delay + 15 resets + reset on a new low row, hard drop + drop protection, DAS/ARR (incl. 0.5f, instant), soft drop, gravity, line clears, hold, top-out, pause, 30 bot Sprints, determinism, incremental render == full render every frame |
| `game/pc/bot.c` | demo/test bot (one face button at a time, same inputs as a player) |
| `game/pc/preview.c` | bot Sprint -> PNG stills, contact sheet and GIF |
| `game/pc/web.c`, `play.html` | the same core + renderer as wasm, playable in a browser with the AP30 one-button limit emulated and DAS/ARR/soft drop menus |
| `game/build.py` | `test`, `preview`, `web`, `device` (ARM size + undefined-symbol check). Host compiler: zig (`pip install ziglang`) |

Outputs go to `build/game_preview/` (play.html, sheet.png, replay.gif).

Look decisions (owner, 2026-11-07): 11 px cells (well 110x220 at 93,10), ghost = 1 px outline in the piece
colour, background = Zeal sky blue with the menu diamonds. Panels: left HOLD, TIME, LINES; right NEXT, PIECES,
PPS, BEST. Labels and values use the 5x7 font at 2x.

Behaviour details chosen while building (change if they feel wrong):
- Spawn at rows 21/22 then drop one row at once (guideline), so the piece shows immediately.
- READY 0.5 s, GO 0.5 s, GO stays up for the first 0.5 s of play; DAS charges during the countdown.
- Hard drop protection: Up is ignored for 5 frames after a hard drop.
- Lock delay: 15 resets with a full timer; the 16th grounded move locks at once; reaching a new lowest row restores them.
- Result screen: A or Start = retry, B = quit. Pause: Up/Down + A, Start resumes.
- Best time is kept across retries in RAM only.

## Device test module T1 (build 19)

`game/device/test_module.c`, linked by `game/device/module.ld`, installed by `tools/build_module.py` into
module 82's table entry (code at code.bin 0x57C20 in the gap after the D1 hook, data right after it;
1048 B code, 28 B data, 32 B ZI; from build 21 at code.bin 0x58800, after the slot-2 helpers). From build 21 the
main menu's Games tile (relabelled BLOCKZ) opens it directly and Select returns to the main menu (TaskSwitch(0, &1)). Mirrors stock module 82: init posts 0x18C/0x186 and calls
0x030664A2, 0x03053822; deinit 0x0306645A, 0x03065BC4; Select (event 0x20000002 or 0x40000002) calls
0x03065BC4, TaskSwitch(0xA, &7) and returns -1; paint saves/restores colour, 0x030643A2(2), 0x030643C6(1).
Screen: palette swatches, then live TICK, SVC, TIME (0x0302288C), KEY/PREV (last GetKey events), ADC
(raw ch1), WHEEL (0x030226E4/E5, total), EVENTS. Answers: does the window contract work, the tick and
service rates, the GetKey event types, whether the ladder can be polled for held keys, wheel counters.

## Device build T2 (build 22)

`game/device/blockz.c` includes core.c and render.c (unity build, `BG_FILL_EXTERN`: the renderer's fills
go to stock FillRect; background drawn as 2 px runs). 6.8 KB code, 840 B data+ZI (limit 0x708).
- Timing: fixed 60 fps steps from the SysTick clock (10 ms ticks + VAL), max 6 frames catch-up.
- Input: Left/Right/Down held from ADC ch1 (Right < 60, Up < 180, Left < 302, Down < 426); A, B, Up, Start
  as GetKey press events (type 0x40) applied to the next frame; Select (0x40 or 0x20) quits to the main menu;
  any wheel step = hold (the wheel still changes volume in T2).
- Drawing: paint renders only what changed (renderer cache); full redraw on window open.
- Build 22 on device: plays well, but about every other piece jumped back or dropped: pressing or releasing one
  ladder button sweeps the voltage through the others (Right -> idle passes Up, Left, Down), and a raw sample
  can land mid-sweep. Build 23 accepts a ladder state only after it has been stable for 16 ms; still seen (worst after
  a hard drop: releasing Up). Build 24: direction-aware: a change to a higher-value button while one is held is
  treated as a release (80 ms), a press needs 24 ms (Down 40 ms), going idle 16 ms. Simulated in `game/test/test_ladder.c`.
- INFO page (build 23): pause, then Left. Read-only dump of stock's play state (source type, folder depth,
  file index, record cluster/file, FS type/current cluster/depth, folder stacks, disk, path) for wiring v0.21.
- Open: frame rate actually reached on device, whether stock's FillRect is fast enough, wheel direction,
  stopping the wheel from changing volume, best time kept only while the game is open.

## Background music (owner, 2026-11-07) - release v0.21

Play a track from storage on loop while the game runs, using stock's music engine (it stays
resident above the game's code window, so no decoder of our own).

- On game start: if music is already playing, leave it. Otherwise play the first audio file in
  `/_blockgame/` (SD card first, then internal storage) with repeat-one. Any stock format works.
- On quit: stop that track (music the user started keeps playing).
- The file is never part of the firmware image.
- Steps: (1) no-code check: start a track on repeat, open stock's block game, listen for dropouts;
  (2) trace how the browser hands a file to the music module (33) and where repeat mode lives;
  (3) call that from the game's init, stop it in deinit.
- Watch: stock's library scan will probably index `_blockgame` (try the FAT hidden attribute);
  stock's ~3 s play start means music begins a couple of seconds after GO until v0.3.

## Engineering plan

1. **Test module:** replace module 82 with a minimal C window (fills screen, prints text, reacts to
   keys, exits back to the menu) to prove the window interface on device.
2. **Core in portable C** (`game/core.c`): field, pieces, SRS, bag, hold, lock delay, DAS/ARR,
   timer, all driven by a per-frame input bitmask. Unit tests and a PC harness that renders frames
   to PNG / replays inputs, so rules are verified before the device.
3. **Device shell:** window callbacks call the core once per frame; draw only changed cells
   (stock's fill is per-pixel); find the screen flush path for a steady 60 fps.
4. **Wheel input** for hold (read the encoder ourselves while the game runs, so it doesn't
   change volume), then the main menu's Games tile launches the game directly ("Blockz").
