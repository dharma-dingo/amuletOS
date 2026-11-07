/* Resident helpers for stock's Now Playing (module 27), linked after the D1 hook in module slot 2
 * (RAM 0x0309F800+, loaded by stock at boot and never overwritten). Called from patched spots in
 * module 27 (tools/patch_code.py np_v2). No data or ZI: stack only. docs/HARDWARE.md, Stock Now Playing. */
#include <stdint.h>

typedef struct { uint16_t x0, y0, x1, y1; } rect_t;

#define TEXTW(s)        ((uint32_t (*)(const uint16_t *))(0x03063D52u | 1))(s)
#define DRAW(a, b, s, m) ((void (*)(rect_t *, rect_t *, const uint16_t *, uint32_t))(0x03063F76u | 1))(a, b, s, m)
#define TITLE   ((uint16_t *)0x030485E0)          /* tag record +0x60: title from the file's tags */
#define FNAME   ((const uint16_t *)0x03048450)    /* library record +0x400: file name */
#define CLEAR(img, x, y) ((void (*)(uint32_t, uint32_t, uint32_t))(0x030648ECu | 1))(img, x, y)
#define STRIP   69                                /* stock's 128x16 line-clear image (panel fill in our art) */
#define LINE_GAP 21                               /* info box line pitch (y 91 / 112 / 133) */
#define LIFT    42                                /* owner: artist from line 1 up two lines (y 49), album
                                                     from line 3 up two (y 91), each wrapping over 2 lines */
#define MAXC    64                                /* stock copies 0x40 chars of the artist */

/* title empty -> use the file name */
void np_title_fill(void)
{
    if (TITLE[0]) return;
    for (int i = 0; i < 63; i++) {
        uint16_t c = FNAME[i];
        TITLE[i] = c;
        if (!c) return;
    }
    TITLE[63] = 0;
}

/* replaces "ldr r0, =FNAME; bl TEXTW" before the scrolling line is drawn */
uint32_t np_title_width(void)
{
    np_title_fill();
    return TEXTW(TITLE);
}

/* draw s in rect r over up to two lines: wraps at a space, or mid-word if one word is too long */
static void wrap2(rect_t *r, const uint16_t *s, uint32_t mode)
{
    CLEAR(STRIP, r->x0, r->y0);                         /* stock only clears y 91/112/133 */
    CLEAR(STRIP, r->x0, r->y0 + LINE_GAP);
    rect_t *r2 = r;
    int w = r->x1 - r->x0;
    if ((int)TEXTW(s) <= w) { DRAW(r, r2, s, mode); return; }

    uint16_t buf[MAXC + 2];
    int cut = -1, next = -1, i;
    for (i = 0; i < MAXC && s[i]; i++) {               /* last space whose prefix fits */
        if (s[i] == ' ' && i > 0) {
            buf[i] = 0;
            if ((int)TEXTW(buf) > w) break;
            cut = i; next = i + 1;
        }
        buf[i] = s[i];
    }
    if (cut < 0) {                                      /* no space fits: split inside the word */
        for (i = 0; i < MAXC && s[i]; i++) {
            buf[i] = s[i]; buf[i + 1] = 0;
            if ((int)TEXTW(buf) > w) { cut = next = i; break; }
        }
        if (cut <= 0) { DRAW(r, r2, s, mode); return; }
    }
    for (i = 0; i < cut; i++) buf[i] = s[i];
    buf[cut] = 0;
    DRAW(r, r2, buf, mode);

    rect_t l2 = *r;
    l2.y0 += LINE_GAP; l2.y1 += LINE_GAP;
    DRAW(&l2, &l2, s + next, mode);
}

static void lifted(rect_t *r, const uint16_t *s, uint32_t mode)
{
    rect_t t = *r;
    t.y0 -= LIFT; t.y1 -= LIFT;
    wrap2(&t, s, mode);
}

/* replace the artist draw (line 1 rect) and the album draw (line 3 rect, source switched to album) */
void np_artist(rect_t *r, rect_t *r2, const uint16_t *s, uint32_t mode) { lifted(r, s, mode); }
void np_album(rect_t *r, rect_t *r2, const uint16_t *s, uint32_t mode)  { lifted(r, s, mode); }

/* replaces the empty-artist fallback: clear the lifted artist lines (y 49 / 70), which stock never clears */
void np_clear_artist(void)
{
    CLEAR(STRIP, 0xA8, 91 - LIFT);
    CLEAR(STRIP, 0xA8, 91 - LIFT + LINE_GAP);
}
