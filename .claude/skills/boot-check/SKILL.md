---
name: boot-check
description: Build eris and prove it still boots under QEMU. Use after any kernel or module change, when the user asks whether it still works, or before opening a pull request.
---

# Proving eris still boots

## Run it

```
make
./scripts/boot-test.sh
```

`boot-test.sh` boots `build/eris32.elf` with the serial port redirected to
`build/boot.log`, then fails on a missing module line, a panic, a taint warning
or an empty log.

## Reading the log

A healthy boot looks like this:

```
[inf] eris 0.1 "Dysnomia" (x86_64, c++23)
[inf] paging: kernel text 28 KiB read execute, data 340 KiB no execute
[inf] acpi: rsdt with 4 tables, 1 cpu, lapic fee00000, ioapic fec00000, hpet fed00000
[inf] lapic: id 0 enabled, timer at 62 MHz
[inf] ioapic: 24 inputs from gsi 0
[inf] irq: routing through the io apic, legacy pic masked
[inf] clock: hpet at 10 ns per tick
[inf] memory: 262144 pages total, 130164 free (508 MiB)
[inf] heap: 2048 KiB
[inf] timers: lapic driven, hpet clock
[inf] 3 builtin modules registered
[inf] module vga 0.1 loaded (GPL-2.0-only)
[inf] module keyboard 0.1 loaded (GPL-2.0-only)
[inf] module desktop 0.1 loaded (GPL-2.0-only)
```

* No output at all means the image never reached `serial_init`. Suspect the boot
  stub, the linker script or the multiboot header.
* Output that stops after the banner means a fault before the IDT was ready, so
  nothing could report it. Rerun QEMU with `-d int,cpu_reset -D build/qemu.log`
  and read the last `check_exception` line.
* `cpu exception 14` is a page fault, and the panic names the region the
  address fell in. Only the first GiB direct map and the parts of the vmalloc
  area something reserved are mapped, so any other address faults by design.
* A module in the failed state keeps its error code, so `module_load` returns
  the same value instead of retrying.

## Looking at the screen

The desktop module paints over the VGA console, so VGA and serial disagree on
purpose after it loads. To see the framebuffer, run QEMU with a monitor socket
and issue `screendump build/screen.ppm`.

## After a fix

Rerun the same two commands and paste the serial log into the pull request. If
the bug came from an invariant nobody had written down, add it to `CLAUDE.md`.
