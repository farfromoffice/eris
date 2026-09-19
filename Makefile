# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice

KERNEL   := build/eris.elf
KERNEL32 := build/eris32.elf
INITRD   := build/initrd.tar
ISO      := build/eris.iso

CXX      := g++
CC       := gcc
LD       := ld
ASM      := nasm
OBJCOPY  := objcopy

INCLUDES := -Iinclude -Imodules

CXXFLAGS := -std=c++23 -O2 -ffreestanding -fno-exceptions -fno-rtti \
            -fno-threadsafe-statics -fno-stack-protector -fno-pic \
            -mno-red-zone -mno-mmx -mno-sse -mno-sse2 \
            -Wall -Wextra -Werror -Wno-unused-parameter \
            -fno-omit-frame-pointer -fno-use-cxa-atexit -MMD -MP $(INCLUDES)

ASMFLAGS := -f elf64
LDFLAGS  := -n -nostdlib --no-warn-rwx-segments -T linker/kernel.ld

# Modules named here are linked into the image. Everything else under modules/
# is built as a loadable object and packed into the initrd.
BUILTIN_MODULES ?= vga keyboard
# The apps that ship with the desktop live one level down, under
# modules/internal_apps, because they are applications rather than drivers.
MODULE_DIRS := $(filter-out modules/internal_apps,\
                 $(patsubst %/,%,$(wildcard modules/*/) $(wildcard modules/internal_apps/*/)))
ALL_MODULES := $(notdir $(MODULE_DIRS))
LOADABLE_MODULES := $(filter-out $(BUILTIN_MODULES),$(ALL_MODULES))

module_dir = $(filter %/$(1),$(MODULE_DIRS))

BUILTIN_SRCS := $(foreach m,$(BUILTIN_MODULES),$(wildcard $(call module_dir,$(m))/*.cpp))
KOBJS := $(foreach m,$(LOADABLE_MODULES),build/modules/$(m).ko)

# Userspace. Freestanding and static, linked well above anything the kernel
# maps, and packed into the initrd next to the modules.
USER_PROGRAMS := init crash spin
USER_BINARIES := $(foreach p,$(USER_PROGRAMS),build/user/$(p))
USER_CFLAGS := -std=gnu17 -O2 -ffreestanding -mcmodel=large -fno-stack-protector -fno-pic \
               -mno-red-zone -mno-mmx -mno-sse -mno-sse2 -Wall -Wextra -Werror \


CXX_SRCS := $(wildcard kernel/*.cpp) $(wildcard kernel/*/*.cpp) $(BUILTIN_SRCS)
TRAMPOLINE_SRC := kernel/cpu/trampoline.asm
ASM_SRCS := $(filter-out $(TRAMPOLINE_SRC),$(wildcard boot/*.asm) $(wildcard kernel/*/*.asm))

OBJS := $(patsubst %.cpp,build/%.o,$(CXX_SRCS)) $(patsubst %.asm,build/%.o,$(ASM_SRCS)) \
        build/trampoline.o
DEPS := $(patsubst %.cpp,build/%.d,$(CXX_SRCS))

.SECONDEXPANSION:

.PHONY: all clean run run-serial iso

all: $(KERNEL) $(KERNEL32) $(INITRD)

# Two link passes: the first one exists so the symbol table can be generated
# from it, the second one carries that table. The table is regenerated from the
# second pass because adding it moves every address after it.
$(KERNEL): $(OBJS) linker/kernel.ld scripts/gen-ksyms.sh
	@mkdir -p $(dir $@)
	./scripts/gen-ksyms.sh > build/ksyms.cpp
	$(CXX) $(CXXFLAGS) -c build/ksyms.cpp -o build/ksyms.o
	$(LD) $(LDFLAGS) -o build/eris.pass1.elf $(OBJS) build/ksyms.o
	./scripts/gen-ksyms.sh build/eris.pass1.elf > build/ksyms.cpp
	$(CXX) $(CXXFLAGS) -c build/ksyms.cpp -o build/ksyms.o
	$(LD) $(LDFLAGS) -o build/eris.pass2.elf $(OBJS) build/ksyms.o
	./scripts/gen-ksyms.sh build/eris.pass2.elf > build/ksyms.cpp
	$(CXX) $(CXXFLAGS) -c build/ksyms.cpp -o build/ksyms.o
	$(LD) $(LDFLAGS) -o $@ $(OBJS) build/ksyms.o

$(KERNEL32): $(KERNEL)
	$(OBJCOPY) -O elf32-i386 $< $@

# A loadable module is a relocatable object, so a module made of several files
# is the partial link of them and the loader still sees one image.
define module_image
build/modules/$(1).ko: $(patsubst %.cpp,build/%.o,$(wildcard $(call module_dir,$(1))/*.cpp))
	@mkdir -p $$(dir $$@)
	$$(LD) -r -o $$@ $$^
endef

$(foreach module,$(LOADABLE_MODULES),$(eval $(call module_image,$(module))))

build/user/crt0.o: user/lib/crt0.asm
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) $< -o $@

build/user/%.o: user/$$*/$$*.c
	@mkdir -p $(dir $@)
	$(CC) $(USER_CFLAGS) -c $< -o $@

build/user/%: build/user/%.o build/user/crt0.o user/link.ld
	$(LD) -n -nostdlib -T user/link.ld -o $@ build/user/crt0.o $<

FONTS := $(wildcard fonts/*.ttf)

$(INITRD): $(KOBJS) $(USER_BINARIES) $(FONTS) scripts/mkinitrd.sh
	./scripts/mkinitrd.sh $@ $(KOBJS) $(USER_BINARIES) $(FONTS)

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# The application processor trampoline is a flat image copied to a fixed page,
# so it is assembled raw and wrapped in an object rather than linked normally.
build/trampoline.bin: $(TRAMPOLINE_SRC)
	@mkdir -p $(dir $@)
	$(ASM) -f bin $< -o $@

build/trampoline.o: build/trampoline.bin
	$(OBJCOPY) -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

build/%.o: %.asm
	@mkdir -p $(dir $@)
	$(ASM) $(ASMFLAGS) $< -o $@

# Enough memory for the desktop to keep a full screen back buffer, and enough
# display memory for the mode it asks the adapter for.
QEMU_MEMORY ?= 2G
QEMU_VIDEO ?= -vga std -global VGA.vgamem_mb=64
QEMU_CORES ?= 4

# Software rendering at sixty frames a second is far more than an interpreted
# CPU can keep up with, and the screen tears visibly without hardware help.
QEMU_ACCEL ?= $(shell test -w /dev/kvm && echo "-enable-kvm -cpu host")

run: $(KERNEL32) $(INITRD)
	qemu-system-x86_64 $(QEMU_ACCEL) -kernel $(KERNEL32) -initrd $(INITRD) -serial stdio \
		-m $(QEMU_MEMORY) -smp $(QEMU_CORES) $(QEMU_VIDEO)

run-serial: $(KERNEL32) $(INITRD)
	qemu-system-x86_64 $(QEMU_ACCEL) -kernel $(KERNEL32) -initrd $(INITRD) -serial stdio -display none \
		-m $(QEMU_MEMORY) -smp $(QEMU_CORES) $(QEMU_VIDEO)

iso: $(KERNEL)
	@mkdir -p build/iso/boot/grub
	cp $(KERNEL) build/iso/boot/eris.elf
	cp boot/grub.cfg build/iso/boot/grub/grub.cfg
	grub2-mkrescue -o $(ISO) build/iso

clean:
	rm -rf build

-include $(DEPS)
