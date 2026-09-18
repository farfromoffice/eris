// SPDX-License-Identifier: GPL-2.0-only
// Copyright (c) 2026 farfromoffice

#pragma once

#include <eris/types.hpp>

namespace eris {

void cmdline_init(u32 multiboot_magic, u64 multiboot_info);

const char* cmdline_raw();
bool cmdline_has(const char* key);
const char* cmdline_value(const char* key);

} // namespace eris
