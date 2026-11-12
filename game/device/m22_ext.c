/* Music menu (module 22) extension, linked after the module's own code by tools/extend_module.py (build 37).
 * Stack only, no data / rodata.
 *
 * Row icons: stock's row paint (0x03081EB0) never draws the table's icon ids, only the selection bar (image 184)
 * and the row text at x = 15 (0x030644A0). Both of its text calls (0x03081F54 full redraw, 0x03082034 selection
 * change) come here instead: draw the row's icon (art/pocket.py LIST_GLYPHS; +10 = the set on the selection bar),
 * then the text 26 px further right. */
#include <stdint.h>

#define TextBox(x, y, w, h, z, id) ((void (*)(int, int, int, int, int, int))(0x030644A0u | 1))(x, y, w, h, z, id)
#define DrawImage(i, x, y)         ((void (*)(uint32_t, int, int))(0x03064D20u | 1))(i, x, y)
#define SEL_ROW  (*(volatile uint8_t *)0x03023B92u)       /* m22 ZI: selected visible row */
#define TEXT_DY  0x42                                     /* stock: text y = 0x42 + 23 * row; row top = 0x3D + 23 * row */
#define SHIFT    26

extern const uint16_t music_menu[];                       /* m22_ext.S: {icon, text id, action} x 8 */

void m22_row_text(int x, int y, int w, int h, int z, int id)
{
    uint32_t icon = 0;
    for (int i = 0; i < 8; i++)
        if (music_menu[3 * i + 1] == (uint16_t)id) { icon = music_menu[3 * i]; break; }
    int row = (y - TEXT_DY) / 23;
    if (icon) DrawImage(icon + (row == SEL_ROW ? 10 : 0), 8, y - (TEXT_DY - 0x3D) - 1);
    TextBox(x + SHIFT, y, w - SHIFT, h, z, id);
}
