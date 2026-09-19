// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/acpi.hpp>
#include <eris/io.hpp>
#include <eris/lock.hpp>
#include <eris/paging.hpp>
#include <eris/pci.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

namespace eris::pci {
namespace {

constexpr u16 config_address_port = 0xCF8;
constexpr u16 config_data_port = 0xCFC;

constexpr u16 offset_vendor = 0x00;
constexpr u16 offset_device = 0x02;
constexpr u16 offset_command = 0x04;
constexpr u16 offset_status = 0x06;
constexpr u16 offset_revision = 0x08;
constexpr u16 offset_header_type = 0x0E;
constexpr u16 offset_bar0 = 0x10;
constexpr u16 offset_secondary_bus = 0x19;
constexpr u16 offset_capabilities = 0x34;
constexpr u16 offset_interrupt_line = 0x3C;

constexpr u16 command_io_space = 1 << 0;
constexpr u16 command_memory_space = 1 << 1;
constexpr u16 command_bus_master = 1 << 2;

constexpr u16 status_capabilities = 1 << 4;

constexpr usize max_devices = 64;

constinit PciDevice devices[max_devices]{};
constinit usize device_total = 0;
constinit bool ready = false;
constinit IrqSpinLock config_lock{};

u32 config_index(Address address, u16 offset)
{
    return 0x80000000u
        | (static_cast<u32>(address.bus) << 16)
        | (static_cast<u32>(address.slot) << 11)
        | (static_cast<u32>(address.function) << 8)
        | (offset & 0xFC);
}

void write_digits(char* out, usize& cursor, u8 value)
{
    constexpr const char* digits = "0123456789abcdef";
    out[cursor++] = digits[value >> 4];
    out[cursor++] = digits[value & 0x0F];
}

void scan_bus(u8 bus);

void scan_function(Address address)
{
    const u16 vendor = read16(address, offset_vendor);
    if (vendor == 0xFFFF)
        return;

    const u16 device = read16(address, offset_device);
    const u32 revision = read32(address, offset_revision);
    const u8 class_code = static_cast<u8>(revision >> 24);
    const u8 subclass = static_cast<u8>(revision >> 16);
    const u8 prog_if = static_cast<u8>(revision >> 8);

    if (device_total < max_devices) {
        devices[device_total] = PciDevice(address, vendor, device, class_code, subclass, prog_if);
        ++device_total;
    }

    // A bridge hides a whole bus behind it, so follow it rather than assuming
    // everything sits on bus zero.
    if (class_code == 0x06 && subclass == 0x04) {
        const u8 secondary = read8(address, offset_secondary_bus);
        if (secondary != address.bus)
            scan_bus(secondary);
    }
}

void scan_slot(u8 bus, u8 slot)
{
    const Address first{bus, slot, 0};
    if (read16(first, offset_vendor) == 0xFFFF)
        return;

    scan_function(first);

    const u8 header = read8(first, offset_header_type);
    if ((header & 0x80) == 0)
        return;

    for (u8 function = 1; function < 8; ++function)
        scan_function(Address{bus, slot, function});
}

void scan_bus(u8 bus)
{
    for (u8 slot = 0; slot < 32; ++slot)
        scan_slot(bus, slot);
}

} // namespace

u32 read32(Address address, u16 offset)
{
    IrqGuard guard(config_lock);

    arch::outl(config_address_port, config_index(address, offset));
    return arch::inl(config_data_port);
}

u16 read16(Address address, u16 offset)
{
    return static_cast<u16>(read32(address, offset) >> ((offset & 2) * 8));
}

u8 read8(Address address, u16 offset)
{
    return static_cast<u8>(read32(address, offset) >> ((offset & 3) * 8));
}

void write32(Address address, u16 offset, u32 value)
{
    IrqGuard guard(config_lock);

    arch::outl(config_address_port, config_index(address, offset));
    arch::outl(config_data_port, value);
}

void write16(Address address, u16 offset, u16 value)
{
    const u32 shift = (offset & 2) * 8;
    const u32 current = read32(address, offset);
    const u32 mask = ~(0xFFFFu << shift);
    write32(address, offset, (current & mask) | (static_cast<u32>(value) << shift));
}

void write8(Address address, u16 offset, u8 value)
{
    const u32 shift = (offset & 3) * 8;
    const u32 current = read32(address, offset);
    const u32 mask = ~(0xFFu << shift);
    write32(address, offset, (current & mask) | (static_cast<u32>(value) << shift));
}

PciDevice::PciDevice(Address address, u16 vendor, u16 device, u8 class_code, u8 subclass,
                     u8 prog_if)
    : address_(address)
    , vendor_(vendor)
    , device_(device)
    , class_code_(class_code)
    , subclass_(subclass)
    , prog_if_(prog_if)
{
    usize cursor = 0;
    write_digits(location_, cursor, address.bus);
    location_[cursor++] = ':';
    write_digits(location_, cursor, address.slot);
    location_[cursor++] = '.';
    location_[cursor++] = static_cast<char>('0' + address.function);
    location_[cursor] = '\0';
}

DeviceId PciDevice::id() const
{
    return DeviceId{BusType::Pci, vendor_, device_, class_code_, subclass_};
}

u8 PciDevice::interrupt_line() const
{
    return read8(address_, offset_interrupt_line);
}

// Sizing a register means writing all ones and reading back what stuck, which
// only works while the device is not decoding.
Bar PciDevice::bar(u8 index) const
{
    if (index >= 6)
        return Bar{};

    const u16 offset = offset_bar0 + index * 4;
    const u32 original = read32(address_, offset);

    const u16 command = read16(address_, offset_command);
    write16(address_, offset_command, command & ~(command_io_space | command_memory_space));

    write32(address_, offset, 0xFFFFFFFF);
    const u32 mask = read32(address_, offset);
    write32(address_, offset, original);

    Bar result{};
    result.memory = (original & 1) == 0;

    if (!result.memory) {
        result.address = original & ~0x3u;
        result.length = (~(mask & ~0x3u) + 1) & 0xFFFF;
        write16(address_, offset_command, command);
        return result;
    }

    const bool sixty_four = ((original >> 1) & 0x3) == 0x2;
    result.prefetchable = (original & 0x8) != 0;
    result.address = original & ~0xFu;

    u64 size_mask = mask & ~0xFu;

    if (sixty_four && index < 5) {
        const u32 high_original = read32(address_, offset + 4);
        write32(address_, offset + 4, 0xFFFFFFFF);
        const u32 high_mask = read32(address_, offset + 4);
        write32(address_, offset + 4, high_original);

        result.address |= static_cast<u64>(high_original) << 32;
        size_mask |= static_cast<u64>(high_mask) << 32;
    } else {
        size_mask |= 0xFFFFFFFF00000000ULL;
    }

    result.length = ~size_mask + 1;
    write16(address_, offset_command, command);
    return result;
}

u8 PciDevice::find_capability(u8 id) const
{
    if ((read16(address_, offset_status) & status_capabilities) == 0)
        return 0;

    u8 offset = read8(address_, offset_capabilities) & 0xFC;

    for (usize guard = 0; guard < 48 && offset >= 0x40; ++guard) {
        const u8 capability = read8(address_, offset);
        if (capability == id)
            return offset;

        offset = read8(address_, offset + 1) & 0xFC;
        if (offset == 0)
            break;
    }

    return 0;
}

void PciDevice::enable_bus_master()
{
    write16(address_, offset_command, read16(address_, offset_command) | command_bus_master);
}

void PciDevice::enable_memory_space()
{
    write16(address_, offset_command,
            read16(address_, offset_command) | command_memory_space | command_io_space);
}

void init()
{
    scan_bus(0);
    ready = true;

    pr_info("pci: %lu device%s found\n",
            static_cast<u64>(device_total),
            device_total == 1 ? "" : "s");

    for (usize i = 0; i < device_total; ++i) {
        const PciDevice& device = devices[i];
        pr_info("  %s %x:%x %s\n",
                device.location(),
                device.vendor(),
                device.device(),
                class_name(device.class_code(), device.subclass()));
    }

    for (usize i = 0; i < device_total; ++i)
        device_announce(&devices[i]);
}

bool available()
{
    return ready;
}

usize device_count()
{
    return device_total;
}

PciDevice* device_at(usize index)
{
    return index < device_total ? &devices[index] : nullptr;
}

const char* class_name(u8 class_code, u8 subclass)
{
    switch (class_code) {
    case 0x00: return "unclassified";
    case 0x01:
        switch (subclass) {
        case 0x01: return "ide controller";
        case 0x06: return "sata controller";
        case 0x08: return "nvme controller";
        default:   return "storage controller";
        }
    case 0x02: return "network controller";
    case 0x03: return "display controller";
    case 0x04: return "multimedia controller";
    case 0x06:
        return subclass == 0x04 ? "pci bridge" : "host bridge";
    case 0x07: return "communication controller";
    case 0x08: return "system peripheral";
    case 0x09: return "input controller";
    case 0x0C:
        return subclass == 0x03 ? "usb controller" : "serial bus controller";
    default:   return "device";
    }
}

} // namespace eris::pci
