// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris::arch {

inline constexpr u8 ist_double_fault = 1;
inline constexpr u8 ist_nmi = 2;
inline constexpr u8 ist_machine_check = 3;

inline constexpr usize exception_stack_size = 16 * page_size;

void exception_stacks_init();
void tss_init();
void gdt_load_tss(u64 base, u32 limit);
void tss_set_kernel_stack(virt_addr stack_top);

virt_addr exception_stack_top(u8 ist_index);

void unmap_stack_guards();
const char* guard_page_owner(virt_addr address);

bool stack_guards_intact();
const char* overflowed_stack_name();

} // namespace eris::arch
