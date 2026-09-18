// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/time.hpp>

namespace eris {
namespace {

constinit u64 tick_count = 0;

void timer_tick(arch::Registers&)
{
    ++tick_count;
}

}

void timer_init(u32 frequency_hz)
{
    constexpr u32 pit_base = 1193182;
    const u32 divisor = pit_base / frequency_hz;

    arch::outb(0x43, 0x36);
    arch::outb(0x40, static_cast<u8>(divisor & 0xFF));
    arch::outb(0x40, static_cast<u8>((divisor >> 8) & 0xFF));

    arch::irq_register(0, timer_tick);
    arch::irq_unmask(0);
}

u64 ticks()
{
    return tick_count;
}

}
