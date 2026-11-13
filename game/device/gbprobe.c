/* Game Boy probe (build 41; build 42: direct framebuffer drawing, no sprite sorting, clock shown): task 20
 * (module 87's slot; the code sits in the file space of stock games 87..99,
 * tools/build_module.py --span). Opened by the Music menu's temporary "Games" row (m22_ext.S).
 *
 * Lists /_gb/*.gb on the SD card, runs the chosen one with Peanut-GB (gb_core.c) at 1x in the middle of the
 * screen, and shows what we need to know before committing to Game Boy support (owner, 2026-11-13):
 *   top:    game title, frames per second reached (target 59.7)
 *   line 2: milliseconds per frame spent emulating, drawing (stock Window + Pixel) and pushing (stock flush)
 *   bottom: page cache (pages, loads per second, ms spent loading), cartridge RAM size
 *   line 4: raw button ADC, GPIO key level, the held Game Boy buttons, the last GetKey event
 * Last list row "Test core B memory": reads the second core's RAM from this core (may crash: that is the answer).
 * Hold Select for 1.5 s to quit. No sound, no saves.
 *
 * Memory (docs/HARDWARE.md "Game Boy probe"): the emulator state, the list and part of the page cache use
 * 0x03090800..0x0309F7C0, which only the library builder (module 39) ever loads into; more cache pages use the
 * overlay window after this module's code, up to the music engine (0x0308BF84). Music can keep playing. */
#include <stdint.h>
#include "../stock.h"
#define EX_NAME 26
#include "exfat.c"
#define PEANUT_GB_HIGH_LCD_ACCURACY 0   /* build 42: no per-line sprite sort (speed) */
#include "gb_core.c"

void *memcpy(void *d, const void *s, size_t n) { uint8_t *a = d; const uint8_t *b = s; while (n--) *a++ = *b++; return d; }
void *memset(void *d, int v, size_t n) { uint8_t *a = d; while (n--) *a++ = (uint8_t)v; return d; }
void abort(void) { for (;;) {} }

#define SYSC       0x03022894u
#define S8(o)      (*(volatile uint8_t *)(SYSC + (o)))
#define DiskSwitch(n)       STOCK_FN(0x03058526, void, uint32_t)(n)
#define SetFont(n)          STOCK_FN(0x030643A2, uint32_t, uint32_t)(n)
#define CharW(c)            STOCK_FN(0x03063BC6, uint32_t, uint32_t)(c)
#define FontRead(o, b, n)   STOCK_FN(0x0306535C, void, uint32_t, void *, uint32_t)(o, b, n)
#define Window(a, b, c, d)  STOCK_FN(0x03064EA8, void, int, int, int, int)(a, b, c, d)
#define Pixel(c)            STOCK_FN(0x03064DC4, void, uint32_t)(c)
#define Flush()             STOCK_FN(0x0306558A, void, void)()
#define ThreadRunning(h, d) STOCK_FN(0x03057C7C, uint32_t, uint32_t, uint32_t)(h, d)
#define FONTBASE   (*(volatile uint32_t *)0x03022868u)
#define SPACING    (*(volatile int16_t *)0x03022E04u)
#define SYS_TICKS  (*(volatile uint32_t *)0x0302288Cu)
#define ST_LOAD    (*(volatile uint32_t *)0xE000E014u)
#define ST_VAL     (*(volatile uint32_t *)0xE000E018u)
#define ADC_CH1    (*(volatile uint16_t *)(0x03005A8Cu + 2))
#define GPIO2      (*(volatile uint32_t *)0x50030000u)

#define U_BASE     0x03090800u
#define U_END      0x0309F7C0u
#define WIN_END    0x0308BF84u
extern char _text_end[];             /* module.ld: end of this module's code */

#define MSG_DRAW   0x186
#define EV_PRESS   0x40u
#define K_UP       0x010
#define K_DOWN     0x020
#define K_B        0x040
#define K_A        0x080
#define K_LEFT     0x008

#define C_INK    RGB565(0x23, 0x25, 0x2e)
#define C_DIM    RGB565(0x7d, 0x82, 0x91)
#define C_WHITE  RGB565(0xff, 0xff, 0xff)
#define C_HEAD   RGB565(0xf2, 0xf3, 0xf6)
#define C_TRACK  RGB565(0xdf, 0xe1, 0xe8)
#define C_SCREEN RGB565(0xfb, 0xfb, 0xf8)
#define C_TINT   RGB565(0xe9, 0xe3, 0xfb)
#define C_GRAPE  RGB565(0x73, 0x53, 0xe0)

#define GB_X     68                  /* (296 - 160) / 2 */
#define GB_Y     48                  /* (240 - 144) / 2 */
#define FRAME_US 16743u              /* 70224 cycles at 4.194304 MHz */
#define ROWS     7
#define NLIST    24

enum { ST_LIST, ST_NOCARD, ST_NODIR, ST_EMPTY, ST_RUN, ST_ERR, ST_CORE_B, ST_QUIT };

typedef struct {
    uint32_t first, len;
    uint8_t  nofat, cgb, nlen;
    uint16_t name[EX_NAME];
} rom_t;

typedef struct {                     /* everything big, at U_BASE */
    gbx_t   g;
    rom_t   list[NLIST];
    uint32_t clus[512];              /* clusters of a fragmented ROM (2 MB in 4 KB clusters at most) */
    uint8_t sec[512];
} big_t;
#define B ((big_t *)U_BASE)

static struct {
    uint8_t state, dirty, nlist, adc_state, adc_cand, gpio_idle, last_hdr;
    int sel, top;
    uint32_t adc_since, sel_since, next, stat_t0;
    uint32_t frames, emu_us, draw_us, push_us, load_us, loads0, late, line_us;
    uint32_t last_ev, err;
    const rom_t *rom;
    uint32_t cb_read, cb_rw;          /* core B test results */
} S;

/* ---- text (stock 16 px font, drawn as runs; as playlists.c) ---- */
static void fill(int x0, int y0, int x1, int y1, uint16_t c) { COLOR = c; FillRect(x0, y0, x1, y1); }

static int text(int x, int y, const uint16_t *s, uint16_t col, int x_max)
{
    SetFont(2);
    uint32_t base = FONTBASE;
    COLOR = col;
    for (; *s; s++) {
        int w = (int)CharW(*s) + SPACING;
        if (x + w > x_max) break;
        if (*s != ' ') {
            uint8_t g[32];
            FontRead(base + *s * 33u, g, 32);
            for (int r = 0; r < 16; r++) {
                uint32_t v = (uint32_t)g[2 * r] << 8 | g[2 * r + 1];
                for (int c = 0; c < 16; ) {
                    if (!(v & (0x8000u >> c))) { c++; continue; }
                    int c0 = c;
                    while (c < 16 && (v & (0x8000u >> c))) c++;
                    FillRect(x + c0, y + r, x + c - 1, y + r);
                }
            }
        }
        x += w;
    }
    return x;
}

/* tiny formatter: %s (char *), %d, %x (8 digits), %1 (value / 10 with one decimal: 597 -> 59.7) */
static void fmt(uint16_t *o, const char *f, const uint32_t *a)
{
    int k = 0;
    for (; *f && k < 60; f++) {
        if (*f != '%') { o[k++] = (uint8_t)*f; continue; }
        char t = *++f; uint32_t v = *a++;
        char tmp[12]; int n = 0;
        if (t == 's') { for (const char *s = (const char *)v; *s && k < 60; s++) o[k++] = (uint8_t)*s; continue; }
        if (t == 'x') { for (int i = 7; i >= 0; i--) { uint32_t d = (v >> (4 * i)) & 15; o[k++] = d < 10 ? '0' + d : 'A' + d - 10; } continue; }
        uint32_t w = t == '1' ? v / 10 : v;
        do { tmp[n++] = (char)('0' + w % 10); w /= 10; } while (w);
        while (n) o[k++] = tmp[--n];
        if (t == '1') { o[k++] = '.'; o[k++] = (uint16_t)('0' + v % 10); }
    }
    o[k] = 0;
}

static void say(int x, int y, uint16_t col, const char *f, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4)
{
    uint32_t a[5] = { a0, a1, a2, a3, a4 };
    uint16_t b[64];
    fmt(b, f, a);
    text(x, y, b, col, 294);
}

static uint32_t now_us(void)
{
    uint32_t t1, v, load;
    do { t1 = SYS_TICKS; v = ST_VAL; load = ST_LOAD; } while (t1 != SYS_TICKS);
    if (v > load) v = load;
    uint32_t per_us = (load + 1u) / 10000u;
    return t1 * 10000u + (per_us ? (load - v) / per_us : 0);
}

/* ---- ROM pages from the SD card ---- */
static uint32_t sec_of(const rom_t *r, uint32_t off)          /* absolute LBA of the sector holding byte off */
{
    uint32_t cb = 9 + X.shift, ci = off >> cb;
    uint32_t c = r->nofat ? r->first + ci : B->clus[ci];
    return ex_lba(c) + ((off >> 9) & ((1u << X.shift) - 1));
}

void gbx_read_page(gbx_t *x, uint32_t page, uint8_t *dst)
{
    uint32_t t0 = now_us(), off = page << PG_SHIFT;
    for (uint32_t k = 0; k < PG_SIZE; k += 512)
        if (off + k < S.rom->len) ReadSec(sec_of(S.rom, off + k), dst + k);
        else for (int i = 0; i < 512; i++) dst[k + i] = 0xFF;
    S.load_us += now_us() - t0;
}

/* Draw straight into stock's RAM framebuffer (build 42; build 41 went through stock's Window + Pixel: 15 ms a
 * frame). Layout, from Pixel 0x03064DC4 / flush 0x0306558A (docs/HARDWARE.md "Display"): rows 0..189 at
 * 0x03006688 + page * 0x1B760, rows 190..239 at 0x0306FD50 + page * 0x73A0, 592 bytes a row; the panel is mounted
 * mirrored (column = 295 - x while the rotation flag is set) and pixels are swapped in pairs (index ^ 1). After a
 * frame, Window() over the Game Boy area marks the rows dirty for the flush, as Pixel would. */
#define FBS        0x03022E48u
#define FB_PAGE    (*(volatile uint16_t *)(FBS + 0x00))
#define FB_DIRTY   (*(volatile uint32_t *)(FBS + 0x0C))
#define FB_ROT     (*(volatile uint32_t *)(FBS + 0x14))

void gbx_line(const uint16_t *rgb, int line)
{
    uint32_t t0 = now_us();
    int y = GB_Y + line;
    uint16_t *row = y < 190 ? (uint16_t *)(0x03006688u + FB_PAGE * 0x1B760u + (uint32_t)y * 592u)
                            : (uint16_t *)(0x0306FD50u + FB_PAGE * 0x73A0u + (uint32_t)(y - 190) * 592u);
    if (FB_ROT) { int c = 295 - GB_X; for (int i = 0; i < LCD_WIDTH; i++, c--) row[c ^ 1] = rgb[i]; }
    else        { int c = GB_X;       for (int i = 0; i < LCD_WIDTH; i++, c++) row[c ^ 1] = rgb[i]; }
    S.line_us += now_us() - t0;
}

static void mark_dirty(void)
{
    Window(GB_X, GB_Y, GB_X + LCD_WIDTH - 1, GB_Y + LCD_HEIGHT - 1);
    FB_DIRTY = 1;
}

/* ---- list ---- */
static int list_cb(const exent_t *e, int match, void *ctx)
{
    if (e->is_dir || S.nlist >= NLIST) return 0;
    if (!(e->elen == 2 && e->ext[0] == 'G' && e->ext[1] == 'B') &&
        !(e->elen == 3 && e->ext[0] == 'G' && e->ext[1] == 'B' && e->ext[2] == 'C')) return 0;
    if (e->nlen && e->name[0] == '.') return 0;                 /* macOS ._ files */
    rom_t *r = &B->list[S.nlist++];
    r->first = e->first; r->len = e->len; r->nofat = (e->flags & 2) != 0;
    r->nlen = (uint8_t)(e->cap < EX_NAME ? e->cap : EX_NAME);
    for (int i = 0; i < r->nlen; i++) r->name[i] = e->name[i];
    return 0;
}

static void build_list(void)
{
    static const uint16_t gbdir[] = { '_', 'G', 'B' };
    exdir_t root, dir;
    ex_root(&root);
    if (!ex_find(&root, gbdir, 3, 1, &dir, 0)) { S.state = ST_NODIR; return; }
    ex_walk(&dir, 0, 0, list_cb, 0);
    if (!S.nlist) { S.state = ST_EMPTY; return; }
    for (int i = 0; i < S.nlist; i++) {                           /* header byte 0x143: 0xC0 = Game Boy Color only */
        rom_t *r = &B->list[i];
        r->cgb = 0;
        if (r->len > 0x150) { ReadSec(ex_lba(r->first), B->sec); r->cgb = B->sec[0x143]; }
    }
}

static void redraw(void) { S.dirty = 1; SendMsg(MSG_DRAW); }

static void init(void)
{
    uint8_t *p = (uint8_t *)&S;
    for (unsigned i = 0; i < sizeof S; i++) p[i] = 0;
    if (!S8(0x1F)) S.state = ST_NOCARD;
    else {
        if (S8(0x0E) != 4) { S8(0x0E) = 4; DiskSwitch(4); }
        if (ex_init()) S.state = ST_NOCARD;
        else build_list();
    }
    GameEnterA();
    GameEnterB();
    redraw();
}

static void deinit(void) { GameLeaveA(); GameLeaveB(); }

static int32_t tick(void) { return 0; }

static void quit(void)
{
    uint16_t tile = 1;
    GameLeaveB();
    TaskSwitch(0, &tile);
    S.state = ST_QUIT;
}

/* ---- buttons: ADC ladder held state (debounced as Blockz) + the GPIO key ---- */
static const uint16_t ADC_EDGE[8] = { 59, 180, 303, 425, 576, 722, 815, 950 };   /* midpoints, docs/HARDWARE.md */
static const uint8_t  ADC_PAD[8]  = { JOYPAD_RIGHT, JOYPAD_UP, JOYPAD_LEFT, JOYPAD_DOWN,
                                      JOYPAD_SELECT, JOYPAD_START, JOYPAD_B, JOYPAD_A };
static uint8_t adc_decode(uint16_t a) { for (int i = 0; i < 8; i++) if (a < ADC_EDGE[i]) return (uint8_t)i; return 8; }

static void poll_adc(uint32_t now)
{
    uint8_t k = adc_decode(ADC_CH1);
    if (k != S.adc_cand) { S.adc_cand = k; S.adc_since = now; return; }
    if (k == S.adc_state) return;
    uint32_t need = k == 8 ? 16000 : S.adc_state == 8 ? (k == 3 ? 40000 : 24000) : k > S.adc_state ? 80000 : 24000;
    if (now - S.adc_since >= need) S.adc_state = k;
}

static uint8_t gpio_key(void) { return (uint8_t)((GPIO2 >> 5) & 1); }

/* ---- running ---- */
static void frame_box(void)
{
    fill(0, 0, 295, 239, C_SCREEN);
    fill(GB_X - 2, GB_Y - 2, GB_X + LCD_WIDTH + 1, GB_Y - 1, C_INK);
    fill(GB_X - 2, GB_Y + LCD_HEIGHT, GB_X + LCD_WIDTH + 1, GB_Y + LCD_HEIGHT + 1, C_INK);
    fill(GB_X - 2, GB_Y, GB_X - 1, GB_Y + LCD_HEIGHT - 1, C_INK);
    fill(GB_X + LCD_WIDTH, GB_Y, GB_X + LCD_WIDTH + 1, GB_Y + LCD_HEIGHT - 1, C_INK);
}

static void start(const rom_t *r)
{
    S.rom = r;
    if (r->cgb == 0xC0) { S.err = 100; S.state = ST_ERR; redraw(); return; }
    if (r->len > (2u << 20)) { S.err = 101; S.state = ST_ERR; redraw(); return; }
    if (!r->nofat) {                                              /* fragmented: follow the FAT once */
        uint32_t c = r->first, n = (r->len + (512u << X.shift) - 1) >> (9 + X.shift);
        if (n > 512) { S.err = 104; S.state = ST_ERR; redraw(); return; }
        for (uint32_t i = 0; i < n; i++) { B->clus[i] = c; c = ex_next(c, 0); }
    }
    /* page buffers: the window after our code first, then the region after big_t */
    static uint8_t *pages[PG_SLOTS];
    uint32_t n = 0, a = ((uint32_t)_text_end + 3) & ~3u;
    for (; a + PG_SIZE <= WIN_END && n < PG_SLOTS; a += PG_SIZE) pages[n++] = (uint8_t *)a;
    a = (U_BASE + sizeof(big_t) + 3) & ~3u;
    for (; a + PG_SIZE <= U_END && n < PG_SLOTS; a += PG_SIZE) pages[n++] = (uint8_t *)a;
    int e = gbx_init(&B->g, r->len, pages, n);
    if (e) { S.err = (uint32_t)e; S.state = ST_ERR; redraw(); return; }
    uint32_t cram = gbx_save_size(&B->g);
    if (cram > 0x8000) cram = 0;                                  /* bigger cartridge RAM: not in the probe */
    uint32_t give = (cram + PG_SIZE - 1) >> PG_SHIFT;             /* cartridge RAM takes the last pages */
    if (give) {
        if (give + 8 > B->g.nslots) { S.err = 102; S.state = ST_ERR; redraw(); return; }
        for (uint32_t i = B->g.nslots - give; i < B->g.nslots; i++)
            if (B->g.owner[i] != 0xFFFF) { S.err = 103; S.state = ST_ERR; redraw(); return; }
        B->g.nslots -= give;
        B->g.cram = B->g.slot[B->g.nslots];                       /* contiguous: the U pages ascend */
        B->g.cram_len = cram;
        for (uint32_t i = 0; i < cram; i++) B->g.cram[i] = 0xFF;
    }
    S.gpio_idle = gpio_key();
    S.adc_state = S.adc_cand = 8;
    S.state = ST_RUN;
    frame_box();
    S.next = S.stat_t0 = now_us();
    S.frames = S.emu_us = S.draw_us = S.push_us = S.load_us = S.late = 0;
    S.loads0 = B->g.misses;
}

static void stats(uint32_t now)
{
    uint32_t span = now - S.stat_t0, f = S.frames ? S.frames : 1;
    char title[17]; gb_get_rom_name(&B->g.gb, title);
    fill(0, 0, 295, GB_Y - 3, C_SCREEN);
    fill(0, GB_Y + LCD_HEIGHT + 2, 295, 239, C_SCREEN);
    say(4, 4, C_INK, "%s  FPS %1", (uint32_t)title, span ? S.frames * 10000000u / span : 0, 0, 0, 0);
    say(4, 24, C_INK, "EMU %1 DRAW %1 PUSH %1 MHZ %d", S.emu_us / f / 100, S.draw_us / f / 100, S.push_us / f / 100,
        (ST_LOAD + 1u) / 10000u, 0);
    say(4, 198, C_INK, "PAGES %d LOADS %d %1MS SAVE %d", B->g.nslots, B->g.misses - S.loads0, S.load_us / 100,
        B->g.cram_len, 0);
    uint8_t pad = (uint8_t)~B->g.gb.direct.joypad;
    say(4, 218, C_DIM, "ADC %d G%d KEYS %x CLK %d", ADC_CH1, gpio_key(), pad,
        *(volatile uint32_t *)0x030065D8u / 1000000u, 0);
    S.stat_t0 = now_us();
    S.frames = S.emu_us = S.draw_us = S.push_us = S.load_us = 0;
    S.loads0 = B->g.misses;
}

static void run(void)
{
    uint32_t now = now_us();
    poll_adc(now);
    uint8_t pad = 0xFF;
    if (S.adc_state < 8) pad &= (uint8_t)~ADC_PAD[S.adc_state];
    if (gpio_key() != S.gpio_idle) pad &= (uint8_t)~JOYPAD_A;   /* the one key that combines with the ladder */
    B->g.gb.direct.joypad = pad;
    if (S.adc_state == 4) { if (!S.sel_since) S.sel_since = now | 1; else if (now - S.sel_since > 1500000) { quit(); return; } }
    else S.sel_since = 0;
    if ((int32_t)(now - S.next) < 0) return;
    S.line_us = 0;
    uint32_t t0 = now_us();
    gb_run_frame(&B->g.gb);
    uint32_t t1 = now_us();
    mark_dirty();
    Flush();
    uint32_t t2 = now_us();
    S.emu_us += (t1 - t0) - S.line_us;
    S.draw_us += S.line_us;
    S.push_us += t2 - t1;
    S.frames++;
    if (B->g.err) { S.err = 200 + (uint32_t)B->g.err; S.state = ST_ERR; redraw(); return; }
    S.next += FRAME_US;
    if ((int32_t)(t2 - S.next) > (int32_t)(3 * FRAME_US)) { S.next = t2; S.late++; }
    if (t2 - S.stat_t0 >= 1000000) { stats(t2); Flush(); }
}

static void core_b_test(void)
{
    volatile uint32_t *p = (volatile uint32_t *)0x01000000u, *q = (volatile uint32_t *)0x0102E000u;
    S.cb_read = *p;
    uint32_t v = *q;
    *q = ~v;
    S.cb_rw = (*q == ~v) ? 1 : 2;
    *q = v;
    S.state = ST_CORE_B;
    redraw();
}

static int32_t service(void)
{
    if (S.state == ST_QUIT) return -1;
    if (S.state == ST_RUN) {
        for (int n = 0; n < 8; n++) { uint32_t ev = GetKey(); if (!ev) break; S.last_ev = ev; }
        run();
        return S.state == ST_QUIT ? -1 : 0;
    }
    for (int n = 0; n < 8; n++) {
        uint32_t ev = GetKey();
        if (!ev) break;
        uint32_t type = ev >> 24, k = ev & 0x1FF;
        if (type != EV_PRESS && type != 0x30) continue;
        if (k == K_B || (k == K_LEFT && type == EV_PRESS)) { quit(); return -1; }
        if (S.state == ST_ERR || S.state == ST_CORE_B) { if (type == EV_PRESS) { S.state = ST_LIST; redraw(); } continue; }
        if (S.state != ST_LIST) continue;
        int count = S.nlist + 1;                                   /* + "Test core B memory" */
        if (k == K_UP || k == K_DOWN) {
            int s = S.sel + (k == K_DOWN ? 1 : -1);
            if (s < 0) s = count - 1;
            if (s >= count) s = 0;
            S.sel = s;
            if (S.sel < S.top) S.top = S.sel;
            if (S.sel >= S.top + ROWS) S.top = S.sel - ROWS + 1;
            redraw();
        } else if (k == K_A && type == EV_PRESS) {
            if (S.sel == S.nlist) core_b_test();
            else start(&B->list[S.sel]);
            if (S.state == ST_RUN) return 0;
        }
    }
    return 0;
}

static void paint(void)
{
    if (!GetMsg(MSG_DRAW) && !S.dirty) return;
    S.dirty = 0;
    if (S.state == ST_RUN || S.state == ST_QUIT) return;
    uint16_t old = COLOR;
    fill(0, 0, 295, 32, C_WHITE);
    fill(0, 33, 295, 34, C_INK);
    fill(0, 35, 295, 58, C_HEAD);
    fill(0, 59, 295, 60, C_INK);
    fill(0, 221, 295, 222, C_INK);
    fill(0, 223, 295, 239, C_SCREEN);
    say(12, 39, C_INK, "Game Boy", 0, 0, 0, 0, 0);
    const char *msg = 0;
    switch (S.state) {
    case ST_NOCARD: msg = "No SD card"; break;
    case ST_NODIR:  msg = "No _gb folder"; break;
    case ST_EMPTY:  msg = "No games"; break;
    }
    for (int r = 0; r < ROWS; r++) fill(0, 61 + r * 23, 295, 61 + r * 23 + 22, C_WHITE);
    if (S.state == ST_ERR) {
        if (S.err == 100) say(12, 64, C_INK, "Game Boy Color only", 0, 0, 0, 0, 0);
        else if (S.err == 101) say(12, 64, C_INK, "Over 2 MB", 0, 0, 0, 0, 0);
        else say(12, 64, C_INK, "Error %d", S.err, 0, 0, 0, 0);
        COLOR = old; return;
    }
    if (S.state == ST_CORE_B) {
        say(12, 64, C_INK, "01000000 reads %x", S.cb_read, 0, 0, 0, 0);
        say(12, 87, C_INK, S.cb_rw == 1 ? "0102E000 write ok" : "0102E000 write lost", 0, 0, 0, 0, 0);
        COLOR = old; return;
    }
    int count = msg ? 0 : S.nlist + 1;
    for (int r = 0; r < ROWS; r++) {
        int i = S.top + r, y = 61 + r * 23, on = i == S.sel && i < count;
        fill(0, y, 295, y + 21, on ? C_TINT : C_WHITE);
        if (on) fill(0, y, 3, y + 21, C_GRAPE);
        fill(0, y + 22, 295, y + 22, r == ROWS - 1 ? C_WHITE : C_TRACK);
        if (msg) { if (r == 0) say(12, y + 3, C_DIM, msg, 0, 0, 0, 0, 0); continue; }
        if (i >= count) continue;
        if (i == S.nlist) { say(12, y + 3, C_INK, "Test core B memory", 0, 0, 0, 0, 0); continue; }
        const rom_t *rm = &B->list[i];
        uint16_t b[EX_NAME + 1];
        for (int c = 0; c < rm->nlen; c++) b[c] = rm->name[c];
        b[rm->nlen] = 0;
        int x = text(12, y + 3, b, rm->cgb == 0xC0 ? C_DIM : C_INK, 290);
        (void)x;
    }
    COLOR = old;
}

__attribute__((section(".desc"), used))
const win_desc_t gb_desc = { 0, 0, tick, service, paint, init, deinit };
