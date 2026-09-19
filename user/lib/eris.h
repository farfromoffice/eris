/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2026 farfromoffice */

#pragma once

/* The whole library a program gets today: the calls the kernel answers, and
   enough string handling to print something useful. */

typedef unsigned long u64;
typedef long i64;
typedef unsigned int u32;
typedef unsigned char u8;

#define SYS_EXIT 0
#define SYS_WRITE 1
#define SYS_READ 2
#define SYS_GETPID 3
#define SYS_SLEEP 4
#define SYS_OPEN 5
#define SYS_CLOSE 6
#define SYS_TIME 7

static inline i64 syscall(u64 number, u64 first, u64 second, u64 third)
{
    i64 result;

    asm volatile("syscall"
                 : "=a"(result)
                 : "a"(number), "D"(first), "S"(second), "d"(third)
                 : "rcx", "r11", "memory");

    return result;
}

static inline void exit(int code)
{
    syscall(SYS_EXIT, (u64)code, 0, 0);
    for (;;) { }
}

static inline i64 write(int handle, const void* buffer, u64 length)
{
    return syscall(SYS_WRITE, (u64)handle, (u64)buffer, length);
}

static inline i64 read(int handle, void* buffer, u64 length)
{
    return syscall(SYS_READ, (u64)handle, (u64)buffer, length);
}

static inline int open(const char* path)
{
    return (int)syscall(SYS_OPEN, (u64)path, 0, 0);
}

static inline int close(int handle)
{
    return (int)syscall(SYS_CLOSE, (u64)handle, 0, 0);
}

static inline int getpid(void)
{
    return (int)syscall(SYS_GETPID, 0, 0, 0);
}

static inline void sleep_ms(u64 milliseconds)
{
    syscall(SYS_SLEEP, milliseconds, 0, 0);
}

static inline u64 monotonic_ns(void)
{
    return (u64)syscall(SYS_TIME, 0, 0, 0);
}

static inline u64 strlen(const char* text)
{
    u64 length = 0;
    while (text[length] != '\0')
        ++length;
    return length;
}

static inline void print(const char* text)
{
    write(1, text, strlen(text));
}

static inline void print_number(u64 value)
{
    char digits[24];
    int index = (int)sizeof(digits);

    digits[--index] = '\0';

    if (value == 0)
        digits[--index] = '0';

    while (value > 0 && index > 0) {
        digits[--index] = (char)('0' + value % 10);
        value /= 10;
    }

    print(&digits[index]);
}
