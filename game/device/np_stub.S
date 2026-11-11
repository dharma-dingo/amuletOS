@ Now Playing mode-1 scrolling-line draw (module 27 0x03083466): same as the original sequence but
@ with the title (file name fallback) instead of the long file name. Uses the caller's sp, r4, r8,
@ so it stays in assembly. Lives in the slot-2 helper blob with np_helpers.c.
    .syntax unified
    .thumb
    .equ TITLE, 0x030485E0
    .equ DRAW,  0x03063F76

    .section .text.np_title_draw
    .global np_title_draw
    .thumb_func
np_title_draw:
    push    {r4, lr}
    bl      np_title_fill
    pop     {r4, lr}
    add     r1, sp, #8              @ caller's rect (sp is the caller's again after the pop)
    strh.w  r4, [r8]                @ caller's colour store
    movs    r3, #2
    ldr     r2, =TITLE
    mov     r0, r1
    ldr     ip, =DRAW + 1
    bx      ip                      @ tail call: returns to the caller
    .ltorg

@ ---- v0.3 one-level carousel (main menu, module 21; tools/patch_code.py menu_carousel) ----
@ The carousel position is kept here across screens (0x0309FFE4, above slot 2): stock passes its own tile
@ numbers back to the menu, which no longer match the new order.
    .equ MENU_IDX, 0x03023B94
    .equ MENU_RAM, 0x0309FFE4
    .equ MENU_TAG, 0xCA5100

@ init: replaces "ldrb r1, [r4]; strh r1, [r0]" (r0 = &index); keeps r0
    .section .text.menu_idx_init
    .global menu_idx_init
    .thumb_func
menu_idx_init:
    ldr     r1, =MENU_RAM
    ldr     r1, [r1]
    lsrs    r2, r1, #8
    ldr     r3, =(MENU_TAG >> 8)
    cmp     r2, r3
    ite     eq
    andeq   r1, r1, #3
    movne   r1, #0
    strh    r1, [r0]
    bx      lr
    .ltorg

@ Left / Right: r0 = 3 (back) or 1 (on); four items, wrapping
    .section .text.menu_step
    .global menu_step
    .thumb_func
menu_step:
    ldr     r2, =MENU_IDX
    ldrh    r1, [r2]
    add     r1, r0
    and     r1, r1, #3
    strh    r1, [r2]
    ldr     r2, =MENU_RAM
    ldr     r3, =MENU_TAG
    orr     r1, r1, r3
    str     r1, [r2]
    bx      lr
    .ltorg

@ ---- Music menu (module 22) with an eighth item "Update" (text 90) -> stock's index-8 library update ----
@ {icon image, text id, action index}; stock's table (data 0x03023B48) has no room before the window descriptor.
    .section .text.music_menu
    .global music_menu
    .balign 4
music_menu:
    .hword 0xE3, 0x51, 0      @ Now playing
    .hword 0xE4, 0x52, 1      @ All Music
    .hword 0xE5, 0x53, 2      @ Artist
    .hword 0xE6, 0x54, 3      @ Album
    .hword 0xE7, 0x55, 4      @ Genre
    .hword 0xE8, 0x57, 6      @ Directory List
    .hword 0xE8, 0x59, 7      @ Favorites
    .hword 0xE8, 0x5A, 8      @ Update

@ main menu key dispatch fallthrough (module 21 0x03081B8A, was "b.w exit"): r1 = event - 0x40000010.
@ Start press (0x40000100, r1 = 0xF0) -> the Settings code at 0x03081BA6 (menu_carousel); anything else -> exit.
    .section .text.menu_key_more
    .global menu_key_more
    .thumb_func
menu_key_more:
    cmp     r1, #0xF0
    ite     eq
    ldreq   r0, =0x03081BA7
    ldrne   r0, =0x03081D75
    bx      r0
    .ltorg
