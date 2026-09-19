// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris {

// The boot loader hands the archive over as a multiboot module. It is a plain
// tar, because a kernel that cannot read a file system yet should not need one
// to find its modules.
void initrd_init(u32 multiboot_magic, u64 multiboot_info);

bool initrd_available();
usize initrd_file_count();

const void* initrd_find(const char* name, usize& length);
const char* initrd_name_at(usize index, usize& length);
const void* initrd_data_at(usize index, const char*& name, usize& length);

} // namespace eris
