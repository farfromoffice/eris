// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/cpu.hpp>
#include <eris/lock.hpp>
#include <eris/panic.hpp>

namespace eris {

u32 IrqSpinLock::current_cpu_index()
{
    return arch::this_cpu().index;
}

u32 RecursiveIrqLock::current_cpu_index()
{
    return arch::this_cpu().index;
}

void IrqSpinLock::lock_recursion_panic()
{
    panic("lock: this cpu already holds the lock it is asking for");
}

} // namespace eris
