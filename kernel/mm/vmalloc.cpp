// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/lock.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>

extern "C" {
extern eris::u8 __kernel_start[];
extern eris::u8 __kernel_end[];
}

namespace eris::mm {
namespace {

// Virtual space above the directly mapped gigabyte. Nothing lives here until
// something asks for it, which is what makes an overrun land in a hole.
constexpr virt_addr vmalloc_base = 4ULL << 30;
constexpr virt_addr vmalloc_end = 8ULL << 30;
constexpr usize max_ranges = 64;

struct Range {
    virt_addr start;
    usize length;
};

constinit Range free_ranges[max_ranges]{};
constinit usize range_count = 0;
constinit bool initialised = false;
constinit IrqSpinLock range_lock{};

void ensure_initialised()
{
    if (initialised)
        return;

    free_ranges[0] = Range{vmalloc_base, vmalloc_end - vmalloc_base};
    range_count = 1;
    initialised = true;
}

usize round_up(usize length)
{
    return (length + page_size - 1) & ~(page_size - 1);
}

} // namespace

// Every reservation carries a guard page after it, so writing past the end of
// one region faults instead of corrupting the next.
virt_addr vmalloc_reserve(usize length)
{
    IrqGuard guard(range_lock);
    ensure_initialised();

    const usize wanted = round_up(length) + page_size;

    for (usize i = 0; i < range_count; ++i) {
        Range& range = free_ranges[i];
        if (range.length < wanted)
            continue;

        const virt_addr address = range.start;
        range.start += wanted;
        range.length -= wanted;

        if (range.length == 0) {
            free_ranges[i] = free_ranges[--range_count];
        }

        return address;
    }

    return 0;
}

void vmalloc_release(virt_addr address, usize length)
{
    IrqGuard guard(range_lock);
    ensure_initialised();

    const usize released = round_up(length) + page_size;

    for (usize i = 0; i < range_count; ++i) {
        Range& range = free_ranges[i];

        if (range.start == address + released) {
            range.start = address;
            range.length += released;
            return;
        }

        if (range.start + range.length == address) {
            range.length += released;
            return;
        }
    }

    if (range_count < max_ranges)
        free_ranges[range_count++] = Range{address, released};
    else
        pr_warn("vmalloc: range table full, %lu KiB leaked\n", static_cast<u64>(released / 1024));
}

void* map_device(phys_addr address, usize length)
{
    const phys_addr aligned = address & ~(static_cast<phys_addr>(page_size) - 1);
    const usize offset = address - aligned;
    const usize span = round_up(length + offset);

    const virt_addr window = vmalloc_reserve(span);
    if (window == 0)
        return nullptr;

    if (!AddressSpace::kernel().map(window, aligned, span,
                                    PageFlags::Write | PageFlags::NoExecute
                                        | PageFlags::NoCache)) {
        vmalloc_release(window, span);
        return nullptr;
    }

    return reinterpret_cast<void*>(window + offset);
}

void unmap_device(void* window, usize length)
{
    if (window == nullptr)
        return;

    const auto address = reinterpret_cast<virt_addr>(window) & ~(virt_addr{page_size} - 1);
    const usize span = round_up(length);

    AddressSpace::kernel().unmap(address, span);
    vmalloc_release(address, span);
}

const char* region_name(virt_addr address)
{
    if (address < page_size)
        return "the null page";
    if (address >= reinterpret_cast<virt_addr>(__kernel_start)
        && address < reinterpret_cast<virt_addr>(__kernel_end))
        return "the kernel image";
    if (address < (4ULL << 30))
        return "the direct map";
    if (address >= vmalloc_base && address < vmalloc_end)
        return "the vmalloc area";

    return "nothing mapped";
}

} // namespace eris::mm
