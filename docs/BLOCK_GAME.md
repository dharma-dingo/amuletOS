# Block Game (v0.2) spec

Our own falling-block game replacing stock's block game (module 82). Gameplay feel inspired by
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
| DAS | Slow 16f · Mid 11f · Fast 9f · Very fast 8f | Fast 9f **(owner?)** |
| ARR | Slow 3f · Mid 2f · Fast 1f · Very fast 0.5f · Instant 0f | Fast 1f **(owner?)** |
| Soft drop | same as ARR, added on top of gravity | Instant **(owner?)** |

Directional delay (DAS resets on direction change): on. Initial hold/rotate (IHS/IRS): later.

## Controls (AP30 hardware: one face button at a time; the wheel and the GPIO key combine)

| Input | Action |
|---|---|
| Left / Right | move (DAS/ARR) |
| Down | soft drop |
| Up | hard drop (with drop protection: ignore a second hard drop within a few frames) |
| A / B | rotate clockwise / counter-clockwise |
| Wheel up / down | rotate clockwise / counter-clockwise (works while holding a direction) |
| Select | hold **(owner?)** (or the GPIO key, once we know which button it is) |
| Start | pause menu (Resume, Retry, Quit) |

## Look

- 296×240 screen, rounded corners: nothing important within ~12 px of a corner.
- Well 10×20 cells of 11 px (110×220) centred; left panel: HOLD box, timer, lines left; right
  panel: NEXT (1 preview). Framed panels and diamond background like the menus.
- Bevelled blocks (light top-left edge, dark bottom-right), guideline piece colours tuned to the
  palette (I cyan, O yellow, T purple, S green, Z red, J blue, L orange). Ghost = outlined cells.
- Plain text labels only (HOLD, NEXT, TIME, LINES, PPS). No themed copy.

## Engineering plan

1. **Test module:** replace module 82 with a minimal C window (fills screen, prints text, reacts to
   keys, exits back to the menu) to prove the window interface on device.
2. **Core in portable C** (`game/core.c`): field, pieces, SRS, bag, hold, lock delay, DAS/ARR,
   timer, all driven by a per-frame input bitmask. Unit tests and a PC harness that renders frames
   to PNG / replays inputs, so rules are verified before the device.
3. **Device shell:** window callbacks call the core once per frame; draw only changed cells
   (stock's fill is per-pixel); find the screen flush path for a steady 60 fps.
4. **Wheel input** for rotation (read the encoder ourselves while the game runs, so it doesn't
   change volume), then the main menu's Games tile launches the game directly ("Block Game").
