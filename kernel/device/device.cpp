// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/device.hpp>
#include <eris/lock.hpp>
#include <eris/printk.hpp>

namespace eris {
namespace {

constexpr usize max_devices = 64;
constexpr usize max_drivers = 32;

constinit BusDevice* devices[max_devices]{};
constinit usize device_total = 0;

constinit Driver* drivers[max_drivers]{};
constinit usize driver_total = 0;

constinit IrqSpinLock registry_lock{};

} // namespace

void driver_register(Driver* driver)
{
    if (driver == nullptr)
        return;

    {
        IrqGuard guard(registry_lock);

        if (driver_total >= max_drivers) {
            pr_warn("device: driver table full, %s refused\n", driver->name());
            return;
        }

        drivers[driver_total++] = driver;
    }

    // A driver that arrives late still gets everything the bus already found.
    for (usize i = 0; i < device_total; ++i) {
        BusDevice* device = devices[i];
        if (device != nullptr && driver->matches(device->id()))
            device_probe(*device);
    }
}

void driver_unregister(Driver* driver)
{
    IrqGuard guard(registry_lock);

    for (usize i = 0; i < driver_total; ++i) {
        if (drivers[i] != driver)
            continue;

        drivers[i] = drivers[--driver_total];
        return;
    }
}

Device* device_probe(BusDevice& bus_device)
{
    const DeviceId id = bus_device.id();

    for (usize i = 0; i < driver_total; ++i) {
        Driver* driver = drivers[i];
        if (driver == nullptr || !driver->matches(id))
            continue;

        Device* device = driver->probe(bus_device);
        if (device == nullptr)
            continue;

        if (!device->start()) {
            pr_warn("device: %s claimed %s and then refused to start\n",
                    driver->name(), bus_device.location());
            continue;
        }

        pr_info("device: %s bound to %s at %s\n",
                driver->name(), device->name(), bus_device.location());
        return device;
    }

    return nullptr;
}

void device_announce(BusDevice* device)
{
    if (device == nullptr)
        return;

    {
        IrqGuard guard(registry_lock);

        if (device_total >= max_devices) {
            pr_warn("device: table full, %s not recorded\n", device->location());
            return;
        }

        devices[device_total++] = device;
    }

    device_probe(*device);
}

usize device_count()
{
    return device_total;
}

BusDevice* device_at(usize index)
{
    return index < device_total ? devices[index] : nullptr;
}

usize driver_count()
{
    return driver_total;
}

} // namespace eris
