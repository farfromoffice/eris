/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 farfromoffice */

ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

SECTIONS
{
    . = 1M;
    __kernel_start = .;

    .boot ALIGN(4K) : { KEEP(*(.multiboot)) }

    .text ALIGN(4K) :
    {
        __text_start = .;
        *(.text .text.*)
        . = ALIGN(4K);
        __text_end = .;
    }

    .rodata ALIGN(4K) :
    {
        __rodata_start = .;
        *(.rodata .rodata.*)

        . = ALIGN(8);
        __eris_modules_start = .;
        KEEP(*(.eris_modules))
        __eris_modules_end = .;

        . = ALIGN(8);
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;

        . = ALIGN(4K);
        __rodata_end = .;
    }

    .data ALIGN(4K) :
    {
        __data_start = .;
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
        . = ALIGN(4K);
        __bss_end = .;
    }

    __data_end = .;
    __kernel_end = .;

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note.*) }
}
