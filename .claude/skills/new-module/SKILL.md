---
name: new-module
description: Scaffold, implement and verify a new eris kernel module. Use when the user asks for a new driver or module, mentions modules/<name>, or wants something split out of the kernel core into a module.
---

# Adding a module to eris

## Decide first

A module is the right answer unless the code has to run before
`module_init_builtin`. Anything the boot path needs, such as the early serial
console or the page allocator, belongs in `kernel/`. Everything else is a module.

Pick the dependencies before writing code. A dependency is another module name,
the framework loads it first and refcounts it.

A module that talks to a network, or that reaches outside the machine in any
way, is not written. eris has no networking by design, see the non goals in
`ROADMAP.md`.

## Scaffold

```
./scripts/new-module.sh <name> [dependency ...]
```

The script writes `modules/<name>/<name>.cpp` with a class, an init, an exit and
the descriptor. The Makefile picks up `modules/*/*.cpp` on its own.

## Implement

* Put the driver state in a class inside `namespace eris::modules` and an
  anonymous namespace. Private members carry a trailing underscore.
* `init` returns 0 on success and a negative value on failure. Do not panic in a
  module unless continuing would corrupt memory.
* `exit` undoes everything `init` did: unregister consoles, unsubscribe
  handlers, mask IRQs, free pages.
* Expose the ABI as `extern "C"` functions named `<name>_<verb>` in
  `modules/<name>/<name>.hpp`, and publish each with `ERIS_EXPORT_SYMBOL`.
* Other modules include it as `#include <name/name.hpp>`, `modules` is on the
  include path.

Interrupt handlers may not allocate and may not block.

## Verify

```
make
./scripts/check-modules.sh
./scripts/boot-test.sh
```

The serial log has to show `module <name> 0.1 loaded (GPL-2.0-only)` and no
taint warning.

## Finish

Update the module table, the export list and the export count in `CLAUDE.md`.
Commit as `<name>: add the module` with a body saying what it drives and why it
is a module rather than core code.
