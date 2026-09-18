# CLAUDE.md

Claude specific working notes for eris. Shared rules live in `AGENTS.md`, read
that first. This file is the live map of the tree: what exists right now, which
invariants are easy to break, and where the edges are.

## Keeping this file current

**Update this file in the same commit whenever the change touches what it
describes.** A stale map is worse than no map, because the next session trusts
it. That means:

* a module is added or removed, or its dependencies or ABI change
* a header in `include/eris/` gains, loses or changes a public declaration
* the module framework in `kernel/module/` changes behaviour
* the boot order in `kernel/main.cpp` changes
* `linker/kernel.ld` gains, drops or moves a section
* a limit or an invariant listed below stops being true

Refactoring inside a module or a subsystem without moving any of that does not
need an update here. If a change makes a statement below wrong, fix the
statement, do not append a note next to it.

The same rule covers the rest of the agent material. A change that makes a skill
in `.claude/skills/`, an agent in `.claude/agents/` or a prompt in
`.codex/prompts/` describe something that no longer exists has to update that
file too. Renaming a script, changing its flags, moving a build step or changing
what a healthy boot log looks like all land in those files.

## Layout

```
boot/head.asm        multiboot header, bss clear, paging, long mode switch
boot/grub.cfg        ISO boot entry
kernel/main.cpp      start_kernel, boot order, boot report
kernel/cpu/          gdt.cpp idt.cpp isr.asm pic.cpp timer.cpp
kernel/mm/           page_alloc.cpp heap.cpp
kernel/module/       module.cpp symbols.cpp
kernel/lib/          console.cpp printk.cpp panic.cpp serial.cpp string.cpp init.cpp
include/eris/        public kernel API
modules/<name>/      in tree modules
linker/kernel.ld     image layout, module and symbol sections
scripts/             build and CI helpers
```

## Boot order

`_start` (boot/head.asm) clears `.bss`, identity maps the first GiB with 2 MiB
pages, enters long mode, runs `call_global_ctors`, then calls `kernel_main`.

`start_kernel` in `kernel/main.cpp` runs in this order and the order matters:

1. `serial_init` so panics have somewhere to go
2. `arch::gdt_init`, `arch::idt_init`, `arch::pic_init`
3. `mm::page_alloc_init` with the multiboot magic and info pointer
4. `mm::heap_init`, which takes 512 contiguous pages
5. `timer_init(100)` then `arch::sti`
6. `module_init_builtin`, which loads every descriptor found in `.eris_modules`
7. `report_modules`, then an idle `hlt` loop

Nothing before step 3 may allocate. Nothing before step 1 may print.

## Public API

| Header | What it gives you |
| --- | --- |
| `eris/types.hpp` | `u8`..`u64`, `usize`, `phys_addr`, `page_size` |
| `eris/compiler.hpp` | `ERIS_PACKED`, `ERIS_ALIGNED`, `ERIS_NORETURN` |
| `eris/console.hpp` | `Console` interface, register and unregister, `console_write` |
| `eris/printk.hpp` | `pr_debug` `pr_info` `pr_warn` `pr_err`, `vprintk` |
| `eris/panic.hpp` | `panic`, never returns |
| `eris/mm.hpp` | page allocator, `heap_init`, `kmalloc` `kzalloc` `kfree` |
| `eris/module.hpp` | `ERIS_MODULE`, load, unload, find, get, put, taint state |
| `eris/export.hpp` | `ERIS_EXPORT_SYMBOL`, `symbol_lookup` |
| `eris/irq.hpp` | `Registers`, `irq_register`, mask, unmask, eoi, table init |
| `eris/io.hpp` | `inb` `outb` `io_wait` `cli` `sti` `hlt` |
| `eris/serial.hpp` | `serial_init` for the early console |
| `eris/time.hpp` | `timer_init`, `ticks` |
| `eris/string.hpp` | `memset` `memcpy` `memmove` `memcmp` `strlen` `strcmp` |

## Modules

| Module | Version | Deps | ABI |
| --- | --- | --- | --- |
| `vga` | 0.1 | none | `vga_clear` `vga_put_cell` `vga_write` `vga_fill_row` `vga_set_color` `vga_console_enable` `vga_width` `vga_height` |
| `keyboard` | 0.1 | none | `keyboard_subscribe` `keyboard_unsubscribe` |
| `desktop` | 0.1 | `vga`, `keyboard` | none, it is a leaf |

Exported symbols right now: 8, six from `vga` and two from `keyboard`.
`vga_width` and `vga_height` are callable but not exported yet.

`desktop` takes over the screen by calling `vga_console_enable(false)`, so kernel
log lines stop appearing on VGA once it loads. Serial keeps everything.

## Invariants

* `eris::arch::Registers` field order mirrors the pushes in `kernel/cpu/isr.asm`.
  Change both or neither.
* Module descriptors live in `.eris_modules` inside `.rodata`, bracketed by
  `__eris_modules_start` and `__eris_modules_end`. They have internal linkage, so
  the section needs `KEEP` in the linker script.
* Exported symbols live in `.eris_symtab` inside `.data`, because the macro
  initialises them at runtime through `.init_array`.
* `.bss` is cleared in the boot stub, not in `call_global_ctors`. The stack and
  the page tables live in `.bss` and are already in use by then.
* The page allocator manages the first GiB only, because that is all the boot
  stub maps. Its bitmap sits right after `__kernel_end`.
* The heap is 512 pages, 2 MiB, first fit with block merging. It never grows.
* Fixed limits: 64 modules, 4 consoles, 8 keyboard subscribers, 16 IRQ lines.
* Interrupt vectors 0 to 31 panic, 32 to 47 dispatch to IRQ handlers and send an
  end of interrupt, everything else logs a warning.
* QEMU's `-kernel` only loads 32-bit ELF, so `make` produces `build/eris32.elf`
  with `objcopy`. `build/eris.elf` is the real image and the one an ISO uses.

## Traps worth remembering

* Adding a header dependency to `include/eris/module.hpp` rebuilds every module,
  which is how a stale object file once produced a duplicate descriptor symbol.
  `make clean` when the macro changes.
* `ERIS_MODULE` builds its dependency array with `__VA_OPT__`. Passing no deps is
  fine, passing an empty string is not.
* A module init that fails leaves the module in the failed state with its error
  code kept. Retrying `module_load` returns the same error instead of running
  init again.
* `pr_*` before `serial_init` writes into a console list with zero entries, so
  the output disappears silently.
* Anything called from an interrupt handler must not use `kmalloc`.

## Verification

```
make
./scripts/boot-test.sh
./scripts/check-spdx.sh
./scripts/check-modules.sh
```

`boot-test.sh` fails on a missing module line, on a panic, on a taint warning
and on an empty serial log. Paste its output in the pull request.

## License

GPL-2.0-only. Every new file needs the SPDX header and the copyright line that
the rest of the tree uses. Modules declare their license in `ERIS_MODULE`, and a
license outside the free list in `kernel/module/module.cpp` taints the kernel.
