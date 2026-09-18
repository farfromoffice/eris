// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/acpi.hpp>
#include <eris/io.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>
#include <eris/time.hpp>

namespace eris {
namespace {

constexpr u32 hpet_capabilities = 0x000;
constexpr u32 hpet_configuration = 0x010;
constexpr u32 hpet_counter = 0x0F0;

constinit volatile u64* hpet = nullptr;
constinit u64 hpet_period_fs = 0;

constinit u64 tsc_hz = 0;
constinit u64 tsc_origin = 0;

u64 read_tsc()
{
    u32 low, high;
    asm volatile("rdtsc" : "=a"(low), "=d"(high));
    return (static_cast<u64>(high) << 32) | low;
}

u64 hpet_read(u32 offset)
{
    return hpet[offset / sizeof(u64)];
}

void hpet_write(u32 offset, u64 value)
{
    hpet[offset / sizeof(u64)] = value;
}

// The one timing source that exists before anything is calibrated.
void pit_wait(u16 divisor)
{
    constexpr u16 control_port = 0x61;

    arch::outb(control_port, static_cast<u8>((arch::inb(control_port) & 0xFD) | 0x01));
    arch::outb(0x43, 0xB2);
    arch::outb(0x42, static_cast<u8>(divisor & 0xFF));
    arch::outb(0x42, static_cast<u8>(divisor >> 8));

    const u8 gate = static_cast<u8>(arch::inb(control_port) & 0xFE);
    arch::outb(control_port, gate);
    arch::outb(control_port, static_cast<u8>(gate | 1));

    while ((arch::inb(control_port) & 0x20) == 0)
        ;
}

bool hpet_init()
{
    if (!acpi::available() || acpi::hpet_address() == 0)
        return false;

    hpet = static_cast<volatile u64*>(mm::map_device(acpi::hpet_address(), page_size));
    if (hpet == nullptr)
        return false;

    hpet_period_fs = hpet_read(hpet_capabilities) >> 32;
    if (hpet_period_fs == 0 || hpet_period_fs > 0x05F5E100) {
        hpet = nullptr;
        return false;
    }

    hpet_write(hpet_configuration, hpet_read(hpet_configuration) | 1);
    return true;
}

void tsc_calibrate()
{
    constexpr u32 pit_hz = 1193182;
    constexpr u32 interval_hz = 100;

    const u64 start = read_tsc();
    pit_wait(pit_hz / interval_hz);
    const u64 end = read_tsc();

    tsc_hz = (end - start) * interval_hz;
    tsc_origin = end;
}

} // namespace

void clock_init()
{
    if (hpet_init()) {
        pr_info("clock: hpet at %lu ns per tick\n", hpet_period_fs / 1000000);
        return;
    }

    tsc_calibrate();
    pr_info("clock: tsc at %lu MHz\n", tsc_hz / 1000000);
}

const char* clock_source()
{
    if (hpet != nullptr)
        return "hpet";
    if (tsc_hz != 0)
        return "tsc";
    return "none";
}

u64 monotonic_ns()
{
    if (hpet != nullptr)
        return (hpet_read(hpet_counter) * hpet_period_fs) / 1000000;

    if (tsc_hz == 0)
        return 0;

    const u64 now = read_tsc();
    const u64 elapsed = now - tsc_origin;
    return (elapsed / tsc_hz) * 1000000000ULL + ((elapsed % tsc_hz) * 1000000000ULL) / tsc_hz;
}

u64 monotonic_ms()
{
    return monotonic_ns() / 1000000;
}

void udelay(u64 microseconds)
{
    const u64 deadline = monotonic_ns() + microseconds * 1000;
    while (monotonic_ns() < deadline)
        asm volatile("pause");
}

void mdelay(u64 milliseconds)
{
    udelay(milliseconds * 1000);
}

} // namespace eris
