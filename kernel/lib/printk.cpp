// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/console.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

namespace eris {
namespace {

constexpr const char* level_prefix(LogLevel level)
{
    switch (level) {
    case LogLevel::Debug: return "[dbg] ";
    case LogLevel::Info:  return "[inf] ";
    case LogLevel::Warn:  return "[wrn] ";
    case LogLevel::Error: return "[err] ";
    }
    return "[???] ";
}

void print_unsigned(u64 value, u32 base, bool uppercase)
{
    const char* digits = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char buffer[24];
    usize index = sizeof(buffer);

    do {
        buffer[--index] = digits[value % base];
        value /= base;
    } while (value != 0);

    while (index < sizeof(buffer))
        console_put(buffer[index++]);
}

void print_signed(i64 value)
{
    if (value < 0) {
        console_put('-');
        print_unsigned(0 - static_cast<u64>(value), 10, false);
        return;
    }
    print_unsigned(static_cast<u64>(value), 10, false);
}

}

void vprintk(const char* fmt, __builtin_va_list args)
{
    for (const char* p = fmt; *p != '\0'; ++p) {
        if (*p != '%') {
            console_put(*p);
            continue;
        }

        ++p;
        bool is_long = false;
        while (*p == 'l') {
            is_long = true;
            ++p;
        }

        switch (*p) {
        case 'c':
            console_put(static_cast<char>(__builtin_va_arg(args, int)));
            break;
        case 's': {
            const char* s = __builtin_va_arg(args, const char*);
            console_write(s != nullptr ? s : "(null)");
            break;
        }
        case 'd':
        case 'i':
            print_signed(is_long ? __builtin_va_arg(args, i64)
                                 : __builtin_va_arg(args, i32));
            break;
        case 'u':
            print_unsigned(is_long ? __builtin_va_arg(args, u64)
                                   : __builtin_va_arg(args, u32),
                           10, false);
            break;
        case 'x':
            print_unsigned(is_long ? __builtin_va_arg(args, u64)
                                   : __builtin_va_arg(args, u32),
                           16, false);
            break;
        case 'X':
            print_unsigned(is_long ? __builtin_va_arg(args, u64)
                                   : __builtin_va_arg(args, u32),
                           16, true);
            break;
        case 'p':
            console_write("0x");
            print_unsigned(reinterpret_cast<u64>(__builtin_va_arg(args, void*)), 16, false);
            break;
        case '%':
            console_put('%');
            break;
        case '\0':
            return;
        default:
            console_put('%');
            console_put(*p);
            break;
        }
    }
}

void printk(LogLevel level, const char* fmt, ...)
{
    console_write(level_prefix(level));

    __builtin_va_list args;
    __builtin_va_start(args, fmt);
    vprintk(fmt, args);
    __builtin_va_end(args);
}

}
