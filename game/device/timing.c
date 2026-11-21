/* v0.4 timing log (build 57), resident in slot 2: where the ~3 s between choosing a track and hearing it goes.
 * There is no phone video, so the device timestamps its own play path (owner, Notion "amuletOS: Build Plan to 1.0").
 * Hooks: timing.S. Readout: the Games screen's "Timing" row (gbprobe.c). Slot 2 has little room, so this is lean:
 * times are stock's 10 ms tick (0x0302288C, counts from power-on), no TaskSwitch hook (the Now Playing load,
 * module 27, marks a start from a list).
 *
 * Log (build 58): the 52 B between the library builder's code end (module 39, 0x0309F7CC) and slot 2 (0x0309F800),
 * which no module uses and nothing points into. Build 57 had it at 0x0309F700, inside the library builder's code:
 * the hooks are live while it runs (owner: Music menu -> library update), so the log wrote into code being run.
 * Words: [0] 0x544C << 16 | events logged, [1] boot-to-menu tick | armed << 31, then TL_N entries
 * tick (20 bits) | tag << 20 | arg << 24, a ring. Tags:
 *   2 ModuleOverlay (arg module)   3 player thread init (MusicInit)   4 Audio Start (file open)
 *   6 first audio refill after 3 / 4 (first sound)   7 track step (Next / Prev / end)
 *   8 main menu opened (the first one after power-on = boot time) */
#include <stdint.h>

#define TL         ((volatile uint32_t *)0x0309F7CCu)
#define TL_N       11u
#define SYS_TICKS  (*(volatile uint32_t *)0x0302288Cu)

void tlog_hook(uint32_t a0, uint32_t a1, uint32_t tag)
{
    volatile uint32_t *t = TL;
    uint32_t h = t[0], b;
    if (h >> 16 != 0x544Cu) { h = 0x544C0000u; b = 0; } else b = t[1];
    if (tag == 6) { if (!(b >> 31)) return; b &= 0x7FFFFFFFu; }   /* every refill comes here: only the first counts */
    else if (tag - 3u <= 1u) b |= 0x80000000u;
    uint32_t now = SYS_TICKS;
    if (tag == 8 && !(b & 0x7FFFFFFFu)) b |= now & 0x7FFFFFFFu;
    t[2 + (h & 0xFFFFu) % TL_N] = (now & 0xFFFFFu) | tag << 20 | (a0 & 0xFFu) << 24;
    t[1] = b;
    t[0] = h + 1;
}
