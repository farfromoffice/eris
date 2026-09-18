// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cpu.hpp>
#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/compiler.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>

extern "C" void* isr_stub_table[256];

namespace eris::arch {
namespace {

struct ERIS_PACKED IdtEntry {
    u16 offset_low;
    u16 selector;
    u8 ist;
    u8 type_attr;
    u16 offset_mid;
    u32 offset_high;
    u32 reserved;
};

struct ERIS_PACKED IdtPointer {
    u16 limit;
    u64 base;
};

constinit IdtEntry idt[256]{};
constinit IdtPointer idt_pointer{};
constinit IrqHandler irq_handlers[16]{};

constexpr const char* exception_names[32] = {
    "divide error", "debug", "nmi", "breakpoint",
    "overflow", "bound range", "invalid opcode", "device not available",
    "double fault", "coprocessor overrun", "invalid tss", "segment not present",
    "stack fault", "general protection", "page fault", "reserved",
    "x87 fpu error", "alignment check", "machine check", "simd fp",
    "virtualization", "control protection", "reserved", "reserved",
    "reserved", "reserved", "reserved", "hypervisor injection",
    "vmm communication", "security exception", "reserved", "reserved",
};

// The faults that can arrive on a broken kernel stack get a stack of their own.
constexpr u8 ist_for_vector(u8 vector)
{
    switch (vector) {
    case 2:  return ist_nmi;
    case 8:  return ist_double_fault;
    case 18: return ist_machine_check;
    default: return 0;
    }
}

void set_gate(u8 vector, void* handler)
{
    const auto address = reinterpret_cast<u64>(handler);
    idt[vector] = IdtEntry{
        .offset_low = static_cast<u16>(address & 0xFFFF),
        .selector = 0x08,
        .ist = ist_for_vector(vector),
        .type_attr = 0x8E,
        .offset_mid = static_cast<u16>((address >> 16) & 0xFFFF),
        .offset_high = static_cast<u32>(address >> 32),
        .reserved = 0,
    };
}

} // namespace

void idt_init()
{
    for (u16 vector = 0; vector < 256; ++vector)
        set_gate(static_cast<u8>(vector), isr_stub_table[vector]);

    idt_pointer.limit = sizeof(idt) - 1;
    idt_pointer.base = reinterpret_cast<u64>(&idt);

    asm volatile("lidt %0" : : "m"(idt_pointer));
}

void irq_register(u8 irq, IrqHandler handler)
{
    if (irq < 16)
        irq_handlers[irq] = handler;
}

void report_page_fault(const Registers& regs)
{
    u64 address;
    asm volatile("mov %%cr2, %0" : "=r"(address));

    pr_err("page fault at %lx: %s, %s, %s%s%s\n",
           address,
           (regs.error_code & 1) ? "protection violation" : "page not present",
           (regs.error_code & 2) ? "write" : "read",
           (regs.error_code & 4) ? "user mode" : "kernel mode",
           (regs.error_code & 8) ? ", reserved bit set" : "",
           (regs.error_code & 16) ? ", instruction fetch" : "");
}

extern "C" void isr_dispatch(Registers& regs)
{
    if (regs.vector < 32) {
        if (regs.vector == 14)
            report_page_fault(regs);

        panic_with_registers(regs,
                             "cpu exception %u (%s)",
                             static_cast<unsigned>(regs.vector),
                             exception_names[regs.vector]);
    }

    if (regs.vector >= 32 && regs.vector < 48) {
        const auto irq = static_cast<u8>(regs.vector - 32);
        if (irq_handlers[irq] != nullptr)
            irq_handlers[irq](regs);
        irq_eoi(irq);
        return;
    }

    pr_warn("unhandled interrupt vector %u\n", static_cast<unsigned>(regs.vector));
}

}
