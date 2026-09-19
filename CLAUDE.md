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

## Hard limits of the project

No networking of any kind is ever added: no drivers, no stack, no sockets, no
remote access, nothing that phones home. The machine talks to its console, its
disks and its removable media, and to nothing else. `AGENTS.md` and the non goals
section of `ROADMAP.md` say this at length, and a task that seems to need the
network is a task to push back on.

## Releases

`include/eris/version.hpp` is the single source for the version and the code
name, and the banner prints both. Release 0.1 is `Dysnomia`, and `ROADMAP.md`
holds the names assigned to later milestones.

## Boot order

`_start` (boot/head.asm) clears `.bss`, identity maps the first GiB with 2 MiB
pages, enters long mode, runs `call_global_ctors`, then calls `kernel_main`.

`start_kernel` in `kernel/main.cpp` runs in this order and the order matters:

1. `arch::percpu_setup(0, 0)` first, because everything that takes a lock asks
   which CPU it is on
2. `serial_init` so panics have somewhere to go, then the banner from
   `eris/version.hpp`, then `cmdline_init`
3. `arch::gdt_init`, `arch::tss_init`, `arch::idt_init`, `arch::pic_init`
4. `mm::page_alloc_init` with the multiboot magic and info pointer
5. `mm::paging_init`, which builds the kernel page tables, applies W^X and turns
   the stack guards into holes
6. `mm::heap_init`, which reserves virtual space and commits the first 2 MiB
7. `acpi::init`, `arch::irq_init` and `clock_init`, in that order, because the
   controller and the clock both come out of the tables
8. `timers_init`, `arch::sti`, `arch::smp_init`, then `sched_init`
9. `work_start` and the `kinit` thread, which carries the rest of the boot
   sequence: `module_init_builtin`, `report_modules` and whichever self tests
   the command line asked for
10. the boot CPU falls into its idle loop, and every other core is already in
    one of its own

Nothing before step 3 may allocate. Nothing before step 1 may print.

## Public API

| Header | What it gives you |
| --- | --- |
| `eris/types.hpp` | `u8`..`u64`, `usize`, `phys_addr`, `page_size` |
| `eris/version.hpp` | Version numbers, code name, arch and language strings |
| `eris/cpu.hpp` | TSS, IST indices, exception stacks, guard page checks |
| `eris/backtrace.hpp` | `backtrace`, `backtrace_from`, `print_symbol` |
| `eris/ksyms.hpp` | `ksyms_lookup` for turning an address into a name |
| `eris/cmdline.hpp` | `cmdline_has`, `cmdline_value`, `cmdline_raw` |
| `eris/multiboot.hpp` | Boot information structures and flags |
| `eris/compiler.hpp` | `ERIS_PACKED`, `ERIS_ALIGNED`, `ERIS_NORETURN`, `ERIS_NOINLINE`, `ERIS_PRINTF` |
| `eris/console.hpp` | `Console` interface, register and unregister, `console_write` |
| `eris/printk.hpp` | `pr_debug` `pr_info` `pr_warn` `pr_err`, `vprintk` |
| `eris/panic.hpp` | `panic`, never returns |
| `eris/mm.hpp` | page allocator, `heap_init`, `kmalloc` `kzalloc` `kfree` |
| `eris/paging.hpp` | `AddressSpace`, `PageFlags`, `vmalloc_reserve`, `map_device`, `region_name`, `phys_to_virt` |
| `eris/module.hpp` | `ERIS_MODULE`, load, unload, find, get, put, taint state |
| `eris/export.hpp` | `ERIS_EXPORT_SYMBOL`, `symbol_lookup` |
| `eris/irq.hpp` | `Registers`, `irq_register`, `irq_init`, mask, unmask, eoi |
| `eris/acpi.hpp` | Table lookup, MADT results, GSI mapping, CPU count |
| `eris/apic.hpp` | Local APIC, IO APIC, the vectors they use |
| `eris/work.hpp` | `schedule_work`, `work_run_pending`, `work_start` |
| `eris/thread.hpp` | `Thread`, `WaitQueue`, `yield`, `thread_sleep_ms`, preempt count |
| `eris/lock.hpp` | `SpinLock`, `IrqSpinLock`, `RecursiveIrqLock`, guards |
| `eris/atomic.hpp` | `Atomic<T>`, `RefCount`, `memory_barrier`, `cpu_relax` |
| `eris/cpu.hpp` | Per CPU block, TSS, IST stacks, `smp_init`, `this_cpu` |
| `eris/io.hpp` | `inb` `outb` `io_wait` `cli` `sti` `hlt` |
| `eris/serial.hpp` | `serial_init` for the early console |
| `eris/time.hpp` | `monotonic_ns`, `timer_after`, `timer_every`, `timer_cancel`, `udelay`, `ticks` |
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
* Vectors 2, 8 and 18 run on IST stacks 2, 1 and 3, because they are the faults
  that can arrive when the kernel stack is already broken. Every other vector
  uses the interrupted stack.
* The boot stack and each exception stack have an unmapped guard page below
  them. An overflow faults on the instruction that caused it, and because the
  fault frame cannot be pushed either, the report comes from the double fault
  handler, which names the stack. The `0xA5` pattern only covers the window
  before `paging_init` runs.
* The image is linked three times. The first pass exists so `scripts/gen-ksyms.sh`
  can read its symbols, the second carries that table, and the third regenerates
  it because adding the table moved every address after it. The kernel builds
  with `-fno-omit-frame-pointer` for the same reason.
* `panic` and `panic_with_registers` never return. With `panic_exit` on the
  command line they leave QEMU through the debug exit port instead of halting,
  which is how the fault suite finishes in under a second.
* Module descriptors live in `.eris_modules` inside `.rodata`, bracketed by
  `__eris_modules_start` and `__eris_modules_end`. They have internal linkage, so
  the section needs `KEEP` in the linker script.
* Exported symbols live in `.eris_symtab` inside `.data`, because the macro
  initialises them at runtime through `.init_array`.
* A module counts two kinds of reference separately. `dependents` is how many
  `Ready` modules list it as a dependency: each takes one on the transition to
  `Ready` and drops it on unload, and a failed load holds none. No failure
  path in the loader has to undo a reference. `users` counts `module_get` calls
  not yet matched by `module_put`. Unload needs both at zero. Dropping either kind
  of reference when none is held panics at the call that did it.
* `.bss` is cleared in the boot stub, not in `call_global_ctors`. The stack, the
  page tables and the allocator bitmap live in `.bss` and are already in use by
  then. The stub clears the direction flag first, because multiboot leaves it
  undefined and both `rep stosb` and the C++ ABI expect it clear.
* The kernel runs on page tables it built itself after `paging_init`. The boot
  stub's tables only exist to reach that point.
* Address space layout: the first GiB is a direct map, writable and never
  executable, because the allocator reaches every frame through it. The kernel
  image inside it is remapped with 4 KiB pages carrying real permissions: text
  read execute, rodata read only, data and bss writable and no execute. The
  range from 1 GiB to 2 GiB is the vmalloc area, empty until something reserves
  part of it, and every reservation gets a guard page after it.
* `CR0.WP` and `EFER.NXE` are set in `paging_init`. Without the first, ring 0
  writes through read only pages; without the second, the NX bit is ignored.
* `phys_to_virt` and `virt_to_phys` are the identity today. They exist so the
  higher half move is one edit rather than a hunt, and nothing should open code
  the conversion.
* Device memory is reached through `mm::map_device`, which returns an uncached
  window in the vmalloc area. A driver never touches a page table and never
  assumes a physical address is mapped.
* The heap reserves 64 MiB of virtual space and commits 2 MiB at a time. It is
  first fit with block merging, grows when an allocation does not fit and never
  shrinks.
* The page allocator manages the first GiB only, because that is all the direct
  map covers. Its bitmap is a 32 KiB array in `.bss`, so it no longer depends on
  whatever sits after `__kernel_end`.
* `page_alloc_init` reserves the low megabyte, the kernel image and the data the
  bootloader left behind, the multiboot info block and the module strings
  included. Handing those pages out corrupts the memory map while it is read.
* Global `operator new` and `delete` are wired to `kmalloc` and `kfree` and are
  `noexcept`. A `new` expression returns `nullptr` when the heap is exhausted,
  and using it before `heap_init` is a null dereference.
* Fixed limits: 64 modules, 4 consoles, 8 keyboard subscribers, 16 IRQ lines.
* Interrupt vectors 0 to 31 panic, 32 to 47 dispatch to IRQ handlers and send an
  end of interrupt, 0x40 is the local APIC timer, 0xFF is the spurious vector
  and is ignored without an end of interrupt, everything else logs a warning.
* `irq_mask`, `irq_unmask` and `irq_eoi` go to whichever controller `irq_init`
  settled on. On the APIC path an unmask also programs the redirection entry,
  and the end of interrupt goes to the local APIC, never to the masked PIC.
* Time comes from the HPET when the firmware reports one and from a calibrated
  TSC otherwise. `monotonic_ns` is the only clock; `ticks` is a jiffy counter
  driven by a periodic timer on top of it.
* The timer queue programs the hardware for the nearest deadline only. A
  callback runs in interrupt context, so it must be short and must not
  allocate. `schedule_work` moves the rest to the `kworker` thread.
* The scheduler holds `sched_lock` across a context switch and the thread that
  resumes releases it. A thread that has never run unlocks it from its
  trampoline, which is why `thread_entry_start` starts with an unlock that looks
  unbalanced.
* Preemption happens on the APIC timer, after the end of interrupt and only when
  the preempt count is zero.
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
* `printk` and `panic` are checked by the compiler as printf formats, but
  `vprintk` implements only `%c %s %d %i %u %x %X %p` with the `l` length. A
  width or a flag compiles cleanly and prints wrong.
* Anything called from an interrupt handler must not use `kmalloc`.
* An `IrqSpinLock` panics if the CPU holding it asks for it again. That is a
  deadlock reported early, not a false alarm, so fix the call path rather than
  reaching for the recursive lock. The console is the one place that genuinely
  needs the recursive variant.
* Per CPU state is reached with `arch::this_cpu()`, which is only valid after
  `percpu_setup`. Before that, reading `gs:0` follows whatever the firmware left
  at address zero and ends in a general protection fault.
* The trampoline page is shared between cores coming up one at a time. A core
  copies its identity out of it and acknowledges before the boot CPU reuses the
  slots.
* Splitting a 2 MiB mapping has to carry the old flags across, or the fresh
  table silently drops `NX`.
* Taking the address of a function in an anonymous namespace can name a clone
  that the linker never emits, which shows up as an undefined reference at link
  time rather than an error where it was written.
* A write to address zero is not a fault: the identity map covers the first page.
  Use an address above the mapped gigabyte to provoke a page fault.
* Recursion written to overflow the stack gets turned into a loop by the
  optimiser unless the frame address is passed to an opaque asm statement.

## Verification

```
make
./scripts/boot-test.sh
./scripts/check-spdx.sh
./scripts/check-modules.sh
```

```
./scripts/faultinject.sh
qemu-system-x86_64 -kernel build/eris32.elf -serial stdio -display none -m 512M -append mmtest
qemu-system-x86_64 -kernel build/eris32.elf -serial stdio -display none -m 512M -append timetest
CPUS=8 ./scripts/smp-test.sh
CPUS=4 ./scripts/thread-test.sh
```

`boot-test.sh` fails on a missing module line, on a panic, on a taint warning
and on an empty serial log. `faultinject.sh` does the opposite: it drives
`unmapped`, `opcode`, `divide`, `doublefault`, `stack`, `text`, `rodata` and
`panic` through the panic path and fails if any of them does not report a panic
with a backtrace. The `mmtest` switch runs the heap growth check at boot and
`timetest` measures the clock against a known delay and a timer deadline.
Paste both in the pull request.

## License

GPL-2.0-only. Every new file needs the SPDX header and the copyright line that
the rest of the tree uses. Modules declare their license in `ERIS_MODULE`, and a
license outside the free list in `kernel/module/module.cpp` taints the kernel.
