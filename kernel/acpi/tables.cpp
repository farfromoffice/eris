// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/acpi.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

namespace eris::acpi {
namespace {

struct ERIS_PACKED Rsdp {
    char signature[8];
    u8 checksum;
    char oem_id[6];
    u8 revision;
    u32 rsdt_address;
    u32 length;
    u64 xsdt_address;
    u8 extended_checksum;
    u8 reserved[3];
};

struct ERIS_PACKED Madt {
    TableHeader header;
    u32 local_apic_address;
    u32 flags;
};

struct ERIS_PACKED HpetTable {
    TableHeader header;
    u32 hardware_id;
    u8 address_space_id;
    u8 register_bit_width;
    u8 register_bit_offset;
    u8 reserved;
    u64 address;
    u8 hpet_number;
    u16 minimum_tick;
    u8 protection;
};

constexpr u32 max_irqs = 16;
constexpr usize max_cpus = 64;

constinit bool present = false;
constinit const TableHeader* root = nullptr;
constinit bool root_is_xsdt = false;
constinit usize root_entries = 0;

constinit phys_addr lapic_address = 0;
constinit phys_addr ioapic_address = 0;
constinit u32 ioapic_gsi_base = 0;
constinit phys_addr hpet_base = 0;
constinit usize cpus = 0;

constinit u32 irq_to_gsi[max_irqs]{};
constinit bool irq_low[max_irqs]{};
constinit bool irq_level[max_irqs]{};

bool checksum_ok(const void* table, usize length)
{
    const auto* bytes = static_cast<const u8*>(table);
    u8 sum = 0;
    for (usize i = 0; i < length; ++i)
        sum = static_cast<u8>(sum + bytes[i]);
    return sum == 0;
}

const Rsdp* scan_for_rsdp(phys_addr start, phys_addr end)
{
    for (phys_addr address = start; address + sizeof(Rsdp) < end; address += 16) {
        const auto* candidate = reinterpret_cast<const Rsdp*>(mm::phys_to_virt(address));
        if (memcmp(candidate->signature, "RSD PTR ", 8) != 0)
            continue;
        if (!checksum_ok(candidate, 20))
            continue;
        return candidate;
    }

    return nullptr;
}

const Rsdp* find_rsdp()
{
    // The BIOS data area is at a fixed low address, which the compiler would
    // rather believe is a null dereference.
    volatile virt_addr bda_pointer = mm::phys_to_virt(0x40E);
    const auto ebda_segment = *reinterpret_cast<const volatile u16*>(bda_pointer);
    const phys_addr ebda = static_cast<phys_addr>(ebda_segment) << 4;

    if (ebda >= 0x400 && ebda < 0xA0000) {
        if (const Rsdp* rsdp = scan_for_rsdp(ebda, ebda + 1024); rsdp != nullptr)
            return rsdp;
    }

    return scan_for_rsdp(0xE0000, 0x100000);
}

const TableHeader* entry_at(usize index)
{
    const auto base = reinterpret_cast<virt_addr>(root) + sizeof(TableHeader);

    const phys_addr address = root_is_xsdt
        ? reinterpret_cast<const u64*>(base)[index]
        : reinterpret_cast<const u32*>(base)[index];

    return reinterpret_cast<const TableHeader*>(mm::phys_to_virt(address));
}

void parse_madt(const Madt& madt)
{
    lapic_address = madt.local_apic_address;

    const auto start = reinterpret_cast<virt_addr>(&madt) + sizeof(Madt);
    const auto end = reinterpret_cast<virt_addr>(&madt) + madt.header.length;

    for (virt_addr cursor = start; cursor + sizeof(MadtEntry) <= end;) {
        const auto* entry = reinterpret_cast<const MadtEntry*>(cursor);
        if (entry->length < sizeof(MadtEntry))
            break;

        switch (static_cast<MadtType>(entry->type)) {
        case MadtType::LocalApic: {
            const auto* cpu = reinterpret_cast<const MadtLocalApic*>(entry);
            if ((cpu->flags & 1) != 0 && cpus < max_cpus)
                ++cpus;
            break;
        }
        case MadtType::IoApic: {
            const auto* io = reinterpret_cast<const MadtIoApic*>(entry);
            if (ioapic_address == 0) {
                ioapic_address = io->address;
                ioapic_gsi_base = io->gsi_base;
            }
            break;
        }
        case MadtType::InterruptOverride: {
            const auto* override_entry = reinterpret_cast<const MadtOverride*>(entry);
            if (override_entry->source < max_irqs) {
                irq_to_gsi[override_entry->source] = override_entry->gsi;
                irq_low[override_entry->source] = (override_entry->flags & 0x3) == 0x3;
                irq_level[override_entry->source] = (override_entry->flags & 0xC) == 0xC;
            }
            break;
        }
        default:
            break;
        }

        cursor += entry->length;
    }
}

} // namespace

bool init()
{
    for (u32 irq = 0; irq < max_irqs; ++irq)
        irq_to_gsi[irq] = irq;

    const Rsdp* rsdp = find_rsdp();
    if (rsdp == nullptr) {
        pr_warn("acpi: no rsdp, falling back to the legacy chips\n");
        return false;
    }

    if (rsdp->revision >= 2 && rsdp->xsdt_address != 0) {
        root = reinterpret_cast<const TableHeader*>(mm::phys_to_virt(rsdp->xsdt_address));
        root_is_xsdt = true;
    } else {
        root = reinterpret_cast<const TableHeader*>(mm::phys_to_virt(rsdp->rsdt_address));
        root_is_xsdt = false;
    }

    if (!checksum_ok(root, root->length)) {
        pr_warn("acpi: root table checksum failed\n");
        root = nullptr;
        return false;
    }

    const usize entry_size = root_is_xsdt ? sizeof(u64) : sizeof(u32);
    root_entries = (root->length - sizeof(TableHeader)) / entry_size;

    if (const TableHeader* madt = find_table("APIC"); madt != nullptr)
        parse_madt(*reinterpret_cast<const Madt*>(madt));

    if (const TableHeader* hpet = find_table("HPET"); hpet != nullptr)
        hpet_base = reinterpret_cast<const HpetTable*>(hpet)->address;

    present = true;

    pr_info("acpi: %s with %lu tables, %lu cpu%s, lapic %lx, ioapic %lx, hpet %lx\n",
            root_is_xsdt ? "xsdt" : "rsdt",
            static_cast<u64>(root_entries),
            static_cast<u64>(cpus),
            cpus == 1 ? "" : "s",
            lapic_address,
            ioapic_address,
            hpet_base);

    return true;
}

bool available()
{
    return present;
}

const TableHeader* find_table(const char* signature)
{
    if (root == nullptr)
        return nullptr;

    for (usize i = 0; i < root_entries; ++i) {
        const TableHeader* table = entry_at(i);
        if (memcmp(table->signature, signature, 4) != 0)
            continue;
        if (!checksum_ok(table, table->length))
            continue;
        return table;
    }

    return nullptr;
}

phys_addr local_apic_address()
{
    return lapic_address;
}

phys_addr io_apic_address()
{
    return ioapic_address;
}

u32 io_apic_gsi_base()
{
    return ioapic_gsi_base;
}

phys_addr hpet_address()
{
    return hpet_base;
}

u32 gsi_for_irq(u8 irq)
{
    return irq < max_irqs ? irq_to_gsi[irq] : irq;
}

bool irq_active_low(u8 irq)
{
    return irq < max_irqs && irq_low[irq];
}

bool irq_level_triggered(u8 irq)
{
    return irq < max_irqs && irq_level[irq];
}

usize cpu_count()
{
    return cpus == 0 ? 1 : cpus;
}

} // namespace eris::acpi
