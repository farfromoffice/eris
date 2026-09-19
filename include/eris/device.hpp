// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris {

enum class BusType : u8 {
    Platform,
    Pci,
};

// What a driver matches on. A zero field means the driver does not care.
struct DeviceId {
    BusType bus = BusType::Pci;
    u16 vendor = 0;
    u16 device = 0;
    u8 class_code = 0;
    u8 subclass = 0;
};

class Device {
public:
    virtual ~Device() = default;

    virtual const char* name() const = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
};

class BusDevice {
public:
    virtual ~BusDevice() = default;

    virtual DeviceId id() const = 0;
    virtual const char* location() const = 0;
};

class Driver {
public:
    virtual ~Driver() = default;

    virtual const char* name() const = 0;
    virtual bool matches(const DeviceId& id) const = 0;
    virtual Device* probe(BusDevice& device) = 0;
};

void driver_register(Driver* driver);
void driver_unregister(Driver* driver);

// Offers one device to every registered driver, first match wins.
Device* device_probe(BusDevice& device);

// Replays the devices the bus already found against a driver that just showed
// up, which is what makes a module loaded after boot still bind.
void device_announce(BusDevice* device);
usize device_count();
BusDevice* device_at(usize index);

usize driver_count();

} // namespace eris
