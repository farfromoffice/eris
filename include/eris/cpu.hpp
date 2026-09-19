// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/compiler.hpp>
#include <eris/types.hpp>

namespace eris::arch {

inline constexpr u8 ist_double_fault = 1;
inline constexpr u8 ist_nmi = 2;
inline constexpr u8 ist_machine_check = 3;

inline constexpr usize exception_stack_size = 16 * page_size;
inline constexpr usize max_cpus = 8;

struct ERIS_PACKED Tss {
    u32 reserved0;
    u64 rsp[3];
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
};

// Everything one CPU owns. Reached through gs, so a handler never has to work
// out which core it is running on.
struct PerCpu {
    PerCpu* self;
    u32 index;
    u32 apic_id;
    bool online;
    virt_addr kernel_stack_top;
    u64 interrupts;
    void* current_thread;
    void* idle_thread;
    u32 preempt_count;
    u64 gdt[7];
    Tss tss;
};

PerCpu& percpu_setup(u32 index, u32 apic_id);
PerCpu& this_cpu();
PerCpu* cpu_at(usize index);
usize cpu_online_count();

void gdt_init();
void gdt_init_cpu(PerCpu& cpu);

void tss_init();
void tss_init_cpu(PerCpu& cpu);
void tss_set_kernel_stack(virt_addr stack_top);

void exception_stacks_init();
virt_addr exception_stack_top(u8 ist_index);

void unmap_stack_guards();
const char* guard_page_owner(virt_addr address);

bool stack_guards_intact();
const char* overflowed_stack_name();

// Brings up every application processor the firmware reported.
void smp_init();
void smp_halt_others();
bool smp_halting();

using SmpJob = void (*)();

// Runs a function on every application processor and waits for them to finish.
bool smp_run_on_others(SmpJob job, u64 timeout_ns);

} // namespace eris::arch
