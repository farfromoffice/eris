# Add a module

Add `$ARGUMENTS` as a module under `modules/`.

Networking is a non goal: no network drivers, no stack, no sockets, no remote
access. If that is what was asked for, say so and stop.

First decide whether it really is a module. Anything that has to run before
`module_init_builtin` belongs in `kernel/`, say so and stop.

Then:

1. `./scripts/new-module.sh <name> [deps...]`
2. Implement the driver as a class in `namespace eris::modules` inside an
   anonymous namespace, private members with a trailing underscore.
3. Expose the ABI as `extern "C"` `<name>_<verb>` functions in
   `modules/<name>/<name>.hpp`, publish each with `ERIS_EXPORT_SYMBOL`.
4. `init` returns 0 or a negative error, `exit` undoes everything `init` did.
5. `make`, `./scripts/check-modules.sh`, `./scripts/boot-test.sh`.
6. Update the module table, the ABI column and the export count in `CLAUDE.md`.

No allocation in interrupt handlers. No mangled C++ symbols in the ABI. Commit
as `<name>: add the module`.
