---
name: update-map
description: Refresh CLAUDE.md and the agent material so they match the tree. Use after adding or removing a module, changing a public header, the module framework, the boot order, the linker script or a script the docs mention, and whenever the map looks out of date.
---

# Refreshing the map

`CLAUDE.md` is the live map of the tree, and the skills, agents and prompts
around it repeat parts of that map. Every session trusts them, so a wrong line
costs more than a missing one.

## When it has to change

* a module is added or removed, or its dependencies or ABI change
* a header in `include/eris/` gains, loses or changes a public declaration
* the module framework in `kernel/module/` changes behaviour
* the boot order in `kernel/main.cpp` changes
* `linker/kernel.ld` gains, drops or moves a section
* a listed limit or invariant stops being true

Refactoring inside a module or a subsystem does not need an update.

## How to check what is true now

```
grep -rn 'ERIS_MODULE(' modules            # module names, versions, deps
grep -rn 'ERIS_EXPORT_SYMBOL' modules      # exported symbols and the count
ls include/eris                            # public headers
sed -n '/void start_kernel/,/^}/p' kernel/main.cpp   # boot order
grep -n 'eris_' linker/kernel.ld           # sections and markers
grep -rn 'constexpr usize max_' kernel modules       # fixed limits
./scripts/boot-test.sh                     # what the kernel reports at runtime
```

## Sections to touch

| Changed | Update |
| --- | --- |
| Module added, removed or renamed | Module table, export count |
| New `ERIS_EXPORT_SYMBOL` | ABI column, export count |
| New or changed header | Public API table |
| Boot sequence | Boot order list |
| Section or marker | Invariants |
| Table size or allocator limit | Invariants |
| New trap someone hit | Traps worth remembering |

Fix wrong statements in place. Do not append a correction next to a stale line,
and do not leave both versions standing.

## The agent material counts too

`.claude/skills/`, `.claude/agents/` and `.codex/prompts/` quote commands, paths
and log lines. When any of those change, grep the whole set and fix every copy:

```
grep -rn 'scripts/' .claude .codex AGENTS.md CLAUDE.md
grep -rn 'make ' .claude .codex
grep -rn 'module .* loaded' .claude .codex
```

A skill that tells the next session to run a script that no longer exists is
worse than no skill at all.

## Verify

Reread the map against `./scripts/boot-test.sh` output. Every module, version
and license the kernel reports at runtime has to appear in the table, and
nothing that is gone may still be listed.
