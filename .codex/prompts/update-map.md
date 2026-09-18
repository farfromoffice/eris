# Update the map

Bring `CLAUDE.md` and the agent material back in line with the tree.

Collect the truth first:

```
grep -rn 'ERIS_MODULE(' modules
grep -rn 'ERIS_EXPORT_SYMBOL' modules
ls include/eris
sed -n '/void start_kernel/,/^}/p' kernel/main.cpp
grep -n 'eris_' linker/kernel.ld
grep -rn 'constexpr usize max_' kernel modules
./scripts/boot-test.sh
```

Then fix, in place, never by appending a correction:

* module table, dependencies, ABI and export count
* public API table
* boot order
* invariants, limits and sections
* traps worth remembering, if this change added one

Finally sweep the agent material for anything that went stale:

```
grep -rn 'scripts/' .claude .codex AGENTS.md CLAUDE.md
grep -rn 'make ' .claude .codex
```

Commit as `doc: refresh the tree map`.
