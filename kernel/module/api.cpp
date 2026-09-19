// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/console.hpp>
#include <eris/export.hpp>
#include <eris/io.hpp>
#include <eris/irq.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/pci.hpp>
#include <eris/printk.hpp>
#include <eris/thread.hpp>
#include <eris/time.hpp>

// The kernel services a loadable module is allowed to call. C linkage on
// purpose: a mangled name is not something an export table can promise to keep
// across a compiler, let alone across a kernel version.

namespace {

// A module handler only needs to know that its line fired, so the register
// frame stops here rather than becoming part of the module ABI.
constinit void (*module_handlers[16])(void*){};

void module_irq_trampoline(eris::arch::Registers& regs)
{
    const auto irq = static_cast<eris::u8>(regs.vector - 32);
    if (irq < 16 && module_handlers[irq] != nullptr)
        module_handlers[irq](nullptr);
}

} // namespace

extern "C" {

void eris_log(const char* message)
{
    eris::console_write(message);
}

void eris_printk(int level, const char* fmt, ...)
{
    __builtin_va_list args;
    __builtin_va_start(args, fmt);

    eris::console_write(level >= 3 ? "[err] " : "[inf] ");
    eris::vprintk(fmt, args);

    __builtin_va_end(args);
}

void* eris_kmalloc(eris::usize size)
{
    return eris::kmalloc(size);
}

void* eris_kzalloc(eris::usize size)
{
    return eris::kzalloc(size);
}

void eris_kfree(void* pointer)
{
    eris::kfree(pointer);
}

eris::u64 eris_alloc_pages(eris::usize count)
{
    return eris::mm::alloc_pages(count);
}

void eris_free_pages(eris::u64 frame, eris::usize count)
{
    eris::mm::free_pages(frame, count);
}

eris::u64 eris_phys_to_virt(eris::u64 address)
{
    return eris::mm::phys_to_virt(address);
}

eris::u64 eris_virt_to_phys(eris::u64 address)
{
    return eris::mm::virt_to_phys(address);
}

void* eris_map_device(eris::u64 address, eris::usize length)
{
    return eris::mm::map_device(address, length);
}

void eris_unmap_device(void* window, eris::usize length)
{
    eris::mm::unmap_device(window, length);
}

void eris_irq_register(eris::u8 irq, void (*handler)(void*))
{
    if (irq >= 16)
        return;

    module_handlers[irq] = handler;
    eris::arch::irq_register(irq, handler != nullptr ? module_irq_trampoline : nullptr);
}

void eris_irq_unmask(eris::u8 irq)
{
    eris::arch::irq_unmask(irq);
}

void eris_irq_mask(eris::u8 irq)
{
    eris::arch::irq_mask(irq);
}

eris::u64 eris_monotonic_ns()
{
    return eris::monotonic_ns();
}

void eris_udelay(eris::u64 microseconds)
{
    eris::udelay(microseconds);
}

void eris_sleep_ms(eris::u64 milliseconds)
{
    eris::thread_sleep_ms(milliseconds);
}

void eris_yield()
{
    eris::yield();
}

eris::u8 eris_inb(eris::u16 port) { return eris::arch::inb(port); }
eris::u16 eris_inw(eris::u16 port) { return eris::arch::inw(port); }
eris::u32 eris_inl(eris::u16 port) { return eris::arch::inl(port); }
void eris_outb(eris::u16 port, eris::u8 value) { eris::arch::outb(port, value); }
void eris_outw(eris::u16 port, eris::u16 value) { eris::arch::outw(port, value); }
void eris_outl(eris::u16 port, eris::u32 value) { eris::arch::outl(port, value); }

eris::usize eris_pci_device_count()
{
    return eris::pci::device_count();
}

bool eris_pci_device_at(eris::usize index, eris::u16* vendor, eris::u16* device,
                        eris::u8* bus, eris::u8* slot, eris::u8* function)
{
    eris::pci::PciDevice* found = eris::pci::device_at(index);
    if (found == nullptr)
        return false;

    if (vendor != nullptr)
        *vendor = found->vendor();
    if (device != nullptr)
        *device = found->device();
    if (bus != nullptr)
        *bus = found->address().bus;
    if (slot != nullptr)
        *slot = found->address().slot;
    if (function != nullptr)
        *function = found->address().function;

    return true;
}

eris::u64 eris_pci_bar(eris::usize index, eris::u8 bar, eris::u64* length, bool* memory)
{
    eris::pci::PciDevice* found = eris::pci::device_at(index);
    if (found == nullptr)
        return 0;

    const eris::pci::Bar decoded = found->bar(bar);
    if (length != nullptr)
        *length = decoded.length;
    if (memory != nullptr)
        *memory = decoded.memory;

    return decoded.address;
}

eris::u8 eris_pci_interrupt_line(eris::usize index)
{
    eris::pci::PciDevice* found = eris::pci::device_at(index);
    return found != nullptr ? found->interrupt_line() : 0;
}

void eris_pci_enable(eris::usize index)
{
    eris::pci::PciDevice* found = eris::pci::device_at(index);
    if (found == nullptr)
        return;

    found->enable_memory_space();
    found->enable_bus_master();
}

} // extern "C"

ERIS_EXPORT_SYMBOL(eris_log);
ERIS_EXPORT_SYMBOL(eris_printk);
ERIS_EXPORT_SYMBOL(eris_kmalloc);
ERIS_EXPORT_SYMBOL(eris_kzalloc);
ERIS_EXPORT_SYMBOL(eris_kfree);
ERIS_EXPORT_SYMBOL(eris_alloc_pages);
ERIS_EXPORT_SYMBOL(eris_free_pages);
ERIS_EXPORT_SYMBOL(eris_phys_to_virt);
ERIS_EXPORT_SYMBOL(eris_virt_to_phys);
ERIS_EXPORT_SYMBOL(eris_map_device);
ERIS_EXPORT_SYMBOL(eris_unmap_device);
ERIS_EXPORT_SYMBOL(eris_irq_register);
ERIS_EXPORT_SYMBOL(eris_irq_unmask);
ERIS_EXPORT_SYMBOL(eris_irq_mask);
ERIS_EXPORT_SYMBOL(eris_monotonic_ns);
ERIS_EXPORT_SYMBOL(eris_udelay);
ERIS_EXPORT_SYMBOL(eris_sleep_ms);
ERIS_EXPORT_SYMBOL(eris_yield);
ERIS_EXPORT_SYMBOL(eris_inb);
ERIS_EXPORT_SYMBOL(eris_inw);
ERIS_EXPORT_SYMBOL(eris_inl);
ERIS_EXPORT_SYMBOL(eris_outb);
ERIS_EXPORT_SYMBOL(eris_outw);
ERIS_EXPORT_SYMBOL(eris_outl);
ERIS_EXPORT_SYMBOL(eris_pci_device_count);
ERIS_EXPORT_SYMBOL(eris_pci_device_at);
ERIS_EXPORT_SYMBOL(eris_pci_bar);
ERIS_EXPORT_SYMBOL(eris_pci_interrupt_line);
ERIS_EXPORT_SYMBOL(eris_pci_enable);
