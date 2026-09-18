// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cpu.hpp>
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

// Only the end nearest the stack is checked on the hot path, because that is
// the part an overflow reaches first.
constexpr usize sentinel_size = 64;

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

bool sentinel_intact(usize index)
{
    const u8* sentinel = guard_of(index) + page_size - sentinel_size;
    for (usize i = 0; i < sentinel_size; ++i) {
        if (sentinel[i] != guard_pattern)
            return false;
    }
    return true;
}

bool guard_intact(usize index)
{
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

bool stack_sentinels_intact()
{
    for (usize i = 0; i <= exception_stacks; ++i) {
        if (!sentinel_intact(i))
            return false;
    }
    return true;
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
