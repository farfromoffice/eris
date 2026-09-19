; SPDX-License-Identifier: GPL-2.0-only
; Copyright (c) 2026 farfromoffice

; Where ring 3 lands. syscall leaves the return address in rcx and the flags in
; r11, and neither the stack nor gs belongs to the kernel yet, so both are
; swapped before anything else happens.

section .text
bits 64

global syscall_entry
global enter_user_mode
global leave_user_mode
extern syscall_dispatch

; Offsets into the per CPU block, which gs points at.
PERCPU_KERNEL_STACK equ 24
PERCPU_USER_STACK   equ 32

syscall_entry:
    swapgs
    mov [gs:PERCPU_USER_STACK], rsp     ; scratch, interrupts are off here
    mov rsp, [gs:PERCPU_KERNEL_STACK]

    ; The user stack goes on the kernel stack rather than staying in the per
    ; CPU block, because the thread may finish this call on another core.
    push qword [gs:PERCPU_USER_STACK]
    push rcx                    ; the address to return to
    push r11                    ; the flags to restore

    ; The frame the dispatcher reads, laid out so the call number sits first.
    push r9
    push r8
    push r10
    push rdx
    push rsi
    push rdi
    push rax

    mov rdi, rsp
    call syscall_dispatch

    ; Everything the program had in a register comes back the way it left it.
    ; Only rax carries the result, and syscall already claimed rcx and r11.
    pop rax
    pop rdi
    pop rsi
    pop rdx
    pop r10
    pop r8
    pop r9

    pop r11
    pop rcx
    pop rsp                     ; the user stack this call arrived on

    swapgs
    o64 sysret

; void enter_user_mode(u64 entry, u64 stack)
; Parks the kernel context where leave_user_mode can find it, then drops to
; ring 3 through an interrupt return.
enter_user_mode:
    push rbp
    push rbx
    push r12
    push r13
    push r14
    push r15
    mov [rel saved_kernel_rsp], rsp

    mov ax, 0x23                ; user data with the requested privilege level
    mov ds, ax
    mov es, ax

    push 0x23                   ; ss
    push rsi                    ; rsp
    push 0x202                  ; rflags with interrupts enabled
    push 0x2B                   ; cs, user code
    push rdi                    ; rip
    iretq

; void leave_user_mode()
; Comes back as if enter_user_mode had returned, which is how a program exiting
; or faulting lands back in the kernel that started it.
leave_user_mode:
    mov rsp, [rel saved_kernel_rsp]
    pop r15
    pop r14
    pop r13
    pop r12
    pop rbx
    pop rbp
    ret

section .data
align 8
saved_kernel_rsp:
    dq 0

section .note.GNU-stack progbits noalloc noexec nowrite align=1
