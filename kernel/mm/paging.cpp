// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cpu.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

extern "C" {
extern eris::u8 __kernel_start[];
extern eris::u8 __kernel_end[];
extern eris::u8 __text_start[];
extern eris::u8 __text_end[];
extern eris::u8 __rodata_start[];
extern eris::u8 __rodata_end[];
extern eris::u8 __data_start[];
extern eris::u8 __data_end[];
}

namespace eris::mm {
namespace {

constexpr u64 pte_present = 1ULL << 0;
constexpr u64 pte_write = 1ULL << 1;
constexpr u64 pte_user = 1ULL << 2;
constexpr u64 pte_write_through = 1ULL << 3;
constexpr u64 pte_no_cache = 1ULL << 4;
constexpr u64 pte_huge = 1ULL << 7;
constexpr u64 pte_global = 1ULL << 8;
constexpr u64 pte_no_execute = 1ULL << 63;
constexpr u64 pte_address_mask = 0x000FFFFFFFFFF000ULL;

constexpr usize huge_page_size = 2 * 1024 * 1024;
constexpr usize entries_per_table = 512;

constexpr phys_addr direct_limit = 1ULL << 30;

constinit AddressSpace kernel_space{};

u64 encode(PageFlags flags)
{
    u64 bits = pte_present;

    if (has(flags, PageFlags::Write))
        bits |= pte_write;
    if (has(flags, PageFlags::User))
        bits |= pte_user;
    if (has(flags, PageFlags::NoExecute))
        bits |= pte_no_execute;
    if (has(flags, PageFlags::Global))
        bits |= pte_global;
    if (has(flags, PageFlags::NoCache))
        bits |= pte_no_cache | pte_write_through;

    return bits;
}

u64* table_at(phys_addr frame)
{
    return reinterpret_cast<u64*>(phys_to_virt(frame & pte_address_mask));
}

u64* allocate_table()
{
    const phys_addr frame = alloc_page();
    if (frame == 0)
        return nullptr;

    auto* table = table_at(frame);
    memset(table, 0, page_size);
    return table;
}

constexpr usize index_of(virt_addr address, u8 level)
{
    return (address >> (12 + 9 * level)) & 0x1FF;
}

void invalidate(virt_addr address)
{
    asm volatile("invlpg (%0)" : : "r"(address) : "memory");
}

void enable_no_execute()
{
    u32 low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(0xC0000080));
    low |= 1u << 11;
    asm volatile("wrmsr" : : "a"(low), "d"(high), "c"(0xC0000080));
}

void enable_write_protect()
{
    u64 cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 1ULL << 16;
    asm volatile("mov %0, %%cr0" : : "r"(cr0) : "memory");
}

} // namespace

AddressSpace& AddressSpace::kernel()
{
    return kernel_space;
}

// Replaces a 2 MiB mapping with a page table covering the same range, so a
// single 4 KiB page inside it can get its own permissions.
bool AddressSpace::split_huge_page(u64* directory_entry, virt_addr address)
{
    const u64 entry = *directory_entry;
    const phys_addr base = entry & pte_address_mask;
    const u64 flags = entry & ~(pte_address_mask | pte_huge);

    u64* table = allocate_table();
    if (table == nullptr)
        return false;

    for (usize i = 0; i < entries_per_table; ++i)
        table[i] = (base + i * page_size) | flags;

    *directory_entry = virt_to_phys(reinterpret_cast<virt_addr>(table))
        | pte_present | pte_write;

    invalidate(address);
    return true;
}

u64* AddressSpace::table_for(virt_addr address, bool create)
{
    u64* table = table_at(root_);

    for (u8 level = 3; level >= 1; --level) {
        u64& entry = table[index_of(address, level)];

        if ((entry & pte_present) == 0) {
            if (!create)
                return nullptr;

            u64* next = allocate_table();
            if (next == nullptr)
                return nullptr;

            entry = virt_to_phys(reinterpret_cast<virt_addr>(next)) | pte_present | pte_write;
        }

        if (level == 1 && (entry & pte_huge) != 0) {
            if (!create)
                return nullptr;
            if (!split_huge_page(&entry, address))
                return nullptr;
        }

        table = table_at(entry);
    }

    return table;
}

bool AddressSpace::map(virt_addr address, phys_addr frame, usize length, PageFlags flags)
{
    const u64 bits = encode(flags);

    for (usize offset = 0; offset < length; offset += page_size) {
        u64* table = table_for(address + offset, true);
        if (table == nullptr)
            return false;

        table[index_of(address + offset, 0)] = ((frame + offset) & pte_address_mask) | bits;
        invalidate(address + offset);
    }

    return true;
}

bool AddressSpace::unmap(virt_addr address, usize length)
{
    for (usize offset = 0; offset < length; offset += page_size) {
        u64* table = table_for(address + offset, true);
        if (table == nullptr)
            return false;

        table[index_of(address + offset, 0)] = 0;
        invalidate(address + offset);
    }

    return true;
}

bool AddressSpace::protect(virt_addr address, usize length, PageFlags flags)
{
    const u64 bits = encode(flags);

    for (usize offset = 0; offset < length; offset += page_size) {
        u64* table = table_for(address + offset, true);
        if (table == nullptr)
            return false;

        u64& entry = table[index_of(address + offset, 0)];
        if ((entry & pte_present) == 0)
            return false;

        entry = (entry & pte_address_mask) | bits;
        invalidate(address + offset);
    }

    return true;
}

phys_addr AddressSpace::translate(virt_addr address) const
{
    const u64* table = table_at(root_);

    for (u8 level = 3; level >= 1; --level) {
        const u64 entry = table[index_of(address, level)];
        if ((entry & pte_present) == 0)
            return 0;

        if ((entry & pte_huge) != 0)
            return (entry & pte_address_mask) + (address & (huge_page_size - 1));

        table = table_at(entry);
    }

    const u64 entry = table[index_of(address, 0)];
    if ((entry & pte_present) == 0)
        return 0;

    return (entry & pte_address_mask) + (address & (page_size - 1));
}

bool AddressSpace::mapped(virt_addr address) const
{
    return translate(address) != 0 || address == 0;
}

void AddressSpace::activate() const
{
    asm volatile("mov %0, %%cr3" : : "r"(root_) : "memory");
}

void paging_init()
{
    enable_no_execute();

    u64* pml4 = allocate_table();
    if (pml4 == nullptr)
        panic("paging: no memory for the top level table");

    kernel_space.adopt(virt_to_phys(reinterpret_cast<virt_addr>(pml4)));

    // The first gigabyte stays directly reachable, because every physical page
    // the allocator hands out is touched through this window. It is writable
    // but never executable, the kernel image below gets the real permissions.
    u64* pdpt = allocate_table();
    u64* directory = allocate_table();
    if (pdpt == nullptr || directory == nullptr)
        panic("paging: no memory for the direct map");

    pml4[0] = virt_to_phys(reinterpret_cast<virt_addr>(pdpt)) | pte_present | pte_write;
    pdpt[0] = virt_to_phys(reinterpret_cast<virt_addr>(directory)) | pte_present | pte_write;

    for (usize i = 0; i < entries_per_table; ++i) {
        const phys_addr frame = static_cast<phys_addr>(i) * huge_page_size;
        if (frame >= direct_limit)
            break;
        directory[i] = frame | pte_present | pte_write | pte_huge | pte_no_execute;
    }

    const auto text_start = reinterpret_cast<virt_addr>(__text_start);
    const auto text_end = reinterpret_cast<virt_addr>(__text_end);
    const auto rodata_start = reinterpret_cast<virt_addr>(__rodata_start);
    const auto rodata_end = reinterpret_cast<virt_addr>(__rodata_end);
    const auto data_start = reinterpret_cast<virt_addr>(__data_start);
    const auto data_end = reinterpret_cast<virt_addr>(__data_end);
    const auto image_start = reinterpret_cast<virt_addr>(__kernel_start);

    // The multiboot header and everything before .text is read only data.
    kernel_space.map(image_start, virt_to_phys(image_start), text_start - image_start,
                     PageFlags::NoExecute);
    kernel_space.map(text_start, virt_to_phys(text_start), text_end - text_start,
                     PageFlags::None);
    kernel_space.map(rodata_start, virt_to_phys(rodata_start), rodata_end - rodata_start,
                     PageFlags::NoExecute);
    kernel_space.map(data_start, virt_to_phys(data_start), data_end - data_start,
                     PageFlags::Write | PageFlags::NoExecute);

    kernel_space.activate();
    enable_write_protect();

    arch::unmap_stack_guards();

    pr_info("paging: kernel text %lu KiB read execute, data %lu KiB no execute\n",
            static_cast<u64>((text_end - text_start) / 1024),
            static_cast<u64>((data_end - data_start) / 1024));
}

} // namespace eris::mm
