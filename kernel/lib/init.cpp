// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/mm.hpp>

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

// noexcept: with -fno-exceptions, new expressions must check for nullptr.
void* operator new(eris::usize size) noexcept { return eris::kmalloc(size); }
void* operator new[](eris::usize size) noexcept { return eris::kmalloc(size); }
void operator delete(void* ptr) noexcept { eris::kfree(ptr); }
void operator delete[](void* ptr) noexcept { eris::kfree(ptr); }
void operator delete(void* ptr, eris::usize) noexcept { eris::kfree(ptr); }
void operator delete[](void* ptr, eris::usize) noexcept { eris::kfree(ptr); }
