# SchalaOS hardware notes (AP30, stock V1.7.8)

Toolchain: Arm GNU Toolchain 13.3.Rel1 (arm-none-eabi-gcc 13.3.1), installed at
`C:\Users\Admin\tools\arm-gnu-toolchain\arm-gnu-toolchain-13.3.rel1-mingw-w64-i686-arm-none-eabi\bin`
(SHA-256 of the zip verified against Arm's published value). Target flags: `-mcpu=cortex-m3 -mthumb`.

Working notes for M1. Addresses are run addresses unless marked `code.bin`.
Index: `python tools/fwmap.py build/<dir>/code.bin build/fwmap` then grep `functions.tsv` / `strings.tsv`;
caller trees: `python tools/xref.py <code.bin> build/fwmap/functions.tsv <ADDR> [depth]`.

## Code organisation

- code.bin loads at 0x03050000; 113 modules, each `code load/exec/size, data load/exec/size, ZI exec/size`.
- Module 0 = resident core, runs at 0x03050000 (0x2E300 bytes). Overlays share run addresses around 0x0308xxxx and are swapped in on demand.
- ARMCC output: most strings are addressed with `ADR` (PC-relative), not literal pools. Some functions load a register before `PUSH`, so prologue-based function detection can miss the first instruction (e.g. 0x03051F8E).
- Calls into 0x01000000+ go to mask ROM (SDK code baked into the chip).
- Two cores: decoders run on core B; core A sends `dec_open` etc. ("A 2 B send dec_open...") and waits with timeouts.

## Key functions

| Run address | Module | What it is |
|---|---|---|
| 0x03065B58 | 0 | `DelayMs(n)`: busy loop, n × (sysclk/3000) iterations of 3 cycles |
| 0x03065B34 | 0 | `DelayUs(n)`: sysclk / 1,000,000 × n |
| 0x030621FC | 0 | CS43198 register write `(reg24 in r0, value in r1)` |
| 0x03062270 | 0 | CS43198 register read |
| 0x030504EC / 0x03050562 | 0 | i2c_write / i2c_read |
| 0x03051F8E | 0 | DAC (re)start: clears state, writes reg 0x0B0000=0x1E, tail-jumps to power-up |
| 0x03051CB2 | 0 | CS43198 power-up sequence (regs 0x02xxxx, 0x05xxxx, 0x09xxxx, 0x0Bxxxx, 0x0Fxxxx, 0x18xxxx; waits 80 ms + 10 ms + 4×100 µs) |
| 0x0308DDD0 | 33 | `MusicInit`: calls DAC (re)start, waits 100 ms |
| 0x0308C8EE | 33 | "Audio Start" / file open, waits 80 ms |
| 0x0308E178 | 33 | AudioControl main (codec select by extension) |
| 0x0308FFBC… | 41–57 | per-format decoder front-ends (MP3, WMA, AAC, WAV, OGG, FLAC, ALAC, APE, DFF, DSF), each one module |
| 0x03083610 | 12 | USB mass storage; INQUIRY handler at 0x03083942 |
| 0x03082F4E | 11 | USB audio (also calls DAC (re)start) |

## Play latency (stock: ~3 s from pressing play to sound, owner report)

Fixed busy-waits found on the music start path: DAC power-up ≈ 90 ms + MusicInit 100 ms + Audio Start 80 ms ≈ **270 ms**.
Stock powers the DAC up from cold on every music start (MusicInit → 0x03051F8E → 0x03051CB2).
That leaves ~2.7 s unexplained. Suspects: overlay loads from flash (music + decoder modules), core B decoder start
and its handshakes, file open / tag parsing, library DB work. Next: owner's slow-motion video to split the 3 s,
then trace the overlay loader and the core B handshake.

**SchalaOS design consequence:** keep the DAC/amp powered while the UI is awake, keep the music path resident
(no overlay swap on play), and never busy-wait on the UI core.

## Peripheral map (SDK hw_memap.h, confirmed by firmware use)

| Block | Base | Notes |
|---|---|---|
| SARADC | 0x400D0000 | DATA +0, STAS +4, CTRL +8 (ch in bits 0-2, start bit 4, IRQ bit 5) |
| TIMER | 0x400E0000 | |
| PWM0 / PWM1 | 0x400F0000 / 0x40100000 | PWM0 likely backlight |
| MAILBOX | 0x40110000 | core A ↔ core B; 78 functions use it |
| I2S1 | 0x40030000 | audio out (TXDR +0x24 per ReChord) |
| I2C0 | 0x40040000 | DAC control |
| GPIO0 / GPIO1 / GPIO2 | 0x40160000 / 0x40170000 / 0x50030000 | data +0, dir +4, int regs +0x30.. |
| CRU / GRF / PMU | 0x40180000 / 0x50010000 / 0x50020000 | clocks, pin mux, power |
| EMMC | 0x60030000 | AP30 storage is eMMC |
| LCD / VOP | 0x60090000 / 0x60070000 | display |

Interrupts: vector = 16 + SDK INT_ID (SRADC 36, MAILBOX0-3 25-28, I2S1 48, GPIO2/1/0 52/53/54). `IntRegister(id, fn)` = 0x03067088. Boot VTOR = 0x03050000, later a RAM copy at 0x03004000.

## Input: buttons and volume wheel

**ADC sampler.** A timer steps through 4 channels round-robin (0x030613BC), the conversion ISR (0x03061440) stores results in a table at 0x03005A8C (u16 per channel); getter 0x030614D6(ch). Only channels 0 and 1 are consumed.

**ADC ch0 = battery.** 0x03081E50 (boot) / 0x03065D66, 0x03065E02 (service): mV = raw × 2500 × 2 / 1024. Power off below 3300 mV when not charging, low warning below 3450 mV, "full" above 3964 mV; 4-entry level table at 0x03005758 for the 5-step icon. Charger detect on GPIO2 pin 3.

**ADC ch1 = all 8 face buttons on one resistor ladder** (decoder 0x03062CCC, 10-bit reading):

| ADC | code | | ADC | code |
|---|---|---|---|---|
| < 70 | 0x004 | | 442–559 | 0x002 |
| 100–183 | 0x010 | | 560–679 | 0x100 |
| 200–309 | 0x008 | | 700–849 | 0x040 |
| 320–429 | 0x020 | | 850–949 | 0x080 |
| ≥ 950 | none | | | |

Plus one key on **GPIO2 pin 5** (code 0x001), checked first.

**Measured on device (D1, 2026-11-07, raw 10-bit ch1, idle = 0x3FF):**

| Button | Raw | Stock code |
|---|---|---|
| Right | 0 | 0x004 |
| Up | 118–119 | 0x010 |
| Left | 240–242 | 0x008 |
| Down | 364 | 0x020 |
| Select | 486–492 | 0x002 |
| Start | 661–680 | 0x100 |
| B | not measured (exits USB mode); by elimination | 0x040 |
| A | 854–855 | 0x080 |

Start and A sit at stock's band edges; SchalaOS should use midpoints between measured values. The volume wheel does not touch ch1.

**Simultaneous face-button presses cannot be read (confirmed).** Every pair reads as its lower-value member: Up+A → Up, Right+A → Right, Left+Select → Left, Up+Right → Right, Down+Start → Down, Select+Start → Select. Series ladder: priority Right > Up > Left > Down > Select > Start > B > A, so A is masked by any other face button. Inputs that can combine with a face button: the volume wheel (GPIO encoder) and the GPIO2 pin 5 key.

**Consequences.** Hardware limit, confirmed on device: one face button at a time. Blockz: rotate on the wheel (or the GPIO key) so move + rotate works. Game Boy: direction + A/B is impossible on the face buttons; A/B would have to move to the wheel / GPIO key.
Stock key timing (SDK AD_Key): 2-tick debounce, repeat after 20 ticks, then every 2 ticks.

**Volume wheel = quadrature encoder on GPIO2 pins 0 (A) and 9 (B)**, polled from the system timer: 0x030534AC configures the pins as inputs and compares A/B with the previous sample (0x0306FBCC / 0x03004144), counting steps per direction at 0x030226E5 / 0x030226E4 (total 0x030226E8). Responsiveness depends on the poll rate.

**Other GPIO:** GPIO2 pin 1 = amp enable/mute (driven at volume 0 and around audio start). GPIO2 pin 8 + GPIO0 pin 13 = jack detect (ORed, 50 ms debounce, posts message 0x25). GPIO0 pin 11 read by most UI modules (likely hold or headphone state).

**Volume path:** level → 0x0306242E (scale ×6) → one of three curves → CS43198 regs 0x090001/0x090002 (0x00 = 0 dB, 0xFF = mute).

**Display:** UI init sets the window to 0–295 × 0–239 (296×240 confirmed).

## Display

- Panel: **296×240, RGB565**, mounted rotated (the window code mirrors x: 295 − x).
- Stock draws into a **RAM framebuffer at 0x03006688** (row stride 592 bytes; pages of 190 rows plus a second path for the rest; double-buffer page select). Pixel write 0x03064DC4, software window/rotation 0x03064EA8.
- Frames reach the panel by **DMA into the LCD data FIFO at 0x6009000C** (LCD controller base 0x60090000, control at +4; 0x0E written before some transfers). Rectangle blits: 0x03064F70 / 0x03064FD4 (window, get DMA channel 0x030614F8, start DMA 0x03061524 with w×h/2 words).
- Stock "clear screen" (0x03064F2A) writes all 71,040 pixels one at a time through the CPU. Wasteful; SchalaOS should fill by DMA.
- The panel driver is **chosen at boot from a table in flash** (`LcdDriverIndex`, LcdInterface.c, module 1 0x03081CC0): its function table is copied to 0x0306FD14 and `ops[1]` (init) is called. The panel's command sequence lives in that flash table, not in the code modules. Controller model not yet identified.
- 0x60070000 (SDK "VOP") is used as per-channel registers (channel × 0x34), more like a DMA engine than display timing.
- **Open (deferred to M2):** bus speed and full-frame push time. Stock's blit routines 0x03064F70 / 0x03064FD4 have no direct callers or stored pointers anywhere in the image, and their window routine 0x03064EA8 only updates software state (no panel command), so the live flush goes through an untraced indirect path (likely the panel-driver table from flash). DMA: DesignWare-style DMAC at 0x60000000 (ChEnReg +0x3A0, channels 0–4, allocator 0x030614F8), linked-list start 0x03061524 with up to 18 blocks of 0xFF8 words, descriptors at 0x03005AD4 + ch×0x1B0. **Correction:** SysTick is stock's system tick: 0x030621C8(ms) sets LOAD = clk * ms / 1000 - 1 and stock calls it with 10 (boot and after clock changes), the SysTick ISR (0x03067470) walks a callback list whose first entry increments the 10 ms counter at 0x0302288C. Sub-tick time: (LOAD - VAL) / (LOAD + 1) of 10 ms, clock-independent (used by Blockz for 60 fps pacing).

## Memory

- Core A SRAM in use: 0x03004000–0x0309F7CC (≈ 622 KB; chip likely 640 KB). Module 0: 185 KB code at 0x03050000, 6.6 KB data at 0x03004000, 120 KB ZI at 0x03005A8C (holds the framebuffer). Overlay code window 0x03080E9C–0x0309F7CC (≈ 120 KB).
- Core B region in use: 0x01000000–0x0102D184 (≈ 180 KB), decoders in modules 71–80.
- **Load model: DISPROVED on device (H0/H0a, 2026-11-07).** The ReChord-based assumption was: the boot ROM loads the whole code section contiguously into RAM, with code.bin offset 0 at 0x03050000. Module-table "load" addresses are therefore real RAM addresses, and overlays are copied from that in-RAM image on demand. RAM extends at least to the end of the loaded image (≈ 0x031D8714), well beyond the ≈ 622 KB the modules execute in.
- Runtime reuse of the loaded image: after boot, module 0 runs at 0x03050000, a heap/buffer area follows at 0x0307E300 (fixed-address globals, e.g. 0x0307E710 has 35 references), and overlays execute at 0x03080E9C–0x0309F7CC. Sources above that (module data and overlay sources from 0x030ABB8C up) must stay intact.
- **Free block: NOT usable.** code.bin 0x57B8C–0x5BB8C (16 KB of zeros) was assumed to sit in RAM at 0x030A7B8C. H0 (INQUIRY swap) and H0a (pure tail-call pass-through) both placed there and called from module 12's `bl usb_send` (0x03083978) hang USB enumeration: the device appears as 071B:3203 with no LUNs, then drops. A pass-through failing means our bytes are not at 0x030A7B8C at run time. Working model: modules are copied to their exec addresses individually and the gap is never loaded. Hooks must live inside a module that is loaded (e.g. module 12 itself or module 0 padding), or in a new module-table entry once the loader is traced.
- Mailbox send (core A → B): 0x0306DCD4(value, channel, slot), registers at 0x40110000 + ch×0x50 + {8,0x10,0x18,0x20}.

## Game Boy feasibility (M5) after M1 so far

| Question | Status |
|---|---|
| RAM | **Unknown again.** The >1 MB figure relied on the disproved load model; ≈ 622 KB core A in use; GB Color needs ~48 KB console RAM + bank cache |
| CPU clock | **200 MHz** in USB mode (D1, `*(0x030065C0+0x18)` = 0x0BEBC200). Music-mode clock still to check |
| Display push speed | Unknown. Measure a full-frame DMA blit on device |
| Simultaneous buttons | **Blocked on face buttons (measured).** Series ladder, lowest wins. Only the wheel and the GPIO key can combine with a face button |

## Flash and recovery

- Loader mode: d-pad corner + A + B while plugging USB → USB 2207:262D (BootROM rockusb, no `db` needed).
- 983040 sectors reported, 974848 readable (last 4 MB refused). fw1 at LBA 0, fw2 at 0x01E00000.
- Recovery proven: `tools/rkwrite.py wtest` (write, verify, restore in a free area). Real recovery: `rkwrite.py wl 0 <good image> --allow-any`.

## Open questions (also decide Game Boy feasibility)

- CPU clock (sysclk variable read by DelayMs at [*0x…+0x18])
- Total SRAM and what's free; can core B's RAM be used
- LCD interface and bus speed (Echo Mini shares the bus with flash)
- Key wiring: are simultaneous presses limited by hardware
- Amp (RT6863) enable/mute GPIO and timing

## Fonts (font0 / font1 sections, decoded 2026-11-07)

Stock draws all UI text at run time from two bitmap fonts covering the Unicode BMP, indexed
directly by code point (record n = U+n). Neither the v0.1 image reskin nor recolour touched them.

| Section | Size | Record | Glyph | Width byte |
|---|---|---|---|---|
| `font0.bin` | 2,104,326 B | 32 B | 12 rows x 16 bits, big-endian, bit 15 = left | byte 24 = 0x80 + advance (A = 0x87) |
| `font1.bin` | 2,169,862 B | 33 B | 16 rows x 16 bits | byte 32 = 0x80 + advance (A = 0x88) |

65536 records each, plus 7174 trailing bytes (not yet decoded). U+0000 is a box glyph.
**Fix chosen (owner, 2026-11-07): style D** = `art/font.py` `bold1x` (original 5x8-grid glyphs, upper and lower case; 16 px font in a bold 1x, 12 px font in regular 1x), applied by `build_reskin.py --font bold1x` from build 17. A data-only font fix is possible: overwrite the ASCII records (U+0020..U+007E) with original
glyphs and their advance widths. Still unknown: which screens use which font, and how stock lays
out text wider than its current advances (clipping or scrolling).

## Tasks (screens) and resident helpers, traced 2026-11-07

- `TaskSwitch(id, arg*)` 0x030680D4 stores the id at 0x030057C0 and copies 0x1C bytes of arg to +0x184. The task
  loader (0x030680AE) loads module `table[0x030057C0 + 0xC4 + 4*id]` with type 7; descriptor pointers at +4.
  Ids: 0 MainMenu (m21), 1 MediaLib (m22), 2 MediaBro (m23), 3 Music/Now Playing (m27), 6 Picture (m17),
  8 Ebook (m30), 9 Browser (m31), 10 Systemset + game list (m32), 11 USB (m9), 12 Charge (m6), 13 (m5),
  14.. games: 14 Snake (m81), **15 block game (m82)**, 16.. other games (game list launches 0xE..0x2B).
- Main menu: focus index u16 at 0x03023B94; tile actions via tbb at 0x030818B0: 0 Music, **1 Games
  (0x03081960: TaskSwitch(10, &7) = game list)**, 2/3 task 9, 4/5 task 10 pages. Returning to the menu:
  TaskSwitch(0, &index). Build 21 makes tile 1 call TaskSwitch(15, 0).
- **Slot 2 is resident:** module 1 loads slot 2 (type 7) at boot while reading the system config
  (0x030827B0), and no module runs above 0x0309F7CC, so code linked into slot 2 at 0x0309F800 stays loaded.
  `tools/build_helpers.py` appends C helpers after the D1 hook (build 21: 384 B, slot size 0x20C of 0x7F0).

## Starting music (partial trace for v0.21, 2026-11-07)

- Music engine = module 33, entry `AudioControl(cmd, arg)` 0x0308BF84 (Now Playing calls it 14 times; e.g. cmd 0xF8
  after volume changes). Audio Start 0x0308C8EE loads module 34 (library/file info), 0x0308FB0A, opens the file with
  0x0308F810 (-1 = "Audio File Open Error", posts msg 0x4F).
- The player runs as a stock thread: handle at 0x030232BC, descriptor 0x030361A4. Running test:
  0x03057C7C(*0x030232BC, 0x030361A4) == 1 (stock block game uses the same check). Start (Now Playing init,
  0x03081774): 0x03057C38(0x030232BC); ModuleOverlay(33, 7); 0x03057B96(0x030232BC, 0x030361A4, window arg).
- Folder browser play request (m31 0x0308236A): music source record 0x03022F14: +0xF2 = source type (browser
  value from its context +0x89; module 34 treats 0x63 and 6 specially), path copy (0x80 UTF-16) to 0x03022BBA,
  +0x10 = 0x030056C0 (browser folder state), +6 = file index, 0x03080644 = context +0x197; then 0x03055304 copies the
  folder stack from the system config 0x03022894 (+0x79/+0x7D, depth +0x18B, cluster stack +0x81/+0x18D, +0x195,
  +0x5E, +0x19D..+0x1A5) into the record; then TaskSwitch(3, {index, 0x46}).
- Files are opened by directory cluster + index (0x030598E6, "file open err: clus = %d, index = %d"); record +0x34 =
  directory cluster, +0x38 = file number (1-based). File system sources: Common\FileSys\{dir,FDT,fat,file}.c.
- Still needed for v0.21: find /_blockgame/ on the SD card (disk select, root cluster, directory entry lookup),
  the browser's file indexing (which entries count, order), and the exact source-type value; then fill the record
  and start the thread without opening Now Playing. Plan: a debug log written to a file on the internal drive
  (stock FileWrite exists) to dump these structures after playing a file from Folders.

## Stock Now Playing (module 27), traced 2026-11-07

- Window descriptor: tick 0x030823BE, service 0x03083688, paint 0x030835EC, init 0x030815C4, deinit 0x03081658.
- Text draw: 0x03063F76(rect*, rect*, str16, mode) in a rect {x0, y0, x1, y1}; 0x030644A0 draws a fallback when a
  string is empty; 0x030648EC clears a line area.
- Info box (0x030829AC): three lines in x 168..296 at y 91 / 112 / 133 from the tag record at 0x03048580:
  line 1 artist (+0x15E), line 2 album (+0x25C), line 3 genre (+0x35A), 0x40 UTF-16 chars each.
- Scrolling line at y 172..187 (0x03083422): one mode draws the long file name (0x030248FC, filled by 0x030639C2),
  the other the title buffer 0x03048450.
- Library record (module 34, 0x0308FA50): read from the hidden FAT volume; +0x400 (128 B) title -> 0x03048450,
  +0x500 artist -> 0x03048580+0x15E, +0x600 album -> +0x25C. A second path (0x0308FAF6) fills the title via 0x0305C364.
- **Title tag:** the tag reader (module 0, dispatcher 0x0306CD04, called by module 33 at play start after
  clearing the record with 0x03055B8A) writes title to record +0x60, artist +0x15E, album +0x25C (Vorbis
  "TITLE" match at 0x0306B1A4). 0x03048450 (library record +0x400) is the **file name**, not the title.
- Format line at (145, 60): string built at 0x030828E0, strip image 69 at 0x0308296E, text 0x03082996.
- Build 20 (`np_title_tag`): after line 1 the info box jumps to its epilogue, freeing 154 B of dead code at
  0x03082A30 for two helpers (hooks/np_title.S) that copy the file name into the title field when it is empty;
  mode 0 uses the title via literal 0x03083554, mode 1 via a call at 0x03083466.
- Build 18 patches (`tools/patch_code.py`): NOP the album and genre draws and their fallbacks; literal 0x030248FC at
  0x03083550 -> 0x03048450 so the scrolling line always shows the title. Unknown until tested: whether the title
  buffer is filled when playing from Folders.

## Module loading (ModuleOverlay)

- **RAM ends at 0x030A0000 (640 KB).** Stack top / initial SP = 0x03004000 (stack below module 0 data). Highest module run end = 0x0309F7CC (module 39). 0x0309FFF0 holds 4 magic words written before a reset (module 0 @0x03053DE8, read by module 1). **Free: 0x0309F800–0x0309FFF0 (2032 bytes)**, no literal in the image points there.
- `ModuleOverlay(n, type)` = **0x030683DE** (wrapper 0x030684E4, returns 0). Reads the 512-byte first sector of the code section from flash (base at `*0x03022864`, word 0 = 0x03050000), then table entry n at base + 8 + 32n, and copies **from flash**: type bit 0 code (cl → ce, cs), bit 1 data (dl → de, ds), bit 2 zero ZI (ze, zs). Copy routine 0x0306836A(flash offset, dst, len); flash byte read 0x03068194(0x30000, offset, len, buf). "Load" addresses are file positions (minus 0x03050000), never RAM.
- 0x0307E300 is a FAT sector cache (512 B buffer, +0x200 dirty flag, +0x204 cached sector), so module 0 cannot grow past 0x0307E300.
- **Empty slot 2 is loaded with type 7 in 21 places**, notably module 10's USB start (0x03081670 ModuleOverlay(12), 0x03081678 ModuleOverlay(2), then the handler). Pointing slot 2 at bytes in the file gap with run address 0x0309F800 makes stock load our code itself (`build_hook.py h1`). Empty slot 43 is loaded once (module 33).

## UI window / game module interface (for the v0.2 Blockz)

- Every UI screen and game is an overlay module whose **data section starts with a window descriptor**: `{0, 0, tick, service, paint, init, deinit}` (seen in modules 5, 6, 9, 17, 21 main menu, 27 Now Playing, 30, 32 settings, 81–111 games). Module 82 = stock Blockz ("LOCK PAUSE Level NEXT Score STOP Run"), module 81 = Snake. Module 82: code 0x1080 B at run 0x030815C4, data 0x3E8 B at 0x03023B48, ZI 0x320 B at 0x03023F30; state struct at 0x03023F34.
- Module 82 callbacks: tick 0x030824DE (increments state+0x14), service 0x0308249C (running → 0x03081FDA, paused/over → 0x030822BA), paint 0x030824EA, init 0x030824A8, deinit 0x030824D0.
- **Input:** `GetKey()` = 0x03062FA6 returns an event word: type in the top byte (0x40…, 0x20…, 0x41/0x42…), ladder key code in the low bits (0x001 GPIO key, 0x002 Select, 0x004 Right, 0x008 Left, 0x010 Up, 0x020 Down, 0x040 B, 0x080 A, 0x100 Start). Exact type meanings (press / repeat / release / long) still to confirm.
- **Messages:** bitset at 0x03023280; post = 0x03067ED4(id), test-and-clear = 0x03067EF0(id). Paint checks message ids such as 0x18C / 0x186 to decide what to redraw.
- **Drawing:** current colour (RGB565) at 0x03022DEC; fill rect 0x03064688(x0, y0, x1, y1) with that colour; text 0x03063E38(x, y, str16). The fill goes pixel by pixel (0x03064DC4), so a 60 fps game will want its own framebuffer writes plus the (still untraced) flush path.
- **Space while music plays:** module 33 (music) runs from 0x0308BF84 and decoders from 0x0308FFBC, so a replacement game module at 0x030815C4 has ~43 KB of code before it collides with the music engine.

## Device experiments log

- **H0 / H0a (2026-11-07), build 11.** Hook in the code.bin 0x57B8C gap, BL at 0x03083978 redirected. Both hang USB mass storage (see Memory). First H0 copy-update was silently consumed without flashing (battery was flat; device would not power on until charged). Second attempt flashed both slots.
- **Sector patching in loader mode works.** The flash copy of a slot is the IMG without its CRC trailer (slot offset 0x1C00000 holds unrelated data), so a change confined to one sector can be written directly with `rkwrite.py wl <slot LBA + sector> <512-byte file> --allow-any` (fw1 LBA 0, fw2 LBA 0xF000). Used to switch H0 → H0a and then restore the stock BL sector (0x40C) in both slots.
- **After a stalled read, the BootROM can drop off USB or fail the descriptor request.** Power off fully (hold 10 s) and replug; send `td` before the first read.
- **H1 (2026-11-07), build 12: PASSED.** Same INQUIRY-swap hook, linked at 0x0309F800 and loaded through module slot 2 (`build_hook.py h1`). Normal copy-update, booted normally; internal disk enumerates as `SCHALAOS ZEAL HOOK` rev `0.1`, SD stays `HIFI MP3 SD`. First custom code running on the AP30. Also shows USB DMA reads fine from 0x0309Fxxx.
- **D1 (2026-11-07), build 13: PASSED.** INQUIRY name = `<clock hex> <ch1 hex> <ch0 hex>`, rev `D1`. Read live without admin and without replugging with `tools/inquiry.py D [--loop N --changes]` (SCSI INQUIRY pass-through on `\\.\D:`). Results: clock 200 MHz, battery ch0 ≈ 0x372 (≈ 4.3 V on USB), buttons above. The ADC sampler keeps running in USB mode. B on the USB screen exits to the main menu (drops USB). The prompt appeared in Chinese after this update (settings likely reset by updates; to check).
- Device now: D1, stamp `26 20 07 11 13`.
