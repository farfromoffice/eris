// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

extern "C" {

// A block device, one sector at a time. The block layer in a later phase turns
// this into a queue, for now it is what a file system prototype would call.
bool virtio_blk_present();
eris::u64 virtio_blk_capacity();
bool virtio_blk_read(eris::u64 sector, void* buffer, eris::usize sectors);
bool virtio_blk_write(eris::u64 sector, const void* buffer, eris::usize sectors);

}
