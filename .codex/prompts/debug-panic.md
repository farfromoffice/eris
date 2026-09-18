# Debug a panic

Find the cause of: $ARGUMENTS

Trace first, guess later.

```
qemu-system-x86_64 -kernel build/eris32.elf -serial stdio -display none \
  -no-reboot -d int,cpu_reset -D build/qemu.log -m 512M
```

Read the last `check_exception` line. `v=0e` page fault, `v=0d` general
protection, `v=08` double fault. CR2 holds the faulting address.

For a live session:

```
qemu-system-x86_64 -kernel build/eris32.elf -display none -s -S -m 512M &
gdb build/eris.elf -ex 'target remote :1234' -ex 'break kernel_main' -ex continue
```

Symbols live in `build/eris.elf`. `build/eris32.elf` is only the copy QEMU's
loader accepts.

Usual causes: the register frame in `include/eris/irq.hpp` drifting from the
pushes in `kernel/cpu/isr.asm`, a handler that skips the end of interrupt,
allocation inside an interrupt, a touch above the first mapped gigabyte, a stale
object file after the module macro changed, or printing before `serial_init`.

Fix the cause, not the symptom. If the bug was an unwritten invariant, add it to
`CLAUDE.md`. Commit as `subsystem: fix ...` with a body explaining what went
wrong.
