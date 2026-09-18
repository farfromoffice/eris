// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cpu.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/panic.hpp>
#include <eris/string.hpp>

namespace eris::arch {
namespace {

constexpr u32 gs_base_msr = 0xC0000101;
constexpr u32 kernel_gs_base_msr = 0xC0000102;

constinit PerCpu cpus[max_cpus]{};
constinit usize online = 0;
constinit bool gs_ready = false;

void write_msr(u32 msr, u64 value)
{
    asm volatile("wrmsr"
                 :
                 : "a"(static_cast<u32>(value)),
                   "d"(static_cast<u32>(value >> 32)),
                   "c"(msr));
}

} // namespace

PerCpu& percpu_setup(u32 index, u32 apic_id)
{
    if (index >= max_cpus)
        panic("percpu: cpu index %u is past the %lu the kernel keeps room for",
              index, static_cast<u64>(max_cpus));

    PerCpu& cpu = cpus[index];
    memset(&cpu, 0, sizeof(cpu));

    cpu.self = &cpu;
    cpu.index = index;
    cpu.apic_id = apic_id;
    cpu.online = true;

    // Both bases point at the same block, so a swapgs on the way in or out of
    // a future ring 3 entry lands somewhere sane either way.
    write_msr(gs_base_msr, reinterpret_cast<u64>(&cpu));
    write_msr(kernel_gs_base_msr, reinterpret_cast<u64>(&cpu));
    gs_ready = true;

    ++online;
    return cpu;
}

PerCpu& this_cpu()
{
    // Before the first setup gs still points at whatever the firmware left at
    // address zero, and following that is a general protection fault.
    if (!gs_ready)
        return cpus[0];

    PerCpu* cpu = nullptr;
    asm volatile("mov %%gs:0, %0" : "=r"(cpu));
    return cpu != nullptr ? *cpu : cpus[0];
}

PerCpu* cpu_at(usize index)
{
    return index < max_cpus && cpus[index].online ? &cpus[index] : nullptr;
}

usize cpu_online_count()
{
    return online == 0 ? 1 : online;
}

} // namespace eris::arch
