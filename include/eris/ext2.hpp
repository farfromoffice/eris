// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/block.hpp>
#include <eris/vfs.hpp>

namespace eris::fs {

// Read only for now. Writing means block and inode bitmaps, and nothing in the
// tree needs to put a file on a disk yet.
FileSystem* ext2_mount(BlockDevice* device);

} // namespace eris::fs
