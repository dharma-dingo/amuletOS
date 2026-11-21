/* v0.4 timing log (build 57), resident in slot 2: where the ~3 s between choosing a track and hearing it goes.
 * There is no phone video, so the device timestamps its own play path (owner, Notion "amuletOS: Build Plan to 1.0").
 * Hooks: timing.S. Readout: the Games screen's "Timing" row (gbprobe.c). Slot 2 has ~300 B left, so this is lean:
 * times are stock's 10 ms tick (0x0302288C, counts from power-on), no TaskSwitch hook (the Now Playing load,
 * module 27, marks a start from a list).
 *
 * Log: 0x0309F700..0x0309F7C0 (192 B at the top of the region only the library builder, module 39, and the Game
 * Boy screen use; the Game Boy screen stops below it). Words: magic, events logged, boot-to-menu tick, armed flag,
 * then TL_N entries {tick, tag | r0 << 8 | r1 << 16 (low bytes)}, a ring. Tags:
 *   2 ModuleOverlay (module, type)   3 player thread init (MusicInit)   4 Audio Start (file open)
 *   6 first audio refill after 3 / 4 (first sound)   7 track step (Next / Prev / end)
 *   8 main menu opened (the first one after power-on = boot time) */
#include <stdint.h>

#define TL         ((volatile uint32_t *)0x0309F700u)
#define TL_MAGIC   0x544C4F47u
#define TL_N       22u
#define SYS_TICKS  (*(volatile uint32_t *)0x0302288Cu)

void tlog_hook(uint32_t a0, uint32_t a1, uint32_t tag)
{
    volatile uint32_t *t = TL;
    if (t[0] != TL_MAGIC) { t[1] = t[2] = t[3] = 0; t[0] = TL_MAGIC; }
    if (tag == 6) { if (!t[3]) return; t[3] = 0; }          /* every refill comes here: only the first counts */
    else if (tag - 3u <= 1u) t[3] = 1;
    uint32_t now = SYS_TICKS, i = t[1]++ % TL_N;
    if (tag == 8 && !t[2]) t[2] = now;
    t[4 + 2 * i] = now;
    t[5 + 2 * i] = tag | (a0 & 0xFF) << 8 | (a1 & 0xFF) << 16;
}
