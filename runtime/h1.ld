ENTRY(h1_bda_main)
SECTIONS
{
    . = 0x83c00020;
    .text : { KEEP(*(.text.h1_bda_entry)) *(.text*) }
    .rodata : { *(.rodata*) }
    .eh_frame : { __eh_frame_start = .; KEEP(*(.eh_frame*)) LONG(0) }
    .gcc_except_table : { *(.gcc_except_table*) }
    .preinit_array : {
        __preinit_array_start = .;
        KEEP(*(.preinit_array*))
        __preinit_array_end = .;
    }
    .init_array : {
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*))) KEEP(*(.init_array))
        __init_array_end = .;
    }
    .fini_array : {
        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*))) KEEP(*(.fini_array))
        __fini_array_end = .;
    }
    .ctors : {
        __ctors_start = .;
        KEEP(*(.ctors)) KEEP(*(SORT(.ctors.*)))
        __ctors_end = .;
    }
    .dtors : {
        __dtors_start = .;
        KEEP(*(.dtors)) KEEP(*(SORT(.dtors.*)))
        __dtors_end = .;
    }
    .data : { *(.data*) }
    . = ALIGN(16);
    .sdata : { _gp = . + 0x7ff0; *(.sdata*) *(.srodata*) *(.got*) }
    __payload_end = .;
    . = ALIGN(16);
    .bss (NOLOAD) : { __bss_start = .; *(.sbss*) *(.scommon*) *(.bss*) *(COMMON) __bss_end = .; }
    . = ALIGN(16);
    .h1_noinit (NOLOAD) : { *(.h1_noinit*) }
    __application_end = .;
    ASSERT(__application_end <= 0x83ff0000,"PvZ Portable overlaps firmware caller stack; use a separate loader layout")
    /DISCARD/ : { *(.comment*) *(.note*) *(.MIPS.abiflags*) *(.reginfo*) }
}
