// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/backtrace.hpp>
#include <eris/ksyms.hpp>
#include <eris/printk.hpp>

extern "C" eris::u8 __kernel_end[];

namespace eris {
namespace {

struct Frame {
    const Frame* previous;
    u64 return_address;
};

constexpr u64 image_start = 1 << 20;
constexpr u64 mapped_limit = 4ULL << 30;
constexpr usize max_frames = 24;

bool plausible_text(u64 address)
{
    return address >= image_start && address < reinterpret_cast<u64>(__kernel_end);
}

bool plausible_frame(const Frame* frame)
{
    const auto address = reinterpret_cast<u64>(frame);
    return address >= image_start && address + sizeof(Frame) < mapped_limit
        && (address & 0x7) == 0;
}

} // namespace

void print_symbol(u64 address)
{
    u64 offset = 0;
    const char* name = ksyms_lookup(address, offset);

    if (name != nullptr)
        printk(LogLevel::Error, "  [<%lx>] %s+0x%lx\n", address, name, offset);
    else
        printk(LogLevel::Error, "  [<%lx>] ?\n", address);
}

void backtrace_from(u64 rip, u64 rbp)
{
    console_write("call trace:\n");

    if (plausible_text(rip))
        print_symbol(rip);

    const auto* frame = reinterpret_cast<const Frame*>(rbp);

    for (usize depth = 0; depth < max_frames; ++depth) {
        if (!plausible_frame(frame))
            break;

        const u64 return_address = frame->return_address;
        if (!plausible_text(return_address))
            break;

        print_symbol(return_address);

        const Frame* previous = frame->previous;
        if (previous <= frame)
            break;

        frame = previous;
    }
}

void backtrace()
{
    u64 rbp = 0;
    asm volatile("mov %%rbp, %0" : "=r"(rbp));
    backtrace_from(reinterpret_cast<u64>(__builtin_return_address(0)), rbp);
}

} // namespace eris
