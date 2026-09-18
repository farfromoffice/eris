// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/apic.hpp>
#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/printk.hpp>
#include <eris/time.hpp>

namespace eris {
namespace {

constexpr usize max_timers = 64;
constexpr u64 minimum_delay_ns = 100000;
constexpr u64 fallback_tick_ns = 10000000;

struct Timer {
    u64 deadline;
    u64 period;
    TimerCallback callback;
    void* context;
    TimerHandle handle;
    bool active;
};

constinit Timer timers[max_timers]{};
constinit TimerHandle next_handle = 1;
constinit u64 tick_count = 0;
constinit bool lapic_driven = false;

Timer* find_free()
{
    for (auto& timer : timers) {
        if (!timer.active)
            return &timer;
    }
    return nullptr;
}

// The hardware timer is only ever programmed for the nearest deadline, so a
// hundred pending timers still cost one interrupt.
void program_next()
{
    if (!lapic_driven)
        return;

    u64 earliest = 0;
    bool found = false;

    for (const auto& timer : timers) {
        if (!timer.active)
            continue;
        if (!found || timer.deadline < earliest) {
            earliest = timer.deadline;
            found = true;
        }
    }

    if (!found) {
        arch::lapic_timer_stop();
        return;
    }

    const u64 now = monotonic_ns();
    const u64 delay = earliest > now ? earliest - now : minimum_delay_ns;
    arch::lapic_timer_oneshot(delay < minimum_delay_ns ? minimum_delay_ns : delay);
}

TimerHandle arm(u64 delay_ns, u64 period_ns, TimerCallback callback, void* context)
{
    if (callback == nullptr)
        return invalid_timer;

    Timer* timer = find_free();
    if (timer == nullptr) {
        pr_warn("timers: table full, a callback was dropped\n");
        return invalid_timer;
    }

    timer->deadline = monotonic_ns() + delay_ns;
    timer->period = period_ns;
    timer->callback = callback;
    timer->context = context;
    timer->handle = next_handle++;
    timer->active = true;

    program_next();
    return timer->handle;
}

void jiffy_tick(void*)
{
    ++tick_count;
}

void pit_tick(arch::Registers&)
{
    timers_run();
}

} // namespace

void timers_init()
{
    lapic_driven = arch::lapic_present();

    if (!lapic_driven) {
        // No APIC: the legacy chip keeps its own beat and every deadline is
        // checked on each of its ticks.
        constexpr u32 pit_base = 1193182;
        constexpr u32 frequency = 100;
        const u32 divisor = pit_base / frequency;

        arch::outb(0x43, 0x36);
        arch::outb(0x40, static_cast<u8>(divisor & 0xFF));
        arch::outb(0x40, static_cast<u8>((divisor >> 8) & 0xFF));

        arch::irq_register(0, pit_tick);
        arch::irq_unmask(0);
    }

    timer_every(fallback_tick_ns, jiffy_tick, nullptr);

    pr_info("timers: %s driven, %s clock\n",
            lapic_driven ? "lapic" : "pit",
            clock_source());
}

TimerHandle timer_after(u64 delay_ns, TimerCallback callback, void* context)
{
    return arm(delay_ns, 0, callback, context);
}

TimerHandle timer_every(u64 period_ns, TimerCallback callback, void* context)
{
    return arm(period_ns, period_ns, callback, context);
}

void timer_cancel(TimerHandle handle)
{
    for (auto& timer : timers) {
        if (timer.active && timer.handle == handle) {
            timer.active = false;
            program_next();
            return;
        }
    }
}

void timers_run()
{
    const u64 now = monotonic_ns();

    for (auto& timer : timers) {
        if (!timer.active || timer.deadline > now)
            continue;

        TimerCallback callback = timer.callback;
        void* context = timer.context;

        if (timer.period != 0)
            timer.deadline = now + timer.period;
        else
            timer.active = false;

        callback(context);
    }

    program_next();
}

u64 ticks()
{
    return tick_count;
}

} // namespace eris
