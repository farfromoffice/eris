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
modules/internal_apps/<name>/  the apps that ship with the desktop
fonts/               the faces the desktop loads at run time
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
name, and the banner prints both. The tree is on 0.1, `Dysnomia`. Roadmap
milestones carry names for later versions, but landing a phase never bumps this
header: the version moves only when a release is cut on purpose.

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
7. `initrd_init`, then `acpi::init`, `arch::irq_init` and `clock_init`, in that
   order, because the controller and the clock both come out of the tables
8. `timers_init`, `arch::sti`, `arch::smp_init`, then `sched_init`
9. `work_start` and the `kinit` thread, which carries the rest of the boot
   sequence: `module_init_builtin`, `pci::init`, the initrd load,
   the mounts, `report_modules`, the first program if there is one, and
   whichever self tests the command line asked for
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
| `eris/module.hpp` | `ERIS_MODULE`, load, unload, find, get, put, `module_get_owner`, `module_snapshot`, taint state, `module_load_image` |
| `eris/elf.hpp` | ELF64 relocatable structures the loader reads |
| `eris/initrd.hpp` | Tar archive lookup for the modules the boot loader passed in |
| `eris/device.hpp` | `Device`, `BusDevice`, `Driver`, the registry that binds them |
| `eris/pci.hpp` | Configuration space, enumeration, BAR decoding, capabilities |
| `eris/module_api.hpp` | The C ABI a loadable module is allowed to call, `eris_schedule_work` and `ErisModuleInfo` included |
| `eris/block.hpp` | `BlockDevice`, the registry, byte granular reads |
| `eris/vfs.hpp` | `Inode`, `FileSystem`, mount, resolve, read, write, list |
| `eris/ramfs.hpp` | Files in the heap |
| `eris/devfs.hpp` | Devices as files |
| `eris/ext2.hpp` | Mounting an ext2 image read only |
| `eris/process.hpp` | `Process`, `process_run`, `syscall_init`, the user base |
| `eris/export.hpp` | `ERIS_EXPORT_SYMBOL`, `symbol_lookup` |
| `eris/irq.hpp` | `Registers`, `irq_register`, `irq_init`, mask, unmask, eoi |
| `eris/acpi.hpp` | Table lookup, MADT results, GSI mapping, CPU count |
| `eris/apic.hpp` | Local APIC, IO APIC, the vectors they use |
| `eris/work.hpp` | `schedule_work`, `work_run_pending`, `work_start`, `work_cancel_owner` |
| `eris/thread.hpp` | `Thread`, `WaitQueue`, `yield`, `thread_sleep_ms`, preempt count |
| `eris/lock.hpp` | `SpinLock`, `IrqSpinLock`, `RecursiveIrqLock`, guards |
| `eris/atomic.hpp` | `Atomic<T>`, `RefCount`, `memory_barrier`, `cpu_relax` |
| `eris/cpu.hpp` | Per CPU block, TSS, IST stacks, `smp_init`, `this_cpu` |
| `eris/io.hpp` | `inb` `outb` `io_wait` `cli` `sti` `hlt` |
| `eris/serial.hpp` | `serial_init` for the early console |
| `eris/time.hpp` | `monotonic_ns`, `timer_after`, `timer_every`, `timer_cancel`, `timer_cancel_owner`, `udelay`, `ticks` |
| `eris/string.hpp` | `memset` `memcpy` `memmove` `memcmp` `strlen` `strcmp` |

## Modules

| Module | Version | Deps | ABI |
| --- | --- | --- | --- |
| `vga` | 0.1 | none | `vga_clear` `vga_put_cell` `vga_write` `vga_fill_row` `vga_set_color` `vga_console_enable` `vga_width` `vga_height` |
| `keyboard` | 0.1 | none | `keyboard_subscribe` `keyboard_unsubscribe` |
| `virtio_blk` | 0.1 | none | `virtio_blk_present` `virtio_blk_capacity` `virtio_blk_read` `virtio_blk_write` |
| `rtc` | 0.1 | none | `rtc_read` `rtc_unix_time` |
| `fbdev` | 0.1 | none | `fb_present` `fb_width` `fb_height` `fb_pitch` `fb_pixels` `fb_blit` |
| `ps2mouse` | 0.1 | none | `mouse_present` `mouse_subscribe` `mouse_unsubscribe` |
| `desktop` | 0.3 | `fbdev`, `keyboard`, `ps2mouse`, `rtc` | `desktop_register_app` `desktop_unregister_app` `desktop_available` `desktop_app_registered` |
| `system` | 0.1 | `desktop` | none, it is a leaf |
| `modules` | 0.1 | `desktop` | none, it is a leaf |
| `console` | 0.1 | `desktop` | none, it is a leaf |
| `about` | 0.1 | `desktop` | none, it is a leaf |

`vga` and `keyboard` are linked into the image. Everything else is built as
`build/modules/<name>.ko`, packed into `build/initrd.tar` and loaded at boot.
`BUILTIN_MODULES` in the Makefile decides which is which.

The last four are applications rather than drivers, so they live under
`modules/internal_apps/<name>/`. The Makefile walks both depths, and the only
thing the nesting changes is where the sources sit.

Exported symbols right now: 57.

An app is a module that registers a `DesktopApp` with the shell, declared in
`modules/desktop/app.hpp`. It brings a name, a subtitle, an icon and a `draw`
callback, and the shell hands it a painter with fills, rounded rectangles,
text and the shell palette. An app never touches the framebuffer, a window
frame or the pointer, which is why the apps and the shell can be built apart
and loaded in any order.

Each app keeps its artwork as `icon.svg` next to its source.
`scripts/gen-icon.py` rasterises it into the committed `icon.cpp`, because the
kernel has no vector rasteriser and is not getting one. Run the script when an
icon changes; the build does not depend on it.

Text is real TrueType. `modules/desktop/truetype.cpp` parses and rasterises the
faces at run time out of `/eris-sans.ttf` and `/eris-mono.ttf`, which the
initrd carries from `fonts/`. Those two are Adwaita Sans and Adwaita Mono cut
down to the characters the shell draws by `scripts/subset-font.py`, and
`modules/desktop/FONT` holds their licence. No font is ever compiled into a
source file.

`desktop` takes over the screen by calling `vga_console_enable(false)`, so kernel
log lines stop appearing on VGA once it loads. Serial keeps everything, and the
console app shows the same stream inside a window.

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
  path in the loader has to undo a reference. `users` counts `module_get` and
  `module_get_owner` calls not yet matched by a put. Unload needs both at zero.
  Dropping either kind of reference when none is held panics at the call that
  did it.
* Nothing may point into a module image when its frames go back to the
  allocator. `module_unload` runs the module's exit, then cancels the timers
  and drops the queued work whose callback lives in the image, waiting out one
  already running and the loader unregisters an image's exports on every path
  that releases it. Code that keeps a pointer into an image for longer, a
  registered block device for instance, holds a reference through
  `module_get_owner` instead and a module in use refuses to unload.
* A module that unloads is `Unloading` for as long as its exit runs. The state
  is claimed under the table lock which keeps a second unload and any new
  `module_get` out while the lock is dropped for the exit itself. The table is
  only rearranged under that lock. A pointer into it is never held across
  the gap: the unload path looks the module up again by name.
* The module ABI hands out copies, never pointers into an image. The strings in
  a descriptor live in the module's own pages. `eris_module_at` fills an
  `ErisModuleInfo` the caller owns.
* `.bss` is cleared in the boot stub, not in `call_global_ctors`. The stack, the
  page tables and the allocator bitmap live in `.bss` and are already in use by
  then. The stub clears the direction flag first, because multiboot leaves it
  undefined and both `rep stosb` and the C++ ABI expect it clear.
* The kernel runs on page tables it built itself after `paging_init`. The boot
  stub's tables only exist to reach that point.
* Address space layout: the first four GiB are a direct map, writable and never
  executable, because the allocator reaches every frame through it. The kernel
  image inside it is remapped with 4 KiB pages carrying real permissions: text
  read execute, rodata read only, data and bss writable and no execute. The
  range from 4 GiB to 8 GiB is the vmalloc area, empty until something reserves
  part of it, and every reservation gets a guard page after it.
* `CR0.WP` and `EFER.NXE` are set in `paging_init`. Without the first, ring 0
  writes through read only pages; without the second, the NX bit is ignored.
* `unmap` and `protect` broadcast a TLB shootdown when they changed anything
  and another core is online. `invlpg` reaches one core, without it another
  core keeps the permissions a page used to have: module text stays writable
  and not executable there which is the opposite of what the loader just
  asked for. The broadcast waits for delivery rather than for the other cores
  to run the handler because it is sent with the page table lock held and a
  core spinning for that lock has interrupts off.
* `phys_to_virt` and `virt_to_phys` are the identity today. They exist so the
  higher half move is one edit rather than a hunt, and nothing should open code
  the conversion.
* Device memory is reached through `mm::map_device`, which returns an uncached
  window in the vmalloc area. A driver never touches a page table and never
  assumes a physical address is mapped.
* The heap reserves 64 MiB of virtual space and commits 2 MiB at a time. It is
  first fit with block merging, grows when an allocation does not fit and never
  shrinks.
* The page allocator manages the first four GiB only, because that is all the
  direct map covers. Its bitmap is a 32 KiB array in `.bss`, so it no longer depends on
  whatever sits after `__kernel_end`.
* `page_alloc_init` reserves the low megabyte, the kernel image and the data the
  bootloader left behind, the multiboot info block and the module strings
  included. Handing those pages out corrupts the memory map while it is read.
* Global `operator new` and `delete` are wired to `kmalloc` and `kfree` and are
  `noexcept`. A `new` expression returns `nullptr` when the heap is exhausted,
  and using it before `heap_init` is a null dereference.
* Fixed limits: 64 modules, 4 consoles, 8 keyboard subscribers, 16 IRQ lines.
* A module image is allocated from the direct map rather than the vmalloc area.
  The relocations a compiler emits for kernel code are 32 bit and relative, so
  an image further than two gigabytes from the kernel cannot reach it.
* Loadable images are relocated against the symbols already exported, so an
  image that calls into another module has to arrive after it. `kernel/main.cpp`
  repeats the pass while it still maps something, which is why the order inside
  the archive does not matter.
* The desktop paints into memory and sends only the rows that differ from what
  the screen already holds. Writes to the aperture are slow enough to be seen,
  so a still picture sends nothing at all.
* The frame timer only asks for a frame, it never paints one. Painting takes
  milliseconds and runs on the work thread, and the request flag is cleared
  after the frame rather than before, or two cores paint the same canvas at
  once and the result tears.
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

* `ModuleInfo` is shared between the kernel and every image built against it.
  Adding or moving a field means bumping `module_abi_version` in the same
  commit, otherwise an older image is accepted and then misread.
* A loadable module is the relocatable object itself. `BUILTIN_MODULES` in the
  Makefile decides what is linked into the image, everything else lands in
  `build/initrd.tar` as a `.ko`.
* `R_X86_64_PC32` reaches two gigabytes, which is why module images come from
  the vmalloc area rather than anywhere else.
* User space starts at `0x8000000000`, above everything the kernel maps, and a
  process address space is the kernel one plus a branch of its own. A user
  mapping needs the user bit on every level of the walk.
* A syscall preserves every register except `rax`, `rcx` and `r11`. The entry
  stub pops the frame back rather than discarding it, because the other side
  compiled on that promise.
* A thread running a program carries its own syscall stack, and the scheduler
  hands the CPU that one rather than the thread's own top while it is in use.
* Ring 3 is preemptible. A tick lands on the thread's syscall stack, so the
  switch saves it like any other kernel context. Everything the program needs
  follows the thread: its syscall stack, its page tables and the `syscall` MSRs,
  which every core installs at boot.
* Mounts are set up after the modules load, because storage arrives as a
  module. The root is ramfs, `/dev` is devfs, and `/mnt` is whatever ext2 image
  the first block device carries.
* ext2 is read only. The group descriptors live in the block after the
  superblock, which is block 2 at a 1 KiB block size and block 1 above that.
* The bus is walked before the loadable modules arrive, so a driver finds its
  device the moment its init runs.
* A loaded image brings its own export table. The loader runs the image's
  constructors first, because that is what fills the entries, then registers the
  table, and drops it again on unload.
* A driver that finds no hardware returns success and stays idle. Failing its
  init only makes the boot log claim something is broken.
* A module ABI is `extern "C"` on purpose. A mangled name is not something the
  export table can promise to keep.
* The loadable modules carry their own header dependencies in `DEPS`, a
  change to `include/eris/module.hpp` rebuilds them and the initrd along with
  the kernel. Without that a bumped `module_abi_version` leaves ABI stamped
  images in the archive that the kernel then refuses one by one.
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
* `make run` asks for KVM when `/dev/kvm` is writable. Without it the whole
  shell is rendered by an interpreted CPU, a frame takes over a hundred
  milliseconds and the screen visibly crawls. It is not a bug in the compositor.
* The desktop needs its faces before it can draw anything, so the root file
  system is mounted before the loadable modules and the fonts ride in the
  initrd. A shell that cannot find them refuses to start rather than drawing
  boxes.
* QEMU's `mouse_move` with a huge delta floods the PS/2 queue and the pointer
  stops answering. Drive it in steps of eighty or so when scripting a click.

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
./scripts/module-test.sh
./scripts/device-test.sh
./scripts/fs-test.sh
./scripts/user-test.sh
./scripts/desktop-test.sh
```

`boot-test.sh` fails on a missing module line, on a panic, on a taint warning
and on an empty serial log. `faultinject.sh` does the opposite: it drives
`unmapped`, `opcode`, `divide`, `doublefault`, `stack`, `text`, `rodata` and
`panic` through the panic path and fails if any of them does not report a panic
with a backtrace. The `mmtest` switch runs the heap growth check at boot and
`timetest` measures the clock against a known delay and a timer deadline.
`desktop-test.sh` boots the shell on a framebuffer, dumps the screen through the
QEMU monitor and checks that the top bar, the icons and the taskbar actually
carry pixels. Paste both in the pull request.

## License

GPL-2.0-only. Every new file needs the SPDX header and the copyright line that
the rest of the tree uses. Modules declare their license in `ERIS_MODULE`, and a
license outside the free list in `kernel/module/module.cpp` taints the kernel.
