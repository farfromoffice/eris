## The bug

What went wrong and how it showed up. Panic message, wrong output, hang, triple
fault.

## Root cause

Why it happened. Point at the code path.

## The fix

What this change does instead, and why this is the right layer to fix it.

## Reproduction

How to see the bug on an unpatched tree:

```

```

## Testing

```
make
./scripts/boot-test.sh
```

Before:

```

```

After:

```

```

## Checklist

- [ ] Root cause fixed, not the symptom
- [ ] No unrelated changes in the diff
- [ ] Invariant that was broken is written down in `CLAUDE.md` if it was missing
