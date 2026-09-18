# Review a change

Review $ARGUMENTS against the rules in `AGENTS.md` and the invariants in
`CLAUDE.md`.

Report findings as `path:line: severity: problem. fix.`, worst first. No praise,
no formatting nits the formatter already handles.

Check in this order:

1. Freestanding environment: hosted headers, libc, exceptions, RTTI, SSE, or a
   bare `new`, which returns `nullptr` here.
2. Interrupt safety: no allocation, no blocking, register frame matching
   `kernel/cpu/isr.asm`, end of interrupt on every IRQ path.
3. Module lifecycle: init returns an error instead of panicking, exit undoes
   what init did, dependencies declared, license set.
4. Memory: nothing allocates before `heap_init`, nothing touches past the first
   mapped gigabyte, zero returns from the page allocator are handled.
5. Design: state in a class with private members, one responsibility,
   constructors that cannot fail, composition over inheritance.
6. Map freshness: `CLAUDE.md` and any affected skill, agent or prompt updated.
7. Build reality: run `make` and `./scripts/boot-test.sh` if boot could be
   affected, and report what they said.

Every finding needs a concrete path through the code to a wrong outcome.
