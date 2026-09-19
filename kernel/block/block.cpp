// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#include <eris/block.hpp>
#include <eris/export.hpp>
#include <eris/lock.hpp>
#include <eris/mm.hpp>
#include <eris/paging.hpp>
#include <eris/printk.hpp>
#include <eris/string.hpp>

namespace eris {
namespace {

constexpr usize max_devices = 8;

constinit BlockDevice* devices[max_devices]{};
constinit usize device_total = 0;
constinit IrqSpinLock registry_lock{};

// A driver in a loadable module exports plain functions, so the block layer
// wraps them in the interface everything above it expects.
class ModuleBlockDevice final : public BlockDevice {
public:
    using ReadFn = bool (*)(u64, void*, usize);
    using WriteFn = bool (*)(u64, const void*, usize);
    using CapacityFn = u64 (*)();

    ModuleBlockDevice() = default;

    void bind(const char* name, ReadFn read, WriteFn write, CapacityFn capacity)
    {
        usize i = 0;
        while (i + 1 < sizeof(name_) && name[i] != '\0') {
            name_[i] = name[i];
            ++i;
        }
        name_[i] = '\0';

        read_ = read;
        write_ = write;
        capacity_ = capacity;
    }

    const char* name() const override { return name_; }
    u64 sector_count() const override { return capacity_ != nullptr ? capacity_() : 0; }

    bool read(u64 sector, void* buffer, usize count) override
    {
        return read_ != nullptr && read_(sector, buffer, count);
    }

    bool write(u64 sector, const void* buffer, usize count) override
    {
        return write_ != nullptr && write_(sector, buffer, count);
    }

private:
    char name_[16]{};
    ReadFn read_ = nullptr;
    WriteFn write_ = nullptr;
    CapacityFn capacity_ = nullptr;
};

constinit ModuleBlockDevice module_devices[max_devices]{};
constinit usize module_device_total = 0;

} // namespace

// Reads that do not start or end on a sector boundary go through a bounce
// buffer, because a device only ever moves whole sectors.
bool BlockDevice::read_bytes(u64 offset, void* buffer, usize length)
{
    auto* out = static_cast<u8*>(buffer);

    const phys_addr frame = mm::alloc_page();
    if (frame == 0)
        return false;

    auto* bounce = reinterpret_cast<u8*>(mm::phys_to_virt(frame));
    bool ok = true;

    while (length > 0) {
        const u64 sector = offset / sector_size;
        const usize inside = offset % sector_size;
        const usize chunk = length < sector_size - inside ? length : sector_size - inside;

        if (!read(sector, bounce, 1)) {
            ok = false;
            break;
        }

        memcpy(out, bounce + inside, chunk);

        out += chunk;
        offset += chunk;
        length -= chunk;
    }

    mm::free_page(frame);
    return ok;
}

bool block_register(BlockDevice* device)
{
    if (device == nullptr)
        return false;

    IrqGuard guard(registry_lock);

    if (device_total >= max_devices) {
        pr_warn("block: table full, %s refused\n", device->name());
        return false;
    }

    devices[device_total++] = device;

    pr_info("block: %s with %lu sectors, %lu MiB\n",
            device->name(),
            device->sector_count(),
            device->sector_count() * sector_size / (1024 * 1024));
    return true;
}

void block_unregister(BlockDevice* device)
{
    IrqGuard guard(registry_lock);

    for (usize i = 0; i < device_total; ++i) {
        if (devices[i] != device)
            continue;

        devices[i] = devices[--device_total];
        return;
    }
}

usize block_device_count()
{
    return device_total;
}

BlockDevice* block_device_at(usize index)
{
    return index < device_total ? devices[index] : nullptr;
}

BlockDevice* block_device_find(const char* name)
{
    for (usize i = 0; i < device_total; ++i) {
        if (strcmp(devices[i]->name(), name) == 0)
            return devices[i];
    }

    return nullptr;
}

bool block_register_module(const char* name, const char* read_symbol,
                           const char* write_symbol, const char* capacity_symbol)
{
    if (module_device_total >= max_devices)
        return false;

    auto read = reinterpret_cast<ModuleBlockDevice::ReadFn>(symbol_lookup(read_symbol));
    auto write = reinterpret_cast<ModuleBlockDevice::WriteFn>(symbol_lookup(write_symbol));
    auto capacity = reinterpret_cast<ModuleBlockDevice::CapacityFn>(symbol_lookup(capacity_symbol));

    if (read == nullptr || capacity == nullptr || capacity() == 0)
        return false;

    ModuleBlockDevice& device = module_devices[module_device_total++];
    device.bind(name, read, write, capacity);

    return block_register(&device);
}

} // namespace eris
