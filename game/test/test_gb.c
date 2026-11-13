/* Host test for the Game Boy probe core (game/device/gb_core.c): runs a ROM headless with a limited page cache,
 * presses Start now and then, and writes the last frame as a PPM. Built 32-bit by game/build.py test (skipped when
 * no ROM is given).
 *   test_gb.exe ROM FRAMES SLOTS OUT.ppm  -> prints page loads per second of game time and the frame checksum */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../device/gb_core.c"

static uint8_t *ROM; static long ROMLEN;
static uint16_t FB[LCD_HEIGHT][LCD_WIDTH];
static uint32_t loads;

void gbx_read_page(gbx_t *x, uint32_t page, uint8_t *dst)
{
    long o = (long)page << PG_SHIFT;
    for (uint32_t i = 0; i < PG_SIZE; i++) dst[i] = o + (long)i < ROMLEN ? ROM[o + i] : 0xFF;
    loads++;
}

void gbx_line(const uint16_t *rgb, int line) { memcpy(FB[line], rgb, sizeof FB[0]); }

static gbx_t X;

int main(int argc, char **argv)
{
    if (argc < 5) { fprintf(stderr, "usage: test_gb ROM FRAMES SLOTS OUT.ppm\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 2; }
    fseek(f, 0, SEEK_END); ROMLEN = ftell(f); fseek(f, 0, SEEK_SET);
    ROM = malloc(ROMLEN); fread(ROM, 1, ROMLEN, f); fclose(f);
    int frames = atoi(argv[2]), slots = atoi(argv[3]);
    static uint8_t pagebuf[PG_SLOTS][PG_SIZE];
    uint8_t *pages[PG_SLOTS];
    for (int i = 0; i < PG_SLOTS; i++) pages[i] = pagebuf[i];
    int r = gbx_init(&X, (uint32_t)ROMLEN, pages, (uint32_t)slots);
    if (r) { printf("FAIL init error %d\n", r - 1); return 1; }
    static uint8_t cram[0x8000];
    X.cram_len = gbx_save_size(&X); if (X.cram_len > sizeof cram) X.cram_len = sizeof cram;
    X.cram = cram; memset(cram, 0xFF, sizeof cram);
    char title[17]; gb_get_rom_name(&X.gb, title);
    uint32_t peak = 0, last = 0;
    for (int n = 0; n < frames; n++) {
        /* tap Start for 6 frames every 4 s (title screens), A for 6 frames every 3 s */
        X.gb.direct.joypad = 0xFF;
        if (n % 240 >= 200 && n % 240 < 206) X.gb.direct.joypad &= ~JOYPAD_START;
        if (n % 180 >= 90 && n % 180 < 96) X.gb.direct.joypad &= ~JOYPAD_A;
        gb_run_frame(&X.gb);
        if (X.err) { printf("FAIL Peanut error %d at %04x, frame %d\n", (int)X.err - 1, (unsigned)X.err_addr, n); return 1; }
        if (n % 60 == 59) { if (loads - last > peak) peak = loads - last; last = loads; }
    }
    uint32_t sum = 0;
    for (int y = 0; y < LCD_HEIGHT; y++) for (int x = 0; x < LCD_WIDTH; x++) sum = sum * 31 + FB[y][x];
    printf("%s: %ld KB, cart RAM %u B, %d frames, %d slots: %u page loads (%.2f / s of game time, worst second %u), "
           "frame sum %08x\n", title, ROMLEN / 1024, (unsigned)X.cram_len, frames, slots, (unsigned)loads,
           loads * 59.73 / frames, (unsigned)peak, (unsigned)sum);
    FILE *o = fopen(argv[4], "wb");
    fprintf(o, "P6 %d %d 255\n", LCD_WIDTH, LCD_HEIGHT);
    for (int y = 0; y < LCD_HEIGHT; y++) for (int x = 0; x < LCD_WIDTH; x++) {
        uint16_t c = FB[y][x];
        uint8_t rgb[3] = { (uint8_t)((c >> 11) << 3), (uint8_t)(((c >> 5) & 63) << 2), (uint8_t)((c & 31) << 3) };
        fwrite(rgb, 1, 3, o);
    }
    fclose(o);
    return 0;
}
