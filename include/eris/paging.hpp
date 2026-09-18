// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris::mm {

enum class PageFlags : u64 {
    None = 0,
    Write = 1 << 0,
    User = 1 << 1,
    NoExecute = 1 << 2,
    Global = 1 << 3,
    NoCache = 1 << 4,
};

constexpr PageFlags operator|(PageFlags a, PageFlags b)
{
    return static_cast<PageFlags>(static_cast<u64>(a) | static_cast<u64>(b));
}

constexpr bool has(PageFlags set, PageFlags flag)
{
    return (static_cast<u64>(set) & static_cast<u64>(flag)) != 0;
}

// The kernel still runs identity mapped over the first gigabyte, so these two
// are the identity. They exist so the move to a higher half is one edit here
// rather than a hunt through the tree.
constexpr virt_addr phys_to_virt(phys_addr address)
{
    return address;
}

constexpr phys_addr virt_to_phys(virt_addr address)
{
    return address;
}

class AddressSpace {
public:
    static AddressSpace& kernel();

    bool map(virt_addr address, phys_addr frame, usize length, PageFlags flags);
    bool unmap(virt_addr address, usize length);
    bool protect(virt_addr address, usize length, PageFlags flags);

    phys_addr translate(virt_addr address) const;
    bool mapped(virt_addr address) const;

    void activate() const;
    phys_addr root() const { return root_; }

    void adopt(phys_addr root) { root_ = root; }

private:
    u64* table_for(virt_addr address, bool create);
    bool split_huge_page(u64* directory_entry, virt_addr address);

    phys_addr root_ = 0;
};

void paging_init();

// Virtual space handed out for the heap, module images and device windows.
virt_addr vmalloc_reserve(usize length);
void vmalloc_release(virt_addr address, usize length);

void* map_device(phys_addr address, usize length);
void unmap_device(void* window, usize length);

const char* region_name(virt_addr address);

} // namespace eris::mm
