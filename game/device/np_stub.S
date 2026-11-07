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
