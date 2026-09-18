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
eris 0.1 "Dysnomia" (x86_64, c++23)
[inf] memory: 262144 pages total, 130235 free (508 MiB)
[inf] heap: 2048 KiB
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
* `cpu exception 14` is a page fault. The identity map covers the first GiB
  only, so anything above that faults by design.
* A module in the failed state keeps its error code, so `module_load` returns
  the same value instead of retrying.

## Looking at the screen

The desktop module paints over the VGA console, so VGA and serial disagree on
purpose after it loads. To see the framebuffer, run QEMU with a monitor socket
and issue `screendump build/screen.ppm`.

## After a fix

Rerun the same two commands and paste the serial log into the pull request. If
the bug came from an invariant nobody had written down, add it to `CLAUDE.md`.
