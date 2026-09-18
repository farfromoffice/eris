# SPDX-License-Identifier: GPL-2.0-only
# Copyright (c) 2026 farfromoffice

KERNEL   := build/eris.elf
KERNEL32 := build/eris32.elf
ISO      := build/eris.iso

CXX      := g++
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

CXX_SRCS := $(wildcard kernel/*.cpp) $(wildcard kernel/*/*.cpp) $(wildcard modules/*/*.cpp)
TRAMPOLINE_SRC := kernel/cpu/trampoline.asm
ASM_SRCS := $(filter-out $(TRAMPOLINE_SRC),$(wildcard boot/*.asm) $(wildcard kernel/*/*.asm))

OBJS := $(patsubst %.cpp,build/%.o,$(CXX_SRCS)) $(patsubst %.asm,build/%.o,$(ASM_SRCS)) \
        build/trampoline.o
DEPS := $(patsubst %.cpp,build/%.d,$(CXX_SRCS))

.PHONY: all clean run run-serial iso

all: $(KERNEL) $(KERNEL32)

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

run: $(KERNEL32)
	qemu-system-x86_64 -kernel $(KERNEL32) -serial stdio -m 512M

run-serial: $(KERNEL32)
	qemu-system-x86_64 -kernel $(KERNEL32) -serial stdio -display none -m 512M

iso: $(KERNEL)
	@mkdir -p build/iso/boot/grub
	cp $(KERNEL) build/iso/boot/eris.elf
	cp boot/grub.cfg build/iso/boot/grub/grub.cfg
	grub2-mkrescue -o $(ISO) build/iso

clean:
	rm -rf build

-include $(DEPS)
