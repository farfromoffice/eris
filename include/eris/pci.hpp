// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/device.hpp>
#include <eris/types.hpp>

namespace eris::pci {

struct Address {
    u8 bus;
    u8 slot;
    u8 function;
};

inline constexpr u8 capability_msi = 0x05;
inline constexpr u8 capability_msix = 0x11;
inline constexpr u8 capability_vendor = 0x09;

u32 read32(Address address, u16 offset);
u16 read16(Address address, u16 offset);
u8 read8(Address address, u16 offset);

void write32(Address address, u16 offset, u32 value);
void write16(Address address, u16 offset, u16 value);
void write8(Address address, u16 offset, u8 value);

// A base address register, decoded into something a driver can map.
struct Bar {
    u64 address;
    u64 length;
    bool memory;
    bool prefetchable;
};

class PciDevice final : public BusDevice {
public:
    PciDevice() = default;
    PciDevice(Address address, u16 vendor, u16 device, u8 class_code, u8 subclass, u8 prog_if);

    DeviceId id() const override;
    const char* location() const override { return location_; }

    Address address() const { return address_; }
    u16 vendor() const { return vendor_; }
    u16 device() const { return device_; }
    u8 class_code() const { return class_code_; }
    u8 subclass() const { return subclass_; }
    u8 prog_if() const { return prog_if_; }
    u8 interrupt_line() const;

    Bar bar(u8 index) const;
    u8 find_capability(u8 id) const;

    void enable_bus_master();
    void enable_memory_space();

private:
    Address address_{};
    u16 vendor_ = 0;
    u16 device_ = 0;
    u8 class_code_ = 0;
    u8 subclass_ = 0;
    u8 prog_if_ = 0;
    char location_[16]{};
};

void init();
bool available();
usize device_count();
PciDevice* device_at(usize index);

const char* class_name(u8 class_code, u8 subclass);

} // namespace eris::pci
