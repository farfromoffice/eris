// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/acpi.hpp>
#include <eris/apic.hpp>
#include <eris/irq.hpp>
#include <eris/printk.hpp>

namespace eris::arch {
namespace {

constinit bool apic_active = false;

} // namespace

// Picks the interrupt controller once, at boot, and everything after that goes
// through the same three calls no matter which one answered.
void irq_init()
{
    pic_init();

    if (lapic_init() && ioapic_init()) {
        pic_disable();
        apic_active = true;
        pr_info("irq: routing through the io apic, legacy pic masked\n");
        return;
    }

    pr_info("irq: no usable apic, staying on the legacy pic\n");
}

void irq_mask(u8 irq)
{
    if (apic_active)
        ioapic_mask(irq);
    else
        pic_mask(irq);
}

void irq_unmask(u8 irq)
{
    if (apic_active) {
        ioapic_route(irq, static_cast<u8>(32 + irq));
        ioapic_unmask(irq);
        return;
    }

    pic_unmask(irq);
}

void irq_eoi(u8 irq)
{
    if (apic_active)
        lapic_eoi();
    else
        pic_eoi(irq);
}

} // namespace eris::arch
