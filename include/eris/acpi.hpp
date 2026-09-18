// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/compiler.hpp>
#include <eris/types.hpp>

namespace eris::acpi {

struct ERIS_PACKED TableHeader {
    char signature[4];
    u32 length;
    u8 revision;
    u8 checksum;
    char oem_id[6];
    char oem_table_id[8];
    u32 oem_revision;
    u32 creator_id;
    u32 creator_revision;
};

struct ERIS_PACKED MadtEntry {
    u8 type;
    u8 length;
};

struct ERIS_PACKED MadtLocalApic {
    MadtEntry header;
    u8 processor_id;
    u8 apic_id;
    u32 flags;
};

struct ERIS_PACKED MadtIoApic {
    MadtEntry header;
    u8 id;
    u8 reserved;
    u32 address;
    u32 gsi_base;
};

struct ERIS_PACKED MadtOverride {
    MadtEntry header;
    u8 bus;
    u8 source;
    u32 gsi;
    u16 flags;
};

enum class MadtType : u8 {
    LocalApic = 0,
    IoApic = 1,
    InterruptOverride = 2,
    LocalApicNmi = 4,
};

bool init();
bool available();

const TableHeader* find_table(const char* signature);

phys_addr local_apic_address();
phys_addr io_apic_address();
u32 io_apic_gsi_base();
phys_addr hpet_address();

// Legacy IRQ to global system interrupt, which the firmware is free to move.
u32 gsi_for_irq(u8 irq);
bool irq_active_low(u8 irq);
bool irq_level_triggered(u8 irq);

usize cpu_count();
u32 cpu_apic_id(usize index);

} // namespace eris::acpi
