# Contributing to eris

eris is a barebone x86_64 kernel. The core stays small on purpose: boot, CPU
tables, memory, timer and the module framework. Everything else is a module.

`AGENTS.md` holds the working rules and `CLAUDE.md` holds the live map of what
exists right now. Read both before you start.

## Getting a build

```
sudo dnf install gcc-c++ nasm binutils qemu-system-x86    # Fedora
sudo apt install build-essential nasm binutils qemu-system-x86   # Debian, Ubuntu

make
make run-serial
```

An ISO needs `grub2-mkrescue` and `xorriso`:

```
make iso
```

## Before you send anything

```
make
./scripts/boot-test.sh
./scripts/check-modules.sh
./scripts/check-spdx.sh
./scripts/faultinject.sh
```

The build runs with `-Wall -Wextra -Werror`, so a warning fails it. `boot-test.sh`
fails on a panic, on a taint warning, on a missing module line and on an empty
serial log.

## What belongs where

A change belongs in a module unless it has to run before `module_init_builtin`.
The early serial console, the page allocator and the interrupt tables are core
because the boot path needs them. A driver, a service or a user facing feature
is a module.

If you are adding a module:

```
./scripts/new-module.sh <name> [dependency ...]
```

Then implement the driver as a class, expose its ABI as `extern "C"` functions,
publish them with `ERIS_EXPORT_SYMBOL`, and make `exit` undo everything `init`
did.

## What will not be accepted

eris has no networking and never will: no network drivers, no protocol stack, no
sockets, no remote access, no telemetry and no update checks. The machine is
closed to the outside world by design, and a patch that opens it is refused
regardless of quality. Vendor firmware blobs are out for the same reason.
`ROADMAP.md` lists the non goals in full.

## Code

* Four spaces, 100 column soft limit, `.clang-format` decides the rest. Run
  `clang-format -i` on files you touch.
* `PascalCase` types, `snake_case` functions and variables, private members with
  a trailing underscore.
* Freestanding only. No libc, no exceptions, no RTTI, no SSE. Global
  `operator new` returns `nullptr`, allocate with `kmalloc` or the page
  allocator.
* State belongs in a class that owns it. Constructors do not fail, module init
  returns a negative value instead.
* Interrupt handlers do not allocate and do not block.
* Every new file carries the SPDX header and the copyright line the tree uses.

## Commits

```
subsystem: imperative summary

The body says what was wrong and why this change is the answer, wrapped at
72 columns. The diff already shows what changed.
```

Subjects stay under 60 characters, lower case after the prefix, no trailing dot.
Prefixes come from the tree: `boot`, `cpu`, `mm`, `module`, `lib`, `build`, `ci`,
`doc`, or the module name. No conventional commit types, no trailers crediting
tooling, and never set the author identity by hand.

One commit does one thing. If the summary needs the word "and", split it.

## Pull requests

Pick the template that fits your change: the default one, or `module.md`,
`fix.md` or `core.md` under `.github/PULL_REQUEST_TEMPLATE/`. Say what the
behaviour was before and after, how you tested it, and paste the serial log.

Update `CLAUDE.md` in the same branch when the change touches a public header,
the module framework, the boot order, the linker script or the set of modules.
The same goes for the skills, agents and prompts under `.claude/` and `.codex/`
if your change makes something they describe untrue.

Reviews come from the people listed in `MAINTAINERS` for the files you touched.
Expect questions about layering, interrupt safety and lifecycle before style.

## Reporting bugs

Open an issue with the template that matches. A panic report without the serial
log cannot be acted on, so run `make run-serial` and paste everything from the
banner onwards. For faults before the IDT comes up, add the output of:

```
qemu-system-x86_64 -kernel build/eris32.elf -serial stdio -display none \
  -no-reboot -d int,cpu_reset -D build/qemu.log -m 512M
```

## License

GPL-2.0-only. By sending a change you agree it ships under that license. Modules
declare their own license in `ERIS_MODULE`, and anything outside the free list
in `kernel/module/module.cpp` taints the kernel at load time.

When a change of yours lands, add yourself to `CREDITS` in the same branch.
