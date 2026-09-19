// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/vfs.hpp>

namespace eris::fs {

// Every device the kernel knows about, as a file. Reading a block device node
// reads the disk, writing the console node prints.
FileSystem* devfs_create();
void devfs_refresh();

} // namespace eris::fs
