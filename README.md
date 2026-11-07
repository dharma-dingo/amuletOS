# SchalaOS

Music-first replacement firmware for the Hidizs AP30 Music Boy, themed after Chrono Trigger's Zeal and Schala. Spec: Notion → Retro Tech → "SchalaOS (AP30 Custom Firmware): Product Spec".
Design mockups: https://claude.ai/artifact/REib6hwrurHvLUW9jQSEuJ

## Layout

- `firmware/` stock images from the Hidizs download center. Never commit or redistribute these.
- `tools/ap30fw.py` image tool: `info`, `verify`, `unpack`, `pack [--stamp HEX16]`
- `tools/rkread.py` read-only loader-mode client (backup); `tools/rkwrite.py` guarded loader-mode writer (recovery)
- `tools/build_hook.py` assembles a `hooks/*.S` hook into the code.bin 0x57B8C gap (proven NOT loaded at run time, needs a new location), redirects its BL, packs and verifies (`build_hook.py h0 2620071111000000` → `build/h0/HIFIYF18.IMG`)
- `tools/inquiry.py` sends a live SCSI INQUIRY to a drive letter (reads D1 diagnostics, no admin)
- `tools/fwmap.py` function/string index; `tools/xref.py` caller trees; `tools/fwdis.py` annotated disassembly
- `docs/HARDWARE.md` M1 hardware notes
- `build/` scratch output (git-ignored)
- `design/` UI mockups

## Status (2026-11-07)

M0 tooling:
- [x] Header and section table decoded (6 groups, 6 populated sections)
- [x] CRC is Rockchip CRC32 (poly 0x04C10DB7, MSB first, init 0); reproduced 0x865CA47B
- [x] Unpack then pack of stock V1.7.8 is byte-identical; resized sections repack and verify
- [x] Stamp decoded: `YY YY DD MM BB 00 00 00` = BCD year (LE), day, month, build. V1.7.8 = `26 20 19 10 08`, device = `26 20 19 08 05`
- [x] Loader mode: hold a d-pad corner + A + B, plug USB → black screen, USB `2207:262D`, WinUSB via Zadig
- [x] Flash backup via `tools/rkread.py` (read-only rockusb). Flash reports 983040 sectors; sectors 974848+ refused (reserved), so the backup is LBA 0..974847 (476 MB)
- [x] Backup verified by a second identical read (SHA-256 89ee0673…98c66cb, see backup/SHA256SUMS)

## Flash layout (device dump, 2026-11-07)

| Offset | Contents |
|---|---|
| 0x00000000 | fw1: Aug build (stamp 2026-08-19, build 05), backup header @0x1B3C000 |
| 0x01C00000 | stale fragment of a Jul-23 build (build 00), partly overwritten |
| 0x01E00000 | fw2: same Aug build |
| 0x0E208000 | FAT32, 250 MB, hidden: probably system data / library DB (the review's "234 MB unidentified") |
| 0x10408000 | FAT32, 216 MB: user storage (D:), holds a deleted copy of the Aug update IMG |
| rest | erased (0xFF) |

The device never took V1.7.8.
- [x] Official V1.7.8 installed on the device via copy + reboot
- [x] **Gate 1a**: V1.7.8 with USB vendor `HIFI MP3` → `SCHALAOS` (code.bin 0x8234C/0x823D4/0x823F8), stamp `26 20 07 11 09`, flashed via copy + reboot, enumerates as "SCHALAOS USB MP3". No code-section signature or checksum.
- [x] **Gate 1b**: one instruction in module 12 (USB MSC overlay) at 0x03083952, `ldr r2,[pc,#0x1d8]` → `#0x1dc` (code.bin 0x8172A: 76→77), stamp build 10. Internal disk now enumerates as "SCHALAOS SD". **Gate 1 passed.**

M1:
- [x] **Recovery write path proven** (`tools/rkwrite.py wtest`): in loader mode, wrote a pattern to 64 free blank sectors (LBA 786432, inside the user FAT32), read back OK, restored 0xFF, re-read in a new session OK. Recovery = `rkwrite.py wl 0 <good image> --allow-any`.
- [x] Function naming / analysis setup (`tools/fwmap.py`, `tools/xref.py`)
- [ ] Audio path (DAC, amp, mute timing, play latency). In progress: DAC power-up runs on every music start; fixed waits ≈ 270 ms of the ~3 s. See docs/HARDWARE.md
- [x] Buttons + volume wheel: 8 face buttons on one ADC ladder (ch1) + 1 GPIO key; wheel = quadrature encoder on GPIO2 pins 0/9. Simultaneous face-button presses likely impossible in hardware (to measure). See docs/HARDWARE.md
- [x] Display (static part): 296×240 RGB565 RAM framebuffer, DMA to LCD FIFO 0x6009000C, panel driver chosen from a flash table at boot. Open: controller model, bus speed (measure on device)
- [x] Memory: ≈ 622 KB core A SRAM in use, ≈ 180 KB core B; the "whole image in RAM" model and the 16 KB free block were disproved on device by H0/H0a
- [x] Arm GNU Toolchain 13.3.Rel1 installed and verified (see docs/HARDWARE.md)
- [x] **Custom code runs (H1, 2026-11-07).** Hook loaded by stock's ModuleOverlay via empty module slot 2 into free RAM 0x0309F800, called from module 12's INQUIRY send: internal disk enumerates as "SCHALAOS ZEAL HOOK". (H0/H0a in the bare file gap failed: never loaded, address past the 640 KB RAM end.)
- [x] D1 diagnostics (build 13): CPU clock 200 MHz (USB mode); all 8 ladder buttons measured; **simultaneous face-button presses impossible** (series ladder, lowest wins)
- [ ] Play-path timing (next: owner's slow-motion video, then timestamp hooks)
- [ ] Music-mode clock
- Screen push time: **deferred to M2** (decided 2026-11-07), measured with SchalaOS's own display driver. Stock's real flush path is indirect; the module-0 blits 0x03064F70/0x03064FD4 have no callers and do not set the panel window
- [ ] Power / battery / charging (read-only)
- [ ] docs/HARDWARE.md

## Code layout

- code.bin loads at 0x03050000. Word 1 = module count (113). Then 113 × 8 words: code load, code exec, code size, data load, data exec, data size, ZI exec, ZI size (load addresses are relative to 0x03050000 = code.bin offset 0).
- Module 0 = resident core: 0x2E300 bytes from code.bin 0x2988C, runs at 0x03050000; data runs at 0x03004000.
- Module 12 = USB mass storage: code runs at 0x03083610 (code.bin 0x813E8), INQUIRY blocks at 0x03036790 (internal) / 0x030367B4 (SD).

Device currently runs D1 (stock V1.7.8 + slot-2 diagnostic INQUIRY hook), stamp build 13. Next update must stamp build 14 or later. Read D1 with `python tools/inquiry.py D`.

## Decisions

- Approach: hybrid. Keep stock's audio engine (decoders on core B, DAC tuning); replace UI, navigation, library and play-start path
- Scope: browse by metadata (Artist > Album > Track), M3U, fast play start, snappy nav, block game with background MP3; Game Boy emulation is M5
- Look: Zeal Sky default palette with lavender accents, compact 6px font, big scrolling titles, no transitions, diamond pattern, arrow cursor, boot splash with floating islands
- P0: sound within 300 ms of pressing play; every press redrawn within one frame; playable within 3 s of power-on

## Backlog (next version)

- **Block game controls (face buttons can't combine, measured by D1).** Stock has the same limit. Options discussed 2026-11-07: (1) rotate on the volume wheel; (2) hybrid, wheel rotates plus A/B rotate when no direction is held (preferred); (3) face buttons only with DAS kept through brief direction release (avoid); (4) the GPIO2 pin 5 key as an extra rotate. Owner: Up = fast (hard) drop; details to settle during the build. Also: adjustable DAS/ARR, wheel = piece control in game with volume in the pause menu, ADC thresholds at midpoints of measured values. Open: wheel position/ergonomics, and which physical button the GPIO key is.
