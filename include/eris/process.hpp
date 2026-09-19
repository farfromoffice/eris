// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/paging.hpp>
#include <eris/types.hpp>

namespace eris {

// User space starts well above anything the kernel maps, so a process address
// space is the kernel one plus a branch of its own.
inline constexpr virt_addr user_base = 0x0000008000000000ULL;
inline constexpr virt_addr user_stack_top = user_base + 0x40000000ULL;
inline constexpr usize user_stack_pages = 16;

enum class ProcessState : u8 {
    Ready,
    Running,
    Exited,
};

class Process {
public:
    static Process* spawn(const char* path);

    u32 id() const { return id_; }
    const char* name() const { return name_; }
    ProcessState state() const { return state_; }
    int exit_code() const { return exit_code_; }

    virt_addr entry() const { return entry_; }
    u64 pages() const { return page_count_; }

    // True while the address the program handed the kernel is one it owns.
    bool owns(virt_addr address, usize length) const;

    void exit(int code);

private:
    friend class ProcessTable;

    static constexpr usize name_length = 24;

    char name_[name_length]{};
    u32 id_ = 0;
    ProcessState state_ = ProcessState::Ready;
    int exit_code_ = 0;

    mm::AddressSpace space_{};
    virt_addr entry_ = 0;
    virt_addr brk_ = 0;
    u64 page_count_ = 0;
};

void syscall_init();

Process* current_process();
usize process_count();
Process* process_at(usize index);

// Runs a program from the file system and waits for it to leave.
int process_run(const char* path, int& exit_code);

} // namespace eris
