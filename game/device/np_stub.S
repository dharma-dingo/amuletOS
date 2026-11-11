@ Slot-2 assembly helpers (main menu carousel, Music menu table), linked with np_helpers.c and libscan.c.
@ (The v0.2 np_title_draw stub was removed in build 33 with the other v0.2 Now Playing helpers.)
    .syntax unified
    .thumb

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
    push    {r0, lr}
    ldr     r1, =MENU_RAM
    ldr     r1, [r1]
    lsrs    r2, r1, #8
    ldr     r3, =(MENU_TAG >> 8)
    cmp     r2, r3
    ite     eq
    andeq   r1, r1, #3
    movne   r1, #1                  @ fresh boot: Music (owner, build 33)
    strh    r1, [r0]
    bl      menu_art                @ grey Now Playing unless the player runs
    pop     {r0, pc}
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
    .hword 0xEA, 0x59, 7      @ Favorites (heart, build 34; stock used the folder)
    .hword 0xEC, 0x56, 0x50   @ Playlists (text 86; action 0x50 -> pl_menu_exit, build 35)
    .hword 0xEB, 0x5A, 8      @ Update (refresh arrows, build 34)

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

@ ---- v0.3 M3U playlists (build 35; tools/patch_code.py playlists) ----
@ 0x0305E930 entry (one step of the music source record, r0 = record, r1 = direction), was
@ "push.w {r3-r11, lr}": pl_step (np_helpers.c) handles playlist songs (return 0 = success, like stock's step).
    .section .text.pl_step_hook
    .global pl_step_hook
    .thumb_func
pl_step_hook:
    push    {r0-r3, r12, lr}
    bl      pl_step
    cmp     r0, #0
    pop     {r0-r3, r12, lr}
    bne     1f
    push.w  {r3, r4, r5, r6, r7, r8, r9, r10, r11, lr}
    ldr     r12, =0x0305E935        @ stock's step after its push
    bx      r12
1:  movs    r0, #0
    bx      lr
    .ltorg

@ Music menu select routine's exit (module 22 0x0308197E, was "add sp, #0x1c; mov r0, r5"): action 0x50 (the
@ Playlists row) opens the Playlists screen, task 19 (module 86, game/device/playlists.c).
    .section .text.pl_menu_exit
    .global pl_menu_exit
    .thumb_func
pl_menu_exit:
    cmp     r4, #0x50
    bne     1f
    movs    r1, #0
    movs    r0, #19
    ldr     r2, =0x030680D5         @ TaskSwitch(19, NULL)
    blx     r2
    movs    r5, #1
1:  add     sp, #0x1c
    mov     r0, r5
    ldr     r2, =0x03081983         @ pop.w {r4-r11, pc}
    bx      r2
    .ltorg

@ Player thread init (module 33, r7 = music source record). For source 0x63 stock counts the current Folders folder
@ (0x0308DD48 -> record total) and seeks to the requested index (0x0305E85C -> record cluster / file number), which
@ for a playlist (or Blockz) uses a stale browser state: hang or wrong file (build 35). While pl_active, skip both.
@ 0x0308DF12, was "ldrh.w r0, [r7, #0xf2]; cmp r0, #0x63; beq 0x0308DF26":
    .equ PL_COUNT_SKIP,  0x0308DF40
    .equ PL_COUNT_STOCK, 0x0308DF26
    .equ PL_CMP6,        0x0308DF1A
    .equ PL_SEEK_SKIP,   0x0308E014
    .equ PL_SEEK_STOCK,  0x0308DFFA
    .equ PL_LIBRARY,     0x0308DFC4
    .section .text.pl_init_count
    .global pl_init_count
    .thumb_func
pl_init_count:
    bl      pl_active
    cbnz    r0, 1f
    ldrh.w  r0, [r7, #0xf2]
    cmp     r0, #0x63
    beq.w   PL_COUNT_STOCK          @ stock: count
    b.w     PL_CMP6                 @ stock: "cmp r0, #6" with r0 = source
1:  b.w     PL_COUNT_SKIP           @ playlist: past the folder count

@ 0x0308DFB8, was "ldrh.w r0, [r7, #0xf2]; cmp r0, #0x63; it ne; cmpne r0, #6; beq 0x0308DFFA":
    .section .text.pl_init_seek
    .global pl_init_seek
    .thumb_func
pl_init_seek:
    bl      pl_active
    cbnz    r0, 1f
    ldrh.w  r0, [r7, #0xf2]
    cmp     r0, #0x63
    it      ne
    cmpne   r0, #6
    beq.w   PL_SEEK_STOCK           @ stock: seek
    b.w     PL_LIBRARY              @ stock: library sources
1:  b.w     PL_SEEK_SKIP            @ playlist: past the seek
