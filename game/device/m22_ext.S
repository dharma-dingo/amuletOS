@ Music menu (module 22) extension: the menu table and the Playlists row exit (tools/extend_module.py,
@ build 37; were in slot 2). Row icons: m22_ext.c.
    .syntax unified
    .thumb

@ ---- Music menu (module 22) with an eighth item "Update" (text 90) -> stock's index-8 library update ----
@ {icon image, text id, action index}; stock's table (data 0x03023B48) has no room before the window descriptor.
    .text
    .global music_menu
    .balign 4
music_menu:
    .hword 0xE3, 0x51, 0      @ Now playing
    .hword 0xE4, 0x52, 1      @ All Music
    .hword 0xE5, 0x53, 2      @ Artist
    .hword 0xE6, 0x54, 3      @ Album
    .hword 0xE7, 0x55, 4      @ Genre
                              @ (Directory List, action 6, removed in build 37: the Library card is the folder browser)
    .hword 0xEA, 0x59, 7      @ Favorites (heart, build 34; stock used the folder)
    .hword 0xEC, 0x56, 0x50   @ Playlists (text 86; action 0x50 -> pl_menu_exit, build 35)
    .hword 0xEB, 0x5A, 8      @ Update (refresh arrows, build 34)
    .hword 0xE8, 0x09, 0x51   @ Games (text 9; action 0x51 -> Game Boy probe, task 20; build 41, temporary)

@ Music menu select routine's exit (module 22 0x0308197E, was "add sp, #0x1c; mov r0, r5"): action 0x50 (the
@ Playlists row) opens the Playlists screen, task 19 (module 86, game/device/playlists.c); action 0x51 (Games)
@ opens the Game Boy probe, task 20 (game/device/gbprobe.c).
    .section .text.pl_menu_exit
    .global pl_menu_exit
    .thumb_func
pl_menu_exit:
    movs    r0, #19
    cmp     r4, #0x50
    beq     2f
    movs    r0, #20
    cmp     r4, #0x51
    bne     1f
2:  movs    r1, #0
    ldr     r2, =0x030680D5         @ TaskSwitch(19 or 20, NULL)
    blx     r2
    movs    r5, #1
1:  add     sp, #0x1c
    mov     r0, r5
    ldr     r2, =0x03081983         @ pop.w {r4-r11, pc}
    bx      r2
    .ltorg

