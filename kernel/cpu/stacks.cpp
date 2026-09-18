// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cpu.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

extern "C" eris::u8 boot_stack_guard[];

namespace eris::arch {
namespace {

// A stack grows down into its guard page, so the guard sits at the low end.
// Until the page tables can unmap it, the guard is a poison pattern, and an
// overflow is caught by noticing the pattern is gone instead of by a fault.
struct alignas(page_size) GuardedStack {
    u8 guard[page_size];
    u8 space[exception_stack_size];
};

constexpr u8 guard_pattern = 0xA5;
constexpr usize exception_stacks = 3;

constinit GuardedStack stacks[exception_stacks]{};

constexpr const char* stack_names[exception_stacks + 1] = {
    "double fault",
    "nmi",
    "machine check",
    "kernel",
};

u8* guard_of(usize index)
{
    return index < exception_stacks ? stacks[index].guard : boot_stack_guard;
}

constinit bool guards_unmapped = false;

bool guard_intact(usize index)
{
    if (guards_unmapped)
        return true;

    const u8* guard = guard_of(index);
    for (usize i = 0; i < page_size; ++i) {
        if (guard[i] != guard_pattern)
            return false;
    }
    return true;
}

} // namespace

void exception_stacks_init()
{
    for (usize i = 0; i <= exception_stacks; ++i)
        memset(guard_of(i), guard_pattern, page_size);
}

virt_addr exception_stack_top(u8 ist_index)
{
    if (ist_index == 0 || ist_index > exception_stacks)
        return 0;

    auto& stack = stacks[ist_index - 1];
    return reinterpret_cast<virt_addr>(stack.space) + sizeof(stack.space);
}

bool stack_guards_intact()
{
    for (usize i = 0; i <= exception_stacks; ++i) {
        if (!guard_intact(i))
            return false;
    }
    return true;
}

// Once the page tables are ours the guards stop being a pattern to check and
// become holes, so an overflow faults on the instruction that caused it.
void unmap_stack_guards()
{
    for (usize i = 0; i <= exception_stacks; ++i) {
        const auto address = reinterpret_cast<virt_addr>(guard_of(i));
        mm::AddressSpace::kernel().unmap(address, page_size);
    }

    guards_unmapped = true;
}

const char* guard_page_owner(virt_addr address)
{
    const virt_addr page = address & ~(virt_addr{page_size} - 1);

    for (usize i = 0; i <= exception_stacks; ++i) {
        if (reinterpret_cast<virt_addr>(guard_of(i)) == page)
            return stack_names[i];
    }

    return nullptr;
}

const char* overflowed_stack_name()
{
    for (usize i = 0; i <= exception_stacks; ++i) {
        if (!guard_intact(i))
            return stack_names[i];
    }
    return nullptr;
}

} // namespace eris::arch
