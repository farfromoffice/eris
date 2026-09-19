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
eris::u32 eris_timer_every(eris::u64 period_ns, void (*callback)(void*), void* context);
void eris_timer_cancel(eris::u32 handle);
void eris_sleep_ms(eris::u64 milliseconds);
void eris_yield();

// Runs a function later, out of interrupt context, on the kernel work thread.
bool eris_schedule_work(void (*work)(void*), void* context);

eris::u8 eris_inb(eris::u16 port);
eris::u16 eris_inw(eris::u16 port);
eris::u32 eris_inl(eris::u16 port);
void eris_outb(eris::u16 port, eris::u8 value);
void eris_outw(eris::u16 port, eris::u16 value);
void eris_outl(eris::u16 port, eris::u32 value);

void eris_console_subscribe(void (*sink)(char));
void eris_console_unsubscribe();

void eris_memory_stats(eris::u64* total_pages, eris::u64* free_pages, eris::u64* heap_bytes);
eris::usize eris_cpu_count();
void eris_version(const char** version, const char** name);

eris::usize eris_module_count();

// Filled with copies because the strings a module descriptor points at live
// inside that module's image and an unload takes them with it.
struct ErisModuleInfo {
    char name[32];
    char version[16];
    char license[32];
    char state[16];
};

bool eris_module_at(eris::usize index, ErisModuleInfo* out);

eris::u64 eris_file_size(const char* path);
eris::i64 eris_file_read(const char* path, void* buffer, eris::usize length);

eris::usize eris_pci_device_count();
bool eris_pci_device_at(eris::usize index, eris::u16* vendor, eris::u16* device,
                        eris::u8* bus, eris::u8* slot, eris::u8* function);
eris::u64 eris_pci_bar(eris::usize index, eris::u8 bar, eris::u64* length, bool* memory);
eris::u8 eris_pci_interrupt_line(eris::usize index);
void eris_pci_enable(eris::usize index);

}

#define pr_module_info(...) eris_printk(1, __VA_ARGS__)
#define pr_module_err(...) eris_printk(3, __VA_ARGS__)
