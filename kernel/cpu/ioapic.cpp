// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/acpi.hpp>
#include <eris/apic.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>

namespace eris::arch {
namespace {

constexpr u32 reg_select = 0x00;
constexpr u32 reg_window = 0x10;

constexpr u32 index_version = 0x01;
constexpr u32 index_redirection = 0x10;

constexpr u64 entry_masked = 1ULL << 16;
constexpr u64 entry_level_triggered = 1ULL << 15;
constexpr u64 entry_active_low = 1ULL << 13;

constinit volatile u32* registers = nullptr;
constinit u32 gsi_base = 0;
constinit u32 entry_count = 0;

u32 read(u32 index)
{
    registers[reg_select / 4] = index;
    return registers[reg_window / 4];
}

void write(u32 index, u32 value)
{
    registers[reg_select / 4] = index;
    registers[reg_window / 4] = value;
}

u64 read_entry(u32 gsi)
{
    const u32 index = index_redirection + 2 * (gsi - gsi_base);
    return (static_cast<u64>(read(index + 1)) << 32) | read(index);
}

void write_entry(u32 gsi, u64 value)
{
    const u32 index = index_redirection + 2 * (gsi - gsi_base);
    write(index, static_cast<u32>(value));
    write(index + 1, static_cast<u32>(value >> 32));
}

bool owns(u32 gsi)
{
    return registers != nullptr && gsi >= gsi_base && gsi < gsi_base + entry_count;
}

} // namespace

bool ioapic_init()
{
    if (!acpi::available() || acpi::io_apic_address() == 0)
        return false;

    registers = static_cast<volatile u32*>(mm::map_device(acpi::io_apic_address(), page_size));
    if (registers == nullptr) {
        pr_warn("ioapic: cannot map the register window\n");
        return false;
    }

    gsi_base = acpi::io_apic_gsi_base();
    entry_count = ((read(index_version) >> 16) & 0xFF) + 1;

    // Everything starts masked, routing is added as drivers ask for a line.
    for (u32 i = 0; i < entry_count; ++i)
        write_entry(gsi_base + i, entry_masked);

    pr_info("ioapic: %u inputs from gsi %u\n", entry_count, gsi_base);
    return true;
}

bool ioapic_present()
{
    return registers != nullptr;
}

void ioapic_route(u8 irq, u8 vector)
{
    const u32 gsi = acpi::gsi_for_irq(irq);
    if (!owns(gsi))
        return;

    u64 entry = vector;
    entry |= static_cast<u64>(lapic_id()) << 56;

    if (acpi::irq_active_low(irq))
        entry |= entry_active_low;
    if (acpi::irq_level_triggered(irq))
        entry |= entry_level_triggered;

    write_entry(gsi, entry | entry_masked);
}

void ioapic_mask(u8 irq)
{
    const u32 gsi = acpi::gsi_for_irq(irq);
    if (!owns(gsi))
        return;

    write_entry(gsi, read_entry(gsi) | entry_masked);
}

void ioapic_unmask(u8 irq)
{
    const u32 gsi = acpi::gsi_for_irq(irq);
    if (!owns(gsi))
        return;

    write_entry(gsi, read_entry(gsi) & ~entry_masked);
}

bool apic_in_use()
{
    return lapic_present() && ioapic_present();
}

} // namespace eris::arch
