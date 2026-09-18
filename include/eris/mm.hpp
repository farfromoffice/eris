// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris::mm {

void page_alloc_init(u32 multiboot_magic, u64 multiboot_info);

phys_addr alloc_page();
phys_addr alloc_pages(usize count);
void free_page(phys_addr page);
void free_pages(phys_addr page, usize count);

usize total_pages();
usize free_pages_count();

void heap_init();

} // namespace eris::mm

namespace eris {

void* kmalloc(usize size);
void* kzalloc(usize size);
void kfree(void* ptr);

usize heap_used();
usize heap_capacity();

} // namespace eris
