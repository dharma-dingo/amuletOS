/* Blockz on the AP30 (T2): the portable core + renderer in stock's module-82 window.
 * Unity build: core.c and render.c are included here with BG_FILL_EXTERN so every pixel goes
 * through stock's FillRect (the RAM-framebuffer flush path is still untraced).
 *
 * Timing: stock runs SysTick at 10 ms and counts ticks at 0x0302288C; SysTick VAL gives the
 * position inside the tick, so time = ticks * 10 ms + (LOAD - VAL) / (LOAD + 1) * 10 ms,
 * independent of the CPU clock. The core steps at a fixed 60 fps from that clock.
 * Input: Left/Right/Down held state from the button ADC (ch1, measured midpoints); A, B, Up,
 * Start as press events from GetKey (type 0x40); Select quits; wheel steps = hold (T2: any
 * direction; the wheel still changes volume). docs/BLOCKZ.md, docs/HARDWARE.md. */
#include <stdint.h>
#include "../stock.h"
#define BG_FILL_EXTERN
#include "../core.c"
#include "../render.c"

#define MSG_CLEAR  0x18C
#define MSG_DRAW   0x186

#define SYS_TICKS  (*(volatile uint32_t *)0x0302288C)
#define ST_LOAD    (*(volatile uint32_t *)0xE000E014)
#define ST_VAL     (*(volatile uint32_t *)0xE000E018)
#define ADC_CH1    (*(volatile uint16_t *)(0x03005A8C + 2))
#define WHEEL_A    (*(volatile uint8_t *)0x030226E4)
#define WHEEL_B    (*(volatile uint8_t *)0x030226E5)

#define EV_PRESS   0x40u
#define K_SELECT   0x002
#define K_UP       0x010
#define K_B        0x040
#define K_A        0x080
#define K_START    0x100

static bg_game_t G;
static bg_rcache_t CACHE;
static struct {
    uint32_t last_us, acc3;      /* acc3: microseconds x3 (a frame is 50000) */
    uint8_t pulses;              /* press events waiting for the next frame */
    uint8_t wa, wb;              /* wheel counters last seen */
    uint8_t dirty, quit;
    uint32_t seed;
} S;

/* libc pieces the compiler may emit for struct copies */
void *memcpy(void *d, const void *s, unsigned n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int v, unsigned n) { uint8_t *a = d; while (n--) *a++ = (uint8_t)v; return d; }

void bg_fill_rect(int x, int y, int w, int h, uint16_t c)
{
    COLOR = c;
    FillRect(x, y, x + w - 1, y + h - 1);
}

static uint32_t now_us(void)
{
    uint32_t t1, v, load;
    do { t1 = SYS_TICKS; v = ST_VAL; load = ST_LOAD; } while (t1 != SYS_TICKS);
    if (v > load) v = load;
    uint32_t per_us = (load + 1u) / 10000u;        /* SysTick counts per microsecond (~200) */
    return t1 * 10000u + (per_us ? (load - v) / per_us : 0);
}

static uint8_t held_from_adc(void)
{
    uint16_t a = ADC_CH1;           /* measured: Right 0, Up ~119, Left ~241, Down 364, idle 1023 */
    if (a < 60)  return IN_RIGHT;
    if (a < 180) return 0;          /* Up: taken from GetKey as a press (hard drop) */
    if (a < 302) return IN_LEFT;
    if (a < 426) return IN_DOWN;
    return 0;
}

static void quit(void)
{
    uint16_t tile = 1;               /* main menu focus: BLOCKZ tile */
    GameLeaveB();
    TaskSwitch(0, &tile);
    COLOR = 0;
    S.quit = 1;
}

static void init(void)
{
    uint8_t *p = (uint8_t *)&S;
    for (unsigned i = 0; i < sizeof S; i++) p[i] = 0;
    CACHE.valid = 0;
    bg_cfg_t c;
    bg_default_cfg(&c);
    S.seed = SYS_TICKS ^ ST_VAL;
    if (G.cfg.goal_lines != 40) G.best = 0;      /* first launch: ZI is zeroed by the loader */
    uint32_t best = G.best;
    bg_init(&G, &c, S.seed);
    G.best = best;
    S.last_us = now_us();
    S.wa = WHEEL_A; S.wb = WHEEL_B;
    SendMsg(MSG_CLEAR);
    GameEnterA();
    GameEnterB();
}

static void deinit(void)
{
    GameLeaveA();
    GameLeaveB();
}

static int32_t tick(void)
{
    return 0;
}

static int32_t service(void)
{
    if (S.quit) return -1;

    /* press events */
    for (int n = 0; n < 8; n++) {
        uint32_t ev = GetKey();
        if (!ev) break;
        if ((ev >> 24) != EV_PRESS && !((ev & 0x1FF) == K_SELECT && (ev >> 24) == 0x20)) continue;
        switch (ev & 0x1FF) {
        case K_SELECT: quit(); return -1;
        case K_A:      S.pulses |= IN_CW; break;
        case K_B:      S.pulses |= IN_CCW; break;
        case K_UP:     S.pulses |= IN_UP; break;
        case K_START:  S.pulses |= IN_START; break;
        }
    }
    /* wheel: any step = hold (T2) */
    uint8_t wa = WHEEL_A, wb = WHEEL_B;
    if (wa != S.wa || wb != S.wb) { S.pulses |= IN_HOLD; S.wa = wa; S.wb = wb; }

    /* fixed 60 fps steps from the SysTick clock */
    uint32_t t = now_us(), dt = t - S.last_us;
    if ((int32_t)dt < 0) dt = 0;
    S.last_us = t;
    if (dt > 100000) dt = 100000;                  /* after a stall: at most 6 frames of catch-up */
    S.acc3 += dt * 3;
    while (S.acc3 >= 50000) {
        S.acc3 -= 50000;
        uint8_t in = held_from_adc() | S.pulses;
        S.pulses = 0;
        uint32_t ev = bg_step(&G, in);
        S.dirty = 1;
        if (ev & EV_QUIT) { quit(); return -1; }
    }
    if (S.dirty) SendMsg(MSG_DRAW);
    return 0;
}

static void paint(void)
{
    uint16_t old_color = COLOR;
    if (GetMsg(MSG_CLEAR)) CACHE.valid = 0;
    if (GetMsg(MSG_DRAW) || !CACHE.valid) {
        bg_render(&G, 0, 0, &CACHE);
        S.dirty = 0;
    }
    COLOR = old_color;
}

/* window descriptor: must be the first bytes of the data section (0x03023B48) */
__attribute__((section(".desc"), used))
const win_desc_t blockz_desc = { 0, 0, tick, service, paint, init, deinit };
