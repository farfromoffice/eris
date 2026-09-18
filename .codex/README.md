# Codex material for eris

`AGENTS.md` in the repository root is the contract. Read it first, then
`CLAUDE.md` for the live map of what exists right now.

This directory holds prompts for recurring work:

| Prompt | Use it for |
| --- | --- |
| `prompts/new-module.md` | Adding a module under `modules/` |
| `prompts/boot-check.md` | Proving the kernel still boots |
| `prompts/debug-panic.md` | Chasing a panic, hang or triple fault |
| `prompts/review.md` | Reviewing a diff against the tree rules |
| `prompts/update-map.md` | Bringing `CLAUDE.md` and the agent files back in line |

Three standing rules, because they are easy to miss:

* eris has no networking and never will. No drivers, no stack, no sockets, no
  remote access, nothing that phones home.
* A change that touches a public header, the module framework, the boot order,
  the linker script or the set of modules updates `CLAUDE.md` in the same branch.
* A change that makes a prompt here, or a skill or agent under `.claude/`,
  describe something untrue updates that file too.
