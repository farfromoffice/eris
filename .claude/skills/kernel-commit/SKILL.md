---
name: kernel-commit
description: Write commit messages and pull request descriptions in the style this tree uses. Use when committing, amending, splitting work into commits, or drafting a pull request body.
---

# Commits in eris

## Subject

```
subsystem: imperative summary
```

Lower case after the prefix, no trailing dot, under 60 characters. The prefix
comes from the tree: `boot`, `cpu`, `mm`, `module`, `lib`, `build`, `ci`, `doc`,
or the module name for anything under `modules/`.

No conventional commit types. `feat:`, `fix:` and `chore:` do not belong here.

## Body

Wrapped at 72 columns. Say what was wrong and why this change is the answer. The
diff already says what changed, so do not narrate it line by line. Skip the body
only when the subject is genuinely complete on its own.

```
mm: keep the heap out of the bitmap range

The bitmap sits right after the kernel image, and heap_init took its pages
from the same run, so the first large allocation walked over the allocator's
own state. Reserve the bitmap before handing anything out.
```

## Splitting

One commit does one thing. A rename goes in its own commit, a behaviour change
in another. If a commit needs the word "and" in the summary, it is two commits.

## Rules

* Never set the author identity by hand.
* No trailers crediting tooling, no co-author lines.
* Commit only what the change needs, no drive by formatting.
* Run `make` and `./scripts/boot-test.sh` before committing.

## Pull requests

Use the template that fits: the default one, or `module.md`, `fix.md` or
`core.md` from `.github/PULL_REQUEST_TEMPLATE/`. Describe behaviour before and
after, paste the serial log, and tick the checklist honestly.

If the change touches a public header, the module framework, the boot order, the
linker script or the set of modules, update `CLAUDE.md` in the same branch.
