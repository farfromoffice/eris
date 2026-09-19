// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris {

inline constexpr usize sector_size = 512;

// What a storage driver has to provide. The driver knows how to move sectors,
// everything above it works in whole blocks and does not care which bus they
// came from.
class BlockDevice {
public:
    virtual ~BlockDevice() = default;

    virtual const char* name() const = 0;
    virtual u64 sector_count() const = 0;

    virtual bool read(u64 sector, void* buffer, usize count) = 0;
    virtual bool write(u64 sector, const void* buffer, usize count) = 0;

    // Bytes rather than sectors, for the readers that think in offsets.
    bool read_bytes(u64 offset, void* buffer, usize length);
};

bool block_register(BlockDevice* device);
void block_unregister(BlockDevice* device);

usize block_device_count();
BlockDevice* block_device_at(usize index);
BlockDevice* block_device_find(const char* name);

// Wraps a driver that only exports C functions, which is every driver that
// lives in a loadable module.
bool block_register_module(const char* name, const char* read_symbol,
                           const char* write_symbol, const char* capacity_symbol);

} // namespace eris
