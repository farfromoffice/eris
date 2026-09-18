/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 farfromoffice */

ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

SECTIONS
{
    . = 1M;

    .boot ALIGN(4K) : { KEEP(*(.multiboot)) }

    .text ALIGN(4K) : { *(.text .text.*) }

    .rodata ALIGN(4K) :
    {
        *(.rodata .rodata.*)

        . = ALIGN(8);
        __eris_modules_start = .;
        KEEP(*(.eris_modules))
        __eris_modules_end = .;

        . = ALIGN(8);
        __init_array_start = .;
        KEEP(*(.init_array .init_array.*))
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        __init_array_end = .;
    }

    .data ALIGN(4K) :
    {
        *(.data .data.*)

        . = ALIGN(8);
        __eris_symtab_start = .;
        KEEP(*(.eris_symtab))
        __eris_symtab_end = .;
    }

    .bss ALIGN(4K) :
    {
        __bss_start = .;
        *(COMMON)
        *(.bss .bss.*)
        __bss_end = .;
    }

    __kernel_end = .;

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note.*) }
}
