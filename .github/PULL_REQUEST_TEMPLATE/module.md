## Module

Name, version and what hardware or feature it drives.

## ABI

The `extern "C"` functions it exposes and which of them are exported with
`ERIS_EXPORT_SYMBOL`.

```

```

## Dependencies

Which modules it needs and why. Say what happens when a dependency is missing.

## Lifecycle

- What `init` allocates, registers or masks in.
- What `exit` gives back. It has to undo everything `init` did.
- What the module does when it is loaded twice or unloaded while in use.

## Testing

```
make
./scripts/check-modules.sh
./scripts/boot-test.sh
```

Serial output showing the module loading:

```

```

## Checklist

- [ ] Driver state lives in a class, not in scattered globals
- [ ] License field set and free, so the kernel stays untainted
- [ ] `CLAUDE.md` module table updated with deps and ABI
- [ ] Nothing in an interrupt path allocates
