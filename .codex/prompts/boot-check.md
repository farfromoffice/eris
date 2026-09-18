# Boot check

Build the kernel and prove it still boots.

```
make
./scripts/boot-test.sh
```

A healthy log carries the banner, the memory and heap lines, the module count
and one `module <name> <version> loaded (<license>)` line per module. The script
fails on a panic, a taint warning, a missing module line or an empty log.

If it fails, report the failing line and the likely layer:

* nothing on serial: boot stub, linker script or multiboot header
* banner then silence: a fault before the IDT was ready
* `cpu exception N`: read the vector, the error code and RIP
* a module in the failed state: its init returned an error, which is kept and
  returned again on the next `module_load`

Paste the serial log in the report.
