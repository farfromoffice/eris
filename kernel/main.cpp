// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/console.hpp>
#include <eris/export.hpp>
#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/mm.hpp>
#include <eris/module.hpp>
#include <eris/printk.hpp>
#include <eris/serial.hpp>
#include <eris/time.hpp>

namespace eris {
namespace {

constexpr const char* banner = "eris kernel 0.1 (x86_64, c++23)\n";

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
    console_write(banner);

    arch::gdt_init();
    arch::idt_init();
    arch::pic_init();

    mm::page_alloc_init(multiboot_magic, multiboot_info);
    mm::heap_init();
    report_memory();

    timer_init(100);
    arch::sti();

    module_init_builtin();
    report_modules();

    for (;;)
        arch::hlt();
}

} // namespace eris

extern "C" void kernel_main(eris::u32 multiboot_magic, eris::u64 multiboot_info)
{
    eris::start_kernel(multiboot_magic, multiboot_info);
}
