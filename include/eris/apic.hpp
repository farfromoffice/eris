// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris::arch {

inline constexpr u8 vector_lapic_timer = 0x40;
inline constexpr u8 vector_spurious = 0xFF;

bool lapic_init();
bool lapic_present();
u32 lapic_id();
void lapic_eoi();

void lapic_timer_oneshot(u64 nanoseconds);
void lapic_timer_stop();
u64 lapic_timer_hz();

bool ioapic_init();
bool ioapic_present();
void ioapic_route(u8 irq, u8 vector);
void ioapic_mask(u8 irq);
void ioapic_unmask(u8 irq);

// True once interrupts arrive through the APIC pair and the legacy chips are
// masked off for good.
bool apic_in_use();

} // namespace eris::arch
