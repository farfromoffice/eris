// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/acpi.hpp>
#include <eris/apic.hpp>
#include <eris/io.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>

namespace eris::arch {
namespace {

constexpr u32 reg_id = 0x020;
constexpr u32 reg_eoi = 0x0B0;
constexpr u32 reg_spurious = 0x0F0;
constexpr u32 reg_lvt_timer = 0x320;
constexpr u32 reg_timer_initial = 0x380;
constexpr u32 reg_timer_current = 0x390;
constexpr u32 reg_timer_divide = 0x3E0;

constexpr u32 lvt_masked = 1u << 16;
constexpr u32 spurious_enable = 1u << 8;
constexpr u32 divide_by_16 = 0x3;

constexpr u32 apic_base_msr = 0x1B;
constexpr u64 apic_global_enable = 1ULL << 11;

constinit volatile u32* registers = nullptr;
constinit u64 timer_hz = 0;

void write(u32 offset, u32 value)
{
    registers[offset / 4] = value;
}

u32 read(u32 offset)
{
    return registers[offset / 4];
}

u64 read_msr(u32 msr)
{
    u32 low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<u64>(high) << 32) | low;
}

void write_msr(u32 msr, u64 value)
{
    asm volatile("wrmsr"
                 :
                 : "a"(static_cast<u32>(value)),
                   "d"(static_cast<u32>(value >> 32)),
                   "c"(msr));
}

// Counts one PIT interval without needing an interrupt, which is the only
// timing source that exists before the APIC is trusted.
void pit_wait(u16 divisor)
{
    constexpr u16 control_port = 0x61;

    outb(control_port, static_cast<u8>((inb(control_port) & 0xFD) | 0x01));
    outb(0x43, 0xB2);
    outb(0x42, static_cast<u8>(divisor & 0xFF));
    outb(0x42, static_cast<u8>(divisor >> 8));

    const u8 gate = static_cast<u8>(inb(control_port) & 0xFE);
    outb(control_port, gate);
    outb(control_port, static_cast<u8>(gate | 1));

    while ((inb(control_port) & 0x20) == 0)
        ;
}

void calibrate()
{
    constexpr u32 pit_hz = 1193182;
    constexpr u32 interval_hz = 100;
    constexpr u16 divisor = pit_hz / interval_hz;

    write(reg_timer_divide, divide_by_16);
    write(reg_lvt_timer, lvt_masked);
    write(reg_timer_initial, 0xFFFFFFFF);

    pit_wait(divisor);

    const u32 remaining = read(reg_timer_current);
    write(reg_timer_initial, 0);

    const u64 ticks = 0xFFFFFFFFULL - remaining;
    timer_hz = ticks * interval_hz;
}

} // namespace

bool lapic_init()
{
    if (!acpi::available() || acpi::local_apic_address() == 0)
        return false;

    registers = static_cast<volatile u32*>(mm::map_device(acpi::local_apic_address(), page_size));
    if (registers == nullptr) {
        pr_warn("lapic: cannot map the register window\n");
        return false;
    }

    write_msr(apic_base_msr, read_msr(apic_base_msr) | apic_global_enable);
    write(reg_spurious, spurious_enable | vector_spurious);

    calibrate();

    pr_info("lapic: id %u enabled, timer at %lu MHz\n",
            lapic_id(),
            timer_hz / 1000000);
    return true;
}

bool lapic_present()
{
    return registers != nullptr;
}

u32 lapic_id()
{
    return registers == nullptr ? 0 : read(reg_id) >> 24;
}

void lapic_eoi()
{
    if (registers != nullptr)
        write(reg_eoi, 0);
}

void lapic_timer_oneshot(u64 nanoseconds)
{
    if (registers == nullptr || timer_hz == 0)
        return;

    u64 count = (timer_hz * nanoseconds) / 1000000000ULL;
    if (count == 0)
        count = 1;
    if (count > 0xFFFFFFFF)
        count = 0xFFFFFFFF;

    write(reg_lvt_timer, vector_lapic_timer);
    write(reg_timer_initial, static_cast<u32>(count));
}

void lapic_timer_stop()
{
    if (registers == nullptr)
        return;

    write(reg_timer_initial, 0);
    write(reg_lvt_timer, lvt_masked);
}

u64 lapic_timer_hz()
{
    return timer_hz;
}

} // namespace eris::arch
