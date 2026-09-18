// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/io.hpp>
#include <eris/console.hpp>
#include <eris/panic.hpp>
#include <eris/printk.hpp>

namespace eris {

void panic(const char* fmt, ...)
{
    arch::cli();

    console_write("\n*** kernel panic: ");

    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vprintk(fmt, args);
    __builtin_va_end(args);

    console_write("\nsystem halted\n");

    for (;;)
        arch::hlt();
}

}
