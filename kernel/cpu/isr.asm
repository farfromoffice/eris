; SPDX-License-Identifier: GPL-2.0-only
; Copyright (c) 2026 farfromoffice

extern isr_dispatch

%macro ISR_NOERR 1
global isr_stub_%1
isr_stub_%1:
    push qword 0
    push qword %1
    jmp isr_common
%endmacro

%macro ISR_ERR 1
global isr_stub_%1
isr_stub_%1:
    push qword %1
    jmp isr_common
%endmacro

section .text
bits 64

isr_common:
    push rax
    push rbx
    push rcx
    push rdx
    push rsi
    push rdi
    push rbp
    push r8
    push r9
    push r10
    push r11
    push r12
    push r13
    push r14
    push r15

    mov rdi, rsp
    call isr_dispatch

    pop r15
    pop r14
    pop r13
    pop r12
    pop r11
    pop r10
    pop r9
    pop r8
    pop rbp
    pop rdi
    pop rsi
    pop rdx
    pop rcx
    pop rbx
    pop rax

    add rsp, 16
    iretq

%assign vec 0
%rep 32
    %if vec == 8 || vec == 10 || vec == 11 || vec == 12 || vec == 13 || vec == 14 || vec == 17 || vec == 21
        ISR_ERR vec
    %else
        ISR_NOERR vec
    %endif
    %assign vec vec + 1
%endrep

%assign vec 32
%rep 224
    ISR_NOERR vec
    %assign vec vec + 1
%endrep

section .rodata
global isr_stub_table
isr_stub_table:
%assign vec 0
%rep 256
    dq isr_stub_ %+ vec
    %assign vec vec + 1
%endrep
