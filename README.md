# eris

Barebone x86_64 kernel written in C++23. Nothing much

### Writing a module

```cpp
#include <eris/module.hpp>

namespace {

int my_init() { return 0; }
void my_exit() {}

}

ERIS_MODULE("mymod", "0.1", "you", "GPL-2.0-only", my_init, my_exit, "vga");
```

## Build and run

```
make
make run          # VGA window plus serial on stdout
make run-serial   # serial only
make iso          # needs grub2-mkrescue and xorriso
```
