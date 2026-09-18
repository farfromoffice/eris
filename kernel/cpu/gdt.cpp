// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/compiler.hpp>
#include <eris/cpu.hpp>
#include <eris/irq.hpp>
#include <eris/string.hpp>

namespace eris::arch {
namespace {

struct ERIS_PACKED GdtPointer {
    u16 limit;
    u64 base;
};

constexpr u16 tss_selector = 0x18;

// Flat descriptors: in long mode the base and limit are ignored, only the
// access and flag bits carry meaning.
constexpr u64 descriptor(u8 access, u8 flags)
{
    return (static_cast<u64>(access) << 40) | (static_cast<u64>(flags) << 52);
}

void load(PerCpu& cpu)
{
    GdtPointer pointer{
        .limit = static_cast<u16>(sizeof(cpu.gdt) - 1),
        .base = reinterpret_cast<u64>(cpu.gdt),
    };

    asm volatile(
        "lgdt %0\n"
        "pushq $0x08\n"
        "leaq 1f(%%rip), %%rax\n"
        "pushq %%rax\n"
        "lretq\n"
        "1:\n"
        "mov $0x10, %%ax\n"
        "mov %%ax, %%ds\n"
        "mov %%ax, %%es\n"
        "mov %%ax, %%ss\n"
        :
        : "m"(pointer)
        : "rax", "memory");
}

} // namespace

void gdt_init_cpu(PerCpu& cpu)
{
    cpu.gdt[0] = 0;
    cpu.gdt[1] = descriptor(0x9A, 0xA);
    cpu.gdt[2] = descriptor(0x92, 0xA);
    cpu.gdt[3] = 0;
    cpu.gdt[4] = 0;
    cpu.gdt[5] = 0;
    cpu.gdt[6] = 0;

    const auto base = reinterpret_cast<u64>(&cpu.tss);
    const u32 limit = sizeof(cpu.tss) - 1;

    cpu.gdt[3] = (static_cast<u64>(limit) & 0xFFFF)
        | ((base & 0xFFFFFF) << 16)
        | (static_cast<u64>(0x89) << 40)
        | (((static_cast<u64>(limit) >> 16) & 0xF) << 48)
        | (((base >> 24) & 0xFF) << 56);
    cpu.gdt[4] = base >> 32;

    load(cpu);
    asm volatile("ltr %0" : : "r"(tss_selector));
}

void gdt_init()
{
    PerCpu& cpu = this_cpu();
    gdt_init_cpu(cpu);
}

} // namespace eris::arch
