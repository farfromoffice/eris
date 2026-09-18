// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/irq.hpp>
#include <eris/compiler.hpp>

namespace eris::arch {
namespace {

struct ERIS_PACKED GdtEntry {
    u16 limit_low;
    u16 base_low;
    u8 base_mid;
    u8 access;
    u8 granularity;
    u8 base_high;
};

struct ERIS_PACKED GdtPointer {
    u16 limit;
    u64 base;
};

constinit GdtEntry gdt[3]{};
constinit GdtPointer gdt_pointer{};

constexpr GdtEntry make_entry(u8 access, u8 granularity)
{
    return GdtEntry{0, 0, 0, access, granularity, 0};
}

}

void gdt_init()
{
    gdt[0] = GdtEntry{};
    gdt[1] = make_entry(0x9A, 0xA0);
    gdt[2] = make_entry(0x92, 0xA0);

    gdt_pointer.limit = sizeof(gdt) - 1;
    gdt_pointer.base = reinterpret_cast<u64>(&gdt);

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
        "mov %%ax, %%fs\n"
        "mov %%ax, %%gs\n"
        :
        : "m"(gdt_pointer)
        : "rax", "memory");
}

}
