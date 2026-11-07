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

Plus one key on **GPIO2 pin 5** (code 0x001), checked first. Code → physical button mapping still to be confirmed on device.

**Consequences.** One ladder means two face buttons pressed together give a single combined voltage, so most simultaneous presses cannot be read (hardware limit, not just the stock single-key driver). Matters for the block game (move + rotate) and Game Boy (direction + A). To measure: log raw ch1 values for button pairs in the first diagnostic build; some pairs may land in distinct gaps.
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
- **Open:** bus speed and full-frame push time. Measure on device (time a full 296×240 DMA blit).

## Memory

- Core A SRAM in use: 0x03004000–0x0309F7CC (≈ 622 KB; chip likely 640 KB). Module 0: 185 KB code at 0x03050000, 6.6 KB data at 0x03004000, 120 KB ZI at 0x03005A8C (holds the framebuffer). Overlay code window 0x03080E9C–0x0309F7CC (≈ 120 KB).
- Core B region in use: 0x01000000–0x0102D184 (≈ 180 KB), decoders in modules 71–80.
- **Load model** (per ReChord `firmware_ap.ld`, consistent with everything seen here): the boot ROM loads the whole code section contiguously into RAM, with code.bin offset 0 at 0x03050000. Module-table "load" addresses are therefore real RAM addresses, and overlays are copied from that in-RAM image on demand. RAM extends at least to the end of the loaded image (≈ 0x031D8714), well beyond the ≈ 622 KB the modules execute in.
- Runtime reuse of the loaded image: after boot, module 0 runs at 0x03050000, a heap/buffer area follows at 0x0307E300 (fixed-address globals, e.g. 0x0307E710 has 35 references), and overlays execute at 0x03080E9C–0x0309F7CC. Sources above that (module data and overlay sources from 0x030ABB8C up) must stay intact.
- **Free block:** code.bin 0x57B8C–0x5BB8C (16 KB of zeros between module 0's code source and its data source) sits in RAM at **0x030A7B8C–0x030ABB8C** after boot. No module executes there and no constant in the image points into it.
- Mailbox send (core A → B): 0x0306DCD4(value, channel, slot), registers at 0x40110000 + ch×0x50 + {8,0x10,0x18,0x20}.

## Game Boy feasibility (M5) after M1 so far

| Question | Status |
|---|---|
| RAM | **OK.** Well over 1 MB of RAM in total; GB Color needs ~48 KB console RAM + bank cache |
| CPU clock | Unknown. DelayMs reads it from a clock variable; measure on device |
| Display push speed | Unknown. Measure a full-frame DMA blit on device |
| Simultaneous buttons | **Risk.** All 8 face buttons share one ADC ladder; most pairs likely unreadable. Measure raw pair values |

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
