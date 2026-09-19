// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cmdline.hpp>
#include <eris/compiler.hpp>
#include <eris/console.hpp>
#include <eris/cpu.hpp>
#include <eris/export.hpp>
#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/acpi.hpp>
#include <eris/elf.hpp>
#include <eris/initrd.hpp>
#include <eris/atomic.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/module.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>
#include <eris/serial.hpp>
#include <eris/thread.hpp>
#include <eris/time.hpp>
#include <eris/work.hpp>
#include <eris/version.hpp>

extern "C" void kernel_main(eris::u32 multiboot_magic, eris::u64 multiboot_info);

namespace eris {
namespace {



void print_banner()
{
    printk(LogLevel::Info, "eris %s \"%s\" (%s, %s)\n",
           version_string,
           version_name,
           version_arch,
           version_language);
}

// Recursion the optimiser cannot turn into a loop, so the stack really grows.
// The warning about it is the point of the function.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"
ERIS_NOINLINE u64 overflow_stack(u64 depth)
{
    volatile u8 filler[256];
    filler[0] = static_cast<u8>(depth);

    // Handing the frame address to the compiler as an opaque value stops it
    // from rewriting this into a loop that never grows the stack.
    asm volatile("" : : "r"(&filler[0]) : "memory");

    return filler[0] + overflow_stack(depth + 1);
}
#pragma GCC diagnostic pop

// Drives the panic path on purpose, so the machinery that reports a fault is
// tested rather than assumed. Selected with fault=<kind> on the command line.
void inject_fault(const char* kind)
{
    pr_warn("injecting the %s fault\n", kind);

    if (strcmp(kind, "unmapped") == 0) {
        auto* target = reinterpret_cast<volatile u64*>(0xFFFF800000000000);
        *target = 1;
    } else if (strcmp(kind, "opcode") == 0) {
        asm volatile("ud2");
    } else if (strcmp(kind, "divide") == 0) {
        asm volatile("xor %%edx, %%edx\n"
                     "mov $1, %%eax\n"
                     "xor %%ecx, %%ecx\n"
                     "div %%ecx\n"
                     :
                     :
                     : "eax", "ecx", "edx");
    } else if (strcmp(kind, "doublefault") == 0) {
        // Point the stack at unmapped memory, then fault. The page fault
        // cannot be delivered because pushing its frame faults as well, which
        // is exactly what the double fault stack exists for.
        asm volatile("mov $0xdeadbe000, %rsp\n"
                     "push $0\n");
    } else if (strcmp(kind, "text") == 0) {
        // W^X means this store has to fault even in ring 0, which only holds
        // while CR0.WP is set.
        auto* code = reinterpret_cast<volatile u8*>(&kernel_main);
        *code = 0xCC;
    } else if (strcmp(kind, "rodata") == 0) {
        auto* constant = const_cast<volatile char*>(version_name);
        *constant = 'x';
    } else if (strcmp(kind, "stack") == 0) {
        overflow_stack(0);
    } else if (strcmp(kind, "panic") == 0) {
        panic("fault injection asked for a panic");
    } else {
        pr_err("unknown fault kind %s\n", kind);
    }
}

// Proves the heap really commits new pages instead of living inside whatever
// the first reservation happened to cover.
void heap_selftest()
{
    constexpr usize chunk = 512 * 1024;
    constexpr usize chunks = 8;

    void* blocks[chunks]{};
    const usize before = heap_capacity();

    for (usize i = 0; i < chunks; ++i) {
        blocks[i] = kmalloc(chunk);
        if (blocks[i] == nullptr) {
            pr_err("heap selftest: allocation %lu failed\n", static_cast<u64>(i));
            return;
        }
        memset(blocks[i], 0x5A, chunk);
    }

    const usize peak = heap_capacity();

    for (usize i = 0; i < chunks; ++i)
        kfree(blocks[i]);

    pr_info("heap selftest: %lu KiB committed, grew from %lu to %lu KiB, %lu KiB in use\n",
            static_cast<u64>((peak - before) / 1024),
            static_cast<u64>(before / 1024),
            static_cast<u64>(peak / 1024),
            static_cast<u64>(heap_used() / 1024));
}

constinit volatile bool timer_fired = false;
constinit volatile u64 timer_fired_at = 0;

void mark_fired(void*)
{
    timer_fired = true;
    timer_fired_at = monotonic_ns();
}

// Checks that the clock moves at the rate it claims and that a deadline lands
// where it was asked to.
void time_selftest()
{
    const u64 start = monotonic_ns();
    mdelay(200);
    const u64 elapsed = monotonic_ns() - start;

    pr_info("time selftest: 200 ms delay measured %lu us on the %s clock\n",
            (elapsed + 500) / 1000,
            clock_source());

    const u64 armed = monotonic_ns();
    timer_after(50000000, mark_fired, nullptr);

    while (!timer_fired && monotonic_ns() - armed < 500000000)
        arch::hlt();

    if (!timer_fired) {
        pr_err("time selftest: the 50 ms timer never fired\n");
        return;
    }

    pr_info("time selftest: 50 ms timer fired after %lu us, jiffies at %lu\n",
            (timer_fired_at - armed + 500) / 1000,
            ticks());
}

constinit Atomic<u64> smp_hits{0};

// Every core hammers the same refcount and the same allocator, which is the
// cheapest way to find out whether the locks added in this phase are real.
void smp_worker()
{
    constexpr u64 rounds = 20000;

    for (u64 i = 0; i < rounds; ++i) {
        if (module_get("vga")) {
            module_put("vga");
            smp_hits.fetch_add(1);
        }

        if (void* block = kmalloc(64); block != nullptr)
            kfree(block);
    }
}

void smp_selftest()
{
    const Module* vga = module_find("vga");
    const u32 before = vga != nullptr ? vga->users.value() : 0;

    const bool answered = arch::smp_run_on_others(smp_worker, 5000000000ULL);
    smp_worker();

    const u32 after = vga != nullptr ? vga->users.value() : 0;

    pr_info("smp selftest: %lu cores, %lu refcount round trips, vga users %u then %u%s\n",
            static_cast<u64>(arch::cpu_online_count()),
            smp_hits.load(),
            before,
            after,
            answered ? "" : ", some core never answered");

    if (before != after)
        pr_err("smp selftest: the refcount did not come back to where it started\n");
}

constinit Atomic<u64> worker_rounds{0};
constinit Atomic<u32> workers_finished{0};
constinit WaitQueue gate{};
constinit Atomic<bool> gate_opened{false};

void counting_worker(void* argument)
{
    const auto rounds = reinterpret_cast<u64>(argument);

    for (u64 i = 0; i < rounds; ++i) {
        worker_rounds.fetch_add(1);
        if ((i % 64) == 0)
            yield();
    }

    workers_finished.fetch_add(1);
}

void sleeping_worker(void* argument)
{
    const auto milliseconds = reinterpret_cast<u64>(argument);
    const u64 start = monotonic_ns();

    thread_sleep_ms(milliseconds);

    const u64 slept = (monotonic_ns() - start) / 1000000;
    pr_info("thread selftest: %s asked for %lu ms and slept %lu ms\n",
            current_thread()->name(), milliseconds, slept);

    workers_finished.fetch_add(1);
}

void blocked_worker(void*)
{
    gate.wait();

    if (!gate_opened.load())
        pr_err("thread selftest: a thread woke before the gate opened\n");

    workers_finished.fetch_add(1);
}

// Threads that do nothing prove nothing: these count, sleep and block, and the
// numbers have to add up at the end.
void thread_selftest()
{
    constexpr u64 rounds = 20000;
    constexpr u32 counters = 4;

    workers_finished.store(0);
    worker_rounds.store(0);

    for (u32 i = 0; i < counters; ++i)
        Thread::spawn("counter", counting_worker, reinterpret_cast<void*>(rounds));

    Thread::spawn("sleeper", sleeping_worker, reinterpret_cast<void*>(u64{50}));
    Thread::spawn("waiter", blocked_worker, nullptr);

    const u64 deadline = monotonic_ns() + 3000000000ULL;
    while (workers_finished.load() < counters + 1 && monotonic_ns() < deadline)
        yield();

    gate_opened.store(true);
    gate.wake_all();

    while (workers_finished.load() < counters + 2 && monotonic_ns() < deadline)
        yield();

    pr_info("thread selftest: %u of %u threads finished, %lu rounds counted, %lu threads alive\n",
            workers_finished.load(),
            counters + 2,
            worker_rounds.load(),
            static_cast<u64>(thread_count()));

    if (worker_rounds.load() != rounds * counters)
        pr_err("thread selftest: lost %lu rounds\n", rounds * counters - worker_rounds.load());
}

void report_modules();
void load_initrd_modules();
void module_selftest();
void time_selftest();
void smp_selftest();
void thread_selftest();
void inject_fault(const char* kind);

// The second half of the boot sequence, running as the first kernel thread.
void kernel_init(void*)
{
    if (cmdline_has("timetest"))
        time_selftest();

    module_init_builtin();
    load_initrd_modules();
    report_modules();

    if (cmdline_has("smptest"))
        smp_selftest();

    if (cmdline_has("threadtest"))
        thread_selftest();

    if (cmdline_has("kotest"))
        module_selftest();

    if (cmdline_has("test_exit")) {
        pr_info("selftests finished, leaving\n");
        arch::outb(0xF4, 0x10);
    }

    if (const char* kind = cmdline_value("fault"); kind != nullptr)
        inject_fault(kind);
}

ERIS_NORETURN void idle_loop()
{
    for (;;) {
        work_run_pending();
        yield();
        arch::hlt();
    }
}

bool blacklisted(const char* name)
{
    const char* list = cmdline_value("modules.blacklist");
    if (list == nullptr)
        return false;

    const usize length = strlen(name);
    for (const char* p = list; *p != '\0';) {
        const char* start = p;
        while (*p != '\0' && *p != ',')
            ++p;

        if (static_cast<usize>(p - start) == length && memcmp(start, name, length) == 0)
            return true;

        if (*p == ',')
            ++p;
    }

    return false;
}

// Everything the initrd carries is loaded unless the command line says
// otherwise, and then whatever registered gets its init run.
void load_initrd_modules()
{
    if (!initrd_available() || cmdline_has("modules.noload"))
        return;

    for (usize i = 0; i < initrd_file_count(); ++i) {
        const char* name = nullptr;
        usize length = 0;

        const void* data = initrd_data_at(i, name, length);
        if (data == nullptr || length == 0)
            continue;

        module_load_image(data, length, name);
    }

    for (usize i = 0; i < module_count(); ++i) {
        Module* module = module_at(i);
        if (module == nullptr || module->state != ModuleState::Registered)
            continue;

        if (blacklisted(module->info->name)) {
            pr_info("module %s is blacklisted, left alone\n", module->info->name);
            continue;
        }

        module_load(module->info->name);
    }
}

// Loads, unloads and reloads a module that was never linked into the image,
// then checks the pages came back and that a stale ABI stamp is refused.
void module_selftest()
{
    const char* name = "desktop";

    const char* file = nullptr;
    usize length = 0;
    const void* image = initrd_data_at(0, file, length);

    if (image == nullptr) {
        pr_err("module selftest: the initrd carries nothing to load\n");
        return;
    }

    const Module* module = module_find(name);
    if (module == nullptr || module->state != ModuleState::Ready) {
        pr_err("module selftest: %s did not load from the initrd\n", name);
        return;
    }

    const usize before_unload = mm::free_pages_count();
    if (module_unload(name) != 0) {
        pr_err("module selftest: %s refused to unload\n", name);
        return;
    }

    const usize after_unload = mm::free_pages_count();
    if (module_find(name) != nullptr)
        pr_err("module selftest: %s is still in the table after unloading\n", name);

    pr_info("module selftest: unloading %s returned %lu pages\n",
            name, static_cast<u64>(after_unload - before_unload));

    // A copy with the wrong stamp has to be refused rather than run.
    auto* copy = static_cast<u8*>(kmalloc(length));
    if (copy != nullptr) {
        memcpy(copy, image, length);
        const auto* header = reinterpret_cast<const elf::Header*>(copy);
        const auto* sections = reinterpret_cast<const elf::SectionHeader*>(
            copy + header->section_header_offset);

        for (u16 i = 0; i < header->section_header_count; ++i) {
            if (sections[i].size >= sizeof(ModuleInfo) && sections[i].type == elf::section_progbits
                && sections[i].size % sizeof(ModuleInfo) == 0) {
                auto* candidate = reinterpret_cast<u32*>(copy + sections[i].offset);
                if (*candidate == module_abi_version)
                    *candidate = module_abi_version + 1;
            }
        }

        if (module_load_image(copy, length, "a module from the future") == 0)
            pr_err("module selftest: an image with the wrong abi was accepted\n");
        else
            pr_info("module selftest: an image with the wrong abi was refused\n");

        kfree(copy);
    }

    if (module_load_image(image, length, file) != 0 || module_load(name) != 0) {
        pr_err("module selftest: %s did not come back\n", name);
        return;
    }

    const usize after_reload = mm::free_pages_count();
    pr_info("module selftest: %s reloaded, %lu pages free before and %lu after the round trip\n",
            name,
            static_cast<u64>(before_unload),
            static_cast<u64>(after_reload));
}

void report_memory()
{
    const auto free = mm::free_pages_count();
    pr_info("memory: %lu pages total, %lu free (%lu MiB)\n",
            static_cast<u64>(mm::total_pages()),
            static_cast<u64>(free),
            static_cast<u64>(free * page_size / (1024 * 1024)));
    pr_info("heap: %lu KiB\n", static_cast<u64>(heap_capacity() / 1024));
}

void report_modules()
{
    pr_info("modules: %lu registered, %lu symbols exported\n",
            static_cast<u64>(module_count()),
            static_cast<u64>(symbol_count()));

    for (usize i = 0; i < module_count(); ++i) {
        const Module* module = module_at(i);
        pr_info("  %s %s [%s] dependents=%u users=%u license=%s\n",
                module->info->name,
                module->info->version,
                module_state_name(module->state),
                module->dependents.value(),
                module->users.value(),
                module->info->license);
    }

    if (kernel_tainted())
        pr_warn("kernel is tainted by a non free module\n");
}

} // namespace

void start_kernel(u32 multiboot_magic, u64 multiboot_info)
{
    arch::percpu_setup(0, 0);

    serial_init();
    print_banner();

    cmdline_init(multiboot_magic, multiboot_info);

    arch::gdt_init();
    arch::tss_init();
    arch::idt_init();

    mm::page_alloc_init(multiboot_magic, multiboot_info);
    mm::paging_init();
    mm::heap_init();

    initrd_init(multiboot_magic, multiboot_info);
    acpi::init();
    arch::irq_init();
    clock_init();
    report_memory();

    if (cmdline_has("mmtest"))
        heap_selftest();

    timers_init();
    arch::sti();

    arch::smp_init();
    sched_init();

    // Everything from here runs in a thread, so a module init is allowed to
    // sleep on hardware instead of spinning on it.
    work_start();

    if (Thread::spawn("kinit", kernel_init, nullptr) == nullptr)
        panic("kinit: no thread to finish booting in");

    idle_loop();
}

} // namespace eris

extern "C" void kernel_main(eris::u32 multiboot_magic, eris::u64 multiboot_info)
{
    eris::start_kernel(multiboot_magic, multiboot_info);
}
