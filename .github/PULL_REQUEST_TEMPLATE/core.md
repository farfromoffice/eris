## Core change

Which part of the kernel core this touches: boot, cpu, mm, module framework or
lib.

## Why it belongs in the core

The core stays small. Explain why a module cannot do this instead.

## Invariants

Which invariants in `CLAUDE.md` this change affects, and how they read now.

- [ ] Boot order in `kernel/main.cpp`
- [ ] Register frame shared by `kernel/cpu/isr.asm` and `include/eris/irq.hpp`
- [ ] Linker sections and their markers
- [ ] Page allocator or heap limits
- [ ] Fixed table sizes

## Compatibility

What breaks for existing modules, and what they have to change.

## Testing

```
make
./scripts/boot-test.sh
./scripts/check-modules.sh
./scripts/kernel-size.sh
```

```

```

## Checklist

- [ ] `CLAUDE.md` invariants and public API tables updated
- [ ] Nothing before the heap comes up allocates
- [ ] Interrupt paths stay allocation free
