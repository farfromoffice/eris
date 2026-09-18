// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris {

using TimerHandle = u32;
using TimerCallback = void (*)(void* context);

inline constexpr TimerHandle invalid_timer = 0;

// Nanoseconds since the clock came up, from the HPET where there is one and
// from a calibrated TSC otherwise.
u64 monotonic_ns();
u64 monotonic_ms();

void clock_init();
const char* clock_source();

void timers_init();
TimerHandle timer_after(u64 delay_ns, TimerCallback callback, void* context);
TimerHandle timer_every(u64 period_ns, TimerCallback callback, void* context);
void timer_cancel(TimerHandle handle);
void timers_run();

void udelay(u64 microseconds);
void mdelay(u64 milliseconds);

// Jiffies, kept for anything that only wants to know that time passed.
u64 ticks();

} // namespace eris
