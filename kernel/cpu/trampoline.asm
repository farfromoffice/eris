; SPDX-License-Identifier: GPL-2.0-only
; Copyright (c) 2026 farfromoffice

; Copied to a fixed page below one megabyte and started with a startup IPI, so
; every address in here is absolute and the page has to live exactly there.
; The boot CPU fills in the three values at the end of the page before it asks
; for a core to come up.

TRAMPOLINE_BASE equ 0x8000
DATA_OFFSET     equ 0xF00

CR3_VALUE   equ TRAMPOLINE_BASE + DATA_OFFSET + 0x00
STACK_VALUE equ TRAMPOLINE_BASE + DATA_OFFSET + 0x08
ENTRY_VALUE equ TRAMPOLINE_BASE + DATA_OFFSET + 0x10
READY_FLAG  equ TRAMPOLINE_BASE + DATA_OFFSET + 0x18
INDEX_VALUE equ TRAMPOLINE_BASE + DATA_OFFSET + 0x20
APICID_VALUE equ TRAMPOLINE_BASE + DATA_OFFSET + 0x28

[bits 16]
org TRAMPOLINE_BASE

ap_start:
    cli
    cld
    xor ax, ax
    mov ds, ax
    mov es, ax
    mov ss, ax

    lgdt [gdt32_pointer]

    mov eax, cr0
    or eax, 1
    mov cr0, eax
    jmp 0x08:protected_mode

[bits 32]
protected_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, [CR3_VALUE]
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, (1 << 8) | (1 << 11)
    wrmsr

    mov eax, cr0
    or eax, (1 << 31) | (1 << 16) | 1
    mov cr0, eax

    lgdt [gdt64_pointer]
    jmp 0x08:long_mode

[bits 64]
long_mode:
    mov ax, 0x10
    mov ds, ax
    mov es, ax
    mov ss, ax

    mov rsp, [STACK_VALUE]
    mov rax, [ENTRY_VALUE]

    mov qword [READY_FLAG], 1
    call rax

.halt:
    cli
    hlt
    jmp .halt

align 8
gdt32:
    dq 0
    dq 0x00CF9A000000FFFF
    dq 0x00CF92000000FFFF
gdt32_pointer:
    dw $ - gdt32 - 1
    dd gdt32

align 8
gdt64:
    dq 0
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)
    dq (1 << 44) | (1 << 47) | (1 << 41)
gdt64_pointer:
    dw $ - gdt64 - 1
    dq gdt64

times DATA_OFFSET - ($ - $$) db 0
trampoline_data:
    dq 0        ; cr3
    dq 0        ; stack top
    dq 0        ; entry point
    dq 0        ; ready flag
    dq 0        ; cpu index
    dq 0        ; apic id
    dq 0        ; identity consumed

times 4096 - ($ - $$) db 0
