; SPDX-License-Identifier: GPL-2.0-only
; Copyright (c) 2026 farfromoffice

; What a program starts on. No arguments yet, so this only has to reach main and
; turn its return value into an exit.

section .text
bits 64

global _start
extern main

_start:
    xor rbp, rbp
    call main

    mov rdi, rax
    xor rax, rax                ; the exit call
    syscall

.hang:
    jmp .hang

section .note.GNU-stack progbits noalloc noexec nowrite align=1
