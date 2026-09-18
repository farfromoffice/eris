// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cmdline.hpp>
#include <eris/compiler.hpp>
#include <eris/console.hpp>
#include <eris/cpu.hpp>
#include <eris/export.hpp>
#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/module.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>
#include <eris/serial.hpp>
#include <eris/time.hpp>
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
        pr_info("  %s %s [%s] refs=%u license=%s\n",
                module->info->name,
                module->info->version,
                module_state_name(module->state),
                module->refcount,
                module->info->license);
    }

    if (kernel_tainted())
        pr_warn("kernel is tainted by a non free module\n");
}

} // namespace

void start_kernel(u32 multiboot_magic, u64 multiboot_info)
{
    serial_init();
    print_banner();

    cmdline_init(multiboot_magic, multiboot_info);

    arch::gdt_init();
    arch::tss_init();
    arch::idt_init();
    arch::pic_init();

    mm::page_alloc_init(multiboot_magic, multiboot_info);
    mm::paging_init();
    mm::heap_init();
    report_memory();

    if (cmdline_has("mmtest"))
        heap_selftest();

    timer_init(100);
    arch::sti();

    module_init_builtin();
    report_modules();

    if (const char* kind = cmdline_value("fault"); kind != nullptr)
        inject_fault(kind);

    for (;;)
        arch::hlt();
}

} // namespace eris

extern "C" void kernel_main(eris::u32 multiboot_magic, eris::u64 multiboot_info)
{
    eris::start_kernel(multiboot_magic, multiboot_info);
}
