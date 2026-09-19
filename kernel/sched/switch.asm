; SPDX-License-Identifier: GPL-2.0-only
; Copyright (c) 2026 farfromoffice

; Saves the callee saved registers of the running thread, parks its stack
; pointer where the scheduler can find it, and continues on the next thread's
; stack. Everything else is already where the C++ calling convention left it.

section .text
bits 64

global context_switch
global thread_entry_trampoline
extern thread_entry_start

; void context_switch(u64* save_here, u64 resume_from)
context_switch:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15

    mov [rdi], rsp
    mov rsp, rsi

    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

; A thread that has never run resumes here, with its entry point and argument
; waiting in the registers the switch restored.
thread_entry_trampoline:
    mov rdi, r12
    mov rsi, r13
    jmp thread_entry_start

section .note.GNU-stack progbits noalloc noexec nowrite align=1
