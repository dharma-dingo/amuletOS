/* Overlay window module replacing stock module 82 (docs/HANDOFF.md 6.2).
 * Code runs in the overlay window; keep it below the music module (0x0308BF84).
 * Data + ZI must stay inside module 82's own RAM footprint (0x03023B48 .. 0x03024250). */
MEMORY
{
    CODE (rx)  : ORIGIN = 0x030815C4, LENGTH = 0x0308BF84 - 0x030815C4
    DATA (rw)  : ORIGIN = 0x03023B48, LENGTH = 0x708
}

SECTIONS
{
    .text : ALIGN(4)
    {
        *(.text .text.*)
        *(.rodata .rodata.*)
        . = ALIGN(4);
    } > CODE

    .data : ALIGN(4)
    {
        KEEP(*(.desc))
        *(.data .data.*)
        . = ALIGN(4);
    } > DATA

    .bss (NOLOAD) : ALIGN(4)
    {
        *(.bss .bss.*)
        *(COMMON)
        . = ALIGN(4);
    } > DATA

    /DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.ARM.attributes) }
}
