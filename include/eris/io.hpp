// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris::arch {

inline void outb(u16 port, u8 value)
{
    asm volatile("outb %0, %1" : : "a"(value), "Nd"(port));
}

inline u8 inb(u16 port)
{
    u8 value;
    asm volatile("inb %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void outl(u16 port, u32 value)
{
    asm volatile("outl %0, %1" : : "a"(value), "Nd"(port));
}

inline u32 inl(u16 port)
{
    u32 value;
    asm volatile("inl %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void outw(u16 port, u16 value)
{
    asm volatile("outw %0, %1" : : "a"(value), "Nd"(port));
}

inline u16 inw(u16 port)
{
    u16 value;
    asm volatile("inw %1, %0" : "=a"(value) : "Nd"(port));
    return value;
}

inline void io_wait()
{
    outb(0x80, 0);
}

inline void cli() { asm volatile("cli"); }
inline void sti() { asm volatile("sti"); }
inline void hlt() { asm volatile("hlt"); }

}
