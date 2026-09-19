// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/backtrace.hpp>
#include <eris/cmdline.hpp>
#include <eris/compiler.hpp>
#include <eris/console.hpp>
#include <eris/cpu.hpp>
#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/module.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>
#include <eris/thread.hpp>

namespace eris {
namespace {

u64 read_cr0()
{
    u64 value;
    asm volatile("mov %%cr0, %0" : "=r"(value));
    return value;
}

u64 read_cr2()
{
    u64 value;
    asm volatile("mov %%cr2, %0" : "=r"(value));
    return value;
}

u64 read_cr3()
{
    u64 value;
    asm volatile("mov %%cr3, %0" : "=r"(value));
    return value;
}

u64 read_cr4()
{
    u64 value;
    asm volatile("mov %%cr4, %0" : "=r"(value));
    return value;
}

void dump_registers(const arch::Registers& regs)
{
    pr_err("rip %lx  cs %lx  rflags %lx\n", regs.rip, regs.cs, regs.rflags);
    pr_err("rsp %lx  ss %lx  error %lx\n", regs.rsp, regs.ss, regs.error_code);
    pr_err("rax %lx  rbx %lx  rcx %lx  rdx %lx\n", regs.rax, regs.rbx, regs.rcx, regs.rdx);
    pr_err("rsi %lx  rdi %lx  rbp %lx\n", regs.rsi, regs.rdi, regs.rbp);
    pr_err("r8  %lx  r9  %lx  r10 %lx  r11 %lx\n", regs.r8, regs.r9, regs.r10, regs.r11);
    pr_err("r12 %lx  r13 %lx  r14 %lx  r15 %lx\n", regs.r12, regs.r13, regs.r14, regs.r15);
}

void dump_control_registers()
{
    pr_err("cr0 %lx  cr2 %lx  cr3 %lx  cr4 %lx\n",
           read_cr0(), read_cr2(), read_cr3(), read_cr4());
}

void dump_instruction_bytes(u64 rip)
{
    constexpr u64 image_start = 1 << 20;
    constexpr u64 mapped_limit = 4ULL << 30;

    if (rip < image_start || rip + 16 >= mapped_limit)
        return;

    const auto* code = reinterpret_cast<const u8*>(rip);
    constexpr const char* digits = "0123456789abcdef";

    console_write("code:");
    for (usize i = 0; i < 16; ++i) {
        console_put(' ');
        console_put(digits[code[i] >> 4]);
        console_put(digits[code[i] & 0x0F]);
    }
    console_put('\n');
}

void dump_stack_guards()
{
    if (arch::stack_guards_intact())
        return;

    const char* name = arch::overflowed_stack_name();
    pr_err("the %s stack overflowed into its guard page\n", name != nullptr ? name : "?");
}

void dump_threads()
{
    Thread* self = current_thread();
    if (self != nullptr)
        pr_err("current thread: %s (%u) on cpu %u\n", self->name(), self->id(), self->cpu());

    pr_err("threads: %lu alive\n", static_cast<u64>(thread_count()));

    for (usize i = 0; i < 16; ++i) {
        const Thread* thread = thread_at(i);
        if (thread == nullptr)
            break;

        pr_err("  %s %u [%s] cpu %u\n",
               thread->name(),
               thread->id(),
               thread_state_name(thread->state()),
               thread->cpu());
    }
}

void dump_modules()
{
    pr_err("modules: %lu registered\n", static_cast<u64>(module_count()));

    for (usize i = 0; i < module_count(); ++i) {
        const Module* module = module_at(i);
        pr_err("  %s %s [%s]\n",
               module->info->name,
               module->info->version,
               module_state_name(module->state));
    }
}

ERIS_NORETURN void halt()
{
    console_write("system halted\n");

    // A test run asks for this so the harness does not wait out a timeout on a
    // machine that is never going to do anything again.
    if (cmdline_has("panic_exit"))
        arch::outb(0xF4, 0x10);

    for (;;)
        arch::hlt();
}

} // namespace

void panic(const char* fmt, ...)
{
    arch::cli();
    const u64 token = console_begin();
    arch::smp_halt_others();

    console_write("\n*** kernel panic: ");

    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vprintk(fmt, args);
    __builtin_va_end(args);
    console_put('\n');

    dump_control_registers();
    dump_stack_guards();
    backtrace();
    dump_threads();
    dump_modules();
    console_end(token);
    halt();
}

void panic_with_registers(const arch::Registers& regs, const char* fmt, ...)
{
    arch::cli();
    const u64 token = console_begin();
    arch::smp_halt_others();

    console_write("\n*** kernel panic: ");

    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vprintk(fmt, args);
    __builtin_va_end(args);
    console_put('\n');

    dump_registers(regs);
    dump_control_registers();
    dump_instruction_bytes(regs.rip);
    dump_stack_guards();
    backtrace_from(regs.rip, regs.rbp);
    dump_threads();
    dump_modules();
    console_end(token);
    halt();
}

} // namespace eris
