// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

// What a loadable module is allowed to call. Everything here is C linkage,
// exported by kernel/module/api.cpp, and covered by the module ABI version.

extern "C" {

void eris_log(const char* message);
void eris_printk(int level, const char* fmt, ...);

void* eris_kmalloc(eris::usize size);
void* eris_kzalloc(eris::usize size);
void eris_kfree(void* pointer);

eris::u64 eris_alloc_pages(eris::usize count);
void eris_free_pages(eris::u64 frame, eris::usize count);
eris::u64 eris_phys_to_virt(eris::u64 address);
eris::u64 eris_virt_to_phys(eris::u64 address);

void* eris_map_device(eris::u64 address, eris::usize length);
void eris_unmap_device(void* window, eris::usize length);

void eris_irq_register(eris::u8 irq, void (*handler)(void*));
void eris_irq_unmask(eris::u8 irq);
void eris_irq_mask(eris::u8 irq);

eris::u64 eris_monotonic_ns();
void eris_udelay(eris::u64 microseconds);
void eris_sleep_ms(eris::u64 milliseconds);
void eris_yield();

eris::u8 eris_inb(eris::u16 port);
eris::u16 eris_inw(eris::u16 port);
eris::u32 eris_inl(eris::u16 port);
void eris_outb(eris::u16 port, eris::u8 value);
void eris_outw(eris::u16 port, eris::u16 value);
void eris_outl(eris::u16 port, eris::u32 value);

eris::usize eris_pci_device_count();
bool eris_pci_device_at(eris::usize index, eris::u16* vendor, eris::u16* device,
                        eris::u8* bus, eris::u8* slot, eris::u8* function);
eris::u64 eris_pci_bar(eris::usize index, eris::u8 bar, eris::u64* length, bool* memory);
eris::u8 eris_pci_interrupt_line(eris::usize index);
void eris_pci_enable(eris::usize index);

}

#define pr_module_info(...) eris_printk(1, __VA_ARGS__)
#define pr_module_err(...) eris_printk(3, __VA_ARGS__)
