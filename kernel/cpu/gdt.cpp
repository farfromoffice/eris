// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cpu.hpp>
#include <eris/irq.hpp>
#include <eris/compiler.hpp>
#include <eris/string.hpp>

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

// Entries 3 and 4 hold the single 16 byte TSS descriptor.
constinit GdtEntry gdt[5]{};
constexpr u16 tss_selector = 0x18;
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
    gdt[3] = GdtEntry{};
    gdt[4] = GdtEntry{};

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

void gdt_load_tss(u64 base, u32 limit)
{
    gdt[3] = GdtEntry{
        .limit_low = static_cast<u16>(limit & 0xFFFF),
        .base_low = static_cast<u16>(base & 0xFFFF),
        .base_mid = static_cast<u8>((base >> 16) & 0xFF),
        .access = 0x89,
        .granularity = static_cast<u8>((limit >> 16) & 0x0F),
        .base_high = static_cast<u8>((base >> 24) & 0xFF),
    };

    // The upper half of a system descriptor is the rest of the base address.
    const auto base_upper = static_cast<u32>(base >> 32);
    memcpy(&gdt[4], &base_upper, sizeof(base_upper));

    asm volatile("ltr %0" : : "r"(tss_selector));
}

}
