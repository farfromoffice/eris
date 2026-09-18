---
name: kernel-debug
description: Track down a panic, hang, triple fault or wrong interrupt behaviour in eris with QEMU and gdb. Use when the kernel crashes, reboots in a loop, prints nothing, or an IRQ misbehaves.
---

# Debugging eris

## Classify the failure first

| Symptom | Where to look |
| --- | --- |
| Nothing on serial | Boot stub, linker script, multiboot header |
| Banner then silence | Fault before the IDT, or a hang in early init |
| Endless reboot | Triple fault, usually paging or the GDT |
| `cpu exception N` | The IDT is alive, read the vector and RIP |
| IRQ never fires | PIC mask, missing `irq_register`, missing end of interrupt |
| Garbage on screen | VGA cell writes, or a module that fights over the console |

## QEMU tracing

```
qemu-system-x86_64 -kernel build/eris32.elf -serial stdio -display none \
  -no-reboot -d int,cpu_reset -D build/qemu.log -m 512M
```

`-no-reboot` stops the loop so the last state stays readable. In the log, read
the final `check_exception` line: `v=0e` is a page fault, `v=0d` a general
protection fault, `v=08` a double fault. CR2 holds the faulting address.

## gdb

```
qemu-system-x86_64 -kernel build/eris32.elf -display none -s -S -m 512M &
gdb build/eris.elf -ex 'target remote :1234' -ex 'break kernel_main' -ex continue
```

The symbols come from `build/eris.elf`, the 64-bit image. `build/eris32.elf` is
only the copy QEMU's loader accepts.

Useful breakpoints: `kernel_main`, `eris::arch::isr_dispatch`, the module init
you suspect. `info registers` after a fault and `x/16i $rip` usually end it.

## Usual suspects

* The register frame in `include/eris/irq.hpp` drifting from the pushes in
  `kernel/cpu/isr.asm`. Every interrupt then reads the wrong fields.
* A handler that forgets the end of interrupt, so the PIC delivers nothing else.
* Allocating in an interrupt handler, which deadlocks against the heap.
* Touching memory above the first GiB, which is not mapped.
* A stale object file after the module macro changed. `make clean` and retry.
* Printing before `serial_init`, which goes nowhere and looks like a hang.

## Closing out

Fix the cause, not the symptom. If the bug was an unwritten invariant, write it
into `CLAUDE.md`. Commit as `subsystem: fix ...` with a body explaining what
went wrong and why this is the right layer.
