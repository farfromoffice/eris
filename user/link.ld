/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 farfromoffice */

ENTRY(_start)
OUTPUT_FORMAT(elf64-x86-64)

/* Well above anything the kernel maps, so a program never collides with it. */
SECTIONS
{
    . = 0x8000000000;

    .text ALIGN(4K) : { *(.text .text.*) }
    .rodata ALIGN(4K) : { *(.rodata .rodata.*) }
    .data ALIGN(4K) : { *(.data .data.*) }
    .bss ALIGN(4K) : { *(.bss .bss.*) *(COMMON) }

    /DISCARD/ : { *(.eh_frame) *(.comment) *(.note.*) }
}
