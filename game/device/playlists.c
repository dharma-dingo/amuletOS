/* SchalaOS Playlists screen (v0.3, build 35): module 86 (task 19, stock game slot nobody reaches), opened by the
 * Music menu's "Playlists" row (np_stub.S pl_menu_exit). Lists /_Playlists/*.m3u* on the SD card; A plays the
 * chosen playlist from track 1 in Now Playing; B goes back to the Music menu.
 *
 * Playing: resolve the entries (pl_core.c) into the slot-2 table (pl_table.h), point the music source record at
 * entry 1 the way the folder browser does (source 0x63 "files": folder cluster + file number), then
 * TaskSwitch(3) so Now Playing's own init starts the player. Next / Prev / track end then go through
 * np_helpers.c pl_step. docs/HARDWARE.md, "Now Playing keys". Plain labels only (owner). */
#include <stdint.h>
#include "../stock.h"
#include "pl_table.h"

#define PL_MAX PLT_MAX
#include "exfat.c"
#include "pl_core.c"

#define SYSC       0x03022894u
#define REC        0x03022F14u
#define PHAND      0x030232BCu
#define PDESC      0x030361A4u
#define ThreadRunning(h, d) STOCK_FN(0x03057C7C, uint32_t, uint32_t, uint32_t)(h, d)
#define ThreadStop(hp, d)   STOCK_FN(0x03057BD4, void, uint32_t, uint32_t)(hp, d)
#define DiskSwitch(n)       STOCK_FN(0x03058526, void, uint32_t)(n)
#define RecPrepare()        STOCK_FN(0x03055304, void, void)()
#define SetFont(n)          STOCK_FN(0x030643A2, uint32_t, uint32_t)(n)
#define DrawImage(i, x, y)  STOCK_FN(0x03064D20, void, uint32_t, int, int)(i, x, y)
#define S8(o)   (*(volatile uint8_t *)(SYSC + (o)))
#define R16(o)  (*(volatile uint16_t *)(REC + (o)))
#define R32(o)  (*(volatile uint32_t *)(REC + (o)))

#define MSG_DRAW  0x186
#define EV_PRESS  0x40u
#define EV_REPEAT 0x30u
#define K_RIGHT 0x004
#define K_LEFT  0x008
#define K_UP    0x010
#define K_DOWN  0x020
#define K_B     0x040
#define K_A     0x080

#define C_INK   RGB565(0x23, 0x25, 0x2e)
#define C_DIM   RGB565(0x7d, 0x82, 0x91)
#define IMG_BG  159             /* art/pocket.py list_bg: status band, header band 35..58, rows of 23 from 61 */
#define IMG_SEL 184             /* selection bar 296 x 22 */
#define IMG_ICON 236            /* playlist row icon; +10 = on the selection bar */
#define ROW_Y   61
#define ROW_H   23
#define ROWS    7

enum { ST_LIST, ST_NOCARD, ST_NODIR, ST_EMPTY, ST_LOAD, ST_NOTRACKS, ST_QUIT };

static struct {
    uint8_t state, dirty;
    int sel, top;
} S;
static pl_list_t L;

static void a16(uint16_t *d, const char *s) { while ((*d++ = (uint8_t)*s++)) {} }

static void text(int x, int y, const uint16_t *s, uint16_t col) { COLOR = col; DrawText(x, y, s); }

static void redraw(void) { S.dirty = 1; SendMsg(MSG_DRAW); }

static void init(void)
{
    uint8_t *p = (uint8_t *)&S;
    for (unsigned i = 0; i < sizeof S; i++) p[i] = 0;
    L.count = 0;
    if (!S8(0x1F)) S.state = ST_NOCARD;                     /* no SD card */
    else {
        if (S8(0x0E) != 4) { S8(0x0E) = 4; DiskSwitch(4); } /* SD card is disk 4 */
        if (ex_init()) S.state = ST_NOCARD;
        else if (pl_list(&L)) S.state = ST_NODIR;
        else if (!L.count) S.state = ST_EMPTY;
    }
    redraw();
}

static void deinit(void) {}

static int32_t tick(void) { return 0; }

static void quit(void)
{
    TaskSwitch(1, 0);                                      /* Music menu */
    S.state = ST_QUIT;
}

/* resolve the chosen playlist into the slot-2 table and start it in Now Playing */
static void play(void)
{
    static pl_res_t R;
    volatile pl_table_t *t = PLT;
    t->magic = 0;
    pl_load(&L, &L.files[S.sel], &R);
    if (!R.n) { S.state = ST_NOTRACKS; redraw(); return; }
    for (int i = 0; i < R.n; i++) { t->clus[i] = R.clus[i]; t->num[i] = R.num[i]; }
    t->n = (uint16_t)R.n; t->cur = 0;
    t->magic = PLT_MAGIC;

    if (ThreadRunning(*(volatile uint32_t *)PHAND, PDESC) == 1) ThreadStop(PHAND, PDESC);
    RecPrepare();                                          /* the browser's play-request prep */
    *(volatile uint16_t *)0x03022BAC = 0x63;               /* list type, as the browser sets it */
    R16(0xF2) = 0x63;
    R32(0x10) = 0x030056C0u;                               /* browser folder state */
    R32(0x34) = t->clus[0];
    R32(0x38) = t->num[0];
    R16(0) = (uint16_t)R.n; R16(2) = 1; R16(6) = 1;
    R32(0x44) = 0; R32(0x48) = 0; R32(0x4C) = 0;
    S8(0x64) = 0;                                          /* no resume point: a reboot does not restart it */
    static uint16_t arg[14];
    arg[0] = 1; arg[1] = 0x46; arg[2] = 0x504C;            /* as Folders: {index, 0x46}; 0x504C: ours */
    TaskSwitch(3, arg);
    S.state = ST_QUIT;
}

static int32_t service(void)
{
    if (S.state == ST_QUIT) return -1;
    if (S.state == ST_LOAD) { play(); return S.state == ST_QUIT ? -1 : 0; }
    for (int n = 0; n < 8; n++) {
        uint32_t ev = GetKey();
        if (!ev) break;
        uint32_t type = ev >> 24, k = ev & 0x1FF;
        if (type != EV_PRESS && type != EV_REPEAT) continue;
        if (k == K_B || (k == K_LEFT && type == EV_PRESS)) { quit(); return -1; }
        if (S.state == ST_NOTRACKS && type == EV_PRESS) { S.state = ST_LIST; redraw(); }
        if (S.state != ST_LIST) continue;
        if (k == K_UP || k == K_DOWN) {
            int s = S.sel + (k == K_DOWN ? 1 : -1);
            if (s < 0) s = L.count - 1;
            if (s >= L.count) s = 0;
            S.sel = s;
            if (S.sel < S.top) S.top = S.sel;
            if (S.sel >= S.top + ROWS) S.top = S.sel - ROWS + 1;
            redraw();
        } else if ((k == K_A || k == K_RIGHT) && type == EV_PRESS && L.count) {
            S.state = ST_LOAD;                             /* paint "Loading", then resolve next service call */
            redraw();
        }
    }
    return 0;
}

static void paint(void)
{
    if (!GetMsg(MSG_DRAW) && !S.dirty) return;
    S.dirty = 0;
    uint16_t old = COLOR;
    uint32_t font = SetFont(2);
    static uint16_t b[32];
    DrawImage(IMG_BG, 0, 0);
    a16(b, "Playlists");
    text(12, 39, b, C_INK);
    const char *msg = 0;
    switch (S.state) {
    case ST_NOCARD:   msg = "No SD card"; break;
    case ST_NODIR:    msg = "No _Playlists folder"; break;
    case ST_EMPTY:    msg = "No playlists"; break;
    }
    if (msg) { a16(b, msg); text(40, ROW_Y + 3, b, C_DIM); }
    else {
        for (int r = 0; r < ROWS && S.top + r < L.count; r++) {
            int i = S.top + r, y = ROW_Y + r * ROW_H, on = i == S.sel;
            if (on) DrawImage(IMG_SEL, 0, y);
            DrawImage(IMG_ICON + (on ? 10 : 0), 8, y - 1);
            const pl_file_t *f = &L.files[i];
            int n = f->nlen < 31 ? f->nlen : 31;
            for (int c = 0; c < n; c++) b[c] = f->name[c];
            b[n] = 0;
            text(40, y + 3, b, C_INK);
        }
        if (S.state == ST_LOAD) { a16(b, "Loading"); text(210, 39, b, C_DIM); }
        if (S.state == ST_NOTRACKS) { a16(b, "No tracks found"); text(150, 39, b, C_DIM); }
    }
    SetFont(font);
    COLOR = old;
}

/* window descriptor: must be the first bytes of the data section (0x03023B48) */
__attribute__((section(".desc"), used))
const win_desc_t pl_desc = { 0, 0, tick, service, paint, init, deinit };
