// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/console.hpp>
#include <eris/cpu.hpp>
#include <eris/printk.hpp>
#include <eris/process.hpp>
#include <eris/string.hpp>
#include <eris/thread.hpp>
#include <eris/time.hpp>
#include <eris/vfs.hpp>

extern "C" void syscall_entry();
extern "C" void leave_user_mode();

namespace eris {
namespace {

constexpr u32 msr_efer = 0xC0000080;
constexpr u32 msr_star = 0xC0000081;
constexpr u32 msr_lstar = 0xC0000082;
constexpr u32 msr_sfmask = 0xC0000084;

constexpr u64 efer_syscall_enable = 1;

enum class Call : u64 {
    Exit = 0,
    Write = 1,
    Read = 2,
    GetPid = 3,
    Sleep = 4,
    Open = 5,
    Close = 6,
    Time = 7,
};

constexpr usize max_open_files = 8;
constexpr usize max_user_string = 1024;

struct OpenFile {
    bool used;
    u64 offset;
    char path[128];
};

constinit OpenFile open_files[max_open_files]{};

u64 read_msr(u32 msr)
{
    u32 low, high;
    asm volatile("rdmsr" : "=a"(low), "=d"(high) : "c"(msr));
    return (static_cast<u64>(high) << 32) | low;
}

void write_msr(u32 msr, u64 value)
{
    asm volatile("wrmsr"
                 :
                 : "a"(static_cast<u32>(value)),
                   "d"(static_cast<u32>(value >> 32)),
                   "c"(msr));
}

// Anything a program hands the kernel is a claim until the address space says
// otherwise.
bool user_range_ok(u64 address, usize length)
{
    Process* process = current_process();
    return process != nullptr && length <= max_user_string && process->owns(address, length);
}

i64 sys_write(u64 handle, u64 buffer, u64 length)
{
    if (!user_range_ok(buffer, length))
        return -1;

    const auto* text = reinterpret_cast<const char*>(buffer);

    if (handle == 1 || handle == 2) {
        for (u64 i = 0; i < length; ++i)
            console_put(text[i]);
        return static_cast<i64>(length);
    }

    const usize index = static_cast<usize>(handle) - 3;
    if (index >= max_open_files || !open_files[index].used)
        return -1;

    const isize written = fs::write_file(open_files[index].path, text, length);
    return written;
}

i64 sys_open(u64 path, u64)
{
    if (!user_range_ok(path, 1))
        return -1;

    const auto* name = reinterpret_cast<const char*>(path);

    fs::Stat status{};
    if (fs::stat(name, status) != 0)
        return -1;

    for (usize i = 0; i < max_open_files; ++i) {
        if (open_files[i].used)
            continue;

        usize length = 0;
        while (length + 1 < sizeof(open_files[i].path) && name[length] != '\0') {
            open_files[i].path[length] = name[length];
            ++length;
        }
        open_files[i].path[length] = '\0';
        open_files[i].offset = 0;
        open_files[i].used = true;

        return static_cast<i64>(i + 3);
    }

    return -1;
}

i64 sys_read(u64 handle, u64 buffer, u64 length)
{
    if (!user_range_ok(buffer, length))
        return -1;

    const usize index = static_cast<usize>(handle) - 3;
    if (handle < 3 || index >= max_open_files || !open_files[index].used)
        return -1;

    fs::Inode* node = fs::resolve(open_files[index].path);
    if (node == nullptr)
        return -1;

    const isize read = node->read(open_files[index].offset,
                                  reinterpret_cast<void*>(buffer), length);
    if (read > 0)
        open_files[index].offset += static_cast<u64>(read);

    return read;
}

i64 sys_close(u64 handle)
{
    const usize index = static_cast<usize>(handle) - 3;
    if (handle < 3 || index >= max_open_files || !open_files[index].used)
        return -1;

    open_files[index].used = false;
    return 0;
}

} // namespace

struct SyscallFrame {
    u64 number;
    u64 first;
    u64 second;
    u64 third;
    u64 fourth;
    u64 fifth;
    u64 sixth;
};

extern "C" void syscall_dispatch(SyscallFrame* frame)
{
    switch (static_cast<Call>(frame->number)) {
    case Call::Exit: {
        Process* process = current_process();
        if (process != nullptr)
            process->exit(static_cast<int>(frame->first));

        // Everything the program owned stays until the kernel tears it down,
        // and the kernel resumes where it dropped into ring 3.
        leave_user_mode();
        break;
    }
    case Call::Write:
        frame->number = static_cast<u64>(sys_write(frame->first, frame->second, frame->third));
        break;
    case Call::Read:
        frame->number = static_cast<u64>(sys_read(frame->first, frame->second, frame->third));
        break;
    case Call::GetPid: {
        Process* process = current_process();
        frame->number = process != nullptr ? process->id() : 0;
        break;
    }
    case Call::Sleep:
        thread_sleep_ms(frame->first);
        frame->number = 0;
        break;
    case Call::Open:
        frame->number = static_cast<u64>(sys_open(frame->first, frame->second));
        break;
    case Call::Close:
        frame->number = static_cast<u64>(sys_close(frame->first));
        break;
    case Call::Time:
        frame->number = monotonic_ns();
        break;
    default:
        pr_warn("syscall: %lu is not a call this kernel answers\n", frame->number);
        frame->number = static_cast<u64>(-1);
        break;
    }
}

void syscall_init()
{
    write_msr(msr_efer, read_msr(msr_efer) | efer_syscall_enable);

    // sysret takes the user selectors from the second base, which is why the
    // GDT puts user data directly before user code.
    const u64 star = (static_cast<u64>(arch::selector_user_base) << 48)
        | (static_cast<u64>(arch::selector_kernel_code) << 32);

    write_msr(msr_star, star);
    write_msr(msr_lstar, reinterpret_cast<u64>(&syscall_entry));
    write_msr(msr_sfmask, 1ULL << 9); // interrupts off while entering

    pr_info("syscall: entry at %p\n", reinterpret_cast<void*>(&syscall_entry));
}

} // namespace eris
