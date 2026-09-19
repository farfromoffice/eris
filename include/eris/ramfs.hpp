// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/vfs.hpp>

namespace eris::fs {

// Files that live in the heap. The first mount, and where anything that needs
// a scratch file goes until there is a disk to put it on.
FileSystem* ramfs_create();
Inode* ramfs_create_file(const char* name);
bool ramfs_remove(const char* name);

} // namespace eris::fs
