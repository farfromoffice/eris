// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/compiler.hpp>
#include <eris/cpu.hpp>
#include <eris/string.hpp>

namespace eris::arch {

// Every CPU needs its own task state segment: the busy bit means one segment
// cannot be loaded twice, and the interrupt stacks must not be shared either.
void tss_init_cpu(PerCpu& cpu)
{
    memset(&cpu.tss, 0, sizeof(cpu.tss));
    cpu.tss.iomap_base = sizeof(cpu.tss);

    cpu.tss.ist[ist_double_fault - 1] = exception_stack_top(ist_double_fault);
    cpu.tss.ist[ist_nmi - 1] = exception_stack_top(ist_nmi);
    cpu.tss.ist[ist_machine_check - 1] = exception_stack_top(ist_machine_check);

    if (cpu.kernel_stack_top != 0)
        cpu.tss.rsp[0] = cpu.kernel_stack_top;
}

void tss_init()
{
    exception_stacks_init();
    tss_init_cpu(this_cpu());
    gdt_init_cpu(this_cpu());
}

void tss_set_kernel_stack(virt_addr stack_top)
{
    PerCpu& cpu = this_cpu();
    cpu.kernel_stack_top = stack_top;
    cpu.tss.rsp[0] = stack_top;
}

} // namespace eris::arch
