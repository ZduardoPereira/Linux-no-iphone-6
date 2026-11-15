ENTRY(_start)
SECTIONS
{
    . = 0;
    .text : {
        *(.text.start)
        *(.text*)
        *(.rodata*)
        *(.data*)
        *(.bss*)
        *(COMMON)
    }
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
    ASSERT(SIZEOF(.text) < 0x3000, "stub grande demais (precisa sobrar espaco para a pilha)")
}
