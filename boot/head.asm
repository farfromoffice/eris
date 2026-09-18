; SPDX-License-Identifier: GPL-2.0-only
; Copyright (c) 2026 farfromoffice

default abs

MB_MAGIC    equ 0x1BADB002
MB_FLAGS    equ 0x00000003
MB_CHECKSUM equ -(MB_MAGIC + MB_FLAGS)

section .multiboot
align 8
    dd MB_MAGIC
    dd MB_FLAGS
    dd MB_CHECKSUM

section .bss
align 4096
pml4:       resb 4096
pdpt:       resb 4096
pd:         resb 4096
align 16
stack_bottom:
    resb 64 * 1024
stack_top:

section .rodata
align 16
gdt64:
    dq 0
.code: equ $ - gdt64
    dq (1 << 43) | (1 << 44) | (1 << 47) | (1 << 53)
.data: equ $ - gdt64
    dq (1 << 44) | (1 << 47) | (1 << 41)
.pointer:
    dw $ - gdt64 - 1
    dq gdt64

section .data
mb_magic_saved: dd 0
mb_info_saved:  dd 0

section .text
bits 32
global _start
extern kernel_main
extern call_global_ctors
extern __bss_start
extern __bss_end

_start:
    cli
    ; Multiboot leaves EFLAGS.DF undefined; rep stosb and the C++ ABI need it clear.
    cld
    mov [mb_magic_saved], eax
    mov [mb_info_saved], ebx

    mov edi, __bss_start
    mov ecx, __bss_end
    sub ecx, edi
    xor eax, eax
    rep stosb

    mov esp, stack_top

    call check_long_mode
    call setup_paging

    mov eax, cr4
    or eax, 1 << 5
    mov cr4, eax

    mov eax, pml4
    mov cr3, eax

    mov ecx, 0xC0000080
    rdmsr
    or eax, 1 << 8
    wrmsr

    mov eax, cr0
    or eax, (1 << 31) | (1 << 0)
    mov cr0, eax

    lgdt [gdt64.pointer]
    jmp gdt64.code:long_start

check_long_mode:
    mov eax, 0x80000000
    cpuid
    cmp eax, 0x80000001
    jb .no_long_mode
    mov eax, 0x80000001
    cpuid
    test edx, 1 << 29
    jz .no_long_mode
    ret
.no_long_mode:
    hlt
    jmp .no_long_mode

setup_paging:
    mov eax, pdpt
    or eax, 0x03
    mov [pml4], eax

    mov eax, pd
    or eax, 0x03
    mov [pdpt], eax

    xor ecx, ecx
.map_pd:
    mov eax, 0x200000
    mul ecx
    or eax, 0x83
    mov [pd + ecx * 8], eax
    mov dword [pd + ecx * 8 + 4], 0
    inc ecx
    cmp ecx, 512
    jne .map_pd
    ret

bits 64
long_start:
    mov ax, gdt64.data
    mov ds, ax
    mov es, ax
    mov ss, ax
    mov fs, ax
    mov gs, ax
    mov rsp, stack_top

    call call_global_ctors

    xor rdi, rdi
    xor rsi, rsi
    mov edi, [mb_magic_saved]
    mov esi, [mb_info_saved]
    call kernel_main
.halt:
    cli
    hlt
    jmp .halt

section .note.GNU-stack progbits noalloc noexec nowrite align=1
