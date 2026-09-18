# Working on eris

eris is a barebone x86_64 kernel written in C++23. The kernel core stays small:
boot, CPU tables, physical memory, heap, timer and a module framework. Anything
that is not required to reach a usable module loader belongs in a module.

Read this file before touching the tree. `CLAUDE.md` carries the live map of
what currently exists and must be updated with every change. `ROADMAP.md` says
what comes next and in which order, which is the fastest way to tell whether a
change is early. `CONTRIBUTING.md` says the same things for people rather than
agents, `MAINTAINERS` says who looks after each area, and `CREDITS` lists who has
worked on it.

## Build and verify

```
make                 # build build/eris.elf and the 32-bit copy for QEMU
make run             # VGA window plus serial on stdout
make run-serial      # serial only, no window
make clean
./scripts/boot-test.sh    # build must exist, boots QEMU and checks the log
./scripts/check-spdx.sh   # SPDX header audit
./scripts/check-modules.sh
./scripts/faultinject.sh  # drives the panic path on purpose
```

A change is not finished until `make` is clean and `./scripts/boot-test.sh`
passes. Anything touching the fault, panic or interrupt paths also runs
`./scripts/faultinject.sh`. The build runs with `-Wall -Wextra -Werror`, so a warning is a failure.

## What this kernel will never have

**No networking, ever.** eris is closed to the outside world on purpose. There
will be no network card drivers, no protocol stack, no sockets, no remote shell,
no remote debugging and no remote management. Do not add one, do not propose one
as a module, and do not leave a hook for one in an API you design. The only ways
in and out of the machine are its console, its disks and its removable media, and
anything that needs data from elsewhere gets it by having that data written onto
a disk image beforehand.

This also rules out anything that reaches outward on its own: telemetry, update
checks, crash reporting, license checks, clock synchronisation over a wire.

If a task looks like it needs the network, it is the wrong task. Say so instead
of finding a way around it. `ROADMAP.md` lists this under non goals along with
vendor firmware blobs, which are out for the same reason.

## Environment rules

The kernel is freestanding. There is no libc, no exceptions, no RTTI, no SSE and
no red zone. Only these headers from the toolchain are allowed: `<cstddef>` and
`<cstdint>`, both already pulled in by `include/eris/types.hpp`. Everything else
comes from the tree.

Global `operator new` is wired to `kmalloc` and is `noexcept`, so a failed
allocation hands back `nullptr` instead of throwing. Every `new` expression has
to check the result, and nothing may allocate before `heap_init` runs. For page
granular memory go to the page allocator directly.

The boot stub identity maps the first gigabyte with 2 MiB pages. Physical and
virtual addresses are the same below that limit, and the page allocator refuses
to hand out anything above it. Do not assume a higher half mapping.

## Code style

Four spaces, no tabs, 100 column soft limit. Braces on the next line for
functions, same line for control flow. `.clang-format` in the root is the
authority, run `clang-format -i` on files you touch.

Naming follows the existing tree: `PascalCase` for types, `snake_case` for
functions, variables and file names, trailing underscore for private data
members, `SCREAMING_CASE` only for macros.

Kernel code lives in `namespace eris`, architecture code in `eris::arch`, module
internals in `eris::modules` plus an anonymous namespace. A module's public ABI
is the only thing that leaves a namespace, and it leaves as `extern "C"`.

## Object oriented design

* Model a device or subsystem as a class that owns its state. `Console` in
  `include/eris/console.hpp` is the pattern: an abstract interface, concrete
  implementations in the modules that provide them.
* Keep data members private with a trailing underscore. Expose behaviour, not
  fields. A getter that only forwards a field is fine, a public field is not.
* Virtual functions are allowed and used. Vtables work because the constructors
  run through `.init_array` before `kernel_main` calls into anything.
* Prefer composition. Inherit only to implement an interface such as `Console`.
* One responsibility per class. The VGA class paints cells, the desktop module
  decides what a window looks like, the module framework decides load order.
* Keep the single instance of a driver in an anonymous namespace inside the
  module and reach it through the module ABI. Do not spread mutable globals
  across the tree.
* Constructors must not fail. Do the work that can fail in the module init
  function and return a negative value from it instead.
* Mark state that must survive zero initialisation with `constinit` so it lands
  in `.bss` deliberately rather than by accident.

## Adding a module

1. Create `modules/<name>/` with `<name>.cpp` and, if it has an ABI, `<name>.hpp`.
2. Implement the driver as a class in `namespace eris::modules` inside an
   anonymous namespace.
3. Export the ABI as `extern "C"` free functions named `<name>_<verb>`, and
   publish each one with `ERIS_EXPORT_SYMBOL`.
4. Declare the module:

```cpp
ERIS_MODULE("name", "0.1", "author", "GPL-2.0-only", name_init, name_exit, "vga");
```

5. `name_init` returns 0 on success, negative on failure. `name_exit` must undo
   everything init did, including unsubscribing from other modules.
6. Dependencies are module names. The framework loads them first and refcounts
   them, so a module in use cannot be unloaded.
7. The Makefile picks up `modules/*/*.cpp` automatically, no edit needed.
8. Update `CLAUDE.md`.

## Interrupt rules

`eris::arch::Registers` in `include/eris/irq.hpp` mirrors the push order in
`kernel/cpu/isr.asm` field by field. Changing one without the other corrupts
every interrupt. Handlers run with interrupts disabled, must not block, and must
not allocate from the heap.

## Commits

Write commits the way the kernel tree does. Subject line is
`subsystem: imperative summary`, lower case after the prefix, no trailing dot,
under 60 characters. The body explains the problem and why this change is the
answer, wrapped at 72 columns. A commit does one thing.

Prefixes match the tree: `boot`, `cpu`, `mm`, `module`, `lib`, `build`, `ci`,
`doc`, and the module name for anything under `modules/`.

```
vga: drop the hardware cursor after clearing

The firmware leaves the text cursor enabled, so the desktop module showed a
stray underscore in the corner of the background. Disable it once while the
module comes up instead of repainting over it.
```

Never set the commit author identity by hand and never add trailer lines that
credit tooling.

## Releases

Every release has a version and a code name, both living in
`include/eris/version.hpp` and nowhere else. The banner, the tag and the release
title all read from there: `eris 0.1 "Dysnomia"`, tag `v0.1`, title
`eris 0.1 Dysnomia`.

Code names come from the far solar system, the bodies out past Neptune the
kernel takes its own name from. A name is chosen when the milestone opens, and it
is never reused. `ROADMAP.md` holds the assigned list.

## Pull requests

Describe the behaviour before and after, say how you tested it, and paste the
relevant serial log lines. A pull request that changes a public header, the
module framework, the boot order in `kernel/main.cpp`, the linker script or the
set of modules has to update `CLAUDE.md` in the same branch. Ordinary edits
inside a module or a subsystem do not need it.

Agent material follows the same rule. If a change makes a skill in
`.claude/skills/`, an agent in `.claude/agents/` or a prompt in `.codex/prompts/`
describe something that is no longer true, update that file in the same branch.
Renamed scripts, changed commands, a different boot log and new build steps all
show up there.

## What not to do

* Do not add a dependency on the host libc or on any external library.
* Do not put driver logic in the kernel core to make it reachable earlier.
* Do not widen the module ABI with C++ name mangled symbols.
* Do not reformat code you are not changing.
* Do not leave a module registered but broken. Fail the init and return an error.
