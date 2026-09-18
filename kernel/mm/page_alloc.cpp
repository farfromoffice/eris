// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/compiler.hpp>
#include <eris/mm.hpp>
#include <eris/multiboot.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

extern "C" eris::u8 __kernel_end[];

namespace eris::mm {
namespace {

constexpr phys_addr managed_limit = 1ULL << 30;
constexpr usize managed_pages = managed_limit / page_size;

constinit u8 bitmap[managed_pages / 8]{};
constinit usize used_pages = 0;

bool test_bit(usize index)
{
    return (bitmap[index / 8] & (1u << (index % 8))) != 0;
}

void set_bit(usize index)
{
    bitmap[index / 8] |= static_cast<u8>(1u << (index % 8));
}

void clear_bit(usize index)
{
    bitmap[index / 8] &= static_cast<u8>(~(1u << (index % 8)));
}

void mark_used(usize index)
{
    if (index < managed_pages && !test_bit(index)) {
        set_bit(index);
        ++used_pages;
    }
}

void mark_range_used(phys_addr start, phys_addr end)
{
    const usize first = start / page_size;
    const usize last = (end + page_size - 1) / page_size;
    for (usize i = first; i < last; ++i)
        mark_used(i);
}

void mark_range_free(phys_addr start, phys_addr end)
{
    const usize first = (start + page_size - 1) / page_size;
    const usize last = end / page_size;
    for (usize i = first; i < last && i < managed_pages; ++i) {
        if (test_bit(i)) {
            clear_bit(i);
            --used_pages;
        }
    }
}

void reserve_string(phys_addr address)
{
    const auto* s = reinterpret_cast<const char*>(address);
    mark_range_used(address, address + strlen(s) + 1);
}

// Keep everything the bootloader handed us out of the allocator. The
// command line and modules stay intact for whoever consumes them later.
void reserve_boot_data(phys_addr info_address, const MultibootInfo& info)
{
    mark_range_used(info_address, info_address + sizeof(info));
    mark_range_used(info.mmap_addr, phys_addr{info.mmap_addr} + info.mmap_length);

    if (info.flags & multiboot_flag_cmdline)
        reserve_string(info.cmdline);

    if (info.flags & multiboot_flag_mods) {
        const auto* mods = reinterpret_cast<const MultibootModule*>(phys_addr{info.mods_addr});
        mark_range_used(info.mods_addr,
                        phys_addr{info.mods_addr} + info.mods_count * sizeof(*mods));

        for (u32 i = 0; i < info.mods_count; ++i) {
            mark_range_used(mods[i].mod_start, mods[i].mod_end);
            if (mods[i].string != 0)
                reserve_string(mods[i].string);
        }
    }
}

} // namespace

void page_alloc_init(u32 multiboot_magic, u64 multiboot_info)
{
    if (multiboot_magic != multiboot_bootloader_magic)
        panic("not booted by a multiboot loader (magic=%x)", multiboot_magic);

    const auto* info = reinterpret_cast<const MultibootInfo*>(multiboot_info);
    if ((info->flags & multiboot_flag_mmap) == 0)
        panic("bootloader gave us no memory map");

    memset(bitmap, 0xFF, sizeof(bitmap));
    used_pages = managed_pages;

    const auto mmap_start = static_cast<u64>(info->mmap_addr);
    const auto mmap_end = mmap_start + info->mmap_length;

    for (u64 cursor = mmap_start; cursor < mmap_end;) {
        const auto* entry = reinterpret_cast<const MultibootMmapEntry*>(cursor);
        if (entry->type == multiboot_mmap_type_available) {
            const phys_addr start = entry->addr;
            const phys_addr end = entry->addr + entry->len;
            if (start < managed_limit)
                mark_range_free(start, end < managed_limit ? end : managed_limit);
        }
        cursor += entry->size + sizeof(entry->size);
    }

    mark_range_used(0, 1 << 20);
    mark_range_used(1 << 20, reinterpret_cast<u64>(__kernel_end));
    reserve_boot_data(multiboot_info, *info);
}

phys_addr alloc_page()
{
    return alloc_pages(1);
}

phys_addr alloc_pages(usize count)
{
    if (count == 0)
        return 0;

    usize run = 0;
    for (usize i = 0; i < managed_pages; ++i) {
        if (test_bit(i)) {
            run = 0;
            continue;
        }

        ++run;
        if (run < count)
            continue;

        const usize first = i + 1 - count;
        for (usize j = first; j <= i; ++j)
            mark_used(j);
        return static_cast<phys_addr>(first) * page_size;
    }

    return 0;
}

void free_page(phys_addr page)
{
    free_pages(page, 1);
}

void free_pages(phys_addr page, usize count)
{
    const usize first = page / page_size;
    for (usize i = first; i < first + count && i < managed_pages; ++i) {
        if (test_bit(i)) {
            clear_bit(i);
            --used_pages;
        }
    }
}

usize total_pages()
{
    return managed_pages;
}

usize free_pages_count()
{
    return managed_pages - used_pages;
}

} // namespace eris::mm
