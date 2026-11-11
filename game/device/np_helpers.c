/* Resident helpers linked after the D1 hook in module slot 2 (RAM 0x0309F800+, loaded by stock at boot and never
 * overwritten): Now Playing loader and main menu. No data or ZI: stack only. The v0.2 Now Playing helpers
 * (np_v2 / np_v3: artist / album wrap, title fallback) were removed in build 33 for room; they are in git
 * history up to e86d06a. */
#include <stdint.h>

/* v0.3 Now Playing: module 27's init ends with "ModuleOverlay(18, 7)" (cover decoder); patched to come here
 * so the layout A drawing code (game/device/npui.c, module slot 28 at 0x03083F0C) is loaded first. */
uint32_t np_load(void)
{
    ((uint32_t (*)(uint32_t, uint32_t))(0x030684E4u | 1))(28, 7);
    return ((uint32_t (*)(uint32_t, uint32_t))(0x030684E4u | 1))(18, 7);
}

/* v0.3 main menu (module 21): the Now Playing card is greyed out and inert while the player thread is not
 * running (fresh boot: stock's Now Playing would open on an empty record and show an error). Same running test
 * as stock's Music menu "Now playing" item. Module 21's tile table {1, i, i, 0, image, text} x6 sits in RAM at
 * 0x03023B64 and paint reads the image from it, so we just swap the image ids: greyed variants 8 / 20 / 24
 * (art/pocket.py GREY_STATES) for cards 16 (Now Playing), 14 (Music) and 22 (Library), whose side icons
 * show Now Playing. */
#define PLAYER_ON() (((uint32_t (*)(uint32_t, uint32_t))(0x03057C7Cu | 1))(*(volatile uint32_t *)0x030232BCu, \
                                                                          0x030361A4u) == 1)
#define MENU_IMG(k) (*(volatile uint16_t *)(0x03023B64u + 8u * (k) + 4u))

void menu_art(void)
{
    uint32_t on = PLAYER_ON();
    MENU_IMG(0) = on ? 16 : 8;
    MENU_IMG(1) = on ? 14 : 20;
    MENU_IMG(3) = on ? 22 : 24;
}

/* Now Playing tile action (patch_code.py menu_carousel): open it while the player runs, else refresh the art
 * (the player may have stopped while the menu was up) and stay. */
void menu_np(void)
{
    if (PLAYER_ON()) {
        ((void (*)(uint32_t, void *))(0x030680D4u | 1))(3, 0);
        return;
    }
    menu_art();
    ((void (*)(uint32_t))(0x03067ED4u | 1))(0x2D);   /* repaint the card */
}
