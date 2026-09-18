// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/types.hpp>

using Constructor = void (*)();

extern "C" {

extern Constructor __init_array_start[];
extern Constructor __init_array_end[];
void call_global_ctors()
{

    for (Constructor* ctor = __init_array_start; ctor != __init_array_end; ++ctor)
        (*ctor)();
}

void __cxa_pure_virtual()
{
    for (;;)
        asm volatile("hlt");
}

int __cxa_atexit(void (*)(void*), void*, void*)
{
    return 0;
}

void* __dso_handle = nullptr;

}

void* operator new(eris::usize) noexcept { return nullptr; }
void* operator new[](eris::usize) noexcept { return nullptr; }
void operator delete(void*) noexcept {}
void operator delete[](void*) noexcept {}
void operator delete(void*, eris::usize) noexcept {}
void operator delete[](void*, eris::usize) noexcept {}
