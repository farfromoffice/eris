# Roadmap

Where eris stands against kernels that already finished this road, what it takes
to close each gap, and in which order. `CLAUDE.md` says what exists today, this
file says what comes next.

Every phase lists the work in the shape it will take in this tree: file paths,
the API other code will call, the modules it produces, and a done test that can
actually be run. If a bullet cannot be turned into a commit, it does not belong
here.

## Contents

1. [Where we are](#where-we-are)
2. [What the others do](#what-the-others-do)
3. [How to read the boxes](#how-to-read-the-boxes)
4. [Non goals](#non-goals)
5. [Gap table](#gap-table)
6. [Phase 1: survive a fault](#phase-1-survive-a-fault) (done)
7. [Phase 2: virtual memory](#phase-2-virtual-memory) (mostly done)
8. [Phase 3: modern interrupts and time](#phase-3-modern-interrupts-and-time) (done)
9. [Phase 4: concurrency and more than one CPU](#phase-4-concurrency-and-more-than-one-cpu) (done)
10. [Phase 5: threads and scheduling](#phase-5-threads-and-scheduling) (done)
11. [Phase 6: loadable modules](#phase-6-loadable-modules)
12. [Phase 7: buses and devices](#phase-7-buses-and-devices)
13. [Phase 8: storage and a file system](#phase-8-storage-and-a-file-system)
14. [Phase 9: userspace](#phase-9-userspace)
15. [Phase 10: graphics and the desktop](#phase-10-graphics-and-the-desktop)
16. [Phase 11: audio](#phase-11-audio)
17. [Phase 12: USB and real hardware](#phase-12-usb-and-real-hardware)
18. [Phase 13: power, security and self hosting](#phase-13-power-security-and-self-hosting)
19. [Phase 14: installation and setup](#phase-14-installation-and-setup)
20. [The driver program](#the-driver-program)
21. [Running alongside: testing and hygiene](#running-alongside-testing-and-hygiene)
22. [Order and dependencies](#order-and-dependencies)
23. [Version milestones](#version-milestones)

## Where we are

Release 0.1, code name Dysnomia. Multiboot entry, long mode, identity mapped
first gigabyte, GDT and IDT, legacy
PIC, PIT at 100 Hz, bitmap page allocator, first fit heap, serial and VGA
consoles, and a module framework with dependency resolution, refcounting, a
license taint check and a symbol export table. Three modules in tree: `vga`,
`keyboard`, `desktop`.

Phase 5 put threads on top: the second half of boot runs as `kinit`, deferred
work has a `kworker` thread, and anything can sleep or block instead of spinning.

Every core the firmware reports is online as of phase 4, with per CPU
descriptors, IRQ safe locks and atomic refcounts on the shared structures.

Interrupts come through the IO APIC as of phase 3, time is a nanosecond clock
from the HPET, and callbacks are deadlines rather than tick counts.

The kernel builds its own page tables at boot: the first gigabyte stays directly
mapped for the allocator, the image carries real permissions, device memory is
mapped on request and the heap grows by committing pages into a reservation.

A fault is survivable as of phase 1: double fault, NMI and machine check run on
their own stacks, the guard pages catch an overflow, and a panic prints the
registers, the control registers, the faulting bytes and a symbolised backtrace.

Everything runs in ring 0 on one CPU, with interrupt masking as the only form of
mutual exclusion. There is no address space management, no scheduler, no
userspace, no storage and no loadable module images.

## What the others do

**Linux.** Small architecture layer under a large generic core. Its module story
is the one eris already copies in miniature: descriptors in a dedicated section,
a symbol table for resolution, refcounts that block unloading, a taint flag. The
parts still missing here are the loadable half, `kallsyms` for backtraces, per
CPU data as a first class concept, and a bus and driver model where a driver
declares what it binds to instead of poking a fixed port.

**xv6.** The smallest honest complete kernel: per process page tables, a trap
frame, round robin scheduling, sleep and wakeup, a log structured file system and
around twenty syscalls. Good target for the shape of phases 5, 8 and 9.

**SerenityOS.** Proof that the object oriented approach survives kernel scale.
Devices are classes behind interfaces, ownership is explicit, and the graphics
stack lives in userspace: a window server owns the framebuffer and applications
talk to it over IPC. Its window server is the model for phase 10, not our
current in kernel painting.

**ToaruOS.** Closest in shape to what eris wants to be: modular kernel, loadable
drivers, VFS, PCI scan, framebuffer, compositor and a small userspace, in a
codebase one person can hold in their head.

**Redox.** Strict separation between address spaces, capabilities and drivers.
The lesson is to fix the module ABI before the module count makes it unchangeable.

**seL4 and Zircon.** One idea each: an explicit object and rights model, and IPC
treated as the primary kernel service rather than something bolted on. eris does
not need either yet, but phase 6 should not make them impossible.

**Haiku and BeOS.** Responsiveness as a design goal rather than a result: many
threads, tight IPC, and a desktop that never blocks on the disk. Worth keeping in
mind before phase 10 turns into a redraw loop.

## How to read the boxes

Work items are checkboxes. When something lands, tick the box and strike the
text in the same commit that lands it:

```
- [ ] not started
- [x] ~~landed, see the commit that did it~~
```

A phase whose items are all ticked gets its heading struck through too, and the
matching row in the version table moves to released. Nothing is ticked before
`make` and `./scripts/boot-test.sh` pass with it in.

## Non goals

These are not on the roadmap and will not be added later. They are written down
so nobody plans around them.

**Networking of any kind.** No network drivers, no protocol stack, no sockets, no
remote access. eris is closed to the outside world by design: the only ways in
and out are the machine's console, its disks and its removable media. A patch that
adds a network card driver, a TCP stack, a socket syscall or any form of remote
control will be refused however well written it is. Anything that needs to reach
another machine happens by copying files onto a disk image and booting it.

**Anything that phones home.** No telemetry, no update checks, no remote crash
reporting. The kernel writes its logs to the console and to local storage, and
that is the whole of it.

**Binary blobs and firmware loading from vendors.** A driver that needs a blob to
function is out of scope, which is part of why wireless is not on the roadmap
either.

## Gap table

| Area | eris today | Reference kernels | Phase |
| --- | --- | --- | --- |
| Fault survival | IST stacks, guard pages, symbolised backtrace, fault injection | Separate stacks for double fault and NMI, symbolised backtrace | done |
| Virtual memory | Own page tables, W^X, vmalloc, growable heap, device windows | Per address space page tables, higher half, demand paging | mostly done |
| Interrupt controller | ACPI tables, local APIC, IO APIC, HPET clock, deadline timers | MSI, x2APIC, per CPU timers | mostly done |
| Concurrency | Spinlocks, IRQ safe locks, atomics, per CPU areas | Lock ordering rules, RCU style readers | done |
| Multiprocessing | Every core online, IPIs, TLB shootdown | Per CPU scheduling, x2APIC | mostly done |
| Scheduling | Kernel threads, round robin, wait queues, preemption | Per CPU run queues, priorities, fair policy | mostly done |
| Loadable modules | Built in descriptors only | Relocatable images loaded at runtime, versioned ABI, initrd | 6 |
| Device discovery | Hard coded ports | PCI enumeration, bus and driver matching, virtio, MSI | 7 |
| Storage and VFS | None | Block layer, VFS, ramfs, devfs, an on disk file system | 8 |
| Userspace | None, everything ring 0 | Syscalls, ELF loading, processes, signals, a small libc | 9 |
| Graphics | Kernel side VGA text desktop | Framebuffer device, userspace window server, toolkit, apps | 10 |
| Audio | None | Mixer, stream API, an HDA or virtio-sound driver | 11 |
| USB | None | Host controller, hub, HID, mass storage | 12 |
| Power and security | None | ACPI shutdown, SMEP, SMAP, KASLR, W^X everywhere | 13 |
| Installation | Boots from a build tree only | Live image, installer, module selection, on disk system | 14 |
| Testing | Boot smoke test in CI | In kernel test suite, fault injection, recorded boot diffs | ongoing |

## ~~Phase 1: survive a fault~~

**Goal.** The kernel reports its own death instead of rebooting silently.
Landed on main, ships in 0.2, Sedna.

**Work**

- [x] ~~`kernel/cpu/tss.cpp`, `include/eris/cpu.hpp`: a `Tss` with `rsp0` kept for
  the future ring 0 entry, `ist[1..3]` pointing at dedicated stacks, and a TSS
  descriptor in the GDT loaded with `ltr`.~~
- [x] ~~`kernel/cpu/idt.cpp`: IST index 1 for double fault, 2 for NMI, 3 for machine
  check, so a broken kernel stack still lands somewhere valid.~~
- [x] ~~`kernel/cpu/stacks.cpp`: a guard page below the boot stack and below every
  exception stack, poisoned and checked until phase 2 can unmap it. The page
  tables moved above the stack so an overflow reaches the guard first.~~
- [x] ~~`kernel/lib/backtrace.cpp`: walks `rbp`, prints each `rip` with the nearest
  symbol, resolved through the table `scripts/gen-ksyms.sh` generates from the
  first link pass. Built with `-fno-omit-frame-pointer`.~~
- [x] ~~`kernel/lib/panic.cpp`: register frame, `cr0`, `cr2`, `cr3`, `cr4`, the
  instruction bytes at the fault, the guard page verdict, the backtrace and the
  module list. Halting every CPU waits for the IPIs in phase 4.~~
- [x] ~~`kernel/cpu/idt.cpp`: page fault error codes decoded into words rather than
  printed as a number.~~
- [x] ~~`kernel/cmdline.cpp`: multiboot command line split into words, which is
  what the fault switch reads.~~
- [x] ~~`scripts/faultinject.sh`: drives six faults through the panic path and
  checks each one reported a panic and a backtrace. `panic_exit` makes QEMU
  leave instead of idling, so the whole suite takes under a second.~~
- [x] ~~`.github/workflows/fault-injection.yml`: the suite runs on every push.~~

**Done when.** A deliberate stack overflow, an unmapped write and an invalid
opcode each print a panic with the fault decoded and a symbolised backtrace, and
QEMU never reboots. Done: the suite covers `unmapped`, `opcode`, `divide`,
`doublefault`, `stack` and `panic`.

**Traps.** The IST stacks are not reentrant, so a double fault inside a double
fault is still fatal. A write to address zero is not a fault yet, the identity
map covers it, which is why the injected page fault targets an unmapped address
instead. Recursion written to overflow the stack gets rewritten into a loop by
the optimiser unless the frame address is handed to it as an opaque value.

## Phase 2: virtual memory

**Goal.** The kernel manages its own address space instead of living inside the
identity map the boot stub made. Landed on main except for the two items below,
ships in 0.2, Sedna.

**Work**

- [x] ~~`kernel/mm/paging.cpp`, `include/eris/paging.hpp`: `AddressSpace` with
  `map`, `unmap`, `protect`, `translate`, `mapped` and `activate`, over 4 KiB
  pages, splitting a 2 MiB mapping when a single page inside it needs its own
  permissions.~~
- [ ] Higher half: link the kernel at `0xFFFFFFFF80000000`, keep a direct physical
  map at `0xFFFF800000000000` so the allocator can still reach every frame, and
  drop the low identity map once the switch is done.

  Deferred, not dropped. QEMU's `-kernel` loader only accepts a 32-bit ELF, and
  a virtual base that high cannot be written in one, so moving the kernel means
  moving the test path to a GRUB image or writing a loader stub first. It goes
  with the boot work rather than ahead of it. `phys_to_virt` and `virt_to_phys`
  already exist as the identity, so the move is one edit in `paging.hpp` plus
  the boot stub.
- [x] ~~`kernel/mm/vmalloc.cpp`: a virtual range allocator above the directly
  mapped gigabyte, with a guard page after every reservation, used by the heap
  and by device windows.~~
- [x] ~~W^X: `.text` read execute, `.rodata` read only, `.data` and `.bss` writable
  and never executable, with `CR0.WP` set so ring 0 does not get a free pass and
  `EFER.NXE` enabled so the bit means something.~~
- [x] ~~Heap growth: the heap reserves 64 MiB of virtual space, commits 2 MiB, and
  maps more when an allocation does not fit, instead of living in a fixed
  arena.~~
- [x] ~~`mm::map_device(phys_addr, usize)` returning an uncached window, and
  `unmap_device` to give it back. The VGA module now reaches its framebuffer
  through it rather than trusting a raw address.~~
- [x] ~~Page fault reports say which region the address belongs to: the null page,
  the kernel image, the direct map, the vmalloc area or nothing mapped.~~
- [x] ~~Stack guards became real holes. An overflow faults on the instruction that
  caused it, the double fault handler names the stack, and the poison pattern is
  only there for the window before paging comes up.~~
- [ ] Slab or size class allocator on top of the heap. Deferred to phase 5, which
  is the first time anything allocates the same object constantly.

**Done when.** Writing to `.text` faults, a module maps a device window by
physical address, the heap grows past its initial commit, and a stack overflow
hits a guard page rather than the neighbouring allocation. Done: `fault=text`,
`fault=rodata` and `fault=stack` all report, `mmtest` shows the heap growing
from 2 MiB to 6 MiB and back, and the VGA module runs through `map_device`.

**Traps.** Splitting a 2 MiB page has to copy the flags of the mapping it
replaces, or the new table quietly loses `NX` and W^X stops meaning anything.
`CR0.WP` is the difference between a read only page and a suggestion. Taking the
address of a function in an anonymous namespace can reference a clone the
linker never emits, which is why the write test targets `kernel_main`.

## Phase 3: modern interrupts and time

**Goal.** Interrupts arrive through the APIC, and time is a real clock rather
than a tick counter. Landed on main, ships in 0.3, Quaoar.

**Work**

- [x] ~~`kernel/acpi/tables.cpp`: RSDP scan through the EBDA and the BIOS area,
  RSDT and XSDT walk with checksums, MADT parsing for the local APIC address,
  the IO APIC and the interrupt source overrides, and the HPET address.~~
- [x] ~~`kernel/cpu/lapic.cpp`: local APIC enabled through its MSR, spurious
  vector installed, timer calibrated against the PIT and driven in one shot
  mode.~~
- [x] ~~`kernel/cpu/ioapic.cpp`: redirection entries built from the MADT with the
  polarity and trigger mode the overrides ask for, everything masked until a
  driver asks for a line, legacy PIC masked off once the pair is up.~~
- [x] ~~`kernel/cpu/irq.cpp`: one place that decides which controller is in charge,
  so `irq_mask`, `irq_unmask` and `irq_eoi` read the same whether the machine
  ended up on the APIC or on the legacy chips.~~
- [x] ~~`kernel/time/clock.cpp`: HPET where the firmware offers one, a TSC
  calibrated against the PIT otherwise, exposed as `monotonic_ns`, with
  `udelay` and `mdelay` on top.~~
- [x] ~~`kernel/time/timers.cpp`: a deadline queue with one shot and periodic
  callbacks. The hardware timer is only ever programmed for the nearest
  deadline, so a hundred pending timers still cost one interrupt.~~
- [x] ~~`kernel/lib/work.cpp`: `schedule_work` hands the long half of an interrupt
  to the idle path, where interrupts are enabled and the handler has already
  returned.~~
- [x] ~~`kernel/cmdline.cpp`: landed with phase 1, which is where the fault switch
  needed it.~~
- [ ] x2APIC mode and the FADT. Neither is needed while there is one CPU and
  nothing asks about power management, and both belong with the SMP work.

**Done when.** The PIC is masked, IRQs arrive through the IO APIC, the clock
drifts less than a millisecond a minute, and a hundred registered timers fire in
order. Done: `timetest` measures a 200 ms delay as 200.061 ms on the HPET and a
50 ms timer firing at 50.181 ms, and the keyboard reaches the desktop through
the IO APIC.

**Traps.** The APIC timer frequency is not fixed across machines, so it gets
calibrated every boot against the PIT, which is the only source that works
before anything else is trusted. The interrupt source overrides in the MADT are
easy to skip and produce a keyboard that works on QEMU and nowhere else.

## Phase 4: concurrency and more than one CPU

**Goal.** Data structures are safe under real concurrency, and every core the
firmware reports is running. Landed on main, ships in 0.4, Orcus.

**Work**

- [x] ~~`include/eris/lock.hpp`: `SpinLock`, `IrqSpinLock` that saves and restores
  the interrupt flag, a recursive variant for the console, and scoped guards for
  all of them.~~
- [x] ~~Lock assertions: an `IrqSpinLock` panics when the CPU holding it asks for
  it again, which is how the trampoline handover race below was found.~~
- [x] ~~`include/eris/atomic.hpp`: the narrow set of atomics the kernel needs and a
  `RefCount` built on them. Module references are atomic now, not plain
  integers.~~
- [x] ~~`kernel/cpu/percpu.cpp`: a per CPU block reached through `gs`, holding the
  index, the APIC id, the GDT and the TSS. The GDT and TSS became per CPU with
  it, because one TSS cannot be loaded twice.~~
- [x] ~~`kernel/cpu/smp.cpp` and `kernel/cpu/trampoline.asm`: the trampoline is
  assembled flat, copied to a fixed page below a megabyte, and started with INIT
  and STARTUP. Each core comes up in long mode on the kernel page tables with
  its own stack and descriptors.~~
- [x] ~~IPIs: a function call broadcast that waits for every core to finish, a TLB
  shootdown that follows an unmap, and an NMI that stops the others when one
  core panics.~~
- [x] ~~Retrofit: the module table, the page allocator, the heap, the vmalloc
  ranges, the timer queue, the work queue and the console all took locks.~~
- [ ] x2APIC and per CPU run queues. The first waits for a machine that needs it,
  the second for phase 5, which is what run queues are for.

**Done when.** The kernel boots every CPU the MADT lists and prints them, a
stress test hammering `module_get` and `module_put` from several cores keeps the
refcount exact, and a panic on one core stops the others. Done: `./scripts/smp-test.sh`
runs on 1, 2, 4 and 8 cores, 160000 refcount round trips at eight cores leave
the count where it started, and a panic under SMP prints one readable report.

**Traps.** The trampoline page is shared, so a core has to copy its identity out
of it before the boot CPU hands the slots to the next core. Without that two
cores read the same index, share a per CPU block, and the lock assertions fire
somewhere unrelated. The console needs a recursive lock because it is written to
from inside functions that already hold it, and a panic takes it for the whole
report or two cores interleave letter by letter.

## Phase 5: threads and scheduling

**Goal.** More than one line of execution, and code that can block instead of
spinning. Landed on main, ships in 0.4, Orcus.

**Work**

- [x] ~~`kernel/sched/thread.hpp` and `scheduler.cpp`: `Thread::spawn`, sleep,
  exit and wake, each thread on its own stack from the page allocator, with the
  bookkeeping private to a `Scheduler` class that Thread names as its friend.~~
- [x] ~~`kernel/sched/switch.asm`: the context switch saves the callee saved
  registers, parks the stack pointer and continues on the next thread's stack.
  A thread that has never run resumes through a trampoline that finds its entry
  point and argument in the registers the switch restored.~~
- [x] ~~Round robin scheduling with an idle thread per CPU. One shared run queue
  for now, which is enough while every core takes work from the same place.~~
- [x] ~~Sleep with real deadlines: a sleeping thread is parked off the run queue
  and comes back when the clock passes its wake time, so it costs nothing while
  it waits.~~
- [x] ~~`WaitQueue`: a thread blocks until something wakes it, which is what a
  driver does instead of spinning on a register.~~
- [x] ~~Preemption from the APIC timer with a preempt count, so a critical path
  can hold off the switch.~~
- [x] ~~Deferred work moved onto a `kworker` thread, which replaces the idle path
  draining the queue.~~
- [x] ~~Module init moved into thread context: the second half of boot runs as
  `kinit`, so an init is allowed to sleep on hardware.~~
- [x] ~~A panic lists the threads and says which one was running on which core.~~
- [ ] Per CPU run queues and priorities. The shared queue is honest while there
  is nothing to be unfair about, and both belong with the first workload that
  cares.

**Done when.** Two kernel threads interleave on one CPU, a thread sleeps 50 ms
and wakes within a millisecond of the deadline, a thread blocked on a wait queue
consumes no CPU, and the same test passes with several cores online. Done:
`./scripts/thread-test.sh` runs on 1, 2 and 4 cores, four counting threads reach
exactly 80000 rounds, the sleeper wakes at 50 ms, and the gate thread stays
blocked until it is opened.

**Traps.** The scheduler lock is taken before a switch and released by whoever
resumes next, so a thread that has never run has to unlock it from its
trampoline rather than inherit a frame that does. Switching inside the timer
interrupt is fine because every thread has its own stack, but only after the end
of interrupt has been sent.

## Phase 6: loadable modules

**Goal.** A module built separately, never linked into the image, loads at
runtime, resolves its symbols, runs and unloads cleanly. This is the point of the
project.

**Why now.** Everything the loader needs exists after phase 2, and phase 5 lets
an init block. Fixing the ABI later, with a dozen modules in tree, is much harder
than fixing it now with three.

**Work**

- [ ] `kernel/module/elf.cpp`: ELF64 relocatable parser. Section headers, symbol
  table, string table, `SHT_RELA` sections, sanity limits on every size read from
  the file.
- [ ] `kernel/module/loader.cpp`: allocate module memory from the phase 2 virtual
  allocator, one region per section group, copy `PROGBITS`, zero `NOBITS`, apply
  relocations, then set protections: text read execute, rodata read only, data no
  execute. No section stays writable and executable at any point.
- [ ] Relocation types needed for `-mcmodel=kernel` code: `R_X86_64_64`,
  `R_X86_64_PC32`, `R_X86_64_PLT32`, `R_X86_64_32S`, `R_X86_64_GOTPCREL` with a
  per module GOT.
- [ ] Symbol resolution against `.eris_symtab`, with a clear error naming the missing
  symbol instead of a fault at first call.
- [ ] ABI versioning: a `kernel_abi` field in `ModuleInfo`, bumped whenever a public
  header changes shape, checked before a single relocation is applied. Refuse a
  mismatch, say which side is older.
- [ ] Module memory accounting, so `module_unload` frees every page and the leak
  shows up in the page count if it does not.
- [ ] Constructors and destructors inside a module image: run its `.init_array` after
  relocation and its `.fini_array` on unload.
- [ ] `ERIS_EXPORT_SYMBOL_DATA` for exported variables, and a symbol namespace prefix
  so two modules cannot export the same name silently.
- [ ] Initrd: `kernel/initrd.cpp` reading a tar archive passed through the multiboot
  modules field, `scripts/mkinitrd.sh` building it, the Makefile producing
  `build/initrd.tar` with every out of tree module.
- [ ] Out of tree build: `modules/<name>/Makefile` fragment producing `<name>.ko`
  against installed headers, so a module can be built without the kernel tree
  open.
- [ ] Runtime control surface: `module_load_image(const void*, usize)`,
  `module_load_from_initrd(const char*)`, plus listing and unloading, and a
  `modules` command in the debug console.
- [ ] Autoload: a module declares the device ids it drives, phase 7 asks the loader
  for the module that matches.

**Choosing what gets loaded.** A module system nobody can steer is just a build
trick, so selection lands with the loader rather than after it.

- [ ] Build time selection: a `config` target writing `build/config.mk` and
  `include/eris/config.hpp`, where every module is one of built in, loadable or
  left out. A text menu is enough, the point is that a build can drop the desktop
  and keep the serial console without editing the Makefile.
- [ ] Profiles shipped with the tree: `minimal` for a serial only kernel, `desktop`
  for the full set, `debug` adding the test modules, each a file listing modules
  rather than a branch in the build system.
- [ ] Boot time selection through the kernel command line: `modules.load=a,b,c` to
  force a set, `modules.blacklist=d` to keep one out, `modules.autoload=off` to
  stop device matching from pulling anything in, so a machine that panics in a
  driver can still boot.
- [ ] A manifest in the initrd listing what to load and in which order, with the
  command line overriding it, so the same image serves several machines.
- [ ] Dependency aware selection: asking for `desktop` pulls `vga` and `keyboard`,
  excluding `vga` refuses the selection with a readable reason instead of
  half loading it.
- [ ] Load results reported in the boot log and kept queryable afterwards: what was
  asked for, what loaded, what was skipped and why.

**Done when.** `make` produces `vga.ko` outside the image, the kernel boots
without it, `module_load_from_initrd("vga")` brings the screen up, `module_unload`
gives every page back, loading a module built against a bumped ABI is refused
with a readable message, and booting with `modules.blacklist=desktop` gives a
working serial only system.

**Traps.** `R_X86_64_PC32` overflows once a module lands further than 2 GiB from
the kernel, so module memory has to be allocated near the kernel image. Unloading
while an interrupt handler from that module is running is a use after free, so
the refcount has to cover registered handlers, not just explicit users.

## Phase 7: buses and devices

**Goal.** Drivers find their hardware instead of assuming it.

**Why now.** Storage, audio and USB all start here, and phase 6
autoloading needs something to match against.

**Work**

- [ ] `kernel/device/device.cpp`, `include/eris/device.hpp`:

  ```cpp
  namespace eris {
  class Device {
  public:
      virtual ~Device() = default;
      virtual const char* name() const = 0;
      virtual bool start() = 0;
      virtual void stop() = 0;
  };
  class Driver {
  public:
      virtual bool matches(const DeviceId& id) const = 0;
      virtual Device* probe(BusDevice& device) = 0;
  };
  void driver_register(Driver*);
  void driver_unregister(Driver*);
  }
  ```

- [ ] `kernel/device/pci.cpp`: configuration space through the legacy ports first,
  then MMCONFIG from the ACPI MCFG table, bus enumeration, bridges, BAR decoding
  and sizing, interrupt line and pin, capability list walking.
- [ ] MSI and MSI-X allocation on top of the phase 3 APIC, one vector per queue.
- [ ] DMA helpers: physically contiguous allocation, a buffer type that carries both
  addresses, cache attribute control.
- [ ] `modules/virtio/`: the shared transport, queue setup, descriptor rings, used
  and available handling, so `virtio-blk`, `virtio-gpu` and
  `virtio-input` are thin modules on top.
- [ ] `modules/ahci/`: real SATA for real machines, port setup, command lists, NCQ
  later.
- [ ] `modules/rtc/`: wall clock time from the CMOS, feeding a real `time_of_day`.
- [ ] `modules/ps2/`: split today's keyboard module into a controller plus keyboard
  and mouse devices, with scancode set 2 handling, modifiers, key repeat and a
  key event type instead of a raw character.
- [ ] `modules/fbdev/`: linear framebuffer from the multiboot framebuffer tag or from
  `virtio-gpu`, mode listing, a device other code draws into.

Per driver detail for everything named here lives in [the driver program](#the-driver-program),
tiers 0 to 2.

**Done when.** The kernel prints a PCI device tree with vendor and class names, a
`virtio-blk` module binds to its device through matching alone, no driver
contains a hard coded port number outside the legacy ones, and the loader
autoloads a driver for a device it did not previously know about.

**Traps.** BAR sizing writes all ones and reads back, which needs the device
disabled first. MSI without the APIC in the right mode delivers to nowhere and
looks exactly like a driver bug.

## Phase 8: storage and a file system

**Goal.** The kernel can read and write files.

**Why now.** Configuration, logs, module images and userspace binaries all live
in files. Nothing after this phase works without it.

**Work**

- [ ] `kernel/block/block.cpp`: block device interface, request structures, a queue
  per device, a completion callback, and a thread per device draining the queue.
- [ ] Buffer cache keyed by device and block, write back with an explicit flush, so
  the file system is not doing IO one sector at a time.
- [ ] `kernel/fs/vfs.cpp`, `include/eris/vfs.hpp`:

  ```cpp
  namespace eris::fs {
  class Inode { public: virtual isize read(u64 offset, void* buffer, usize length) = 0; /* ... */ };
  class FileSystem { public: virtual Inode* root() = 0; virtual const char* name() const = 0; };
  int mount(const char* path, FileSystem* fs);
  int open(const char* path, int flags);
  isize read(int fd, void* buffer, usize length);
  isize write(int fd, const void* buffer, usize length);
  int stat(const char* path, Stat& out);
  }
  ```

  with a path walker, a dentry cache, mount points and file descriptor tables
  that will later be per process.
- [ ] `modules/ramfs/`: in memory files and directories, the first mount, and the
  place `/tmp` lives forever after.
- [ ] `modules/devfs/`: every `Device` visible as a node, consoles, block devices,
  the framebuffer, input devices.
- [ ] `modules/ext2/`: read only first, superblock, block groups, inodes, directory
  walking, indirect blocks, then writing, then journalling never.
- [ ] `modules/fat32/`: because an EFI system partition is FAT and real machines need
  it.
- [ ] Module loading from the file system, retiring the initrd path to a bootstrap
  role only.
- [ ] Kernel log to a file once a writable file system is mounted, with the ring
  buffer carried over from early boot.

**Done when.** The kernel mounts ramfs at the root, devfs at `/dev`, opens
`/dev/serial0` and writes to it, mounts an ext2 image from `virtio-blk`, reads a
file from it, and loads a module out of that file system.

**Traps.** The buffer cache and the block queue are the first structures with
real concurrency, so phase 4 primitives are not optional here. Path walking with
symlinks and mount points crossing each other is where every kernel grows its
first ugly function, so keep the walker small and tested.

## Phase 9: userspace

**Goal.** Programs run in ring 3, and a fault in one kills the program rather
than the machine.

**Why now.** This is the line between a kernel and an operating system, and the
desktop phase depends entirely on it.

**Work**

- [ ] `kernel/proc/process.cpp`: a process owning an `AddressSpace`, a file
  descriptor table, a working directory, a thread list, an exit code and a parent
  link.
- [ ] Ring 3 entry: TSS `rsp0` per CPU, `syscall` and `sysret` with `MSR_STAR`,
  `MSR_LSTAR` and `MSR_SFMASK`, a syscall entry stub that switches stacks and
  saves the user frame.
- [ ] `kernel/proc/syscall.cpp`: a syscall table with an argument count and a
  validating wrapper per entry. Start with exit, write, read, open, close, seek,
  stat, mmap, munmap, spawn, wait, getpid, sleep, ioctl.
- [ ] `kernel/proc/uaccess.cpp`: `copy_from_user` and `copy_to_user` validating every
  range against the process address space, with fault fixups rather than a
  pre check that races.
- [ ] `kernel/proc/elf.cpp`: static ELF64 executable loading, segment mapping with
  correct protections, a fresh stack carrying argv, envp and an auxiliary vector.
- [ ] Process lifecycle: spawn, wait, exit, orphan reparenting, resource teardown on
  exit including mappings and open files.
- [ ] Signals, or a simpler event delivery mechanism, enough for a terminal to
  interrupt a program.
- [ ] IPC: at minimum pipes, plus a message port with handles, since the desktop in
  phase 10 needs a real channel and retrofitting one is painful.
- [ ] `user/libc/`: a small static libc, `crt0`, `malloc` over `mmap`, `stdio`
  through file descriptors, `string.h`, and the syscall stubs.
- [ ] `user/init/`, `user/sh/`, `user/coreutils/`: init that mounts and starts the
  shell, a shell with pipes and redirection, and the dozen utilities that make
  the system inspectable.
- [ ] `modules/tty/`: line discipline, canonical mode, echo, control characters, the
  thing that turns a serial port into a terminal.

**Done when.** The kernel starts `/bin/init` from ext2, init starts a shell on
the serial console, `ls | grep something` works, a program that dereferences null
dies alone with a message, and the shell survives it.

**Traps.** Every pointer that crosses the boundary is a security bug until it is
validated. `sysret` has sharp edges around non canonical addresses, which is one
of the classic privilege escalation paths, so follow the checks the manual asks
for exactly.

## Phase 10: graphics and the desktop

**Goal.** A desktop worth showing: a userspace window server on a real
framebuffer, hardware cursor, smooth redraw, a toolkit, and applications that use
it. The current in kernel `desktop` module is a placeholder and retires here.

**Why now.** Everything it needs exists after phase 9: processes, IPC, a
framebuffer device, input devices and threads. Building it earlier means building
it twice.

### 10.1 Kernel side, kept deliberately thin

- [ ] `modules/fbdev/`: mode setting, double buffering, a mapping of the framebuffer
  into a client address space, damage reporting, vsync events where the hardware
  offers them.
- [ ] `modules/virtio_gpu/`: resource creation, transfers and flushes, plus mode
  setting, so QEMU gets a fast path instead of a scanout copy.
- [ ] `modules/input/`: an event device with a shared ring buffer, keyboard events
  with keycodes and modifiers, mouse events with relative motion, buttons and
  wheel, absolute pointers for tablets and QEMU's absolute mouse.
- [ ] Hardware cursor plane where available, a composited cursor where not, so the
  pointer never lags the redraw.
- [ ] A shared memory mechanism between processes, since window buffers must not be
  copied twice per frame.

### 10.2 The window server

`user/wsrv/`, a single process owning the framebuffer:

- [ ] Compositor with a damage model: clients submit buffers, the server composites
  only the changed rectangles, and a full screen redraw is the exception.
- [ ] Double or triple buffering with a frame clock, targeting a steady 60 frames per
  second rather than redrawing as fast as the loop spins.
- [ ] Window management: stacking order, focus, move and resize, minimise and
  maximise, snapping, virtual desktops.
- [ ] Client protocol over the phase 9 IPC: create surface, attach buffer, commit
  damage, receive input, receive configure events. Versioned from the first
  commit, because every client depends on it.
- [ ] Input routing: focus follows click, keyboard grabs, pointer grabs during a
  drag, a global hotkey path that reaches the server before any client.
- [ ] Multi monitor once mode setting reports more than one output.
- [ ] Effects that cost nothing to get right early: alpha blended window shadows,
  fade in and out, and a smooth minimise, all driven by the frame clock rather
  than sleeps.

### 10.3 Rendering and text

`user/libgfx/`:

- [ ] Software rasteriser: filled and stroked rectangles, rounded rectangles, lines,
  circles, alpha blending, clipping, and a blit fast path for the common case.
- [ ] Framebuffer format handling and a colour type, so a 32 bit and a 16 bit mode
  do not fork the drawing code.
- [ ] Font rendering: a bitmap font to bring text up, then TrueType parsing with a
  glyph cache, kerning, hinting good enough to be readable, and subpixel or
  greyscale antialiasing.
- [ ] Image decoding for at least one format, so icons and wallpapers exist.
- [ ] Optional later: a tiny scene graph so the toolkit is not redrawing from scratch.

### 10.4 Toolkit and applications

`user/libui/` and `user/apps/`:

- [ ] Widgets: window, layout containers, button, label, text field, list, scroll
  view, menu, dialog, checkbox, slider, tab strip.
- [ ] Event loop per application with timers and IPC integrated, so an app never
  polls.
- [ ] Theming: colours, spacing, corner radius and font in one place, light and dark,
  so the system looks like one system.
- [ ] Applications worth having, in this order: a terminal that is genuinely fast, a
  file manager, a text editor, a system monitor showing threads and memory, an
  image viewer, and a settings panel.
- [ ] A panel and launcher: clock, running applications, a menu, notifications.

Kernel side drivers for this phase are in [the driver program](#the-driver-program),
tier 4.

**Done when.** The machine boots to a login or straight to a session, the panel
and wallpaper are drawn, a terminal opens and runs the shell at a usable speed,
two windows can be dragged over each other without tearing, the pointer stays
smooth while a window redraws, and killing the window server restarts it without
taking the system down.

**Traps.** A compositor that redraws the whole screen per frame will look fine on
QEMU and terrible on real hardware, so build the damage model first, not after.
Deciding the client protocol casually is the mistake that is impossible to undo
once three applications depend on it.

## Phase 11: audio

**Goal.** Sound that does not stutter.

**Work**

- [ ] `modules/hda/` for real hardware and `modules/virtio_snd/` for QEMU: stream
  descriptors, ring buffers, interrupt driven refill.
- [ ] A kernel side mixer with per stream volume and a single output format, so a
  device driver never sees more than one stream.
- [ ] An audio device under devfs with a ring buffer clients write into, plus a
  latency target and underrun reporting.
- [ ] `user/libaudio/` and a sound daemon if the mixer needs to live in userspace,
  which is the likelier answer once more than two clients exist.

Driver detail in [the driver program](#the-driver-program), tier 5.

**Done when.** Two programs play at the same time, volume is per program, and
there is no underrun during a full screen window drag.

## Phase 12: USB and real hardware

**Goal.** eris boots and is usable on a physical machine.

**Work**

- [ ] `modules/xhci/`: controller init, command and event rings, transfer rings,
  port and device enumeration, hubs.
- [ ] `modules/usb_hid/`: keyboard and mouse, report descriptor parsing.
- [ ] `modules/usb_storage/`: bulk only transport and SCSI commands, feeding the
  phase 8 block layer.
- [ ] Real machine boot: UEFI entry beside multiboot, GOP framebuffer, memory map
  translation, and an installer or at least a bootable USB image.
- [ ] NVMe, because SATA is not where storage lives any more.

Driver detail in [the driver program](#the-driver-program), tier 3.

**Done when.** eris boots from a USB stick on a physical machine, with USB input
working, a framebuffer at native resolution and an NVMe disk visible.

## Phase 13: power, security and self hosting

**Goal.** The system behaves like a system, not a demo.

**Work**

- [ ] ACPI beyond tables: enough AML interpretation for shutdown and reboot, power
  button events, and battery reporting on laptops.
- [ ] CPU idle states in the idle thread, and frequency scaling if it proves cheap.
- [ ] Security hardening: SMEP and SMAP enabled, KASLR for the kernel image, stack
  protector with a real canary, `.rodata` after init made read only, and user
  copy routines audited as a set.
- [ ] Permissions: users, groups, file modes, and a privilege check on the syscalls
  that need it.
- [ ] Self hosting: port a compiler and an assembler, so eris can build eris. The
  final proof that the libc and the file system are real.

**Done when.** The power button shuts the machine down cleanly, SMEP and SMAP are
on, and the tree builds under eris itself.

## Phase 14: installation and setup

**Goal.** Someone who is not us can put eris on a machine, choose what goes on
it, and end up with a system that boots from its own disk.

**Why now.** Everything it needs exists: storage, a file system, userspace, a
desktop and loadable modules. Before that there is nothing to install and no way
to choose anything.

**The shape of it.** The installer is an ordinary eris program running on a live
image, not a special mode of the kernel. What it does is pick a disk, lay out a
file system, copy the system onto it, write a module selection, and install a
boot loader. Nothing it needs comes from the outside world, so the media carries
every module that can be installed.

### Live image

- [ ] `scripts/mkimage.sh` building a bootable image: kernel, initrd, the module
  store, the userspace tree and the installer, as an ISO for optical and USB boot
  and a raw image for direct writing.
- [ ] Boot menu offering the live session, the installer, and a rescue shell with a
  minimal module set, so a machine that hangs on a driver still gives a prompt.
- [ ] A live session that runs fully from memory, with a writable ramfs overlay, so
  the hardware can be tried before anything on disk is touched.
- [ ] Hardware probe on the live image writing a report: CPUs, memory, PCI devices,
  disks, displays, input devices, and which modules bound to what. The same report
  is what a bug about unsupported hardware should carry.

### The module store

- [ ] Every module the release ships, built as a `.ko`, kept on the media with a
  manifest: name, version, license, dependencies, size, the hardware it drives,
  and a one line description.
- [ ] Manifest signing or at least hashing, verified before a module is installed,
  so a damaged medium fails loudly rather than installing a broken driver.
- [ ] Module sets as data, not code: `minimal`, `desktop`, `developer`, each a list
  referring to the manifest, editable by hand and by the installer.

### Choosing what to install

- [ ] A selection screen listing modules grouped by what they are for: storage,
  input, graphics, audio, USB, platform. Each row shows the description, the size
  and whether the live probe found hardware for it.
- [ ] Dependencies resolved while selecting: ticking `desktop` ticks `vga` and
  `keyboard`, unticking something another selection needs explains what would
  break instead of silently letting it happen.
- [ ] A recommended selection precomputed from the hardware probe, so the default
  path is one keypress, and a full manual mode for everything else.
- [ ] A summary before anything is written: the disk to be changed, the file system
  to be made, the module list and the total size, with the chance to go back.
- [ ] The selection written to `/etc/modules.conf` on the target, which is the file
  the boot path reads afterwards, so what the installer chose and what an
  administrator edits later are the same thing.

### Putting it on disk

- [ ] Disk selection listing every block device with its size and model, with a
  clear refusal to touch the medium the installer is running from.
- [ ] Partitioning: a guided whole disk layout and a manual editor, GPT first, MBR
  for old firmware, with an EFI system partition when the machine booted through
  UEFI.
- [ ] File system creation on the target, the phase 8 file system, plus a FAT32 EFI
  partition where one is needed.
- [ ] Copying the system with progress and a verification pass, since a silent bad
  copy is a debugging session that never ends.
- [ ] Boot loader installation: GRUB for the common case, with the kernel command
  line prefilled from the choices made, and the module manifest placed in the
  initrd.
- [ ] Root user creation and the first account, keyboard layout, time zone and
  host name, all written to `/etc` as plain files.

### After the first boot

- [ ] `erisctl`, the tool that does at runtime what the installer did once:
  `erisctl module list`, `install`, `remove`, `enable`, `disable`, `info`, reading
  the same manifest from the installation media or from a local store.
- [ ] Module changes taking effect either immediately through `module_load` or at
  the next boot through `/etc/modules.conf`, with the tool saying which one it
  did.
- [ ] A settings panel in the desktop wrapping the same operations, so the
  graphical path and the command line path share one implementation.
- [ ] Recovery: booting with `modules.load=` reduced to the minimum from the boot
  menu, and a rescue shell that can repair `/etc/modules.conf` after a bad choice.
- [ ] Upgrades kept honest: a new release is a new medium, the installer can update
  a system in place, and the module store on disk is replaced as a whole. No
  partial upgrades, no fetching anything from anywhere.

**Done when.** A blank QEMU disk plus the release image produce a booting system:
the installer runs, the hardware probe fills in a sensible default selection, a
manual selection of modules survives into `/etc/modules.conf`, the machine boots
from its own disk with exactly the chosen set, `erisctl module install` adds one
afterwards, and a deliberately bad selection can be undone from the rescue shell.

**Traps.** An installer that writes before it verifies costs someone their disk.
Choosing modules the machine has no hardware for should be allowed but marked,
because a disk driver installed for the wrong controller is discovered on the
reboot, when the system does not come up.

## The driver program

The phases above say when a driver becomes possible. This chapter says what each
one actually involves, because drivers are where most of the remaining work
lives and where vague plans turn into three weeks of guessing.

Tiers are ordered by what they depend on, not by importance. Everything in tier 0
works with what the kernel has today. Tier 1 needs PCI and the loadable module
loader. Later tiers build on tier 1.

### The contract every driver follows

Before writing a single register poke, a driver in this tree looks like this:

* A class deriving from `Device`, owning its state, with private members. No
  driver state outside the class, no globals shared between drivers.
* A `Driver` object registered with the bus layer, declaring what it binds to:
  a PCI vendor and device id list, a class code, or a legacy fixed resource for
  tier 0 devices.
* `probe` claims the device, maps its resources and returns a `Device`, or
  returns null without side effects. `start` brings it up, `stop` puts it back
  exactly as it was found, including masking its interrupt and freeing its DMA
  memory.
* A module descriptor with dependencies and a license, and an `extern "C"` ABI
  with `ERIS_EXPORT_SYMBOL` for anything another module calls.
* Interrupt handlers do the minimum: read the status register, acknowledge,
  move completed work to a queue, wake a thread. No allocation, no locks held
  across a wait, no printing except on a real error.
* Every wait on hardware has a timeout. A driver that spins forever on a status
  bit takes the machine down on hardware that behaves slightly differently.
* Every value read from hardware or from a device provided structure is treated
  as untrusted: lengths, counts, offsets and indices get bounds checked before
  use. A malicious or broken device must not be able to walk kernel memory.
* Resources are released in reverse order of acquisition, so `stop` is a mirror
  image of `start` and unloading leaks nothing.
* A quirk table where the hardware needs it, kept next to the driver rather than
  sprinkled through the code.

Supporting infrastructure every driver shares, built once in phase 7:

* `Mmio` wrapper with explicit width accessors and compiler barriers, so a
  register read is never optimised away or reordered.
* `DmaBuffer`, carrying both the virtual and the physical address, allocated
  physically contiguous with the right cache attributes, with an address mask
  for devices that cannot reach above a limit.
* IRQ registration that works the same for a legacy line, an MSI vector and an
  MSI-X table entry, so a driver does not care which one it got.
* A per device log prefix, so a message says which device complained.

### Tier 0: legacy devices, no PCI needed

These need nothing beyond what exists today plus the phase 3 APIC work, and they
are what makes the machine usable while the rest is built.

**16550 UART, `modules/serial/`.** Today's early console grows into a real
driver. Ports at 0x3F8, 0x2F8, 0x3E8, 0x2E8 probed by writing and reading back
the scratch register. Divisor latch for the baud rate, line control for 8N1,
FIFO control with a trigger level, modem control for loopback self test. Then the
part that does not exist yet: interrupt driven receive and transmit on IRQ4 and
IRQ3, a receive ring buffer, a transmit queue that blocks the writer when full,
line status error counters for overrun, parity and framing, and optional RTS and
CTS flow control. Becomes the backend of the phase 9 tty.

**i8042 controller, `modules/i8042/`.** The current keyboard module skips the
controller entirely, which works on QEMU and is a coin flip elsewhere. The real
sequence: disable both ports, flush the output buffer, read the configuration
byte with command 0x20, clear the interrupt and translation bits, write it back
with 0x60, run the controller self test 0xAA and expect 0x55, test port one with
0xAB, enable the ports, reset each device with 0xFF and expect 0xFA then 0xAA,
then identify it. Dual channel detection, IRQ1 for the first port and IRQ12 for
the second, a status register check before every read so a mouse byte is never
parsed as a key, and a timeout everywhere because a missing controller answers
nothing at all.

**PS/2 keyboard, `modules/ps2kbd/`.** Scancode set 2 with the translation layer
off, a state machine for the 0xE0 and 0xE1 prefixes so the arrow keys, the right
modifiers and pause are distinguishable, key press and release rather than a
character, a keycode to keysym layer with layouts kept as data, modifier and lock
state, LED updates through command 0xED, typematic rate with 0xF3, and an event
queue with a subscriber list. The character stream today becomes one consumer of
that, not the interface.

**PS/2 mouse, `modules/ps2mouse/`.** Enable through the controller's write to the
second port, defaults with 0xF6, data reporting with 0xF4, the three byte packet
with overflow and sign bits, the Intellimouse sample rate knock of 200, 100, 80
to unlock the fourth byte for the wheel, then 200, 200, 80 for five buttons.
Packet resynchronisation when a byte is lost, because it always happens
eventually. Absolute pointer support comes from USB or virtio-input instead,
which matters for a comfortable QEMU experience.

**CMOS RTC, `modules/rtc/`.** Index port 0x70 with NMI masking and data port
0x71, register B to learn whether the values are BCD or binary and 12 or 24 hour,
the update in progress flag polled so a read never straddles a tick, a second
read to confirm, century from the ACPI FADT rather than a guess, and the periodic
interrupt on IRQ8 as a fallback timer. Feeds a wall clock that the monotonic
clock is anchored to at boot.

**Legacy timers.** The PIT stays as the calibration source and the fallback. The
HPET driver arrives with phase 3: capability register for the tick period and
comparator count, the main counter as a monotonic source, comparators programmed
in one shot mode, and legacy replacement mode turned off once the APIC timer
takes over.

**VGA text and VBE, `modules/vga/`.** The current module gains what it lacks:
cursor positioning through the CRTC index and data ports, palette control, and
mode information taken from the multiboot framebuffer tag so a graphical mode
works at all. It stays as the fallback console after `fbdev` exists.

### Tier 1: PCI and the virtio family

**PCI core, `kernel/device/pci.cpp`.** Configuration space through the legacy
0xCF8 and 0xCFC pair first, then memory mapped configuration from the ACPI MCFG
table, which is required for anything past bus 255 worth of devices and for
extended capabilities. Recursive enumeration following bridges rather than a flat
scan of every bus. Header type parsing, BAR decoding for memory and IO, 32 and 64
bit, with sizing done by writing all ones and reading back while the device is
disabled. Capability list walking for MSI, MSI-X, power management and PCI
Express. Bus mastering enabled explicitly per device rather than globally. A
device tree printed at boot with vendor, device, class and revision resolved
through a small compiled in table.

**MSI and MSI-X.** Message address and data built from the phase 3 APIC layout,
one vector per queue for devices that want several, the MSI-X table mapped from
its own BAR with the pending bit array respected, and a clean fallback to the
legacy line when the firmware or the device refuses.

**virtio transport, `modules/virtio/`.** The shared half every virtio device
uses, exported so each device driver is thin. Modern PCI layout discovered
through the vendor capabilities that point at the common, notify, ISR and device
configuration structures, with the legacy IO port layout kept as a fallback since
older QEMU still offers it. Feature negotiation with the status register driven
through reset, acknowledge, driver, features ok, driver ok, and a failure path
that sets the failed bit rather than hanging. Split virtqueues: the descriptor
table, the available ring the driver writes, the used ring the device writes,
indirect descriptors for long scatter lists, the event index feature to cut
notifications, and correct memory barriers around every index update, which is
where virtio drivers actually go wrong. Packed rings later if it proves worth it.

**virtio-blk, `modules/virtio_blk/`.** Configuration space for capacity, block
size, topology and the read only flag. Requests are a header with type and
sector, the data descriptors, and a status byte the device writes. Read, write,
flush and discard. A request per block layer entry with a completion callback, a
queue depth that matches the ring size, and correct handling of a used ring that
completes out of order. The first real storage in the tree and the one the phase 8
file system work is developed against.

**virtio-gpu, `modules/virtio_gpu/`.** The fast path for graphics under QEMU.
Display info to learn the scanouts and their sizes, EDID where offered, 2D
resource creation, attaching guest memory as backing, transfer to host for the
damaged rectangle only, set scanout once, resource flush per frame, and the
separate cursor queue so moving the pointer never touches the main queue.
Multiple scanouts for multi monitor. No 3D, that is a different project.

**virtio-input, `modules/virtio_input/`.** Keyboard, mouse and tablet as clean
event sources without the PS/2 state machines. Configuration select and subselect
to read the device name, the identifier and the supported event bits, then an
event queue carrying the same event structure the input core uses. The absolute
pointer here is what makes the QEMU pointer feel right.

**virtio-snd, virtio-rng, virtio-balloon.** Sound in phase 12, entropy for the
random pool as a two hundred line module, and ballooning only if memory pressure
ever becomes interesting.

### Tier 2: real storage

**AHCI, `modules/ahci/`.** SATA on almost every machine made in the last fifteen
years. BAR5 is the HBA memory. Read the capability register for the port count,
command slots, 64 bit addressing and NCQ support, take ownership from the BIOS
through the handoff bits when the enclosure requires it, reset the HBA through
the global control register, enable AHCI mode, then per port: stop the command
engine, allocate the command list and the received FIS area, program their
addresses, clear errors, wait for the port to report a device present with a
stable communication status, start the engine. A command is a header pointing at
a command table holding the FIS and a physical region descriptor table for the
data. Identify device gives the model, the sector size, the LBA48 support and the
queue depth. Read and write DMA extended for the basic path, first party DMA
queued commands for NCQ once the block layer benefits. Error recovery matters
more than the happy path: a task file error requires stopping the engine,
clearing the error register, restarting, and reissuing the outstanding commands,
and a port that will not come back needs a port reset rather than a hang.

**NVMe, `modules/nvme/`.** Where storage actually lives now. BAR0 holds the
controller registers. Read the capability register for the doorbell stride, the
maximum queue entries and the required memory page size, disable the controller
through the configuration register, wait for the status register to report not
ready, program the admin queue attributes and the admin submission and completion
queue addresses, set the page size and the command set, enable and wait for
ready. Identify controller and identify namespace give the geometry and the
limits. Create one IO queue pair per CPU once phase 4 lands, each with its own
MSI-X vector, so completion handling scales. Data transfer through PRP lists,
with the two entry shortcut for small requests and a PRP list page for large
ones, or SGLs if the controller advertises them. Flush, dataset management for
trim, and asynchronous event requests for media errors. The completion path is a
phase tag flip rather than a valid flag, which is the detail every first NVMe
driver gets wrong.

**ATA PIO, `modules/ata/`.** Not fast, but it boots on anything and it is a
hundred lines. Identify through the command port, LBA28 and LBA48 addressing,
sector at a time transfers through the data port, the busy and data request bits
polled with a timeout, and a device selection delay that is easy to skip and
produces impossible bugs. Kept as the fallback when neither AHCI nor NVMe binds.

**Partitions and the boot path.** An MBR and GPT parser in the block layer rather
than in each driver, so `virtio-blk`, AHCI and NVMe all expose partitions the
same way, with GPT header and entry checksums actually verified.

### Tier 3: USB

**xHCI, `modules/xhci/`.** The only host controller worth writing, since it
handles USB 1 through 3 and every machine has one. Capability registers give the
operational, runtime and doorbell offsets and the structural parameters, which
say how many slots, interrupters and ports exist. Bring up: stop the controller,
reset it, wait for the controller not ready bit to clear, program the device
context base address array, allocate the command ring and the event ring with its
segment table, set the interrupter's event ring address and the moderation value,
set the maximum slots enabled, start the controller. Port routing separates USB 2
and USB 3 ports through the extended capabilities, so a device appears once
rather than twice. Device bring up is a sequence of commands on the command ring:
enable slot, address device with the input context describing endpoint zero,
read the descriptors, configure endpoint for the rest. Transfers are transfer
request blocks on per endpoint rings, with completion events arriving on the
event ring, and the cycle bit as the ownership marker.

**USB core, `modules/usb/`.** Descriptor parsing for device, configuration,
interface and endpoint. Control transfers with the standard requests. Address
assignment, configuration selection, and a driver binding layer matching on
class, subclass and protocol or on vendor and product, wired into the phase 6
autoload path. Hub support, since keyboards live behind hubs and monitors, with
port power, reset and status change handling. Transfer abstractions for control,
interrupt, bulk and isochronous, the last only when audio wants it.

**USB HID, `modules/usb_hid/`.** Boot protocol first, because it is eight bytes
and works everywhere, then report protocol with an actual report descriptor
parser, which is what makes tablets, extra mouse buttons and media keys work.
Keyboard, mouse and absolute pointer feeding the same input core as PS/2 and
virtio-input.

**USB mass storage, `modules/usb_storage/`.** Bulk only transport: a command
block wrapper out, data in or out, a command status wrapper back, with the reset
recovery sequence when a transfer stalls. SCSI on top: inquiry, test unit ready,
read capacity, read and write in the ten byte forms. Presents a block device, so
a USB stick is just another disk to the file system.

### Tier 4: graphics

**Framebuffer core, `modules/fbdev/`.** The device everything draws through. Mode
information, a mapping into a client address space, damage submission, page
flipping between two or three buffers, and a vsync event where the hardware can
report one. Backed by virtio-gpu under emulation, by the boot loader's linear
framebuffer on real hardware, and by VGA text as the last resort.

**EDID and modes.** Parse the EDID blob for the preferred mode and the physical
size, so the desktop picks a sane resolution and knows the pixel density instead
of assuming 1024 by 768.

**Native modesetting.** Intel, AMD and NVIDIA modesetting is explicitly out of
scope until much later, and probably forever for NVIDIA. The boot framebuffer
plus virtio-gpu covers emulation and most real machines at native resolution
through UEFI GOP.

### Tier 5: audio

**Intel HDA, `modules/hda/`.** The standard audio controller on real machines.
Reset the controller through the global control register, wait for the codec
state change status to report which codecs answered, set up the command output
ring buffer and the response input ring buffer with their sizes and addresses,
then walk the codec: root node, function groups, and the widget graph of audio
outputs, mixers, selectors and pin complexes, following connection lists to find
a path from a stream to a jack that is actually connected. Stream descriptors get
a buffer descriptor list of cyclic buffers, a format encoding the rate, depth and
channels, and a position buffer for the playback pointer. Interrupt on buffer
completion, refill, and report an underrun rather than clicking. Jack detection
for headphone insertion.

**AC97 and virtio-snd.** AC97 as a much simpler fallback for old emulation, and
virtio-snd as the clean path under QEMU: PCM stream setup, period based buffers
and channel maps, without the codec graph walk.

### Tier 6: platform and the rest

**ACPI beyond tables, `kernel/acpi/`.** Enough AML to evaluate the objects that
matter: the sleep states for shutdown and reboot, the power button event through
the fixed event registers, and on laptops the embedded controller, the battery
and the thermal zones. A full interpreter is a project of its own, so the goal is
the narrow set, with a clear refusal to grow beyond it without a reason.

**SMBus and I2C, `modules/i2c/`.** Needed for the embedded controller, for sensor
reading and for anything on a development board later.

**IOMMU, `kernel/device/iommu.cpp`.** Intel VT-d and AMD-Vi, at first only to run
devices in passthrough safely, later to contain a misbehaving device's DMA. This
is the difference between trusting every driver absolutely and not having to.

**Watchdog and reboot paths.** The ACPI reset register, the keyboard controller
pulse as the fallback, and a triple fault as the last resort, so a reboot request
always terminates.

### QEMU test matrix

Every driver needs a way to be exercised in CI, and QEMU offers one for almost
all of them.

| Driver | QEMU flags | How the test proves it |
| --- | --- | --- |
| serial | `-serial file:boot.log` | Boot log arrives complete |
| i8042, ps2kbd, ps2mouse | default, `-device i8042` | Injected keystrokes appear as events through the monitor |
| rtc | default | Wall clock matches the host within a second |
| hpet | `-machine hpet=on` | Monotonic clock drift stays inside a bound |
| pci core | any | Device list matches the expected topology |
| virtio-blk | `-drive if=virtio,file=disk.img` | A known file reads back byte identical |
| virtio-gpu | `-device virtio-gpu` | Screendump matches a reference image |
| virtio-input | `-device virtio-tablet-pci` | Absolute pointer events land at the right pixel |
| ahci | `-device ich9-ahci -drive if=none...` | Same disk image test as virtio-blk |
| nvme | `-device nvme,serial=deadbeef` | Same disk image test, plus a queue per CPU |
| ata | `-drive if=ide` | Same disk image test at lower speed |
| xhci, usb_hid | `-device qemu-xhci -device usb-kbd -device usb-tablet` | Input events arrive through USB |
| usb_storage | `-device usb-storage,drive=...` | Disk image test over USB |
| hda | `-device intel-hda -device hda-duplex` | A generated tone captured on the host matches |
| virtio-snd | `-device virtio-sound-pci` | Same tone test |

Each row becomes a CI job once the driver exists, reusing the harness from the
testing section, so a driver that regresses fails a named check rather than being
noticed months later.

### Driver quality bar

A driver is finished when, beyond working:

* It survives its device disappearing, either through hot unplug or through a
  controller reset, without taking the kernel down.
* It can be unloaded and reloaded ten times with no page leak and no duplicated
  IRQ registration.
* Every hardware wait has a timeout with a log line naming what timed out.
* Its interrupt handler is bounded and does no allocation.
* It reports errors up rather than retrying forever in silence.
* A fuzzed configuration space or a device that returns garbage produces a
  refused probe rather than a fault.
* It has an entry in the test matrix and a job in CI.

## Running alongside: testing and hygiene

Not a phase, work that grows with each of the above.

- [ ] In kernel test suite: `kernel/test/`, tests registered through a section the
  way modules are, run when the command line asks for it, results over serial,
  QEMU exit code set through the debug exit device so CI can gate on it.
- [ ] Per subsystem tests written with the subsystem, allocator, paging, scheduler,
  VFS, so a refactor has something to fail against.
- [ ] Fault injection: an allocator mode that fails every nth allocation, so error
  paths are executed rather than assumed.
- [ ] Poisoned freed memory, a guarded heap mode with red zones, and a use after free
  check on the slab allocator.
- [ ] Recorded boot logs in CI, compared against the previous run, so an unexpected
  new line is noticed.
- [ ] Performance baselines once phase 5 exists: context switch cost, syscall cost,
  page fault cost, tracked per commit.
- [ ] `kallsyms` style symbol table from phase 1 kept accurate as the image grows.
- [ ] A debug console module with commands for modules, memory, threads, devices and
  mounts, which pays for itself in every phase after 5.

## Order and dependencies

```
1  fault survival ......... done
2  virtual memory ......... done except the higher half move
3  apic and time .......... done
4  locks and SMP .......... done
5  threads ................ done
6  loadable modules ....... needs 2, much better with 5
7  buses and devices ...... needs 3 and 6
8  storage and VFS ........ needs 7
9  userspace .............. needs 2, 5, 8
10 graphics and desktop ... needs 7, 9
11 audio .................. needs 7, 9
12 USB and real hardware .. needs 7
13 power and security ..... needs 9
14 installation ........... needs 8, 9, 10, and 6 for the module store
```

Phase 6 is what makes eris what it claims to be, and phases 1 to 3 exist so that
phase 6 has somewhere safe to load code into. Phase 10 is the one people will
see, and it is worth the wait, because a desktop built before phase 9 is a
drawing loop in the kernel rather than a desktop.

## Version milestones

Every release carries a code name. The theme is the far end of the solar system,
the cold bodies out past Neptune where the kernel takes its own name from. The
name is picked when the milestone opens, printed in the boot banner, used in the
tag and never reused.

| Version | Code name | Contents | State |
| --- | --- | --- | --- |
| 0.1 | Dysnomia | Boot, interrupts, memory, the module framework, three built in modules | released |
| 0.2 | Sedna | Phases 1 and 2. Panics with a backtrace, W^X, real page table API. The higher half move waits for the boot path work | phase 1 and most of 2 landed |
| 0.3 | Quaoar | Phase 3. ACPI tables, APIC, nanosecond clock, timer subsystem | landed |
| 0.4 | Orcus | Phases 4 and 5. Locks, SMP, threads, scheduler, wait queues | landed |
| 0.5 | Makemake | Phase 6. Out of tree modules loaded from an initrd, versioned ABI | planned |
| 0.6 | Haumea | Phases 7 and 8. PCI, virtio, block layer, VFS, ext2, devfs | planned |
| 0.7 | Gonggong | Phase 9. Ring 3, syscalls, libc, init and a shell | planned |
| 0.8 | Varuna | Phase 10. Window server, toolkit, terminal, panel | planned |
| 0.9 | Ixion | Phases 11 and 12. Audio, USB, boots on real hardware | planned |
| 1.0 | Charon | Phases 13 and 14. Power management, hardening, self hosting, the installer | planned |

Names past 1.0 keep the theme: Salacia, Varda, Chaos, Huya, Arrokoth, Eunomia.

The banner prints `eris 0.1 "Dysnomia" (x86_64, c++23)`, the tag is `v0.1`, and
the release title is `eris 0.1 Dysnomia`. The name lives in one place,
`include/eris/version.hpp`, and the build and the release workflow both read it
from there.
