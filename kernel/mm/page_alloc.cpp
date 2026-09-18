// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/compiler.hpp>
#include <eris/mm.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

extern "C" eris::u8 __kernel_end[];

namespace eris::mm {
namespace {

struct ERIS_PACKED MultibootInfo {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    u32 syms[4];
    u32 mmap_length;
    u32 mmap_addr;
};

struct ERIS_PACKED MultibootMmapEntry {
    u32 size;
    u64 addr;
    u64 len;
    u32 type;
};

constexpr u32 multiboot_bootloader_magic = 0x2BADB002;
constexpr u32 mmap_type_available = 1;
constexpr phys_addr managed_limit = 1ULL << 30;

constinit u8* bitmap = nullptr;
constinit usize bitmap_pages = 0;
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
    if (index < bitmap_pages && !test_bit(index)) {
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
    for (usize i = first; i < last && i < bitmap_pages; ++i) {
        if (test_bit(i)) {
            clear_bit(i);
            --used_pages;
        }
    }
}

} // namespace

void page_alloc_init(u32 multiboot_magic, u64 multiboot_info)
{
    if (multiboot_magic != multiboot_bootloader_magic)
        panic("not booted by a multiboot loader (magic=%x)", multiboot_magic);

    const auto* info = reinterpret_cast<const MultibootInfo*>(multiboot_info);
    if ((info->flags & (1u << 6)) == 0)
        panic("bootloader gave us no memory map");

    bitmap_pages = managed_limit / page_size;
    bitmap = reinterpret_cast<u8*>((reinterpret_cast<u64>(__kernel_end) + page_size - 1)
                                   & ~(static_cast<u64>(page_size) - 1));

    const usize bitmap_bytes = bitmap_pages / 8;
    memset(bitmap, 0xFF, bitmap_bytes);
    used_pages = bitmap_pages;

    const auto mmap_start = static_cast<u64>(info->mmap_addr);
    const auto mmap_end = mmap_start + info->mmap_length;

    for (u64 cursor = mmap_start; cursor < mmap_end;) {
        const auto* entry = reinterpret_cast<const MultibootMmapEntry*>(cursor);
        if (entry->type == mmap_type_available) {
            const phys_addr start = entry->addr;
            const phys_addr end = entry->addr + entry->len;
            if (start < managed_limit)
                mark_range_free(start, end < managed_limit ? end : managed_limit);
        }
        cursor += entry->size + sizeof(entry->size);
    }

    mark_range_used(0, 1 << 20);
    mark_range_used(1 << 20, reinterpret_cast<u64>(bitmap) + bitmap_bytes);
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
    for (usize i = 0; i < bitmap_pages; ++i) {
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
    for (usize i = first; i < first + count && i < bitmap_pages; ++i) {
        if (test_bit(i)) {
            clear_bit(i);
            --used_pages;
        }
    }
}

usize total_pages()
{
    return bitmap_pages;
}

usize free_pages_count()
{
    return bitmap_pages - used_pages;
}

} // namespace eris::mm
