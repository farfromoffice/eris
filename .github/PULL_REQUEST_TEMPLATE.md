## What this changes

Describe the behaviour before and after. One change per pull request.

## Why

What was wrong, or what the tree could not do yet.

## Testing

```
make
./scripts/boot-test.sh
```

Paste the serial lines that prove it works:

```

```

## Checklist

- [ ] `make` is clean, no new warnings
- [ ] `./scripts/boot-test.sh` passes
- [ ] Commit subjects use a subsystem prefix and an imperative summary
- [ ] New files carry the SPDX header
- [ ] `CLAUDE.md` updated if a public header, the module framework, the boot
      order, the linker script or the module set changed
- [ ] Skills, agents and prompts under `.claude/` and `.codex/` updated if this
      change made anything they describe untrue
- [ ] A new module declares its license and unwinds itself in its exit function
