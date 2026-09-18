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
            -fno-use-cxa-atexit -MMD -MP $(INCLUDES)

ASMFLAGS := -f elf64
LDFLAGS  := -n -nostdlib --no-warn-rwx-segments -T linker/kernel.ld

CXX_SRCS := $(wildcard kernel/*.cpp) $(wildcard kernel/*/*.cpp) $(wildcard modules/*/*.cpp)
ASM_SRCS := $(wildcard boot/*.asm) $(wildcard kernel/*/*.asm)

OBJS := $(patsubst %.cpp,build/%.o,$(CXX_SRCS)) $(patsubst %.asm,build/%.o,$(ASM_SRCS))
DEPS := $(patsubst %.cpp,build/%.d,$(CXX_SRCS))

.PHONY: all clean run run-serial iso

all: $(KERNEL) $(KERNEL32)

$(KERNEL): $(OBJS) linker/kernel.ld
	@mkdir -p $(dir $@)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(KERNEL32): $(KERNEL)
	$(OBJCOPY) -O elf32-i386 $< $@

build/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

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
