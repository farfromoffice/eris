// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/compiler.hpp>
#include <eris/cpu.hpp>
#include <eris/string.hpp>

namespace eris::arch {
namespace {

struct ERIS_PACKED Tss {
    u32 reserved0;
    u64 rsp[3];
    u64 reserved1;
    u64 ist[7];
    u64 reserved2;
    u16 reserved3;
    u16 iomap_base;
};

constinit Tss tss{};

} // namespace

void tss_init()
{
    exception_stacks_init();

    memset(&tss, 0, sizeof(tss));
    tss.iomap_base = sizeof(tss);

    tss.ist[ist_double_fault - 1] = exception_stack_top(ist_double_fault);
    tss.ist[ist_nmi - 1] = exception_stack_top(ist_nmi);
    tss.ist[ist_machine_check - 1] = exception_stack_top(ist_machine_check);

    gdt_load_tss(reinterpret_cast<u64>(&tss), sizeof(tss) - 1);
}

void tss_set_kernel_stack(virt_addr stack_top)
{
    tss.rsp[0] = stack_top;
}

} // namespace eris::arch
