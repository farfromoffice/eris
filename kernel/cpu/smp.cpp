// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/acpi.hpp>
#include <eris/apic.hpp>
#include <eris/atomic.hpp>
#include <eris/cpu.hpp>
#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>
#include <eris/process.hpp>
#include <eris/string.hpp>
#include <eris/thread.hpp>
#include <eris/time.hpp>

extern "C" {
extern const eris::u8 _binary_build_trampoline_bin_start[];
extern const eris::u8 _binary_build_trampoline_bin_end[];
}

namespace eris::arch {
namespace {

constexpr phys_addr trampoline_page = 0x8000;
constexpr usize data_offset = 0xF00;
constexpr usize ap_stack_pages = 8;

constinit Atomic<u32> cpus_online{1};
constinit Atomic<u64> job_generation{0};
constinit Atomic<u32> job_finished{0};
constinit SmpJob pending_job = nullptr;
constinit Atomic<bool> halting{false};

u64* trampoline_data(usize slot)
{
    const auto base = mm::phys_to_virt(trampoline_page) + data_offset;
    return reinterpret_cast<u64*>(base) + slot;
}

void install_trampoline()
{
    const usize length = static_cast<usize>(_binary_build_trampoline_bin_end
                                            - _binary_build_trampoline_bin_start);

    // The page has to be executable for the few instructions an application
    // processor runs there, and it goes back to being data afterwards.
    mm::AddressSpace::kernel().protect(mm::phys_to_virt(trampoline_page), page_size,
                                       mm::PageFlags::Write);

    memcpy(reinterpret_cast<void*>(mm::phys_to_virt(trampoline_page)),
           _binary_build_trampoline_bin_start,
           length < page_size ? length : page_size);
}

void retire_trampoline()
{
    mm::AddressSpace::kernel().protect(mm::phys_to_virt(trampoline_page), page_size,
                                       mm::PageFlags::Write | mm::PageFlags::NoExecute);
}

virt_addr allocate_stack()
{
    const phys_addr frames = mm::alloc_pages(ap_stack_pages);
    if (frames == 0)
        return 0;

    return mm::phys_to_virt(frames) + ap_stack_pages * page_size;
}

} // namespace

// Where an application processor lands once it is in long mode on the kernel
// page tables. Runs on its own stack with its own descriptors.
extern "C" void ap_entry()
{
    // Copy the identity out of the shared page before telling the boot CPU it
    // may hand the slots to the next core.
    const auto index = static_cast<u32>(*trampoline_data(4));
    const auto apic_id = static_cast<u32>(*trampoline_data(5));

    memory_barrier();
    *trampoline_data(6) = 1;

    PerCpu& cpu = percpu_setup(index, apic_id);

    // The stack the boot CPU handed this core is also the one an interrupt
    // from ring 3 has to land on until a thread brings its own.
    cpu.kernel_stack_top = *trampoline_data(1);

    tss_init_cpu(cpu);
    gdt_init_cpu(cpu);
    idt_init();
    lapic_init_cpu();
    syscall_init_cpu();

    sched_start_cpu();

    cpus_online.fetch_add(1);
    cpu.online = true;

    sti();

    u64 seen = job_generation.load();

    for (;;) {
        const u64 generation = job_generation.load();
        if (generation != seen) {
            seen = generation;
            SmpJob job = pending_job;
            if (job != nullptr)
                job();
            job_finished.fetch_add(1);
        }

        yield();
        cpu_relax();
    }
}

void smp_init()
{
    const usize count = acpi::cpu_count();
    if (count <= 1 || !lapic_present()) {
        pr_info("smp: one cpu\n");
        return;
    }

    install_trampoline();

    *trampoline_data(0) = mm::AddressSpace::kernel().root();

    usize started = 1;

    for (usize i = 1; i < count && i < max_cpus; ++i) {
        const u32 apic_id = acpi::cpu_apic_id(i);
        if (apic_id == lapic_id())
            continue;

        const virt_addr stack = allocate_stack();
        if (stack == 0) {
            pr_warn("smp: no stack for cpu %lu\n", static_cast<u64>(i));
            break;
        }

        *trampoline_data(1) = stack;
        *trampoline_data(2) = reinterpret_cast<u64>(&ap_entry);
        *trampoline_data(3) = 0;
        *trampoline_data(4) = i;
        *trampoline_data(5) = apic_id;
        *trampoline_data(6) = 0;

        lapic_send_init(apic_id);
        mdelay(10);
        lapic_send_startup(apic_id, static_cast<u8>(trampoline_page >> 12));
        mdelay(1);

        if (*trampoline_data(3) == 0) {
            lapic_send_startup(apic_id, static_cast<u8>(trampoline_page >> 12));
            mdelay(10);
        }

        const u64 deadline = monotonic_ns() + 100000000;
        while (*trampoline_data(6) == 0 && monotonic_ns() < deadline)
            cpu_relax();

        if (*trampoline_data(6) == 0) {
            pr_warn("smp: cpu %lu did not answer the startup\n", static_cast<u64>(i));
            continue;
        }

        ++started;
    }

    const u64 deadline = monotonic_ns() + 200000000;
    while (cpus_online.load() < started && monotonic_ns() < deadline)
        cpu_relax();

    retire_trampoline();

    pr_info("smp: %u of %lu cpus online\n",
            cpus_online.load(),
            static_cast<u64>(count));
}

bool smp_run_on_others(SmpJob job, u64 timeout_ns)
{
    const u32 others = static_cast<u32>(cpu_online_count()) - 1;
    if (others == 0 || job == nullptr)
        return true;

    job_finished.store(0);
    pending_job = job;
    memory_barrier();
    job_generation.fetch_add(1);

    const u64 deadline = monotonic_ns() + timeout_ns;
    while (job_finished.load() < others && monotonic_ns() < deadline)
        cpu_relax();

    return job_finished.load() >= others;
}

void smp_halt_others()
{
    halting.store(true);

    if (lapic_present())
        lapic_broadcast_nmi();
}

// The other cores answer the halt with an NMI, which is a stop rather than a
// second panic to interleave with the first.
bool smp_halting()
{
    return halting.load();
}

} // namespace eris::arch
