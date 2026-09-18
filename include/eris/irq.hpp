// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris::arch {

struct Registers {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vector, error_code;
    u64 rip, cs, rflags, rsp, ss;
};

using IrqHandler = void (*)(Registers&);

void gdt_init();
void idt_init();
void pic_init();
void irq_register(u8 irq, IrqHandler handler);
void irq_unmask(u8 irq);
void irq_mask(u8 irq);
void irq_eoi(u8 irq);

} // namespace eris::arch
